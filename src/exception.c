/* exception.c - Exception raising and stack traceback creation

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "opcode.h"
#include "runtime.h"
#include "class.h"
#include "array.h"
#include "str.h"
#include "gc.h"
#include "error.h"
#include "files.h"
#include "std_module.h"
#include "internal.h"
#include "util.h"
#include "memberid.h"
/* #include "jitcomp.h" */

#include <ctype.h>


#define MAX_TRACEBACK_MESSAGE_LENGTH 256
#define MAX_TRACEBACK_ENTRIES 2000
#define MAX_PATH_LENGTH 256


static int GetLineNumber(AThread *t, AOpcode *ip, AFunction *func);
static const char *OperatorStr(AOperator op);
static ABool AHasPrefix(const unsigned char *str, const char *prefix,
                        int strLen);


/* FIX: perhaps this ain't perfect */
/* NOTE: must be given a class */
AValue ARaiseByNum(AThread *t, int classNum, const char *message)
{
    AValue exception;

    /* IDEA: Try to allocate if possible, otherwise use a predefined
              exception object. */

    if (!AAllocTempStack(t, 2))
        return AError;

    t->tempStackPtr[-2] = ADefault;
    t->tempStackPtr[-1] = ANil;
    if (message != NULL) {
        t->tempStackPtr[-2] = ACreateStringFromCStr(t, message);
        if (t->tempStackPtr[-2] == AError)
            goto Leave;
    }

    exception = ACallValue(t, AGlobalByNum(classNum), message != NULL,
                           t->tempStackPtr - 2);
    if (!AIsError(exception))
        ARaiseExceptionValue(t, exception);

  Leave:

    t->tempStackPtr -= 2;
    return AError;
}


AValue ARaiseExceptionValue(AThread *t, AValue exception)
{
    t->uncaughtExceptionStackPtr = t->stackPtr;
    t->isExceptionReraised = FALSE;
    t->exception = exception;
    return AError;
}


AValue ACreateBasicExceptionInstance(AThread *t, int num, const char *message)
{
    AInstance *inst;
    ATypeInfo *type;
    int i;

    if (message != NULL)
        *t->tempStack = AMakeStr(t, message);
    else
        *t->tempStack = ANil;
    
    type = AValueToType(AGlobalByNum(AErrorClassNum[num]));
    
    inst = AAlloc(t, type->instanceSize);
    if (inst == NULL)
        ADispatchException(t);
    
    AInitInstanceBlock(&inst->type, type);
    
    inst->member[AError_TRACEBACK] = ANil;
    inst->member[AError_MESSAGE] = *t->tempStack;

    for (i = 2; i < type->totalNumVars; i++)
        inst->member[i] = AZero;

    return AInstanceToValue(inst);
}


AValue ARaiseStackOverflowError(AThread *t)
{
    /* NOTE: We need to use CreateBasicExceptionInstance instead of
       ARaiseExeption since the stack is probably full and we may not be able
       to call the exception constructor. */
    return ARaiseExceptionValue(
        t, ACreateBasicExceptionInstance(t, EX_RUNTIME_ERROR,
                                        AMsgTooManyRecursiveCalls));
}


/* FIX this is an ugly hack */
#define TMP (t->tempStackPtr - 2)


