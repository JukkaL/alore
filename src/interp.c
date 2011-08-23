/* interp.c - Alore interpreter

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "interp.h"
#include "class.h"
#include "opcode.h"
#include "memberid.h"
#include "mem.h"
#include "array.h"
#include "tuple.h"
#include "int.h"
#include "float.h"
#include "str.h"
#include "internal.h"
#include "debug_runtime.h"
#include "gc.h"
/* #include "jitcomp.h" */

#include <math.h>


#if (A_VALUE_INT_BITS & 1) == 1
#error Int value has an odd number of significant bits
#endif


/* Exception error messages indexed using EM_* (defined in interp.h). */
static const char *ExceptionErrorMessages[] = {
    NULL,
    AMsgNotClassInstance,
    AMsgArrayIndexOutOfRange,
    AMsgStrIndexOutOfRange,
    AMsgTooFewValuesToAssign,
    AMsgTooManyValuesToAssign,
    AMsgDivisionByZero,
    AMsgTooManyRecursiveCalls,
    AMsgCallInterface,
    AMsgSliceIndexMustBeIntOrNil
};


#ifdef HAVE_JIT_COMPILER
#define CHECK_JIT \
    if (AIsJitCompilerActive \
        && !AValueToFunction(stack[1])->isJitFunction) { \
        if (!AJitCompileFunction(t, stack[1]))      \
            goto ExceptionRaised;                       \
        else                                            \
            goto TransferControlToCompiled;             \
    }
#else
#define CHECK_JIT
#endif


/* Check if an interrupt is active; handle it if it is. Go to label
   ExceptionRaised if an exception raised by the interrupt. */
#define PERIODIC_INTERPRETER_CHECK \
    do { \
        if (AIsInterrupt && AHandleInterrupt(t)) \
            goto ExceptionRaised; \
        CHECK_JIT; \
    } while (0)


/* Depth indicating no except blocks should be skipped */
#define NO_SKIP 100000000


/* Create error id for mismatched OP_EXPAND bytecode. */
#define ExpandErrorId(sourceLen, targetLen) \
    AErrorId(EX_VALUE_ERROR, (targetLen) < (sourceLen) ? \
             EM_TOO_MANY_VALUES_TO_ASSIGN : EM_TOO_FEW_VALUES_TO_ASSIGN)


/* Run the interpreter main loop starting at the specified opcode. The stack
   argument should point to the topmost stack frame in the stack. The frame
   must be properly initialized. Return the return value from this function
   or method, or AError on error. Direct exceptions may be raised. */
