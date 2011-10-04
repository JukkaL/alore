/* call.c - Calling Alore functions from C code

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "class.h"
#include "memberid.h"
#include "array.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"
#include "debug_runtime.h"


const AOpcode AArgInd[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };


/* NOTE: args must reside eg. in stack so that they can be traced. */
/* NOTE: args must contain an additional place if funcVal is a type, an
         instance or a bound method. */
/* IDEA: Can we store arguments in thread->tempStack[0..n]? */
/* IDEA: Use SetupFrame? */
AValue ACallValue(AThread *t, AValue funcVal, int fullNumArgs,
                   AValue *args)
{
    int i;
    AInstance *instance;
    AValue *stack;
    int numArgs;

    /* This function has been carefully constructed to never cause garbage
       collection while funcVal may point to a new generation (i.e. movable)
       object, because otherwise funcVal might become corrupt. */

    stack = t->stackPtr;
    numArgs = fullNumArgs & ~A_VAR_ARG_FLAG;

    if (AIsGlobalFunction(funcVal)) {
        AFunction *func;
        AValue *newStack;
        unsigned stackFrameSize;
        AValue *sp;

      TheCall:

        func = AValueToFunction(funcVal);

        stackFrameSize = func->stackFrameSize;
        newStack = APreviousFrame(stack, stackFrameSize);
        if (newStack < t->stack)
            return ARaiseStackOverflowError(t);

        /* Initialize the new stack frame. */
        newStack[2] = A_COMPILED_FRAME_FLAG;
        newStack[1] = funcVal;
        newStack[0] = stackFrameSize;

        /* Clear temporaries. */
        for (sp = newStack + 3 + func->minArgs;
             (void *)sp < ANextFrame(newStack, stackFrameSize); sp++)
            *sp = AZero;

        /* Check and store arguments. */
        if (fullNumArgs != func->maxArgs || numArgs != fullNumArgs) {
            const AOpcode *argInd;
            ABool result;

            argInd = AArgInd;

            if (numArgs >= A_ARG_IND_LENGTH) {
                AOpcode *ind;

                /* AllocStatic() is safe since there can be no direct
                   exceptions before FreeStatic(). */
                ind = AAllocStatic(numArgs * sizeof(AOpcode));
                if (ind == NULL)
                    return ARaiseMemoryErrorND(t);

                for (i = 0; i < numArgs; i++)
                    ind[i] = i;

                argInd = ind;
            }

            t->stackPtr = newStack;
            result = ASetupArguments(t, args, argInd, fullNumArgs,
                                    newStack + 3, func->minArgs,
                                    func->maxArgs);

            if (argInd != AArgInd)
                AFreeStatic((void *)argInd);

            if (!result) {
                t->stackPtr = stack;
                return AError;
            }
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

            /* Restore stack pointer. */
            t->stackPtr = stack;

            return retVal;
        } else
            return ARun(t, func->code.opc, newStack);
    } else if (AIsNonSpecialType(funcVal)) {
        ATypeInfo *type;

        type = AValueToType(funcVal);
        funcVal = AGlobalByNum(type->create);

        if (type->isInterface)
            return ARaiseValueError(t, AMsgCallInterface);

        /* Create the instance. */
        if (t->heapPtr + type->instanceSize <= t->heapEnd
            && !A_NO_INLINE_ALLOC) {
            instance = (AInstance *)t->heapPtr;
            t->heapPtr += type->instanceSize;
        } else {
            instance = AAlloc(t, type->instanceSize);
            if (instance == NULL)
                return AError;
        }

        AInitInstanceBlock(&instance->type, type);
        AInitInstanceData(instance, type);

      CallWithInstance2:

        /* Shift arguments to get space for the class instance. */
        for (i = numArgs; i > 0; i--)
            args[i] = args[i - 1];

        numArgs++;
        fullNumArgs++;

        args[0] = AInstanceToValue(instance);

        goto TheCall;
    } else if (AIsMethod(funcVal)) {
        AMixedObject *method = AValueToMixedObject(funcVal);
        instance = AValueToInstance(method->data.boundMethod.instance);
        funcVal = AGlobalByNum(AValueToInt(method->data.boundMethod.method));
        goto CallWithInstance2;
    } else if (AIsInstance(funcVal)) {
        int funcNum;

        instance = AValueToInstance(funcVal);
        /* M_CALL is a special member function and it cannot be a member
           variable or accessed using an accessor function. Therefore there
           will not be garbage collection during the call below. */
        /* IDEA: Why cannot it be accessed using overloaded . operator (if/when
                 we support it)? Is this an unnecessary limitation? */
        funcNum = AGetInstanceMember(instance, MT_METHOD_PUBLIC, AM_CALL);
        if (funcNum != -1) {
            funcVal = AGlobalByNum(funcNum);
            goto CallWithInstance2;
        } else
            return ARaiseMemberErrorND(t, funcVal, AM_CALL);
    } else
        return ARaiseTypeErrorND(t, NULL);

    /* Not reached */
}