ABool ADisplayStackTraceback(AThread *t,
                           ABool (*display)(const char *msg, void *data),
                           void *data)
{
    char msg[MAX_TRACEBACK_MESSAGE_LENGTH];
    char buf[MAX_TRACEBACK_MESSAGE_LENGTH];
    char buf2[MAX_TRACEBACK_MESSAGE_LENGTH];
    AValue msgVal;
    int i;

    *t->tempStack = t->exception;
    
    if (!AAllocTempStack(t, 2))
        return FALSE;

    TMP[0] = AZero;
    TMP[1] = *t->tempStack;

    t->exception = TMP[1];
    TMP[0] = AFilterTracebackArray(t);
    if (AIsError(TMP[0]))
        goto Fail;

    if (!display("Traceback (most recent call last):", data))
        goto Fail;
        
    for (i = AArrayLen(TMP[0]) - 1; i >= 0; i--) {
        AString *s;
        int len;

        /* Traceback array contents are known to narrow string objects. */
        s = AValueToStr(AArrayItem(TMP[0], i));

        len = AMin(AGetStrLen(s), MAX_TRACEBACK_MESSAGE_LENGTH - 1);
        ACopyMem(buf, s->elem, len);
        buf[len] = '\0';

        AFormatMessage(msg, MAX_TRACEBACK_MESSAGE_LENGTH, "  %s", buf);
        
        if (!display(msg, data))
            goto Fail;
    }

    AFormatMessage(msg, MAX_TRACEBACK_MESSAGE_LENGTH, "%q",
                   AGetInstanceType(AValueToInstance(TMP[1]))->sym);

    /* Add the exception message to the traceback if it is available. */
    msgVal = AValueToInstance(TMP[1])->member[AError_MESSAGE];
    if (!AIsNil(msgVal)) {
        msgVal = ANormalizeNarrowString(t, msgVal);

        /* If there was an exception, either the message type is invalid or
           we ran out of memory. In both cases, it is ok to simply skip
           displaying the message (although we could do better). */
        if (!AIsError(msgVal)) {
            AString *str = AValueToStr(msgVal);
            int len = AMin(AGetStrLen(str), MAX_TRACEBACK_MESSAGE_LENGTH - 1);
            ACopyMem(buf2, str->elem, len);
            buf2[len] = '\0';
            strcpy(buf, msg);
            AFormatMessage(msg, MAX_TRACEBACK_MESSAGE_LENGTH, "%s: %s",
                          buf, buf2);
        }
    }

    if (!display(msg, data))
        goto Fail;

    t->tempStackPtr -= 2;
    return TRUE;

  Fail:

    t->tempStackPtr -= 2;
    return FALSE;
}


static int GetLineNumber(AThread *t, AOpcode *ip, AFunction *func)
{
    int line;
    AOpcode *found;
    AOpcode *cur;
    int foundLine;
    AOpcode *opc;
    AOpcode *last;

#ifdef HAVE_JIT_COMPILER
    if (AIsCompiledFunction(func))
        func = AGetOriginalInterpretedFunction(t, func);
#endif

    line = 0 /* FIX: func->lineNum */;
    opc = func->code.opc + func->codeLen;
    last = APtrAdd(func, sizeof(AValue) +
                               AGetNonPointerBlockDataLength(&func->header));
    cur = func->code.opc;
    found = cur;
    foundLine = line;

#if A_OPCODE_BITS == 32

    while (opc < last) {
        if (AIsLineNumberCode(*opc)) {
            AOpcode val = *opc >> 3;

            /* FIX: use macros. optimize */
            if (val & 1)
                line = val >> 1;
            else {
                val >>= 1;

                cur += val & 15;
                line += (val >> 4) & 7;
                for (;;) {
                    if (cur <= ip && cur >= found) {
                        found = cur;
                        foundLine = line;
                    }
                    
                    val >>= 7;
                    if (val == 0)
                        break;

                    cur += val & 15;
                    line += (val >> 4) & 7;
                }
            }
        } else if (AIsExceptCode(*opc))
            opc += 2;
        else if (AIsFinallyCode(*opc))
            opc++;

        opc++;
    }
    
#else

    FIX;
    
#endif

    return foundLine;
}


/* FIX ugly hack */
#undef TMP
#define TMP (t->tempStackPtr - 3)


/* Create an array of traceback entries (strings). The topmost included stack
   frame is thread->uncaughtExceptionStackPtr and the bottommost stack frame
   is thread->stackPtr. The traceback array will be included in the exception
   instance. */