AValue ARun(AThread *t, AOpcode *ip_, AValue *stack_)
{
    /* The interpreter implementation uses several macros to allow for
       different implementation techniques to be used to implement the
       interpreter main loop. Two variants of these macros are provided:
       The first one is a portable set that can be used with any C compiler.
       The second one uses gcc extensions, resulting in significantly
       improved performance when compiled using a supporting compiler.

       This function is somewhat heavily optimized for performance, and
       therefore it is not written using very readable or maintainable coding
       practices. Much functionality is inlined manually, and several
       operations are implemented using low-level operations.
    
       IMPORTANT: Direct exceptions should generally not be raised in the
       interpreter main loop, since the opcode index is not always updated (to
       improve performance). This also means that C API functions are not used
       in the interpreter main loop. Any Alore-level functions called within
       the interpreter main loop can, of course, raise direct exceptions. */
    
    A_INITIALIZE_INTERPRETER

    /* Instruction pointer. Before each interpreter main loop interation points
       to the next instruction to execute. */
    register AOpcode *ip A_IP_REG;
    /* Pointer to the stack frame of the currently executing function. */
    register AValue *stack A_STACK_REG;
    
    AExceptionType exception;
    unsigned numArgs;
    unsigned member;
    AValue *args;
    AValue funcVal;
    AValue base;

    /* Initialize some local variables to get rid of compiler warnings, even
       though this is strictly not necessary. */
    AAvoidWarning_M(member = 0);
    AAvoidWarning_M(base = 0);

    ip = ip_;
    stack = stack_;

    args = t->tempStackPtr;

  LoopTop:
    
    A_BEGIN_INTERPRETER_LOOP;

#ifdef A_DEBUG
    ADebugNextInstruction();
#ifdef A_DEBUG_INSTRUCTION_TRACE
    if (ADebugIsOn())
        ADebug_Trace(stack, ip);
#endif
#endif

    /* The main interpreter loop. Repeatedly dispatch instructions. */

    /* Comments that describe instructions use x{n} to specify ip-relative
       opcodes; e.g. lvar{2} refers to local variable whose id is stored at
       ip{2}. */

    A_BEGIN_INTERPRETER_LOOP_2
    
    A_OPCODE(OP_NOP) {
        /* Do nothing */
        A_END_OPCODE;
    }
    
    A_OPCODE(OP_ASSIGN_IL) {
        /* int{1} -> lvar{2} */
        stack[ip[2]] = ip[1];
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_LL) {
        /* lvar{1} -> lvar{2} */
        stack[ip[2]] = stack[ip[1]];
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_GL) {
        /* gvar{1} -> lvar{2} */
        stack[ip[2]] = AGlobalByNum(ip[1]);
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_ML) {
        /* lvar{1}.member{2} -> lvar{3} */
        
        AValue val = stack[ip[1]];
        unsigned key = ip[2];

        /* Create a temporary wrapper instance for values with special object
           representations. */
        if (!AIsInstance(val)) {
            val = AWrapObject(t, val);
            if (AIsError(val))
                goto ExceptionRaised;
        }

        {
            AInstance *inst = AValueToInstance(val);
            ATypeInfo *type = AGetInstanceType(inst);
            AMemberHashTable *table = AGetMemberTable(type, MT_VAR_GET_PUBLIC);
            AMemberNode *node;
            
            node = &table->item[key & table->size];

            /* Search the get hash table. */
            while (node->key != key) {
                if (node->next == NULL) {
                    if (type->super == NULL) {
                        AValue item;

                        item = AGetInstanceCodeMember(t, inst, key);
                        if (!AIsError(item)) {
                            stack[ip[3]] = item;
                            goto LeaveASSIGN_ML;
                        } else
                            goto ExceptionRaised;
                    } else {
                        type = type->super;
                        table = AGetMemberTable(type, MT_VAR_GET_PUBLIC);
                        node = &table->item[key & table->size];
                    }
                } else
                    node = node->next;
            }

            if (node->item >= A_VAR_METHOD) {
                /* Call getter method. */
                numArgs = 1;
                args[0] = val;
                funcVal = AGlobalByNum(node->item & ~A_VAR_METHOD);
                ip += 3;
                goto FunctionCall;
            } else {
                /* Direct access to slot. */
                stack[ip[3]] = inst->member[node->item];
            }
        }

      LeaveASSIGN_ML:

        ip += 4;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_VL) {
        /* self.slot{1} -> lvar{2} */
        stack[ip[2]] = AValueToInstance(stack[3])->member[ip[1]];
        ip += 3;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_MDL) {
        /* self.slot-or-getter{1} -> lvar{3} */
        
        /* Notes:
            -  ip[2] is ignored; it is used by the OP_ASSIGN_LMD opcode
               variant.
            - If the instruction refers to a getter, ip[1] has A_VAR_METHOD
              flag set. */
        
        if (ip[1] & A_VAR_METHOD) {
            /* Call getter method. */
            numArgs = 1;
            args[0] = stack[3];
            funcVal = AGlobalByNum(ip[1] & ~A_VAR_METHOD);
            ip += 3;
            goto FunctionCall;
        } else {
            /* Read a slot. */
            stack[ip[3]] = AValueToInstance(stack[3])->member[ip[1]];
            ip += 4;
        }
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_EL) {
        /* lvar{1}[0] -> lvar{2}

           Assign the value of an exposed local variable to a local
           variable. Assume that lvar{1} is a FixArray object. */
        stack[ip[2]] = AFixArrayItem(stack[ip[1]], 0);
        ip += 3;
        A_END_OPCODE;
    }
    
    A_OPCODE(OP_ASSIGN_FL) {
        /* self.method{1} -> lvar{2} */
        AValue bound = ACreateBoundMethod(t, stack[3], ip[1]);
        if (AIsError(bound))
            goto ExceptionRaised;
        
        stack[ip[2]] = bound;
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_LL_REV) {
        /* lvar{2} -> lvar{1} */
        stack[ip[1]] = stack[ip[2]];
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_LG) {
        /* lvar{2} -> gvar{1} */
        AGlobalVars[ip[1]] = stack[ip[2]];
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_LGC) {
        /* lvar{2} -> gvar{1} (constant target; record assignment for gc) */
        if (!ASetConstGlobalValue(t, ip[1], stack[ip[2]])) {
            exception = EX_MEMORY_ERROR;
            goto RaiseException;
        }
        ip += 3;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_LM) {
        /* lvar{3} -> lvar{1}.member{2} */
        
        AValue val = stack[ip[1]];
        unsigned key = ip[2];

        if (!AIsInstance(val)) {
            /* Non-special object representations do not support assignment to
               member. */
            exception = EX_MEMBER_ERROR;
            args[0] = val;
            member = key;
            goto RaiseException;
        } else {
            /* Common object representation (AInstance) */
            AInstance *inst = AValueToInstance(val);
            ATypeInfo *type = AGetInstanceType(inst);
            AMemberHashTable *table = AGetMemberTable(type, MT_VAR_SET_PUBLIC);
            AMemberNode *node;

            node = &table->item[key & table->size];

            /* Search the setter hash table. */
            while (node->key != key) {
                if (node->next == NULL) {
                    if (type->super == NULL) {
                        exception = EX_MEMBER_ERROR;
                        args[0] = val;
                        member = key;
                        goto RaiseException;
                    } else {
                        type = type->super;
                        table = AGetMemberTable(type, MT_VAR_SET_PUBLIC);
                        node = &table->item[key & table->size];
                    }
                } else
                    node = node->next;
            }

            if (node->item >= A_VAR_METHOD) {
                /* Call setter method. */
                numArgs = 2;
                args[0] = val;
                args[1] = stack[ip[3] - A_NO_RET_VAL];
                funcVal = AGlobalByNum(node->item & ~A_VAR_METHOD);
                ip += 3;
                goto FunctionCall;
            } else {
                /* Modify a slot. */
                unsigned member = node->item;
                ABool result;
                AValue *src = &stack[ip[3] - A_NO_RET_VAL];

                AModifyObject_M(t, &inst->type, inst->member + member,
                               src, result);
                if (!result) {
                    exception = EX_MEMORY_ERROR;
                    goto RaiseException;
                }
            }
        }
            
        ip += 4;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_LV) {
        /* lvar{2} -> self.slot{1} */
        AInstance *inst;
        unsigned member;
        ABool result;

        inst = AValueToInstance(stack[3]);
        member = ip[1];

        AModifyObject_M(t, &inst->type, inst->member + member,
                       stack + ip[2], result);
        if (!result) {
            exception = EX_MEMORY_ERROR;
            goto RaiseException;
        }
        
        ip += 3;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_LMD) {
        /* lvar{3} -> self.slot-or-setter{2} */

        /* Notes:
            - ip[1] is ignored; it is used by the OP_ASSIGN_MDL opcode variant.
            - If the instruction refers to a setter, ip[2] has A_VAR_METHOD
              flag set.
            - The lvar{3} parameter is displaced by A_NO_RET_VAL. This
              allows reusing the ordinary function call implementation when
              calling the setter. */

        if (ip[2] & A_VAR_METHOD) {
            /* Call setter method. */
            numArgs = 2;
            args[0] = stack[3];
            args[1] = stack[ip[3] - A_NO_RET_VAL];
            funcVal = AGlobalByNum(ip[2] & ~A_VAR_METHOD);
            ip += 3;
            goto FunctionCall;
        } else {
            /* Modify a slot. */
            AInstance *inst = AValueToInstance(stack[3]);
            if (!ASetInstanceMember(t, inst, ip[2],
                                   stack + ip[3] - A_NO_RET_VAL))
                goto ExceptionRaised;
        }
        
        ip += 4;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_LE) {
        /* lvar{2} -> lvar{1}[0]

           Assign a local variable to an exposed local variable. Assume lvar{1}
           is a FixArray object. */
        
        ABool result;
        AFixArray *a = AValueToFixArray(stack[ip[1]]);

        AModifyObject_M(t, &a->header, AGetFixArrayItemPtr(a, AIntToValue(0)),
                        &stack[ip[2]], result);
        if (!result) {
            exception = EX_MEMORY_ERROR;
            goto RaiseException;
        }
        
        ip += 3;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_NILL) {
        /* nil -> lvar{1} */
        stack[ip[1]] = ANil;
        ip += 2;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_ASSIGN_PL) {
        /* offset{2} -> lvar{1} */
        stack[ip[1]] = AIntToValue(ABranchTarget(ip + 2) -
                                   AValueToFunction(stack[1])->code.opc);
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_LEAVE_FINALLY) {
        /* leave_finally lvar{1}, int{2}, offset{3} */
        
        AValue val = stack[ip[1]];
            
        if (val == AZero) {
            /* Continue excecution normally -- finally block was entered
               on the normal path of execution. */
            ip += 4;
        } else if (AIsInstance(val)) {
            /* Re-raise exception -- finally block was entered because of a
               raised exception. */
            t->isExceptionReraised = TRUE;
            t->exception = val;
            t->uncaughtExceptionStackPtr = stack;
            goto ReraiseException;
        } else {
            /* Should we branch to another location in the current function
               as the result of a break statement? */
            if (AIsShortInt(val)) {
                AOpcode *base = AValueToFunction(stack[1])->code.opc; 
                
                if (AValueToInt(val) < ip - base + 3 + ip[3] -
                                      A_DISPLACEMENT_SHIFT) {
                    t->contextIndex -= AValueToInt(stack[ip[1] + 2]);
                    ip = base + AValueToInt(val);
                    A_END_OPCODE;
                }
            }

            /* IDEA: Use constant for 1 << .. */
            if (ip[3] == (1U << (A_OPCODE_BITS - 1))) {
                /* Return from the function. The return value is stored in the
                   stack. */
                AValue retVal = stack[ip[1] + 1];
                t->contextIndex -= AValueToInt(stack[ip[1] + 2]);
                stack[ip[1]] = retVal;
                goto RET_L_Opcode;
            } else {
                /* Go to the next finally block in the finally chain. */
                unsigned local = ip[1];
                
                /* Pop thread contexts. */
                t->contextIndex -= ip[2];

                ip = ABranchTarget(ip + 3);
                
                /* Pass on the state values. */
                stack[ip[-1]] = stack[local];
                stack[ip[-1] + 1] = stack[local + 1];
                stack[ip[-1] + 2] = stack[local + 2];
            }
        }
        A_END_OPCODE;
    }

    A_OPCODE(OP_INC_L) {
        /* lvar{1} + 1 -> lvar{1} */
        int varNum = ip[1];
        AValue val = stack[varNum];

        /* IDEA: Combine some code (with DEC_L?) if performance won't
                 degrade. */
        if (AIsShortInt(val)) {
            if (val != AIntToValue(A_SHORT_INT_MAX))
                stack[varNum] = val + AIntToValue(1);
            else {
                val = ACreateLongIntFromIntND(t, A_SHORT_INT_MAX + 1);
                if (AIsError(val))
                    goto ExceptionRaised;
                stack[varNum] = val;
            }
        } else if (AIsFloat(val)) {
            val = ACreateFloat(t, AValueToFloat(val) + 1.0);
            if (AIsError(val))
                goto ExceptionRaised;
            stack[varNum] = val;
        } else if (AIsLongInt(val)) {
            val = AAddLongIntSingle(t, val, 1);
            if (AIsError(val))
                goto ExceptionRaised;
            stack[varNum] = val;
        } else {
            /* Try operator overloading. */
            member = AM_ADD;
            numArgs = 2;
            args[0] = val;
            args[1] = AIntToValue(1);
            ip++;
            goto MethodCall;
        }

        ip += 2;

        A_END_OPCODE;
    }

    A_OPCODE(OP_DEC_L) {
        /* lvar{1} - 1 -> lvar{1} */
        int varNum = ip[1];
        AValue val = stack[varNum];

        /* IDEA: Combine some code (with INC_L?) if performance won't
                 degrade. */

        if (AIsShortInt(val)) {
            if (val != AIntToValue(A_SHORT_INT_MIN))
                stack[varNum] = val - AIntToValue(1);
            else {
                val = ACreateLongIntFromIntND(t, A_SHORT_INT_MIN - 1);
                if (AIsError(val))
                    goto ExceptionRaised;
                stack[varNum] = val;
            }
        } else if (AIsFloat(val)) {
            val = ACreateFloat(t, AValueToFloat(val) - 1.0);
            if (AIsError(val))
                goto ExceptionRaised;
            stack[varNum] = val;
        } else if (AIsLongInt(val)) {
            val = ASubLongIntSingle(t, val, 1);
            if (AIsError(val))
                goto ExceptionRaised;
            stack[varNum] = val;
        } else {
            /* Try operator overloading. */
            member = AM_SUB;
            numArgs = 2;
            args[0] = val;
            args[1] = AIntToValue(1);
            ip++;
            goto MethodCall;
        }

        ip += 2;

        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_FALSEL) {
        /* False -> lvar{1} */
        stack[ip[1]] = AFalse;
        ip += 2;
        A_END_OPCODE;
    }
            
    A_OPCODE(OP_MINUS_LL) {
        /* -lvar{1} -> lvar{2} */
        AValue val = stack[ip[1]];

        if (!AIsShortInt(val)) {
            if (AIsFloat(val)) {
                double result = -AValueToFloat(val);
                if (t->heapPtr == t->heapEnd || A_NO_INLINE_ALLOC)
                    val = ACreateFloat(t, result);
                else {
                    *(double *)t->heapPtr = result;
                    val = AFloatPtrToValue(t->heapPtr);
                    t->heapPtr += A_FLOAT_SIZE;
                }                   
            } else if (AIsLongInt(val))
                val = ANegateLongInt(t, val);
            else {
                member = AM_NEGATE;
                args[0] = val;
                numArgs = 1;
                ip += 2;
                goto MethodCall;
            }
            
            if (AIsError(val))
                goto ExceptionRaised;
            
            stack[ip[2]] = val;
        } else {
            if (val == AIntToValue(A_SHORT_INT_MIN)) {
                val = ACreateLongIntFromIntND(t, -A_SHORT_INT_MIN);
                if (AIsError(val))
                    goto ExceptionRaised;
                stack[ip[2]] = val;
            } else
                stack[ip[2]] = -(ASignedValue)val;
        }

        ip += 3;

        A_END_OPCODE;
    }

    A_OPCODE(OP_HALT) {
        /* Return ANil unconditionally. */
        t->stackPtr = ANextFrame(stack, stack[0]);
        return ANil;
    }
    
    {
        AValue index;
        AValue item;
        
        A_OPCODE(OP_AGET_LLL) {
            /* lvar{1}[lvar{2}] -> lvar{3} */
            base = stack[ip[1]];
            ip += 2;
            goto GetItem;
        }
            
        A_OPCODE(OP_AGET_GLL) {
            /* gvar{1}[lvar{2}] -> lvar{3} */
            base = AGlobalVars[ip[1]];
            ip += 2;
            goto GetItem;
        }
            
      GetItem:

        /* Indexing operation base[lvar{0}] -> lvar{1} */

        index = stack[ip[0]];

        if (AIsArray(base)) {
            /* Inline Array indexing for efficiency. */
            AInstance *i = AValueToInstance(base);
            if (AIsShortInt(index)
                && index < i->member[A_ARRAY_LEN]) {
                stack[ip[1]] = AFixArrayItemV(i->member[A_ARRAY_A], index);
                ip += 2;
                A_END_OPCODE;
            } else if (AIsShortInt(index) &&
                       (-index - AIntToValue(1)) < i->member[A_ARRAY_LEN]) {
                stack[ip[1]] = AFixArrayItemV(i->member[A_ARRAY_A],
                                             i->member[A_ARRAY_LEN] + index);
                ip += 2;
                A_END_OPCODE;
            } else if (AIsPair(index)) {
                /* IDEA: No need to inline this here. */
                AMixedObject *pair = AValueToMixedObject(index);
                AValue lo = pair->data.pair.head;
                AValue hi = pair->data.pair.tail;
                AValue res;

                if (AIsNil(lo))
                    lo = AZero;
                if (AIsNil(hi))
                    hi = AIntToValue(A_SLICE_END);

                if (!AIsShortInt(lo) || !AIsShortInt(hi)) {
                    exception = AErrorId(EX_TYPE_ERROR,
                                         EM_SLICE_INDEX_MUST_BE_INT_OR_NIL);
                    goto RaiseException;
                }
                res = ASubArrayND(t, base, AValueToInt(lo), AValueToInt(hi));
                if (AIsError(res))
                    goto ExceptionRaised;
                
                stack[ip[1]] = res;
                ip += 2;
                A_END_OPCODE;
            } else if (AIsShortInt(index) || AIsLongInt(index)) {
                exception = AErrorId(EX_INDEX_ERROR,
                                     EM_ARRAY_INDEX_OUT_OF_RANGE);
                goto RaiseException;
            } else {
                ARaiseTypeErrorND(t, "Array indexed with non-integer (%T)",
                                  index);
                goto ExceptionRaised;
            }
        }

        if (AIsInstance(base)) {
            member = AM_GET_ITEM;
            numArgs = 2;
            args[0] = base;
            args[1] = index;
            ip++;
            goto MethodCall;
        } else {
            /* Inlined string indexing. */
            /* IDEA: No need inline this here, at least not this much. */
            unsigned char ch;
            AWideChar wch;
            
            if (AIsNarrowStr(base)) {
                if (AIsShortInt(index)) {
                    ABool result;
                    AString *s;
                    
                    s = AValueToStr(base);

                    /* Check that index is within range. */
                    if (index >= AGetStrLenAsInt(s)) {
                        index = ANormIndexV(index, AGetStrLenAsInt(s));
                        if (index >= AGetStrLenAsInt(s)) {
                            exception = AErrorId(EX_INDEX_ERROR,
                                                 EM_STR_INDEX_OUT_OF_RANGE);
                            /* FIX: index error? */
                            goto RaiseException;
                        }
                    }

                    ch = s->elem[AValueToInt(index)];

                  GetCharNarrow:

                    /* Create a short string object */
                    AAlloc_M(t, AGetBlockSize(sizeof(AValue) + 1),
                            s, result);
                    if (!result) {
                        exception = EX_MEMORY_ERROR;
                        goto RaiseException;
                    }
                    AInitNonPointerBlock(&s->header,  1);
                    s->elem[0] = ch;

                    item = AStrToValue(s);
                } else {
                    
                  TrySubStr:
                    
                    if (AIsPair(index)) {
                        AMixedObject *pair = AValueToMixedObject(index);
                        AValue lo = pair->data.pair.head;
                        AValue hi = pair->data.pair.tail;

                        if (AIsNil(lo))
                            lo = AZero;
                        if (AIsNil(hi))
                            hi = AIntToValue(A_SLICE_END);
                        
                        if (!AIsShortInt(lo) || !AIsShortInt(hi)) {
                            exception = AErrorId(
                                EX_TYPE_ERROR,
                                EM_SLICE_INDEX_MUST_BE_INT_OR_NIL);
                            goto RaiseException;
                        }

                        item = ACreateSubStr(t, base,
                                             AValueToInt(lo), AValueToInt(hi));
                        if (AIsError(item))
                            goto ExceptionRaised;
                    } else {
                        ARaiseTypeErrorND(t, AMsgStrIndexedWithNonInteger,
                                          index);
                        goto ExceptionRaised;
                    }
                }
            } else if (AIsSubStr(base)) {
                if (AIsShortInt(index)) {
                    ASubString *ss;
                    
                    ss = AValueToSubStr(base);

                    if (index >= ss->len) {
                        index = ANormIndexV(index, ss->len);
                        if (index >= ss->len) {
                            exception = AErrorId(EX_INDEX_ERROR,
                                                 EM_STR_INDEX_OUT_OF_RANGE);
                            goto RaiseException;
                        }
                    }

                    if (AIsNarrowStr(ss->str)) {
                        ch = AValueToStr(ss->str)->elem[
                            AValueToInt(index) + AValueToInt(ss->ind)];
                        goto GetCharNarrow;
                    } else {
                        wch = AValueToWideStr(ss->str)->elem[
                            AValueToInt(index) + AValueToInt(ss->ind)];
                        goto GetCharWide;
                    }
                } else
                    goto TrySubStr;
            } else if (AIsWideStr(base)) {
                if (AIsShortInt(index)) {
                    ABool result;
                    AWideString *s;
                    
                    s = AValueToWideStr(base);

                    /* Check that index is within range. */
                    if (index >= AIntToValue(AGetWideStrLen(s))) {
                        index = ANormIndexV(index,
                                           AIntToValue(AGetWideStrLen(s)));
                        if (index >= AIntToValue(AGetWideStrLen(s))) {
                            exception = AErrorId(EX_INDEX_ERROR,
                                                 EM_STR_INDEX_OUT_OF_RANGE);
                            goto RaiseException;
                        }
                    }

                    wch = s->elem[AValueToInt(index)];

                  GetCharWide:

                    /* Create a string object */ /* FIX: perhaps narrow? */
                    AAlloc_M(t, AGetBlockSize(sizeof(AValue) +
                                                 sizeof(AWideChar)), s,
                            result);
                    if (!result) {
                        exception = EX_MEMORY_ERROR;
                        goto RaiseException;
                    }
                    
                    AInitNonPointerBlock(&s->header, sizeof(AWideChar));
                    s->elem[0] = wch;

                    item = AWideStrToValue(s);
                } else
                    goto TrySubStr;
            } else {
                /* Invalid base object. */
                AValue args[2];
                args[0] = base;
                args[1] = index;
                ARaiseMethodCallExceptionND(t, AM_GET_ITEM, args, 1);
                goto ExceptionRaised;
            }
        }

        if (AIsError(item))
            goto ExceptionRaised;

        stack[ip[1]] = item;
        ip += 2;

        A_END_OPCODE;
    }

    A_OPCODE(OP_JMP) {
        /* goto offset{1} */
        ip = ABranchTarget(ip + 1);
        PERIODIC_INTERPRETER_CHECK;
        A_END_OPCODE;
    }

    A_OPCODE(OP_ASSIGN_TRUEL_SKIP) {
        /* True -> lvar{2}

           Ignore ip[1]. */
        stack[ip[2]] = ATrue;
        ip += 3;
        A_END_OPCODE;
    }

    {
        AValue index;
        AValue base;
        AValue *itemPtr;
            
        A_OPCODE(OP_ASET_LLL) {
            /* lvar{3} -> lvar{1}[lvar{2}] */
            base = stack[ip[1]];
            ip += 2;
            goto SetItem;
        }
            
        A_OPCODE(OP_ASET_GLL) {
            /* lvar{3} -> gvar{1}[lvar{2}] */
            base = AGlobalVars[ip[1]];
            ip += 2;
            goto SetItem;
        }
            
      SetItem:

        index = stack[ip[0]];
        itemPtr = &stack[ip[1] - A_NO_RET_VAL];

        if (AIsArray(base)) {
            AInstance *i = AValueToInstance(base);

            if (AIsShortInt(index) && index < i->member[A_ARRAY_LEN]) {
                ABool result;
                AFixArray *a = AValueToFixArray(i->member[A_ARRAY_A]);

                AModifyObject_M(t, &a->header,
                               AGetFixArrayItemPtr(a, index),
                               itemPtr, result);
                if (!result) {
                    exception = EX_MEMORY_ERROR;
                    goto RaiseException;
                }
                    
                ip += 2;
                    
                A_END_OPCODE;
            } else if (AIsShortInt(index) &&
                       (-index - AIntToValue(1)) < i->member[A_ARRAY_LEN]) {
                ABool result;
                AFixArray *a = AValueToFixArray(i->member[A_ARRAY_A]);

                AModifyObject_M(t, &a->header,
                               AGetFixArrayItemPtr(a, i->member[A_ARRAY_LEN] +
                                                   index),
                               itemPtr, result);
                if (!result) {
                    exception = EX_MEMORY_ERROR;
                    goto RaiseException;
                }
                    
                ip += 2;
                    
                A_END_OPCODE;
            } else if (AIsShortInt(index) || AIsLongInt(index)) {
                exception = EX_INDEX_ERROR;
                goto RaiseException;
            } else {
                exception = EX_TYPE_ERROR;
                goto RaiseException;
            }
        }

        if (AIsInstance(base)) {
            member = AM_SET_ITEM;
            numArgs = 3;
            args[0] = base;
            args[1] = index;
            args[2] = *itemPtr;
            ip++;
            goto MethodCall;
        } else {
            exception = EX_TYPE_ERROR;
            goto RaiseException;
        }
    }

    {
        unsigned fullArgCnt;
        unsigned argCnt;
        AValue funcVal;
        AInstance *instance;
        
        A_OPCODE(OP_CALL_L) {
            /* Call lvar{1} int{2} args... -> lvar{} */
            funcVal = stack[ip[1]];
            ip += 3;

            goto Call;
        }

        A_OPCODE(OP_CALL_G) {
            /* Call gvar{1} int{2} args... -> lvar{} */
            funcVal = AGlobalByNum(ip[1]);
            ip += 3;

            goto Call;
        }

        A_OPCODE(OP_CALL_M) {
            /* Call member{1} int{2} lvar{3} args... -> lvar{} */
            
            /* Call member function. lvar{3} is the base object and the first
               argument. */

            unsigned key = ip[1];
            AValue object = stack[ip[3]];

            /* Wrap non-instance objects (primitive types such as Int and
               Str). */
            if (!AIsInstance(object)) {
                object = AWrapObject(t, object);
                if (AIsError(object))
                    goto ExceptionRaised;
            }

            /* Search instance has tables. */
            {
                AInstance *inst = AValueToInstance(object);
                ATypeInfo *type = AGetInstanceType(inst);
                AMemberHashTable *table = AGetMemberTable(type,
                                                          MT_METHOD_PUBLIC);
                AMemberNode *node;

                /* Get the initial node in a hash chain. */
                node = &table->item[key & table->size];
                
                while (node->key != key) {
                    if (node->next == NULL) {
                        if (type->super == NULL) {
                            funcVal = AGetInstanceDataMember(t, inst, key);
                            if (!AIsError(funcVal)) {
                                ip += 4;
                                fullArgCnt = ip[-2] - 1;
                                goto FullArgcReady;
                            } else {
                                exception = EX_MEMBER_ERROR;
                                args[0] = stack[ip[3]];
                                member = key;
                                goto RaiseException;
                            }
                        } else {
                            type = type->super;
                            table = AGetMemberTable(type, MT_METHOD_PUBLIC);
                            node = &table->item[key & table->size];
                        }
                    } else
                        node = node->next;
                }

                funcVal = AGlobalByNum(node->item);
            
                ip += 3;
            }
            
          Call:

            /* Stack frame format:

               stack:
                 <stack frame size>
                 <function value>
                 <return address>
                 <arguments>
                 <local values>
               previous function invocation:
                 ...
            */

            fullArgCnt = ip[-1];

          FullArgcReady:

            argCnt = fullArgCnt & ~A_VAR_ARG_FLAG;

            if (AIsInterrupt) {
                *t->tempStack = funcVal;
                if (AHandleInterrupt(t))
                    goto ExceptionRaised;
                funcVal = *t->tempStack;
            }

            if (AIsGlobalFunction(funcVal)) {
                AFunction *func;
                AValue *newStack;
                unsigned stackFrameSize;
                AValue *sp;

                /* Store the return opcode address. */
                stack[2] = AMakeInterpretedOpcodeIndex(
                    ip + argCnt - AValueToFunction(stack[1])->code.opc);
                
                func = AValueToFunction(funcVal);

                stackFrameSize = func->stackFrameSize;
                newStack = APreviousFrame(stack, stackFrameSize);
                if (newStack < t->stack) {
                    exception = AErrorId(EX_RUNTIME_ERROR,
                                         EM_TOO_MANY_RECURSIVE_CALLS);
                    goto RaiseException;
                }
                
                /* Initialize the header of the new stack frame. */
                newStack[2] = A_COMPILED_FRAME_FLAG;
                newStack[1] = funcVal;
                newStack[0] = stackFrameSize;

                /* Clear temporaries in the new stack frame. */
                for (sp = newStack + 3 + func->minArgs;
                     (void *)sp < ANextFrame(newStack, stackFrameSize); sp++)
                    *sp = AZero;

                /* Check and store arguments. FIX: document this behaviour? */
                if (fullArgCnt != func->maxArgs || argCnt != fullArgCnt) {
                    t->stackPtr = newStack;
                    if (!ASetupArguments(t, stack, ip, fullArgCnt,
                                         newStack + 3, func->minArgs,
                                         func->maxArgs))
                        goto ExceptionRaised;
                } else {
                    /* Handle the basic case (no default args or varargs) as
                       a special case (faster). */
                    int i;
                    for (i = 0; i < argCnt; i++)
                        newStack[3 + i] = stack[ip[i]];
                }

                /* Record new stack pointer. */
                /* FIX: move above before arg check */
                t->stackPtr = newStack;

                /* Is it a native code function that is directly callable from
                   C? */
                if (AIsCompiledFunction(func)) {
                    AValue retVal;

                    ip += argCnt;

                    /* Call the function */
                    retVal = func->code.cfunc(t, newStack + 3);
                    
                    if (!AIsError(retVal)) {
                        if (ip[0] < A_NO_RET_VAL)
                            stack[ip[0]] = retVal;
                    } else {
                        /* Exception was raised in the C function. */
                        stack = newStack;
                        ip = NULL;
                        goto ExceptionRaised;
                    }

                    t->stackPtr = stack;
                    
                    ip++;
                } else {
                    /* Update stack frame. */
                    stack = newStack;

                    /* Begin excuting the function. */
                    ip = func->code.opc;
                }
            } else if (AIsNonSpecialType(funcVal)) {
                AFunction *func;
                AValue *newStack;
                unsigned stackFrameSize;
                AValue *sp;
                ATypeInfo *type;

                type = AValueToType(funcVal);
                if (type->isInterface) {
                    exception = AErrorId(EX_VALUE_ERROR, EM_CALL_INTERFACE);
                    goto RaiseException;
                }
                
                funcVal = AGlobalByNum(type->create);

                /* Create the instance. */
                if (t->heapPtr + type->instanceSize <= t->heapEnd
                    && !A_NO_INLINE_ALLOC) {
                    instance = (AInstance *)t->heapPtr;
                    t->heapPtr += type->instanceSize;
                } else {
                    instance = AAlloc(t, type->instanceSize);
                    if (instance == NULL)
                        goto ExceptionRaised;
                }
                
                AInitInstanceBlock(&instance->type, type);
                AInitInstanceData(instance, type);

              CallWithInstance:

                /* Store the return opcode address. */
                stack[2] = AMakeInterpretedOpcodeIndex(
                    ip + argCnt - AValueToFunction(stack[1])->code.opc);
                
                func = AValueToFunction(funcVal);

                stackFrameSize = func->stackFrameSize;
                /* FIX: handle underflow? */
                newStack = APreviousFrame(stack, stackFrameSize);
                
                if (newStack < t->stack) {
                    exception = AErrorId(EX_RUNTIME_ERROR,
                                         EM_TOO_MANY_RECURSIVE_CALLS);
                    goto RaiseException;
                }

                /* Initialize the new stack frame header. */
                newStack[2] = A_COMPILED_FRAME_FLAG;
                newStack[1] = funcVal;
                newStack[0] = stackFrameSize;

                /* Clear temporaries. */
                for (sp = newStack + 3 + func->minArgs;
                     (void *)sp < ANextFrame(newStack, stackFrameSize); sp++)
                    *sp = AZero;

                /* Store self. */
                newStack[3] = AInstanceToValue(instance);

                /* Check and store arguments. */
                if (fullArgCnt != func->maxArgs - 1 || argCnt != fullArgCnt) {
                    t->stackPtr = newStack;
                    if (!ASetupArguments(t, stack, ip, fullArgCnt, newStack
                                        + 4, func->minArgs - 1,
                                        func->maxArgs - 1))
                        goto ExceptionRaised;
                } else {
                    int i;
                    for (i = 0; i < argCnt; i++)
                        newStack[3 + 1 + i] = stack[ip[i]];
                }

                /* Record stack pointer. */
                t->stackPtr = newStack;
                
                if (AIsCompiledFunction(func)) {
                    AValue retVal;
                    
                    ip += argCnt;
                    
                    /* Call the function */
                    retVal = func->code.cfunc(t, newStack + 3);

                    if (!AIsError(retVal)) {
                        if (ip[0] < A_NO_RET_VAL)
                            stack[ip[0]] = retVal;
                    } else {
                        /* Exception was raised in the C function. */
                        stack = newStack;
                        ip = NULL;
                        goto ExceptionRaised;
                    }

                    t->stackPtr = stack;

                    ip++;
                } else {
                    /* Update stack frame. */
                    stack = newStack;

                    /* Begin excuting the function. */
                    ip = func->code.opc;
                }
            } else if (AIsMixedValue(funcVal)) {
                AMixedObject *method = AValueToMixedObject(funcVal);

                if (method->type == A_BOUND_METHOD_ID) {
                    instance = AValueToInstance(
                                   method->data.boundMethod.instance);
                    funcVal = AGlobalByNum(AValueToInt(
                                           method->data.boundMethod.method));
                    goto CallWithInstance;
                } else {
                    ARaiseInvalidCallableErrorND(t, funcVal);
                    goto ExceptionRaised;
                }
            } else if (AIsInstance(funcVal)) {          
                int funcNum;

                instance = AValueToInstance(funcVal);
                funcNum = AGetInstanceMember(instance, MT_METHOD_PUBLIC,
                                             AM_CALL);
                if (funcNum != -1) {
                    funcVal = AGlobalByNum(funcNum);
                    goto CallWithInstance;
                } else {
                    ARaiseInvalidCallableErrorND(t, funcVal);
                    goto ExceptionRaised;
                }
            } else {
                ARaiseInvalidCallableErrorND(t, funcVal);
                goto ExceptionRaised;
            }
            
            A_END_OPCODE;
        }
    }

    A_OPCODE(OP_RAISE_L) {
        /* raise lvar{1} */
        
        AValue val = stack[ip[1]];

        if (AIsOfType(val, AGlobalByNum(AStdExceptionNum)) != A_IS_TRUE) {
            ARaiseTypeErrorND(t,
                "Exception must be derived from std::Exception (not %T)", val);
            goto ExceptionRaised;
        }
        
        /* Record state information related to the raised exception. */
        t->exception = val;
        t->uncaughtExceptionStackPtr = stack;

        goto ExceptionRaised;
    }

    A_OPCODE(OP_RET_L) {
        /* return lvar{1} */

        AValue retVal;

      RET_L_Opcode:
        
#ifdef HAVE_JIT_COMPILER
        /* If the current function is not part of the JIT compiler, compile
           it. */
        if (AIsJitCompilerActive
            && !AValueToFunction(stack[1])->isJitFunction) {
            if (!AJitCompileFunction(t, stack[1]))
                goto ExceptionRaised;
        }
#endif

        retVal = stack[ip[1]];
#ifdef A_DEBUG_SHOW_RETURN_VALUES
        if (ADebugIsOn()) {
            ADebugPrint((A_MSG_HEADER "return "));
            ADebugPrintValueWithType(retVal);
            ADebugPrint(("\n"));
        }
#endif
        
        stack = ANextFrame(stack, stack[0]);
        t->stackPtr = stack;

        if (!AIsInterpretedFrame(stack))
            return retVal;
        
        ip = AGetFrameIp(stack);
        
        /* Does the caller expect a return value? */
        if (ip[-1] < A_NO_RET_VAL) 
            stack[ip[-1]] = retVal;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_RET) {
        /* return nil */
        
#ifdef HAVE_JIT_COMPILER
        /* If the current function is not part of the JIT compiler, compile
           it. */
        if (AIsJitCompilerActive
            && !AValueToFunction(stack[1])->isJitFunction) {
            if (!AJitCompileFunction(t, stack[1]))
                goto ExceptionRaised;
        }
#endif

        stack = ANextFrame(stack, stack[0]);
        t->stackPtr = stack;
        
        if (!AIsInterpretedFrame(stack))
            return ANil;
        
        ip = AGetFrameIp(stack);
        
        /* Does the caller expect a return value? */
        if (ip[-1] < A_NO_RET_VAL)
            stack[ip[-1]] = ANil;

        A_END_OPCODE;
    }

    A_OPCODE(OP_CREATE_ANON) {
        /* OP_CREATE_ANON Gvar N Lvar1 ... LvarN -> Lvar
           Create an anonymous function object. */
        AInstance *inst;
        AFixArray *a;
        int i;
        int n;

        /* IDEA: Optimize if ip[2] == 0? */
        
        /* Number of exposed variables to store in the object */
        n = ip[2];
        
        /* Allocate FixArray containing exposed variable containers used by
           the anonymous function. */
        a = AAlloc(t, sizeof(AValue) + n * sizeof(AValue));
        if (a == NULL)
            goto ExceptionRaised;
        AInitValueBlock(&a->header, n * sizeof(AValue));
        for (i = 0; i < n; i++)
            a->elem[i] = stack[ip[3 + i]];
        stack[ip[3 + n]] = AFixArrayToValue(a);
        
        /* Construct anonymous function object. */
        inst = AAlloc(t, sizeof(AValue) * 3);
        if (inst == NULL)
            goto ExceptionRaised;
        AInitInstanceBlock(&inst->type,
                           AValueToType(AGlobalByNum(AAnonFuncClassNum)));
        inst->member[A_ANON_EXPOSED_VARS] = stack[ip[3 + n]];
        inst->member[A_ANON_IMPLEMENTATION_FUNC] = AGlobalByNum(ip[1]);
        stack[ip[3 + n]] = AInstanceToValue(inst);
        
        ip += 4 + n;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_CREATE_EXPOSED) {
        /* Create a value container that is used to store the value of an
           exposed local variable. It is actually a FixArray object with
           length 1. */
        
        AFixArray *a = AAlloc(t, sizeof(AValue) * 2);
        if (a == NULL)
            goto ExceptionRaised;        
        AInitValueBlock(&a->header, 1 * sizeof(AValue));
        a->elem[0] = stack[ip[1]];
        
        stack[ip[1]] = AFixArrayToValue(a);
        
        ip += 2;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_CHECK_TYPE) {
        /* if not lvar{1} is gvar{2} raise CastError(...) */
        AValue inst = stack[ip[1]];
        AValue type = AGlobalByNum(ip[2]);
        if (AIsOfType(inst, type) != A_IS_TRUE) {
            ARaiseCastErrorND(t, inst, type);
            goto ExceptionRaised;
        }
        ip += 3;
        A_END_OPCODE;
    }

    A_OPCODE(OP_FOR_INIT) {
        /* Initialize for lvar{1} in lvar{2}; goto offset{3}

           For arrays, store array at lvar{1} + 1 and current index at
           lvar{1} + 2.

           For ranges, store lower bound at lvar{1} + 1 and upper bound at
           lvar{1} + 2.
           
           Otherwise, store iterator object at lvar{1} + 1. */
        AValue collection = stack[ip[2]];

        if (AIsArray(collection)) {
            /* Iterator over an array. */
            stack[ip[1] + 1] = collection;
            stack[ip[1] + 2] = AIntToValue(0);
        } else if (!AIsRange(collection)) {
            /* Generic for loop (call iterator() method). */
            AValue iter;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX Line number missing */
            iter = ACallMethodByNum(t, AM_ITERATOR, 0, stack + ip[2]);
            if (AIsError(iter))
                goto ExceptionRaised;
            if (!AIsInstance(iter)) {
                exception = EX_TYPE_ERROR;
                goto RaiseException;
            }
            stack[ip[1] + 1] = iter;
        } else {
            /* Iterate over range. */
            AValue lo = ARangeStart(collection);
            AValue hi = ARangeStop(collection);

            if ((AIsShortInt(lo) || AIsLongInt(lo)) &&
                (AIsShortInt(hi) || AIsLongInt(hi))) {
                stack[ip[1] + 1] = lo;
                stack[ip[1] + 2] = hi;
            } else {
                exception = EX_TYPE_ERROR;
                goto RaiseException;
            }
        }
        
        ip = ABranchTarget(ip + 3);

        A_END_OPCODE;
    }

    A_OPCODE(OP_FOR_LOOP) {
        /* Advance an iteration of a for loop. Use optimized special cases for
           iteration over arrays and ranges. */
        AValue v1 = stack[ip[1] + 1];

        if (AIsArray(v1)) {
            int i = AValueToInt(stack[ip[1] + 2]);
            if (i < AArrayLen(v1)) {
                /* Perform another iteration. */
                stack[ip[1]] = AArrayItem(v1, i);
                stack[ip[1] + 2] += AIntToValue(1);
                ip = ABranchTarget(ip + 2);
                PERIODIC_INTERPRETER_CHECK;
            } else {
                /* End loop. */
                ip += 3;
            }
        } else if (AIsInstance(v1)) {
            AValue r;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX Line number missing */
            r = ACallMethodByNum(t, AM_HAS_NEXT, 0, stack + ip[1] + 1);
            if (AIsError(r))
                goto ExceptionRaised;

            if (AIsTrue(r)) {
                /* Perform another iteration. */
                r = ACallMethodByNum(t, AM_NEXT, 0, stack + ip[1] + 1);
                if (AIsError(r))
                    goto ExceptionRaised;
                stack[ip[1]] = r;
                ip = ABranchTarget(ip + 2);
                PERIODIC_INTERPRETER_CHECK;
            } else if (AIsFalse(r)) {
                /* End loop. */
                ip += 3;
            } else {
                exception = EX_TYPE_ERROR;
                goto RaiseException;
            }
        } else {
            /* Int / LongInt range */
            
            AValue res = ACompareOrder(t, v1, stack[ip[1] + 2], OPER_LT);
            if (AIsError(res))
                goto ExceptionRaised;

            if (res != AZero) {
                /* Perform another iteration. */
                stack[ip[1]] = v1;
                v1 = AAddInts(t, v1, AIntToValue(1));
                if (AIsError(v1))
                    goto ExceptionRaised;
                stack[ip[1] + 1] = v1;
                ip = ABranchTarget(ip + 2);
                PERIODIC_INTERPRETER_CHECK;
            } else {
                /* End loop. */
                ip += 3;
            }
        }

        A_END_OPCODE;
    }

    A_OPCODE(OP_FOR_LOOP_RANGE) {
        /* Advance an iteration of a for loop over a range. */
        AValue lo = stack[ip[1]];
        AValue hi = stack[ip[1] + 1];
        if (!(AIsShortInt(lo) && AIsShortInt(hi)
              && (ASignedValue)lo + (ASignedValue)AIntToValue(1) <
                      (ASignedValue)hi)) {
            if ((AIsShortInt(lo) && AIsShortInt(hi)))
                ip += 3;
            else {
                lo = AAddInts(t, lo, AIntToValue(1));
                if (AIsError(lo))
                    goto ExceptionRaised;
                if (ACompareOrder(t, lo, stack[ip[1] + 1], OPER_GTE) != AZero)
                    ip += 3;
                else {
                    stack[ip[1]] = lo;
                    ip = ABranchTarget(ip + 2);
                    PERIODIC_INTERPRETER_CHECK;
                }
            }
        } else {
            stack[ip[1]] = lo + AIntToValue(1);
            ip = ABranchTarget(ip + 2);
            PERIODIC_INTERPRETER_CHECK;
        }

        A_END_OPCODE;
    }

    A_OPCODE(OP_IF_TRUE_L) {
        if (stack[ip[1]] == ATrue) {
            ip = ABranchTarget(ip + 2);
            PERIODIC_INTERPRETER_CHECK;
        } else if (stack[ip[1]] == AFalse)
            ip += 3;
        else {
            ARaiseInvalidBooleanErrorND(t, stack[ip[1]]);
            goto ExceptionRaised;
        }
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_IF_FALSE_L) {
        if (stack[ip[1]] == AFalse) {
            ip = ABranchTarget(ip + 2);
            PERIODIC_INTERPRETER_CHECK;
        } else if (stack[ip[1]] == ATrue)
            ip += 3;
        else {
            ARaiseInvalidBooleanErrorND(t, stack[ip[1]]);
            goto ExceptionRaised;
        }
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_CREATE_ARRAY) {
        unsigned len;
        AValue array;
        ABool result;
        int start;
        int i;
        
        array = AZero; /* Initialize to avoid a warning. */
        len = ip[1];
        AMakeUninitArray_M(t, len, array, result);
        if (!result) {
            exception = EX_MEMORY_ERROR;
            goto RaiseException;
        }

        start = ip[2];
        for (i = 0; i < len; i++)
            ASetArrayItemNewGen(array, i, stack[start + i]);
        stack[ip[3]] = array;
        
        ip += 4;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_CREATE_TUPLE) {
        unsigned len;
        AValue tuple;
        ABool result;
        int start;
        int i;
        
        tuple = AZero; /* Initialize to avoid a warning. */
        len = ip[1];
        AMakeUninitTuple_M(t, len, tuple, result);
        if (!result) {
            exception = EX_MEMORY_ERROR;
            goto RaiseException;
        }

        start = ip[2];
        for (i = 0; i < len; i++)
            ASetTupleItemNewGen(tuple, i, stack[start + i]);
        stack[ip[3]] = tuple;
        
        ip += 4;
        
        A_END_OPCODE;
    }

    A_OPCODE(OP_EXPAND) {
        AValue val;
        int num;
        int i;

        /* NOTE: The targets and the input may overlap, or at least the last
                 target. */

        val = stack[ip[1]];
        if (AIsTuple(val)) {
            num = ip[2];
            if (ATupleLen(val) != num) {
                exception = ExpandErrorId(ATupleLen(val), num);
                goto RaiseException;
            }
            
            for (i = 0; i < num; i++)
                stack[ip[3 + i]] = ATupleItem(val, i);
            
            ip += 3 + num;
            
            A_END_OPCODE;
        } else if (AIsArraySubType(val)) {
            num = ip[2];
            if (AArrayLen(val) != num) {
                exception = ExpandErrorId(AArrayLen(val), num);
                goto RaiseException;
            }
            
            for (i = 0; i < num; i++)
                stack[ip[3 + i]] = AArrayItem(val, i);
            
            ip += 3 + num;
            
            A_END_OPCODE;
        } else {
            ARaiseTypeErrorND(t, "Array or Tuple expected (but %T found)",
                              val);
            goto ExceptionRaised;
        }
    }

    A_OPCODE(OP_IS_DEFAULT) {
        if (stack[ip[1]] != ADefault)
            ip = ABranchTarget(ip + 2);
        else
            ip += 3;
        
        A_END_OPCODE;
    }

    {
        AOpcode opcode;
        AValue left;
        AValue right;
            
        A_OPCODE(OP_ADD_LLL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                ip += 2;
                goto IntAdd;
            } else {
                ip += 2;
                opcode = OP_ADD_L;
                goto ArithmeticOpcode;
            }
        }
        
        A_OPCODE(OP_SUB_LLL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                ip += 2;
                goto IntSub;
            } else {
                ip += 2;
                opcode = OP_SUB_L;
                goto ArithmeticOpcode;
            }
        }
            
        A_OPCODE(OP_EQ_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left == (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                ip += 2;
                opcode = OP_EQ;
                goto ArithmeticOpcode;
            }
        }
        
        A_OPCODE(OP_NEQ_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left != (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                ip += 2;
                opcode = OP_NEQ;
                goto ArithmeticOpcode;
            }
        }
            
        A_OPCODE(OP_LT_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left < (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                opcode = OP_LT;
                ip += 2;
                goto ArithmeticOpcode;
            }
        }
            
        A_OPCODE(OP_GTE_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left >= (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                opcode = OP_GTE;
                ip += 2;
                goto ArithmeticOpcode;
            }
        }
        
        A_OPCODE(OP_GT_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left > (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                opcode = OP_GT;
                ip += 2;
                goto ArithmeticOpcode;
            }
        }
            
        A_OPCODE(OP_LTE_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right)) {
                if ((ASignedValue)left <= (ASignedValue)right) {
                    ip = ABranchTarget(ip + 3);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 4;
                A_END_OPCODE;
            } else {
                opcode = OP_LTE;
                ip += 2;
                goto ArithmeticOpcode;
            }
        }
            
        A_OPCODE(OP_GET_LL) {
            left  = stack[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_LI) {
            left  = stack[ip[1]];
            right = ip[2];

            if (AIsShortInt(left))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_LG) {
            left  = stack[ip[1]];
            right = AGlobalVars[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_IL) {
            left  = ip[1];
            right = stack[ip[2]];

            if (AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_II) {
            left  = ip[1];
            right = ip[2];
            goto IntOper;
        }
            
        A_OPCODE(OP_GET_IG) {
            left  = ip[1];
            right = AGlobalVars[ip[2]];

            if (AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_GL) {
            left  = AGlobalVars[ip[1]];
            right = stack[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_GI) {
            left  = AGlobalVars[ip[1]];
            right = ip[2];

            if (AIsShortInt(left))
                goto IntOper;
            else
                goto NonIntOper;
        }
            
        A_OPCODE(OP_GET_GG) {
            left  = AGlobalVars[ip[1]];
            right = AGlobalVars[ip[2]];

            if (AIsShortInt(left) && AIsShortInt(right))
                goto IntOper;
            else
                goto NonIntOper;
        }

      IntOper:

        ip += 3;

        /* FIX: this switch stmt probably needs some restructuring... */

        switch (*ip) {
        case OP_ADD_L: {
            AValue sum;
                
          IntAdd:

            sum = left + right;
            if (AIsAddOverflow(sum, left, right)) {
                sum = ACreateLongIntFromIntND(t, AValueToInt(left) +
                                           AValueToInt(right));
                if (AIsError(sum))
                    goto ExceptionRaised;
            }

            stack[ip[1]] = sum;
            ip += 2;
            
            break;
        }

        case OP_SUB_L: {
            AValue dif;
                
          IntSub:

            dif = left - right;         
            if (AIsSubOverflow(dif, left, right)) {
                dif = ACreateLongIntFromIntND(t, AValueToInt(left) -
                                           AValueToInt(right));
                if (AIsError(dif))
                    goto ExceptionRaised;
            }

            stack[ip[1]] = dif;
            ip += 2;
            
            break;
        }

        case OP_MUL_L: {
            AValue prod;
            
            if (AHi(left) == 0 && AHi(right) == 0) {
                prod = AValueToInt(left) * (ASignedValue)right;
                if ((ASignedValue)prod >= 0)
                    goto MulOk;
            }

            prod = AMultiplyShortInt(t, left, right);
            if (AIsError(prod))
                goto ExceptionRaised;

          MulOk:

            stack[ip[1]] = prod;
            ip += 2;

            break;
        }
            
        case OP_DIV_L: {
            AValue quot;

            if (right == AZero) {
                exception = AErrorId(EX_ARITHMETIC_ERROR,
                                    EM_DIVISION_BY_ZERO);
                goto RaiseException;
            }

            quot = ACreateFloat(t, (double)AValueToInt(left) /
                                AValueToInt(right));
            if (AIsError(quot))
                goto ExceptionRaised;

            stack[ip[1]] = quot;
            ip += 2;

            break;
        }
            
        case OP_IDV_L: {
            AValue quot;
            AValue mod;

            quot = AIntDivMod(t, left, right, &mod);
            if (AIsError(quot))
                goto ExceptionRaised;

            stack[ip[1]] = quot;
            ip += 2;

            break;
        }
            
        case OP_MOD_L: {
            /* FIX: Perhaps check if can use bitwise and..? */
            AValue mod;

            if (AIntDivMod(t, left, right, &mod) == AError)
                goto ExceptionRaised;

            stack[ip[1]] = mod;
            ip += 2;
            
            break;
        }
            
        case OP_IN_L:
        case OP_NOT_IN_L:
            /* "in" not suported for ints. */
            ARaiseBinopTypeErrorND(t, OPER_IN, left, right);
            goto ExceptionRaised;

        case OP_IS_L:
        case OP_IS_NOT_L:
            ARaiseBinopTypeErrorND(t, OPER_IS, left, right);
            goto ExceptionRaised;

          CreatePair:
        case OP_PAIR_L: {
            AMixedObject *pair;
            ABool result;

            t->tempStack[0] = left;
            t->tempStack[1] = right;

            AAlloc_M(t, AGetBlockSize(sizeof(AMixedObject)), pair, result);

            if (!result) {
                exception = EX_MEMORY_ERROR;
                goto RaiseException;
            }
            
            AInitValueBlock(&pair->header, 3 * sizeof(AValue));
            pair->type = A_PAIR_ID;
            pair->data.pair.head = t->tempStack[0];
            pair->data.pair.tail = t->tempStack[1];
            
            stack[ip[1]] = AMixedObjectToValue(pair);
            ip+= 2;
            
            break;
        }

          CreateRange:
        case OP_RNG_L: {
            AMixedObject *rng;
            ABool result;

            if (!AIsInt(left)) {
                ARaiseTypeErrorND(t, AMsgInvalidRangeLowerBound, left);
                goto ExceptionRaised;
            }
            if (!AIsInt(right)) {
                ARaiseTypeErrorND(t, AMsgInvalidRangeUpperBound, right);
                goto ExceptionRaised;
            }

            t->tempStack[0] = left;
            t->tempStack[1] = right;
            
            AAlloc_M(t, AGetBlockSize(sizeof(AMixedObject)), rng, result);

            if (!result) {
                exception = EX_MEMORY_ERROR;
                goto RaiseException;
                /* IDEA: Combine all similar exception raise statements into
                         a single goto destination. */
            }
            
            AInitValueBlock(&rng->header, 3 * sizeof(AValue));
            rng->type = A_RANGE_ID;
            rng->data.range.start = t->tempStack[0];
            rng->data.range.stop = t->tempStack[1];
            
            stack[ip[1]] = AMixedObjectToValue(rng);
            ip += 2;

            break;
        }

        case OP_POW_L: {
            AValue power = APowInt(t, left, right);
            if (AIsError(power))
                goto ExceptionRaised;
            
            stack[ip[1]] = power;
            ip += 2;
            
            break;
        }

        case OP_EQ:
            if (left == right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;

        case OP_NEQ:
            if (left != right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;

        case OP_LT:
            if ((ASignedValue)left < (ASignedValue)right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;
                
        case OP_GTE:
            if ((ASignedValue)left >= (ASignedValue)right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;
            
        case OP_GT:
            if ((ASignedValue)left > (ASignedValue)right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;
                
        case OP_LTE:
            if ((ASignedValue)left <= (ASignedValue)right) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            break;

        case OP_FOR_L:
            goto InitForRange;
        }

        A_END_OPCODE;

      NonIntOper:

        ip += 3;
        opcode = *ip;

      ArithmeticOpcode:

        switch (opcode) {
        case OP_ADD_L: {
            AValue sum;
            
            for (;;) {
                AValue temp;
                
                if (AIsFloat(left) && AIsFloat(right)) {
                    double result = AValueToFloat(left) + AValueToFloat(right);
                    if (t->heapPtr == t->heapEnd
                        || A_NO_INLINE_ALLOC) {
                        sum = ACreateFloat(t, result);
                        if (AIsError(sum))
                            goto ExceptionRaised;
                    } else {
                        *(double *)t->heapPtr = result;
                        sum = AFloatPtrToValue(t->heapPtr);
                        t->heapPtr += A_FLOAT_SIZE;
                    }
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_ADD;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                if (AIsInstance(right) && (AIsInt(left) || AIsFloat(left))) {
                    member = AM_ADD;
                    args[0] = right;
                    args[1] = left;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                {
                    unsigned char *leftStr;
                    unsigned char *rightStr;
                    int leftLen;
                    int rightLen;
                    
                    if (AIsNarrowStr(left)) {
                        AString *res;
                        int blockSize;
                        int resultLen;
                        
                        leftLen = AGetStrLen(AValueToStr(left));
                        leftStr = AValueToStr(left)->elem;

                      NarrowLeft:
                        
                        if (AIsNarrowStr(right)) {
                            rightLen = AGetStrLen(AValueToStr(right));
                            rightStr = AValueToStr(right)->elem;
                        } else if (AIsSubStr(right)) {
                            ASubString *subStr;

                            subStr = AValueToSubStr(right);
                            if (AIsWideStr(subStr->str))
                                goto ConcatWide;

                            rightStr = AValueToStr(subStr->str)->elem +
                                       AValueToInt(subStr->ind);
                            rightLen = AValueToInt(subStr->len);
                        } else
                            goto ConcatWide;
                        
                        resultLen = leftLen + rightLen;
                        blockSize = AGetBlockSize(sizeof(AValue) + resultLen);
                        
                        if (t->heapPtr + blockSize > t->heapEnd
                            || A_NO_INLINE_ALLOC) {
                            t->tempStack[0] = left;
                            t->tempStack[1] = right;
                            
                            res = AAlloc(t, blockSize);
                            if (res == NULL)
                                goto ExceptionRaised;

                            left  = t->tempStack[0];
                            right = t->tempStack[1];

                            if (AIsNarrowStr(left))
                                leftStr = AGetStrElem(t->tempStack[0]);
                            else
                                leftStr = AGetSubStrElem(t->tempStack[0]);
                            
                            if (AIsNarrowStr(right))
                                rightStr = AGetStrElem(t->tempStack[1]);
                            else
                                rightStr = AGetSubStrElem(t->tempStack[1]);
                        } else {
                            res = (AString *)t->heapPtr;
                            t->heapPtr += blockSize;
                        }
                        
                        AInitNonPointerBlock(&res->header, resultLen);
                        
                        ACopyMem(res->elem, leftStr, leftLen);
                        ACopyMem(res->elem + leftLen, rightStr, rightLen);
                        
                        sum = AStrToValue(res);
                        break;
                    }   
                    
                    if (AIsSubStr(left)) {
                        ASubString *subStr;

                        subStr = AValueToSubStr(left);
                        
                        if (AIsWideStr(subStr->str))
                            goto ConcatWide;

                        leftStr = AValueToStr(subStr->str)->elem +
                                  AValueToInt(subStr->ind);
                        leftLen = AValueToInt(subStr->len);
                        
                        goto NarrowLeft;
                    }
                    
                    if (AIsWideStr(left)) {

                      ConcatWide:
                        
                        sum = AConcatWideStrings(t, left, right);
                        if (AIsError(sum))
                            goto ExceptionRaised;
                        break;
                    }
                }

                if (AIsLongInt(left) && AIsLongInt(right)) {
                    sum = AAddLongInt(t, left, right);
                    if (AIsError(sum))
                        goto ExceptionRaised;
                    break;
                }

                left = ACoerce(t, OPER_PLUS, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;
                
                right = temp;
            }

            stack[ip[1]] = sum;
            ip += 2;
            
            break;
        }
                
        case OP_SUB_L: {
            AValue dif;
            
            for (;;) {
                AValue temp;
                
                if (AIsFloat(left) && AIsFloat(right)) {
                    double result = AValueToFloat(left) - AValueToFloat(right);
                    if (t->heapPtr == t->heapEnd
                        || A_NO_INLINE_ALLOC) {
                        dif = ACreateFloat(t, result);
                        if (AIsError(dif))
                            goto ExceptionRaised;
                    } else {
                        *(double *)t->heapPtr = result;
                        dif = AFloatPtrToValue(t->heapPtr);
                        t->heapPtr += A_FLOAT_SIZE;
                    }
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_SUB;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }
            
                if (AIsLongInt(left) && AIsLongInt(right)) {
                    dif = ASubLongInt(t, left, right);
                    if (AIsError(dif))
                        goto ExceptionRaised;
                    break;
                }

                left = ACoerce(t, OPER_MINUS, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;
                
                right = temp;
            }

            stack[ip[1]] = dif;
            ip += 2;
            
            break;
        }
                
        case OP_EQ:
            if (AIsConstant(left)) {
                if (left == right) {
                    ip = ABranchTarget(ip + 1);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 2;
            } else {
                AValue res;

                stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers
                                                     missing */
                res = AIsEqual(t, left, right);
                if (AIsError(res))
                    goto ExceptionRaised;
                if (res != AZero) {
                    ip = ABranchTarget(ip + 1);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 2;
            }       
            break;

        case OP_NEQ:
            if (AIsConstant(left)) {
                if (left != right) {
                    ip = ABranchTarget(ip + 1);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 2;
            } else {
                AValue res;

                stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers
                                                     missing */
                res = AIsEqual(t, left, right);
                if (AIsError(res))
                    goto ExceptionRaised;
                if (res == AZero) {
                    ip = ABranchTarget(ip + 1);
                    PERIODIC_INTERPRETER_CHECK;
                } else
                    ip += 2;
            }
            break;

        case OP_LT: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers missing */
            res = ACompareOrder(t, left, right, OPER_LT);

            if (AIsError(res))
                goto ExceptionRaised;
            
            if (res != AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
                
        case OP_GTE: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers missing */
            res = ACompareOrder(t, left, right, OPER_GTE);

            if (AIsError(res))
                goto ExceptionRaised;
            
            if (res != AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
            
        case OP_GT: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers missing */
            res = ACompareOrder(t, left, right, OPER_GT);

            if (AIsError(res))
                goto ExceptionRaised;
            
            if (res != AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
                
        case OP_LTE: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Line numbers missing */
            res = ACompareOrder(t, left, right, OPER_LTE);

            if (AIsError(res))
                goto ExceptionRaised;
            
            if (res != AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
                
        case OP_IN_L: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG;
            res = AIsIn(t, left, right);
            if (AIsError(res))
                goto ExceptionRaised;

            if (res != AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
            
        case OP_NOT_IN_L: {
            AValue res;

            stack[2] = A_COMPILED_FRAME_FLAG;
            res = AIsIn(t, left, right);
            if (AIsError(res))
                goto ExceptionRaised;

            if (res == AZero) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else
                ip += 2;
            
            break;
        }
                
        case OP_IS_L: {
            /* IDEA: IsResult is inconsistent with other return values
                     (CompareOrder). */
            
            AIsResult res = AIsOfType(left, right);
                
            if (res == A_IS_TRUE) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else if (res == A_IS_FALSE)
                ip += 2;
            else {
                ARaiseBinopTypeErrorND(t, OPER_IS, left, right);
                goto ExceptionRaised;
            }
            break;
        }
                
        case OP_IS_NOT_L: {
            AIsResult res = AIsOfType(left, right);
            
            if (res == A_IS_FALSE) {
                ip = ABranchTarget(ip + 1);
                PERIODIC_INTERPRETER_CHECK;
            } else if (res == A_IS_TRUE)
                ip += 2;
            else {
                ARaiseBinopTypeErrorND(t, OPER_IS, left, right);
                goto ExceptionRaised;
            }
            break;
        }
        
        case OP_MUL_L: {
            AValue prod;
            
            for (;;) {
                AValue temp;
                
                if (AIsFloat(left) && AIsFloat(right)) {
                    double result = AValueToFloat(left) * AValueToFloat(right);
                    if (t->heapPtr == t->heapEnd
                        || A_NO_INLINE_ALLOC) {
                        prod = ACreateFloat(t, result);
                        if (AIsError(prod))
                            goto ExceptionRaised;
                    } else {
                        *(double *)t->heapPtr = result;
                        prod = AFloatPtrToValue(t->heapPtr);
                        t->heapPtr += A_FLOAT_SIZE;
                    }
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_MUL;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                if (AIsInstance(right) && (AIsInt(left) || AIsFloat(left))) {
                    member = AM_MUL;
                    args[0] = right;
                    args[1] = left;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }
            
                if (AIsLongInt(left) && AIsLongInt(right)) {
                    prod = AMultiplyLongInt(t, left, right);
                    if (AIsError(prod))
                        goto ExceptionRaised;
                    break;
                }

                /* FIX: longint * int? */

                /* FIX: perhaps first check the integer..? */
                if (AIsNarrowStr(left) || AIsSubStr(left)
                        || AIsWideStr(left)) {
                    /* FIX: longint? */
                    if (!AIsShortInt(right)) {
                        ARaiseBinopTypeErrorND(t, OPER_MUL, left, right);
                        goto ExceptionRaised;
                    }

                    prod = ARepeatString(t, left, AValueToInt(right));
                    if (AIsError(prod))
                        goto ExceptionRaised;
                    break;
                }

                if (AIsNarrowStr(right) || AIsSubStr(right)
                      || AIsWideStr(right)) {
                    /* FIX: longint? */
                    if (!AIsShortInt(left)) {
                        ARaiseBinopTypeErrorND(t, OPER_MUL, left, right);
                        goto ExceptionRaised;
                    }

                    prod = ARepeatString(t, right, AValueToInt(left));
                    if (AIsError(prod))
                        goto ExceptionRaised;
                    break;
                }
                
                left = ACoerce(t, OPER_MUL, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;
                
                right = temp;
            }

            stack[ip[1]] = prod;
            ip += 2;
            
            break;
        }
            
        case OP_DIV_L: {
            AValue quot;

            for (;;) {
                AValue temp;

                if (AIsFloat(left) & AIsFloat(right)) {
                    double result;

                    if (AValueToFloat(right) == 0.0) {
                        exception = AErrorId(EX_ARITHMETIC_ERROR,
                                            EM_DIVISION_BY_ZERO);
                        goto ExceptionRaised;
                    }

                    result = AValueToFloat(left) / AValueToFloat(right);
                    if (t->heapPtr == t->heapEnd
                        || A_NO_INLINE_ALLOC) {
                        quot = ACreateFloat(t, result);
                        if (AIsError(quot))
                            goto ExceptionRaised;
                    } else {
                        *(double *)t->heapPtr = result;
                        quot = AFloatPtrToValue(t->heapPtr);
                        t->heapPtr += A_FLOAT_SIZE;
                    }
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_DIV;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                if (AIsLongInt(left) && AIsLongInt(right)) {
                    quot = ACreateFloat(t, ALongIntToFloat(left) /
                                        ALongIntToFloat(right));
                    if (AIsError(quot))
                        goto ExceptionRaised;
                    break;
                }

                left = ACoerce(t, OPER_DIV, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;

                right = temp;
            }

            stack[ip[1]] = quot;
            ip += 2;

            break;
        }
            
        case OP_IDV_L: {
            AValue quot;

            /* IDEA: If right is small enough, use ADivModSignle for improved
                     performance. */
            
            for (;;) {
                AValue temp;

                if (AIsLongInt(left) && AIsLongInt(right)) {
                    quot = ADivModLongInt(t, left, right, &temp);
                    if (AIsError(quot))
                        goto ExceptionRaised;
                    break;
                }
                
                if (AIsInstance(left)) {
                    member = AM_IDIV;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                if (AIsFloat(left) && AIsFloat(right)) {
                    if (AValueToFloat(right) == 0.0) {
                        exception = AErrorId(EX_ARITHMETIC_ERROR,
                                            EM_DIVISION_BY_ZERO);
                        goto RaiseException;
                    }
                    
                    quot = ACreateFloat(t, floor(AValueToFloat(left) /
                                                     AValueToFloat(right)));
                    if (AIsError(quot))
                        goto ExceptionRaised;
                    
                    break;
                }

                if (right == AZero && AIsLongInt(left)) {
                    exception = AErrorId(EX_ARITHMETIC_ERROR,
                                        EM_DIVISION_BY_ZERO);
                    goto RaiseException;
                }
            
                left = ACoerce(t, OPER_IDIV, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;

                right = temp;
            }

            stack[ip[1]] = quot;
            ip += 2;

            break;
        }
            
        case OP_MOD_L: {
            AValue mod;

            for (;;) {
                AValue temp;

                if (AIsLongInt(left) && AIsLongInt(right)) {
                    temp = ADivModLongInt(t, left, right, &mod);
                    if (AIsError(temp))
                        goto ExceptionRaised;
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_MOD;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                if (AIsFloat(left) && AIsFloat(right)) {
                    double result;
                    double rightFloat;

                    rightFloat = AValueToFloat(right);
                    if (rightFloat == 0.0) {
                        exception = AErrorId(EX_ARITHMETIC_ERROR,
                                            EM_DIVISION_BY_ZERO);
                        goto RaiseException;
                    }

                    result = fmod(AValueToFloat(left), rightFloat);
                    if (result * rightFloat < 0)
                        result += rightFloat;

                    mod = ACreateFloat(t, result);
                    if (AIsError(mod))
                        goto ExceptionRaised;
                    break;
                }

                if (right == AZero && AIsLongInt(left)) {
                    exception = AErrorId(EX_ARITHMETIC_ERROR,
                                        EM_DIVISION_BY_ZERO);
                    goto RaiseException;
                }
            
                left = ACoerce(t, OPER_MOD, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;

                right = temp;
            }

            stack[ip[1]] = mod;
            ip += 2;

            break;
        }
                
        case OP_PAIR_L:
            goto CreatePair;
                
        case OP_RNG_L:
            goto CreateRange;

        case OP_POW_L: {
            AValue power;

            for (;;) {
                AValue temp;

                if (AIsFloat(left) && AIsFloat(right)) {
                    if (AValueToFloat(left) < 0.0) {
                        exception = EX_ARITHMETIC_ERROR;
                        goto RaiseException;
                    }

                    power = ACreateFloat(t, pow(AValueToFloat(left),
                                                    AValueToFloat(right)));
                    if (AIsError(power))
                        goto ExceptionRaised;
                    break;
                }

                /* FIX: float and short int / long int. */

                if ((AIsLongInt(left) || AIsShortInt(left))
                    && (AIsShortInt(right) || AIsLongInt(right))) {
                    power = APowInt(t, left, right);
                    if (AIsError(power))
                        goto ExceptionRaised;
                    break;
                }

                if (AIsInstance(left)) {
                    member = AM_POW;
                    args[0] = left;
                    args[1] = right;
                    numArgs = 2;
                    ip++;
                    goto MethodCall;
                }

                left = ACoerce(t, OPER_POW, left, right, &temp);
                if (AIsError(left))
                    goto ExceptionRaised;

                right = temp;
            }

            stack[ip[1]] = power;
            ip += 2;

            break;
        }
            
          InitForRange:
        case OP_FOR_L:
            /* Initialize for loop over a range. */
            if (AIsShortInt(left)
                    && (ASignedValue)left > AIntToValue(A_SHORT_INT_MIN)) {
                stack[ip[1]] = left - AIntToValue(1);
                stack[ip[1] + 1] = right;
            } else {
                if (!AIsInt(left)) {
                    /* Invalid lower bound */
                    ARaiseTypeErrorND(t, AMsgInvalidRangeLowerBound, left);
                    goto ExceptionRaised;
                } else if (!AIsInt(right)) {
                    /* Invalid upper bound */
                    ARaiseTypeErrorND(t, AMsgInvalidRangeUpperBound, right);
                    goto ExceptionRaised;
                } else {
                    left = ASubInts(t, left, AIntToValue(1));
                    if (AIsError(left))
                        goto ExceptionRaised;
                    stack[ip[1]] = left;
                    stack[ip[1] + 1] = right;
                }
            }
            
            ip = ABranchTarget(ip + 2);
            
            break;
        }

        A_END_OPCODE;
    }

    A_OPCODE(OP_TRY) {
        int val;
        
        if ((val = AHandleException(t))) {
            if (val == 2) /* IDEA: Use symbolic constant! */
                goto ExceptionRaised;
            
            /* Unwind the stack. Invariant: The current stack
               frame is the next stack frame with an active indirect try
               statement, i.e. stack frames below the original frame can be
               skipped without having to pop from the context stack. */
            stack = t->stackPtr;
            ip = AGetFrameIp(stack) - 1;
            
            goto ExceptionRaised;
        }
        ip++;
        A_END_OPCODE;
    }

    A_OPCODE(OP_TRY_END) {
        t->contextIndex -= ip[1];
        ip += 2;
        A_END_OPCODE;
    }

    A_END_INTERPRETER_LOOP

  MethodCall:
    {
        /* Try to call a method. */

        /* args[0] = potential instance to be called
           args[1..numArgs-1] = arguments
           member = member id to call */
        
        AInstance *inst;
        ATypeInfo *type;
        AMemberHashTable *table;
        AMemberNode *node;

        if (!AIsInstance(args[0])) {
            ARaiseMethodCallExceptionND(t, member, args, numArgs - 1);
            goto ExceptionRaised;
        }
        
        inst = AValueToInstance(args[0]);
        type = AGetInstanceType(inst);
        table = AGetMemberTable(type, MT_METHOD_PUBLIC);

        node = &table->item[member & table->size];

        while (node->key != member) {
            if (node->next == NULL) {
                if (type->super == NULL) {
                    /* Reserve space in temp stack for the arguments so that
                       they will be traced by gc. Later unnecessary
                       additional values may be allocated, but this doesn't
                       matter. */
                    if (!AAllocTempStack(t, numArgs))
                        goto ExceptionRaised;

                    funcVal = AGetInstanceDataMember(t, inst, member);
                    if (!AIsError(funcVal)) {
                        int i;
                        
                        for (i = 1; i < numArgs; i++)
                            args[i - 1] = args[i];
                        
                        numArgs--;
                        
                        goto FunctionCall;
                    } else {
                        ARaiseMethodCallExceptionND(t, member, args,
                                                    numArgs - 1);
                        goto ExceptionRaised;
                    }
                } else {
                    type = type->super;
                    table = AGetMemberTable(type, MT_METHOD_PUBLIC);
                    node = &table->item[member & table->size];
                }
            } else
                node = node->next;
        }
            
        funcVal = AGlobalByNum(node->item);
    }
    
  FunctionCall:
    {
        /* Call a function. */

        /* numArgs = number of arguments (1..2)
           funcVal = callee
           args[]  = arguments */
        
        int i;
        AInstance *instance;

        *t->tempStack = funcVal;
        
        if (!AAllocTempStack_M(t, numArgs))
            goto ExceptionRaised;
        
        if (AIsInterrupt && AHandleInterrupt(t))
            goto ExceptionRaised;

        funcVal = *t->tempStack;
        
        if (AIsGlobalFunction(funcVal)) {
            AFunction *func;
            AValue *newStack;
            unsigned stackFrameSize;
            AValue *sp;

          TheCall:

            /* Store the return opcode address. */
            stack[2] = AMakeInterpretedOpcodeIndex(
                ip - AValueToFunction(stack[1])->code.opc);
            
            func = AValueToFunction(funcVal);

            stackFrameSize = func->stackFrameSize;
            newStack = APreviousFrame(stack, stackFrameSize);
            if (newStack < t->stack) {
                exception = AErrorId(EX_RUNTIME_ERROR,
                                    EM_TOO_MANY_RECURSIVE_CALLS);
                goto RaiseException;
            }

            /* Initialize the new stack frame header. */
            newStack[2] = A_COMPILED_FRAME_FLAG;
            newStack[1] = funcVal;
            newStack[0] = stackFrameSize;

            /* Clear temporaries. */
            for (sp = newStack + 3 + func->minArgs;
                 (void *)sp < ANextFrame(newStack, stackFrameSize); sp++)
                *sp = AZero;

            /* Check and store arguments. */
            if (numArgs != func->maxArgs) {
                t->stackPtr = newStack;
                if (!ASetupArguments(t, stack, AArgInd, numArgs, newStack +
                                    3, func->minArgs, func->maxArgs))
                    goto ExceptionRaised;
            } else {
                for (i = 0; i < numArgs; i++)
                    newStack[3 + i] = args[i];
            }

            /* Record stack pointer. */
            t->stackPtr = newStack;
            
            if (AIsCompiledFunction(func)) {
                AValue retVal;
                    
                /* Call the function */
                retVal = func->code.cfunc(t, newStack + 3);

                t->stackPtr = stack;

                if (!AIsError(retVal)) {
                    if (ip[0] < A_NO_RET_VAL)
                        stack[ip[0]] = retVal;
                } else {
                    /* Exception was raised in the C function. */
                    stack = newStack;
                    t->stackPtr = stack;
                    ip = NULL;
                    goto ExceptionRaised;
                }

                ip++;
            } else {
                /* Update stack frame. */
                stack = newStack;

                /* Begin excuting the function. */
                ip = func->code.opc;
            }
        } else if (AIsNonSpecialType(funcVal)) {
            ATypeInfo *type;

            type = AValueToType(funcVal);
            funcVal = AGlobalByNum(type->create);

            /* Create the instance. Performance is not critical, so Alloc()
               call is not partially inlined. */
            instance = AAlloc(t, type->instanceSize);
            if (instance == NULL)
                goto ExceptionRaised;

            AInitInstanceBlock(&instance->type, type);
            AInitInstanceData(instance, type);

          CallWithInstance2:
            
            for (i = numArgs; i > 0; i--)
                args[i] = args[i - 1];

            numArgs++;

            args[0] = AInstanceToValue(instance);

            goto TheCall;
        } else if (AIsMethod(funcVal)) {
            AMixedObject *method = AValueToMixedObject(funcVal);

            instance = AValueToInstance(method->data.boundMethod.instance);
            funcVal = AGlobalByNum(
                AValueToInt(method->data.boundMethod.method));
            goto CallWithInstance2;
        } else if (AIsInstance(funcVal)) {              
            int funcNum;

            instance = AValueToInstance(funcVal);
            funcNum = AGetInstanceMember(instance, MT_METHOD_PUBLIC, AM_CALL);
            if (funcNum != -1) {
                funcVal = AGlobalByNum(funcNum);
                goto CallWithInstance2;
            } else {
                exception = EX_MEMBER_ERROR;
                args[0] = funcVal;
                member = AM_CALL;
                goto RaiseException;
            }
        } else {
            exception = EX_TYPE_ERROR;
            goto RaiseException;
        }

        t->tempStackPtr = args;

        goto LoopTop;
    }

  ExceptionRaised:

    if (t->uncaughtExceptionStackPtr == stack && ip != NULL) {
        stack[2] = AMakeInterpretedOpcodeIndex(
            ip - AValueToFunction(stack[1])->code.opc);
    }
    
    exception = EX_RAISED;
    goto RaiseException;

  RaiseException:
    
    {
        /* exception == type of exception (one of EX_x contansts in interp.h)
           thread->exception == raised instance, if exception == EX_RAISED
           stack == current stack frame */

        ATypeInfo *exceptType;

        /* If the exception instance hasn't been generated yet (ie only the
           type is known), create it. */
        if (exception != EX_RAISED) {
            switch (exception) {
            case EX_MEMBER_ERROR:
                ARaiseMemberErrorND(t, args[0], member);
                args[0] = AZero;
                break;

            default:
                /* Special handling for stack overflow exceptions, since we
                   cannot call the constructor due to stack being full. */
                if (exception == AErrorId(EX_RUNTIME_ERROR,
                                         EM_TOO_MANY_RECURSIVE_CALLS))
                    ARaiseStackOverflowError(t);
                else                
                    ARaiseByNum(
                        t, AErrorClassNum[AGetErrorTypeFromId(exception)],
                        ExceptionErrorMessages[
                            AGetErrorMessageFromId(exception)]);
                break;
            }
            
            if (ip != NULL)
                stack[2] = AMakeInterpretedOpcodeIndex(
                    ip - AValueToFunction(stack[1])->code.opc);
        }

        t->isExceptionReraised = FALSE;

      ReraiseException:

        /* Return temp stack to its initial position. */
        t->tempStackPtr = args;

        exceptType = AGetInstanceType(AValueToInstance(t->exception));

        /* Unwind the stack a frame at a time. */
        for (;;) {
            t->stackPtr = stack;
            
            /* Have we reached the top of the stack? */
            if (stack[0] == AZero) {
                /* We have reached the top of the whole stack => the main
                   thread ends (other threads return to BeginNewThread). */
                return AError;
            } else if (ip != NULL) {
                AFunction *func;
                unsigned codeInd;
                int handlerInd;
                
                /* Process a stack frame. It is either a C function or Alore
                   function stack frame. This is the normal case. */

                func = AValueToFunction(stack[1]);

                /* IDEA: Is this needed? there are two checks for returning to
                         the previous function... But calltrace seems to need
                         this. */
                /* Is the current stack frame based on a C function? */
                if (stack[1] == A_THREAD_BOTTOM_FUNCTION
                    || AIsCompiledFunction(func))
                    return AError;

                /* The stack frame is based on an Alore function. */

                /* Calculate opcode index. */
                codeInd = ip - func->code.opc;

                handlerInd = AGetExceptionHandler(t, func, codeInd);
                if (handlerInd >= 0) {
                    ip = func->code.opc + handlerInd;
                    goto LoopTop;
                } else if (handlerInd == -2)
                    goto ExceptionRaised;
                
                stack = ANextFrame(stack, stack[0]);
                
                /* Is the current stack frame based on a compiled function? */
                if (!AIsInterpretedFrame(stack)) {
                    t->stackPtr = stack;
                    return AError;
                }
                    
                ip = AGetFrameIp(stack) - 1;
            } else {
                /* Skip stack frame without any additional processing. When a
                   exception is raised from a C function, the first stack frame
                   will be handled here. */

                stack = ANextFrame(stack, stack[0]);
                ip = AGetFrameIp(stack) - 1;
            }
        }
    }

#ifdef HAVE_JIT_COMPILER
  TransferControlToCompiled:
    {
        AValue retVal;

        /* Change the current function invocation to a compiled one. */
        stack[2] = A_COMPILED_FRAME_FLAG;
        
        /* Transfer the execution of the current function invocation to the
           compiled function. The value returned by TransferControl is the
           return value of the invocation. */        
        retVal = ATransferControl(t, stack, ip);
        
        /* Perform function return processing (similar to RET/RET_L). */
        stack = ANextFrame(stack, stack[0]);
        t->stackPtr = stack;
        
        if (!AIsInterpretedFrame(stack))
            return retVal;
        
        ip = AGetFrameIp(stack);
        
        if (AIsError(retVal))
            goto ExceptionRaised;
        
        if (ip[-1] < A_NO_RET_VAL)
            stack[ip[-1]] = retVal;
        
        goto LoopTop;
    }
#endif
}