AValue ACallMethodByNum(AThread *t, int member, int numArgs, AValue *args)
{
    AInstance *inst;
    ATypeInfo *type;
    AMemberHashTable *table;
    AMemberNode *node;
    AValue funcVal;
    AValue self;

    self = args[0];
    if (!AIsInstance(args[0])) {
        self = AWrapObject(t, args[0]);
        if (AIsError(self))
            return AError;
    }

    inst = AValueToInstance(self);
    type = AGetInstanceType(inst);

    /* Try to find a method in the method table, going up in the inheritance
       chain if not found. */
    table = AGetMemberTable(type, MT_METHOD_PUBLIC);
    node = &table->item[member & table->size];
    while (node->key != member) {
        if (node->next == NULL) {
            if (type->super == NULL) {
                /* Not found. Try to fetch a data member and call that
                   instead. */
                funcVal = AGetInstanceDataMember(t, inst, member);
                if (!AIsError(funcVal)) {
                    int i;

                    /* Setup the arguments for ordinary function call. Move the
                       arguments back one index, overwriting the now
                       unnecessary instance value. This way the last position
                       in the args array is available (needed by
                       CallFunction). */
                    for (i = 1; i <= numArgs; i++)
                        args[i - 1] = args[i];

                    return ACallValue(t, funcVal, numArgs, args);
                } else
                    return ARaiseMethodCallExceptionND(t, member, args,
                                                       numArgs);
            } else {
                /* Try the superclass. */
                type = type->super;
                table = AGetMemberTable(type, MT_METHOD_PUBLIC);
                node = &table->item[member & table->size];
            }
        } else
            node = node->next;
    }

    funcVal = AGlobalByNum(node->item);
    return ACallValue(t, funcVal, numArgs + 1, args);
}


AValue ACallIndirect(struct AThread_ *t, AValue funcVal,
                    int fullNumArgs, AOpcode *argInd)
{
    AValue *newStack;

    newStack = ASetupFrame(t, funcVal, fullNumArgs, argInd);

    if (newStack != NULL) {
        AValue *stack;
        AFunction *func;

        stack = t->stackPtr;
        t->stackPtr = newStack;
        func = AValueToFunction(newStack[1]);

        if (AIsCompiledFunction(func)) {
            AValue retVal;

            /* Call the function */
            retVal = func->code.cfunc(t, newStack + 3);

            /* Restore stack pointer. */
            t->stackPtr = stack;

            return retVal;
        } else
            return ARun(t, func->code.opc, newStack);
    } else
        return AError;
}