ABool ACreateTracebackArray(AThread *t)
{
    AValue *stack;
    int len;
    AOpcode *ip;
    int i, j;
    AInstance *exceptInst;
    AValue *oldStackPtr;
    AValue tmp;
    ASymbolInfo *prev;

    /* FIX: split into smaller functions */

    /* Figure out the length of the traceback array. */
    stack = t->uncaughtExceptionStackPtr;
    len = t->isExceptionReraised ? 0 : 1;

    /* Calculate the length of the array. */
    prev = NULL;
    while (stack != t->stackPtr) {
        /* Skip class constructor, because the constructor only calls some
           other functions and doesn't relate to any specific source code
           lines. */
        if (!AIsClassConstructor(stack[1]) ||
            AValueToFunction(stack[1])->sym != prev)
            len++;
        prev = AValueToFunction(stack[1])->sym;
        stack = ANextFrame(stack, stack[0]);
    }

    if (stack[0] == AZero || stack[1] == A_THREAD_BOTTOM_FUNCTION)
        len--;

    /* Return the stack pointer the value it had before raising the exception.
       Otherwise the stack may be corrupted by the garbage collector. Record
       the current stack pointer values so that they can restored. */
    oldStackPtr = t->stackPtr;
    t->stackPtr = t->uncaughtExceptionStackPtr;
    
    if (!AAllocTempStack(t, 3))
        return FALSE;

    TMP[0] = t->exception; /* FIX: t->exception saved across gc? */
    TMP[1] = AZero;
    TMP[2] = AZero;

    tmp = AMakeArrayND(t, AMin(MAX_TRACEBACK_ENTRIES, len));
    if (AIsError(tmp))
        goto Fail;
    TMP[1] = tmp;

    /* Fill the array with stack traceback entries. */
    
    stack = t->uncaughtExceptionStackPtr;

    prev = NULL;
    j = 0;
    for (i = 0; i < len; ) {
        if (AIsInterpretedFrame(stack))
            ip = AGetFrameIp(stack) - 1;
#ifdef HAVE_JIT_COMPILER
        else if (stack[2] != A_COMPILED_FRAME_FLAG) {
            /* Get the ip from a JIT compiled function. We must first map
               the compiled function to the original, interpreted form. */
            AFunction *func = AGetOriginalInterpretedFunction(
                t, AValueToFunction(stack[1]));
            ip = func->code.opc + AInterpretedOpcodeIndex(stack) - 1;
        }
#endif
        else
            ip = NULL;
        
        if (!AIsClassConstructor(stack[1])
            || AValueToFunction(stack[1])->sym != prev) {
            AFunction *func;
            char msg[MAX_TRACEBACK_MESSAGE_LENGTH];
            AValue str;
            
            func = AValueToFunction(stack[1]);
            prev = func->sym;
            
            AFormatMessage(msg, MAX_TRACEBACK_MESSAGE_LENGTH, "%F", func);
            
            if (!AIsCompiledFunction(func)
                    || stack[2] != A_COMPILED_FRAME_FLAG) {
                char file[MAX_PATH_LENGTH];
                int line;
                
                AGetFilePath(file, MAX_PATH_LENGTH, func->fileNum);
                line = GetLineNumber(t, ip, func);

                if (line > 0)
                    AFormatMessage(msg + strlen(msg),
                                  MAX_TRACEBACK_MESSAGE_LENGTH - strlen(msg),
                                  " (%f, line %d)", file, line);
                else
                    AFormatMessage(msg + strlen(msg),
                                  MAX_TRACEBACK_MESSAGE_LENGTH - strlen(msg),
                                  " (%f)", file);
            }

            if (t->isExceptionReraised) {
                i--;
                t->isExceptionReraised = FALSE;
            } else {
                /* Limit the maximum number of entries to
                   MAX_TRACEBACK_ENTIRES. Skip some entries in the middle of
                   the stack trace if necessary. */
                if (i < MAX_TRACEBACK_ENTRIES / 2 ||
                    i >= len - MAX_TRACEBACK_ENTRIES / 2) {
                    if (i + 1 == MAX_TRACEBACK_ENTRIES / 2
                        && len > MAX_TRACEBACK_ENTRIES)
                        AFormatMessage(msg, MAX_TRACEBACK_MESSAGE_LENGTH,
                                       "... %d entries skipped ...",
                                       len - MAX_TRACEBACK_ENTRIES + 1);
                    
                    str = ACreateStringFromCStr(t, msg);
                    if (AIsError(str))
                        goto Fail;

                    if (!ASetArrayItemND(t, TMP[1], j, str))
                        goto Fail;

                    j++;
                }
            }

            i++;
        }
        
        stack = ANextFrame(stack, stack[0]);
    }

    exceptInst = AValueToInstance(TMP[0]);
    if (exceptInst->member[AError_TRACEBACK] != ANil) {
        AValue newArr = AConcatArrays(t,
                                      exceptInst->member[AError_TRACEBACK],
                                      TMP[1]);
        if (AIsError(newArr))
            goto Fail;
        TMP[1] = newArr;
    }
    
    if (!ASetInstanceMember(t, AValueToInstance(TMP[0]),
                           AError_TRACEBACK, &TMP[1]))
        goto Fail;

    t->exception = TMP[0];
    t->tempStackPtr -= 3;
    
    t->stackPtr = oldStackPtr;
    
    return TRUE;

  Fail:

    t->tempStackPtr -= 3;
    
    t->stackPtr = oldStackPtr;
    
    return FALSE;
}


