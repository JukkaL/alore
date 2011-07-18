/* runtime.h - Definitions related to the virtual machine

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef RUNTIME_H_INCL
#define RUNTIME_H_INCL

#include "common.h"

#include <stdarg.h>
#include "value.h"
#include "thread.h"
#include "operator.h"
#include "globals.h"


#define A_COMPILED_FRAME_FLAG AIntToValue(1)

/* GetExceptionHandler return values */
#define A_PROPAGATE_TO_CALLER -1
#define A_EXCEPTION_RAISED -2


AValue ACreateBoundMethod(AThread *t, AValue inst, int methodNum);


AValue ASetMemberHelper(AThread *t, AValue *inst, int member);
ABool ASetMemberDirectND(AThread *t, AValue inst, int memberIndex,
                         AValue val);


ABool ASetupCommandLineArgs(AThread *t, int argc, char **argv);

AValue ARun(AThread *t, AOpcode *ip, AValue *stack);

ABool ASetupArguments(AThread *t, AValue *stack, const AOpcode *ip, int argc,
                    AValue *newStack, int minArgs, int maxArgs);
AValue *ASetupFrame(AThread *t, AValue funcVal, int fullNumArgs,
                    AOpcode *argInd);
AValue *ASetupMethodFrame(AThread *t, AValue funcVal, AValue instance,
                          int fullNumArgs, AOpcode *argInd);

AValue ACallIndirect(AThread *t, AValue funcVal, int numArgs, AOpcode *argInd);
AValue ACallMethodIndirect(AThread *t, int memeber, int numArgs,
                           AOpcode *argInd);


#define A_ARG_IND_LENGTH 12

extern const AOpcode AArgInd[A_ARG_IND_LENGTH];


#define AMakeInterpretedOpcodeIndex(n) ((n) << 3)
#define AIsInterpretedFrame(frame) (!((frame)[2] & A_COMPILED_FRAME_FLAG))
#define AInterpretedOpcodeIndex(frame) ((frame)[2] >> 3)
#define AGetFrameIp(frame) \
    (AValueToFunction((frame)[1])->code.opc + ((frame)[2] >> 3) + 1)


#define ANextFrame(sp, frameSizeValue) \
    APtrAdd((sp), (frameSizeValue))

#define APreviousFrame(sp, frameSizeValue) \
    APtrSub((sp), (frameSizeValue))


int AGetExceptionHandler(AThread *t, AFunction *func, int codeInd);

AValue ACreateBasicExceptionInstance(AThread *t, int num, const char *message);

AValue ARaiseExceptionValue(AThread *t, AValue exception);
AValue ARaiseStackOverflowError(AThread *t);
#ifdef A_HAVE_WINDOWS
AValue ARaiseWin32Exception(AThread *t, int classNum);
A_APIFUNC const char *AGetWinsockErrorMessage(int code);
#endif

AValue ARaiseByNumFmt(AThread *t, int num, const char *fmt, ...);
AValue ARaiseByNumFmtVa(AThread *t, int num, const char *fmt, va_list args);

AValue ARaiseCastErrorND(AThread *t, AValue inst, AValue type);
AValue ARaiseBinopTypeErrorND(AThread *t, AOperator op, AValue left,
                              AValue right);
AValue ARaiseInvalidBooleanErrorND(AThread *t, AValue value);
AValue ARaiseMethodCallExceptionND(AThread *t, int member, AValue *args,
                                   int numArgs);
AValue ARaiseInvalidCallableErrorND(AThread *t, AValue value);

AValue ARaiseIntExpected(AThread *t, AValue actual);

ABool ADisplayStackTraceback(AThread *t,
                             ABool (*display)(const char *msg, void *data),
                             void *data);

A_APIFUNC ABool ACreateTracebackArray(AThread *t);
A_APIFUNC AValue AFilterTracebackArray(AThread *t);


/* IDEA: Consider unifying CompareOrder() and IsEqual(). */
AValue ACompareOrder(AThread *t, AValue left, AValue right,
                     AOperator operator);
AValue AIsEqual(AThread *t, AValue left, AValue right);

AValue ACoerce(AThread *t, AOperator op, AValue left, AValue right,
               AValue *rightRes);

AValue AIsIn(AThread *t, AValue left, AValue right);

AValue AWrapObject(AThread *thread, AValue obj, int member);


AValue AGetInstanceCodeMember(AThread *t, AInstance *inst,
                            unsigned member);
AValue AGetInstanceDataMember(AThread *t, AInstance *inst,
                            unsigned member);
int AGetInstanceMember(AInstance *inst, AMemberTableType type,
                       unsigned member);


void AAddFinalizeInst(AInstance *inst);

/* Initialize data members of a newly allocated instance block. */
#define AInitInstanceData(instance, type) \
    do {                                              \
        int i_;                                       \
        int total_ = (type)->totalNumVars;            \
        for (i_ = 0; i_ < total_; i_++)               \
            (instance)->member[i_] = ANil;            \
                                                      \
        if ((type)->hasFinalizerOrData != 0) {        \
            /* Initialize binary data to 0. */                 \
            memset(APtrAdd((instance), (type)->dataOffset), 0, \
                   (type)->dataSize);                          \
            if ((type)->hasFinalizer != 0)            \
                AAddFinalizeInst(instance);           \
        }                                             \
    } while (0)


/* Declarations specific to exit handlers that run when the vm is being
   shut down. */

void AInitializeExitHandlers(void);
ABool AExecuteExitHandlers(AThread *t);

extern AValue *AExitHandlers;
extern int AExitBlockInd;


#endif