/* IDEA: Share common parts with ACallMethodByNum. */
AValue ACallMethodIndirect(AThread *t, int member, int numArgs,
                          AOpcode *argInd)
{
    AInstance *inst;
    ATypeInfo *type;
    AMemberHashTable *table;
    AMemberNode *node;
    AValue funcVal;
    AValue self;
    AValue *newStack;

    self = t->stackPtr[argInd[0]];
    if (!AIsInstance(self)) {
        self = AWrapObject(t, self);
        if (AIsError(self))
            return AError;
    }

    inst = AValueToInstance(self);
    type = AGetInstanceType(inst);

    /* Try to find a method in the method table, going up in the inheritance
       chain if not found. */
    table = AGetMemberTable(type, MT_METHOD_PUBLIC);
    node = &table->item[member & table->size];
    while (node->key != member) {
        if (node->next == NULL) {
            if (type->super == NULL) {
                /* Not found. Try to fetch a data member and call that
                   instead. */
                funcVal = AGetInstanceDataMember(t, inst, member);
                if (!AIsError(funcVal))
                    return ACallIndirect(t, funcVal, numArgs, argInd + 1);
                else
                    return ARaiseMemberErrorND(t,
                                            t->stackPtr[argInd[0]],
                                            member);
            } else {
                /* Try the superclass. */
                type = type->super;
                table = AGetMemberTable(type, MT_METHOD_PUBLIC);
                node = &table->item[member & table->size];
            }
        } else
            node = node->next;
    }

    funcVal = AGlobalByNum(node->item);
    newStack = ASetupMethodFrame(t, funcVal, self, numArgs, argInd + 1);
    if (newStack != NULL) {
        AValue *stack;
        AFunction *func;

        stack = t->stackPtr;
        t->stackPtr = newStack;;
        func = AValueToFunction(newStack[1]);

        if (AIsCompiledFunction(func)) {
            AValue retVal;

            /* Call the function */
            retVal = func->code.cfunc(t, newStack + 3);

            /* Restore stack pointer. */
            t->stackPtr = stack;

            return retVal;
        } else
            return ARun(t, func->code.opc, newStack);
    } else
        return AError;
}