/* Filter lines from the traceback array that should not be displayed. */
/* NOTE: Keeps thread->exception in thread->tempStack[1] */
AValue AFilterTracebackArray(AThread *t)
{
    int i;
    int drop;
    int arrayLen;
    AValue src;
    AValue dst;

    t->tempStack[0] =
        AValueToInstance(t->exception)->member[AError_TRACEBACK];
    arrayLen = AArrayLen(t->tempStack[0]);

    t->tempStack[1] = t->exception;
    
    /* Create a new array in the new generation. It may be longer than what
       we'll need, but we will truncate it later if necessary. */
    dst = AMakeArrayND(t, arrayLen);
    if (AIsError(dst))
        return AError;

    src = t->tempStack[0];

    /* Go through the traceback array and remove unnecessary entries. */
    drop = 0;
    for (i = 0; i < arrayLen; i++) {
        AString *str;
        int len;

        /* IDEA: This code seriously needs to cleaned up! */

        ASetArrayItemNewGen(dst, i - drop, AArrayItem(src, i));
        
        str = AValueToStr(AArrayItem(src, i));
        len = AGetStrLen(str);

        if (AHasPrefix(str->elem, "Main ", len)) {
            drop++;
            i++;
        } else if (AHasPrefix(str->elem, "anonymous function", len) ||
                   AHasPrefix(str->elem, "at main level", len)) {
            drop++;
            i++;
        }
    }

    /* Truncate traceback array. */
    ASetMemberDirect(t, dst, A_ARRAY_LEN, AIntToValue(arrayLen - drop));

    t->exception = t->tempStack[1];

    return dst;
}


AValue ARaiseByNumFmt(AThread *t, int num, const char *fmt, ...)
{
    char msg[256];
    va_list args;

    va_start(args, fmt);
    if (fmt != NULL)
        AFormatMessageVa(msg, sizeof(msg), fmt, args);
    va_end(args);

    return ARaiseByNum(t, num, fmt != NULL ? msg : NULL);
}


AValue ARaiseByNumFmtVa(AThread *t, int num, const char *fmt, va_list args)
{
    char msg[256];
    if (fmt != NULL) {
        AFormatMessageVa(msg, sizeof(msg), fmt, args);
        return ARaiseByNum(t, num, msg);
    } else
        return ARaiseByNum(t, num, NULL);
}


AValue ARaiseMemoryErrorND(AThread *t)
{
    return ARaiseByNum(t, AErrorClassNum[EX_MEMORY_ERROR], NULL);
}


AValue ARaisePreallocatedMemoryErrorND(AThread *t)
{
    /* FIX: Multiple threads might raise the same instance. Allocate a
            separate instance for each thread. */

    /* Clear traceback member. */
    AValueToInstance(AGlobalByNum(GL_MEMORY_ERROR_INSTANCE))->member[
        AError_TRACEBACK] = ANil;
    
    return ARaiseExceptionValue(t, AGlobalByNum(GL_MEMORY_ERROR_INSTANCE));
}


AValue ARaiseValueErrorND(AThread *t, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    return ARaiseByNumFmtVa(t, AErrorClassNum[EX_VALUE_ERROR], fmt, args);
    va_end(args);

    return AError;
}


AValue ARaiseTypeErrorND(AThread *t, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    ARaiseByNumFmtVa(t, AErrorClassNum[EX_TYPE_ERROR], fmt, args);
    va_end(args);

    return AError;
}


AValue ARaiseMemberErrorND(AThread *t, AValue inst, int member)
{
    return ARaiseByNumFmt(t, AErrorClassNum[EX_MEMBER_ERROR],
                          "%T has no member \"%M\"", inst, member);
}


AValue ARaiseCastErrorND(AThread *t, AValue inst, AValue type)
{
    ASymbolInfo *sym;
    if (AIsNonSpecialType(type))
        sym = AValueToType(type)->sym;
    else
        sym = AValueToFunction(type)->sym;
    return ARaiseByNumFmt(t, AErrorClassNum[EX_CAST_ERROR],
                          "Cannot cast %T to %q", inst, sym);
}


AValue ARaiseBinopTypeErrorND(AThread *t, AOperator op, AValue left,
                              AValue right)
{
    const char *opstr = OperatorStr(op);;
    return ARaiseByNumFmt(t, AErrorClassNum[EX_TYPE_ERROR],
                          "Unsupported operand types for %s (%T and %T)",
                          opstr, left, right);
}


AValue ARaiseInvalidBooleanErrorND(AThread *t, AValue value)
{
    return ARaiseByNumFmt(t, AErrorClassNum[EX_TYPE_ERROR],
                          "Invalid type for boolean (%T)", value);
}


AValue ARaiseIntExpected(AThread *t, AValue actual)
{
    return ARaiseTypeError(t, AMsgIntExpectedBut, actual);
}


AValue ARaiseMethodCallExceptionND(AThread *t, int member, AValue *args,
                                   int numArgs)
{
    int operator = -1;
    int opNumArgs = 1;

    switch (member) {
    case AM_ADD:
        operator = OPER_PLUS;
        break;
    case AM_SUB:
        operator = OPER_MINUS;
        break;
    case AM_MUL:
        operator = OPER_MUL;
        break;
    case AM_DIV:
        operator = OPER_DIV;
        break;
    case AM_MOD:
        operator = OPER_MOD;
        break;
    case AM_IDIV:
        operator = OPER_IDIV;
        break;
    case AM_POW:
        operator = OPER_POW;
        break;
    case AM_LT:
        operator = OPER_LT;
        break;
    case AM_GT:
        operator = OPER_GT;
        break;
    case AM_IN:
        operator = OPER_IN;
        break;
    case AM_NEGATE:
        operator = OPER_NOT;
        opNumArgs = 0;
        break;
    case AM_GET_ITEM:
        operator = OPER_INDEX;
        break;
    case AM_ITERATOR:
        operator = OPER_ITERATOR;
        opNumArgs = 0;
        break;
    }

    if (operator != -1 && numArgs == opNumArgs) {
        /* Operator exception (TypeError). */
        if (operator == OPER_INDEX)
            return ARaiseTypeErrorND(t, "%T object is not indexable", args[0]);
        else if (operator == OPER_NOT)
            return ARaiseTypeErrorND(
                t,"Invalid operand type for unary - (%T)", args[0]);
        else if (operator == OPER_ITERATOR)
            return ARaiseByNumFmt(t, AErrorClassNum[EX_MEMBER_ERROR],
                                  "Invalid iterable (%T)", args[0]);
        else if (operator != OPER_IN)
            return ARaiseBinopTypeErrorND(t, operator, args[0], args[1]);
        else {
            /* Switch the order of the operands for "in" since the right
               operand is the base object. */
            return ARaiseBinopTypeErrorND(t, operator, args[1], args[0]);
        }
    } else
        return ARaiseMemberErrorND(t, args[0], member);
}


AValue ARaiseInvalidCallableErrorND(AThread *t, AValue value)
{
    return ARaiseTypeErrorND(t, "%T is not callable", value);
}


#define PluralS(n) ((n) == 1 ? "" : "s")


/* Raise a ValueError exception as a response to an invalid number of
   arguments when calling a function. The actual number of arguments is nargs.
   Subtract numHidden from the number of arguments accepted by func (it should
   be 0 unless the function represents an anonymous function). */