AValue *ASetupFrame(AThread *t, AValue funcVal, int fullNumArgs,
                   AOpcode *argInd)
{
    int i;
    AInstance *instance;
    AValue *stack;
    int numArgs;

    /* This function has been carefully constructed to never cause garbage
       collection while funcVal may point to a new generation (i.e. movable)
       object, because otherwise funcVal might become corrupt. */

    stack = t->stackPtr;
    numArgs = fullNumArgs & ~A_VAR_ARG_FLAG;

    if (AIsGlobalFunction(funcVal)) {
        AFunction *func;
        AValue *newStack;
        unsigned stackFrameSize;
        AValue *sp;

        func = AValueToFunction(funcVal);

        stackFrameSize = func->stackFrameSize;
        newStack = APreviousFrame(stack, stackFrameSize);
        if (newStack < t->stack) {
            ARaiseStackOverflowError(t);
            return NULL;
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
        if (fullNumArgs != func->maxArgs || numArgs != fullNumArgs) {
            t->stackPtr = newStack;
            if (!ASetupArguments(t, stack, argInd, fullNumArgs,
                                newStack + 3, func->minArgs, func->maxArgs)) {
                t->stackPtr = stack;
                return NULL;
            }
            t->stackPtr = stack;
        } else {
            for (i = 0; i < numArgs; i++)
                newStack[3 + i] = stack[argInd[i]];
        }

        return newStack;
    } else if (AIsNonSpecialType(funcVal)) {
        ATypeInfo *type;

        type = AValueToType(funcVal);
        funcVal = AGlobalByNum(type->create);

        /* Create the instance. */
        if (t->heapPtr + type->instanceSize <= t->heapEnd
            && !A_NO_INLINE_ALLOC) {
            instance = (AInstance *)t->heapPtr;
            t->heapPtr += type->instanceSize;
        } else {
            instance = AAlloc(t, type->instanceSize);
            if (instance == NULL)
                return NULL;
        }

        AInitInstanceBlock(&instance->type, type);
        AInitInstanceData(instance, type);

        return ASetupMethodFrame(t, funcVal, AInstanceToValue(instance),
                                fullNumArgs, argInd);
    } else if (AIsMethod(funcVal)) {
        AMixedObject *method = AValueToMixedObject(funcVal);
        funcVal = AGlobalByNum(AValueToInt(method->data.boundMethod.method));
        return ASetupMethodFrame(t, funcVal,
                                method->data.boundMethod.instance, fullNumArgs,
                                argInd);
    } else if (AIsInstance(funcVal)) {
        int funcNum;

        instance = AValueToInstance(funcVal);
        /* M_CALL is a special member function and it cannot be a member
           variable or accessed using an accessor function. Therefore there
           will not be garbage collection during the call below. */
        funcNum = AGetInstanceMember(instance, MT_METHOD_PUBLIC, AM_CALL);
        if (funcNum != -1) {
            funcVal = AGlobalByNum(funcNum);
            return ASetupMethodFrame(t, funcVal, AInstanceToValue(instance),
                                    fullNumArgs, argInd);
        } else {
            ARaiseMemberErrorND(t, funcVal, AM_CALL);
            return NULL;
        }
    } else {
        ARaiseTypeErrorND(t, NULL);
        return NULL;
    }
}


AValue *ASetupMethodFrame(AThread *t, AValue funcVal, AValue instance,
                         int fullNumArgs, AOpcode *argInd)
{
    int i;
    AValue *stack;
    int numArgs;
    AFunction *func;
    unsigned stackFrameSize;
    AValue *newStack;
    AValue *sp;

    stack = t->stackPtr;
    numArgs = fullNumArgs & ~A_VAR_ARG_FLAG;

    func = AValueToFunction(funcVal);

    stackFrameSize = func->stackFrameSize;
    newStack = APreviousFrame(stack, stackFrameSize);
    if (newStack < t->stack) {
        ARaiseStackOverflowError(t);
        return NULL;
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
    if (fullNumArgs != func->maxArgs - 1 || numArgs != fullNumArgs) {
        t->stackPtr = newStack;
        if (!ASetupArguments(t, stack, argInd, fullNumArgs, newStack + 4,
                            func->minArgs - 1, func->maxArgs - 1)) {
            t->stackPtr = stack;
            return NULL;
        }
        t->stackPtr = stack;
    } else {
        for (i = 0; i < numArgs; i++)
            newStack[4 + i] = stack[argInd[i]];
    }

    return newStack;
}


/* Setup function arguments. thread->stackPtr must point to the new stack
   frame. Other than the arguments, the stack frame must be initialized.
   src[ip[i]] define argument values, and they are copied to dst[j]. argc is
   the actual number of arguments, and minArgs and maxArgs are the number of
   arguments for the function. */
ABool ASetupArguments(AThread *t, AValue *src, const AOpcode *ip, int argc,
                     AValue *dst, int minArgs, int maxArgs)
{
    int i;
    int maxArgsCnt;
    ABool result;

    /* Does the caller use variable number of arguments? */
    if (argc & A_VAR_ARG_FLAG) {
        int arrayArgs;
        AValue seqVal;
        AValue srcA;
        int srcInd;

        /* argc is the number of caller arguments, not counting varargs. */
        argc -= A_VAR_ARG_FLAG + 1;

        seqVal = src[ip[argc]];
        if (!AIsArraySubType(seqVal) && !AIsTuple(seqVal)) {
            ARaiseTypeErrorND(t,
                 "Array or Tuple value expected after * (%T found)",
                              seqVal);
            return FALSE;
        }

        srcA = seqVal;
        arrayArgs = AArrayOrTupleLen(srcA);
        srcInd = 0;

        /* Copy all fixed arguments (non-varargs and non-optional). */
        for (i = 0; i < minArgs && i < argc; i++)
            dst[i] = src[ip[i]];

        maxArgsCnt = maxArgs;
        if (maxArgs & A_VAR_ARG_FLAG)
            maxArgsCnt = (maxArgs - 1) & ~A_VAR_ARG_FLAG;

        if (i < minArgs) {
            if (i + arrayArgs < minArgs) {
                argc += arrayArgs;
                goto WrongNumberOfArgs;
            }

            do {
                dst[i] = AArrayOrTupleItem(srcA, srcInd);
                srcInd++;
                i++;
            } while (i < minArgs);
        } else {
            /* Copy optional arguments that are fixed in call. */
            for ( ; i < argc && i < maxArgsCnt; i++)
                dst[i] = src[ip[i]];
        }

        /* Copy optional arguments that are varargs in call. */
        for ( ; i < maxArgsCnt && srcInd < arrayArgs; i++) {
            dst[i] = AArrayOrTupleItem(srcA, srcInd);
            srcInd++;
        }

        /* Initialize default arguments. */
        for ( ; i < maxArgsCnt; i++)
            dst[i] = ADefault;

        /* Does the callee take varargs? */
        if (maxArgs != maxArgsCnt) {
            AValue a = AZero;
            int aLen;

            /* Calculate the number of varargs the callee will get. */
            aLen = AMax(argc + arrayArgs - maxArgsCnt, 0);

            /* Initialiaze the value of the vararg argument so that the gc
               won't see garbage. */
            dst[maxArgsCnt] = AZero;

            if (aLen < 0) {
                argc += arrayArgs;
                goto WrongNumberOfArgs;
            }

            /* Allocate an array for the rest of the arguments. */
            *t->tempStack = srcA;
            AMakeUninitArray_M(t, aLen, a, result);
            if (!result)
                return FALSE;
            srcA = *t->tempStack;
            *t->tempStack = AZero;

            /* Copy rest of the fixed arguments. */
            for (i = 0; i < argc - maxArgsCnt; i++)
                ASetArrayItemNewGen(a, i, src[ip[maxArgsCnt + i]]);

            /* Copy the rest of the array. */
            for ( ; srcInd < arrayArgs; srcInd++) {
                ASetArrayItemNewGen(a, i, AArrayOrTupleItem(srcA, srcInd));
                i++;
            }

            dst[maxArgsCnt] = a;
        } else if (argc + arrayArgs >
                   maxArgsCnt /* FIX: could be simpler? */) {
            argc += arrayArgs;
            goto WrongNumberOfArgs;
        }

        return TRUE;
    }

    if (argc < minArgs)
        goto WrongNumberOfArgs;

    /* Copy the fixed arguments. */
    for (i = 0; i < minArgs; i++)
        dst[i] = src[ip[i]];

    maxArgsCnt = maxArgs;
    if (maxArgs & A_VAR_ARG_FLAG)
        maxArgsCnt = (maxArgs - 1) & ~A_VAR_ARG_FLAG;

    for ( ; i < argc && i < maxArgsCnt; i++)
        dst[i] = src[ip[i]];

    for ( ; i < maxArgsCnt; i++)
        dst[i] = ADefault;

    /* Does the callee take variable number of arguments? */
    if (maxArgs != maxArgsCnt) {
        AValue a = AZero;
        int aLen;

        /* Extra arguments go into an array. */

        aLen = (argc > maxArgsCnt ? argc - maxArgsCnt : 0);

        /* Initialiaze the value of the vararg argument so that the gc won't
           see garbage. */
        dst[maxArgsCnt] = AZero;

        /* Allocate an array for the rest of the arguments. */
        AMakeUninitArray_M(t, aLen, a, result);
        if (!result)
            return FALSE;

        /* Copy the rest of the arguments to the array. */
        while (aLen > 0) {
            aLen--;
            ASetArrayItemNewGen(a, aLen, src[ip[maxArgsCnt + aLen]]);
        }

        dst[maxArgsCnt] = a;
    } else if (argc > maxArgs)
        goto WrongNumberOfArgs;

    return TRUE;

  WrongNumberOfArgs:

    ARaiseArgumentErrorND(t, t->stackPtr[1],
                          argc + dst - (t->stackPtr + 3), 0);
    return FALSE;
}