AValue ARaiseArgumentErrorND(AThread *t, AValue func, int nargs, int numHidden)
{
    char fstr[256];
    char msg[256];
    int isMethod = AValueToFunction(func)->sym != NULL &&
                   AValueToFunction(func)->sym->type == ID_GLOBAL_CLASS;
    int minArgs = AValueToFunction(func)->minArgs - isMethod - numHidden;
    int maxArgs = AValueToFunction(func)->maxArgs - isMethod - numHidden;

    nargs -= isMethod;
    
    AFormatMessage(fstr, 256, "%F", AValueToFunction(func));

    if (maxArgs == 0)
        AFormatMessage(msg, 256, "%s expects no arguments (%d given)", fstr,
                       nargs);
    else if (minArgs == maxArgs)
        AFormatMessage(msg, 256, "%s expects %d argument%s (%d given)", fstr,
                       minArgs, PluralS(minArgs), nargs);
    else if (nargs < minArgs)
        AFormatMessage(msg, 256,
                       "%s expects at least %d argument%s (%d given)",
                       fstr, minArgs, PluralS(minArgs), nargs);
    else
        AFormatMessage(msg, 256, "%s expects at most %d argument%s (%d given)",
                       fstr, maxArgs, PluralS(maxArgs), nargs);
    
    return ARaiseByNum(t, AErrorClassNum[EX_VALUE_ERROR], msg);
}


AValue ARaiseArithmeticErrorND(AThread *t, const char *msg)
{
    return ARaiseByNum(t, AErrorClassNum[EX_ARITHMETIC_ERROR], msg);
}


/* Raise a KeyError exception with the representation of key as the message.
   The message may be truncated. If the representation cannot be produced,
   omit the message, but still raise KeyError.

   See also ARaiseKeyError. */
AValue ARaiseKeyErrorWithRepr(AThread *t, AValue key)
{
    char msg[200];
    if (AGetRepr(t, msg, sizeof(msg), key))
        return ARaiseByNum(t, AErrorClassNum[EX_KEY_ERROR], msg);
    else
        return ARaiseByNum(t, AErrorClassNum[EX_KEY_ERROR], NULL);
}


/* Depth indicating no except blocks should be skipped */
#define NO_SKIP 100000000


/* Return the opcode index of the next opcode to execute after a raised
   exception. Return PROPAGATE_TO_CALLER (-1) if the execption should be
   propagated to the calling function or EXCEPTION_RAISED (-2) if an exception
   was raised during exception propagation.
   
   Preconditions:
     exception raised
     thread->stackPtr is valid
   Potential side effects:
     update thread->contextIndex
     update stack contents as expected by a except/finally block
     creation of a traceback array and raising MemoryError */
int AGetExceptionHandler(AThread *t, AFunction *func, int codeInd)
{
    int skipDepth;
    int depth;
    ATypeInfo *exceptType;
    char isDirect[100]; /* FIX: Check overflow! */
    AOpcode *exceptInfo;
    int oldContextIndex;

    oldContextIndex = t->contextIndex;
    exceptInfo = func->code.opc + func->codeLen;

    /* If the exception was raised within an except block, skipDepth
       is used to keep track of the depth of the enclosing try statement
       so that the except blocks of this statement can be ignored:
       
         try
           ..
         except X
           .. <-- codeInd
         except Y
           ..
         end

       In this example, the "except Y" block is ignored. The value NO_SKIP
       means that the condition is not satisfied. */
    /* FIX: There may be multiple such cases active at a time, so skipDepth
            should probably be a set... */
    skipDepth = NO_SKIP;
    /* This variable is used to keep track of the depth of the try
       statements. */
    depth = 0;
    exceptType = AGetInstanceType(AValueToInstance(t->exception));
        
    /* Find out whether there is a except or finally block
       corresponding to the opcode index. */
    /* IDEA: Is skipDepth used properly? */
    for (;;) {
        int code = *exceptInfo;
        
        if (AIsExceptCode(code)) {
            /* Handle a single except case in a try-except statement.
               Invariant: depth > 0 */
            
            unsigned exceptIndex = exceptInfo[1];
            if (exceptIndex > codeInd && depth != skipDepth) {
                unsigned global = exceptInfo[2];
                ATypeInfo *type = AValueToType(AGlobalByNum(global));
                ATypeInfo *e = exceptType;
                
                if (isDirect[depth - 1]) {
                    t->contextIndex--;
                    isDirect[depth - 1] = FALSE;
                }
                
                do {
                    if (e == type) {
                        t->stackPtr[exceptInfo[0] >> 3] =
                            t->exception;
                        return exceptIndex;
                    }
                    e = e->super;
                } while (e != NULL);
            } else if (skipDepth == NO_SKIP) {
                isDirect[depth - 1] = FALSE;
                skipDepth = depth;
            }
            
            exceptInfo += 3;
        } else if (AIsEndTryCode(code)) {
            /* End a try statement. */
            
            exceptInfo++;
            if (depth == skipDepth)
                skipDepth = NO_SKIP;
            depth--;
        } else if (AIsBeginTryCode(code)) {
            /* Begin a try statement. */
            
            exceptInfo++;
            
            /* If try statement begins after current code index, skip it. */
            if (AGetBeginTryCodeIndex(code) > codeInd) {
                unsigned count = 1;
                for (;;) {
                    code = *exceptInfo;
                    
                    if (AIsEndTryCode(code)) {
                        exceptInfo++;
                        count--;
                        if (count == 0)
                            break;
                    } else if (AIsBeginTryCode(code)) {
                        exceptInfo++;
                        count++;
                    } else if (AIsExceptCode(code))
                        exceptInfo += 3;
                    else if (AIsFinallyCode(code))
                        exceptInfo += 2;
                }
            } else {
                /* FIX: what if depth is too large? */
                isDirect[depth] = AIsDirectBeginTryCode(code);
                depth++;
            }
        } else if (AIsFinallyCode(code)) {
            /* Handle the finally block in a try-finally statement. */
            
            if (AGetFinallyCodeIndex(exceptInfo) > codeInd) {
                /* Execute the finally block. */
                
                /* Generate partial stack trace. */
                if (!ACreateTracebackArray(t)) {
                    t->contextIndex = oldContextIndex;
                    return A_EXCEPTION_RAISED;
                }

                /* Store the exception in the stack. */
                t->stackPtr[AGetFinallyLvar(code)] = t->exception;
                
                if (isDirect[depth - 1])
                    t->contextIndex--;
                
                return AGetFinallyCodeIndex(exceptInfo);
            } else {
                /* Ignore the finally block. */
                exceptInfo += 2;
            }
        } else {
            /* Start of the line number information block signals the end of
               exception handling information. No relevant handler was
               found. */
            return A_PROPAGATE_TO_CALLER;
        }
    }

    /* Not reached. */
}


static const char *OperatorStr(AOperator op)
{
    switch (op) {
    case OPER_PLUS:
        return "+";
    case OPER_MINUS:
        return "-";
    case OPER_EQ:
        return "==";
    case OPER_NEQ:
        return "!=";
    case OPER_LT:
    case OPER_GTE:
        return "< or >=";
    case OPER_GT:
    case OPER_LTE:
        return "> or <=";
    case OPER_IN:
    case OPER_NOT_IN:
        return "\"in\"";
    case OPER_IS:
    case OPER_IS_NOT:
        return "\"is\"";
    case OPER_MUL:
        return "*";
    case OPER_DIV:
        return "/";
    case OPER_IDIV:
        return "div";
    case OPER_MOD:
        return "mod";
    case OPER_POW:
        return "**";
    case OPER_PAIR:
        return ":";
    case OPER_RANGE:
        return "to";
    case OPER_AND:
        return "and";
    case OPER_OR:
        return "or";
    case OPER_NOT:
        return "not";
    default:
        return "operator"; /* Default fallback */
    }

    /* Not reached */
}


/* Check if str (with length strLen) starts with prefix. Never access any
   location at or beyond str + strLen. */
static ABool AHasPrefix(const unsigned char *str, const char *prefix,
                        int strLen)
{
    int pl = strlen(prefix);
    return strLen >= pl && memcmp(str, prefix, pl) == 0;
}
