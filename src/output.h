/* output.h - Generating compiled bytecode functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef OUTPUT_H_INCL
#define OUTPUT_H_INCL

#include "opcode.h"
#include "value.h"
#include "parse.h"


/* Maxmimum number of different code output streams active at the same time.
   1 is needed for the module main level, 1 for global functions or the class
   main level, 1 for methods. Any additional sections can be used by anonymous
   functions. */
#define A_MAX_ACTIVE_SECTIONS (3 + A_MAX_ANON_SUB_DEPTH)


typedef struct {
    unsigned bufInd;
    unsigned debugInfoBufInd;
    unsigned exceptInd;
    unsigned lineIndex;
    unsigned prevLineCodeIndex;
    unsigned prevLine;
} ASectionState;


ABool AInitializeOutput(void);
void ADeinitializeOutput(void);

void AEnterSection(void);
AValue ALeaveSection(struct ASymbolInfo_ *sym, int minArgs, int maxArgs,
                     int num);
void AQuitSection(void);

void ASaveCode(int size);
void ARestoreCode(int size);

void AEmitOpcode(AOpcode opc);
void AEmitOpcodeArg(AOpcode opc, AOpcode arg);
void AEmitOpcode2Args(AOpcode opc, AOpcode arg1, AOpcode arg2);
void AEmitArg(AOpcode arg);
void AEmit2Args(AOpcode arg1, AOpcode arg2);
#define AEmitOpcode3Args(opc, arg1, arg2, arg3) \
    do {                                       \
         AEmitOpcode2Args(opc, arg1, arg2);     \
         AEmitArg(arg3);                        \
    } while (0)

#define AEmitBackwardBranch(dest) \
    AEmitOpcodeArg(OP_JMP, (dest) - AGetCodeIndex() - 1 + A_DISPLACEMENT_SHIFT)


#define AGetCodeIndex() (ABufInd)
#define ASetOpcode(index, value) (ABuf[(index)] = (value))
#define AGetOpcode(index) (ABuf[(index)])
#define AToggleBranch(index) (ABuf[(index)] ^= 1)
#define ASetBranchDest(index, dest) \
    ASetOpcode((index) + AOpcodeSizes[AGetOpcode(index)] - 1,           \
               (dest) - (index) - AOpcodeSizes[AGetOpcode(index)] + 1 + \
               A_DISPLACEMENT_SHIFT)
#define AGetPrevOpcodeIndex() (APrevOpcodeInd)
#define AGetPrevOpcode() AGetOpcode(AGetPrevOpcodeIndex())
#define AClearPrevOpcode() (APrevOpcodeInd = 0)
#define AEmitBack(num) (ABufInd -= (num))


extern AOpcode *ABuf;
extern unsigned ABufSize;
extern unsigned ABufInd;
extern unsigned APrevOpcodeInd;

extern unsigned ASaveBufSize;
extern unsigned ASaveSize;
extern AOpcode *ASaveBuf;

extern unsigned ANumActiveSections;
extern ASectionState ASection[A_MAX_ACTIVE_SECTIONS];


#define AEmitBinaryOperation(operator, leftType, left, rightType, right) \
    do {                                                                 \
        if ((leftType) == A_OT_LOCAL && (rightType) == A_OT_LOCAL        \
            && AIsQuickOperator(operator))                               \
            AEmitOpcode2Args(OP_ADD_LLL + (operator), (left), (right));  \
        else {                                                           \
            AEmitOpcode2Args(OP_GET_LL + (leftType) * 3 + (rightType),   \
                            (left), (right));                        \
            AEmitOpcode(OP_ADD_L + (operator));                      \
                                                                     \
        }                                                            \
                                                                     \
        if (AIsComparisonOperator(operator))                         \
            AEmitArg(0);                                             \
    } while (0)


#define AEmitNegateOperation(src) \
    AEmitOpcodeArg(OP_MINUS_LL, (src))


/* src == local variable with an array to expand
   num == number of variables to expand
   dst == array of local variable numbers */
#define AEmitArrayExpand(src, num, dst) \
    do {                                             \
        int i;                                       \
                                                     \
        AEmitOpcode2Args(OP_EXPAND, (src), (num)); \
        for (i = 0; i < num; i++)                    \
            AEmitArg((dst)[i]);                      \
    } while (0)


#define AEmitArrayCreate(srcFirst, num) \
    AEmitOpcode2Args(OP_CREATE_ARRAY, num, srcFirst)

#define AEmitTupleCreate(srcFirst, num) \
    AEmitOpcode2Args(OP_CREATE_TUPLE, num, srcFirst)


#define AUnemitArrayOrTupleCreate() \
    AEmitBack(3)


#define AEmitDestination(num) \
    AEmitArg(num)


#define AEmitGetIndex(baseType, base, index)  \
    AEmitOpcode2Args((baseType) == A_OT_LOCAL \
                    ? OP_AGET_LLL : OP_AGET_GLL, (base), (index))


#define AEmitSetIndex(baseType, base, index, item) \
    AEmitOpcode3Args((baseType) == A_OT_LOCAL      \
                    ? OP_ASET_LLL : OP_ASET_GLL, (base), (index), (item))


#define AEmitJump(dst) \
    AEmitOpcodeArg(OP_JMP, dst)


#define AEmitIfBoolean(src, dst) \
    AEmitOpcode2Args(OP_IF_TRUE_L, src, dst)


void AEmitAssignmentDestination(int num);


void AToggleInstruction(int index);

#define ATogglePreviousInstruction() AToggleInstruction(AGetPrevOpcodeIndex())


void AToggleMultipleAssignment(AExpressionType type, int num,
                               int oldCodeIndex, int saveSize);


#define ARecordAssignment(index) \
    (AAssignPos[index] = AGetPrevOpcodeIndex())


#define AEmitCall(type, callee, argCnt, isVarArg, args) \
    do {                                                                   \
        int i;                                                             \
                                                                           \
        AEmitOpcode2Args((type) == A_OT_LOCAL ? OP_CALL_L : OP_CALL_G,     \
                         (callee),                                         \
                         ((argCnt) + ((type) == A_OT_MEMBER_FUNCTION)) |   \
                         ((isVarArg) ? A_VAR_ARG_FLAG : 0));               \
                                                                           \
        if ((type) == A_OT_MEMBER_FUNCTION)                                \
            AEmitArg(3);                                                   \
                                                                           \
        for (i = 0; i < (argCnt) && i < A_MAX_QUICK_ARGS; i++)             \
            AEmitArg((args)[i]);                                           \
                                                                           \
        for (; i < (argCnt); i++)                                          \
            AEmitArg(ANumLocalsActive - (argCnt) + i);                     \
    } while (0)


#define AEmitMemberCall(type, callee, member, argCnt, isVarArg, args, rest) \
    do {                                                                  \
        int i;                                                            \
                                                                          \
        AEmitOpcode3Args(OP_CALL_M,      (member),                        \
                         ((argCnt) + 1) | (isVarArg ? A_VAR_ARG_FLAG : 0),\
                         (callee));                                       \
                                                                          \
        for (i = 0; i < (argCnt) && i < A_MAX_QUICK_ARGS; i++)            \
            AEmitArg(quickArgs[i]);                                       \
                                                                          \
        for (; i < numArgs; i++)                                          \
            AEmitArg((rest) + i);                                         \
    } while (0)


#define AEmitPrivateMemberRead(readId, writeId) \
    AEmitOpcode2Args(OP_ASSIGN_MDL, readId, writeId)


#define AEmitDirectMemberRead(key) \
    AEmitOpcodeArg(OP_ASSIGN_VL, key)


/* NOTE: The order of the A_OT_x constants is important! Do not change it. */
typedef enum {
    A_OT_LOCAL,
    A_OT_INT,
    A_OT_GLOBAL,
    A_OT_MEMBER,
    A_OT_MEMBER_VARIABLE,
    A_OT_MEMBER_FUNCTION
} AOperandType;


#define AEmitTryBlockBegin(isDirect) \
    AEmitExceptInfo(((AGetCodeIndex() - \
                      ASection[ANumActiveSections - 1].bufInd) << 2) + \
                    (isDirect << 1))

#define AEmitTryBlockEnd() \
    AEmitExceptInfo(A_END_TRY_BLOCK)

#define AEmitExcept(local, global) \
    do {                                                              \
        AEmitExceptInfo(A_EXCEPT | ((local) << A_EXCEPT_CODE_SHIFT)); \
        AEmitExceptInfo(AGetCodeIndex() -                             \
                       ASection[ANumActiveSections - 1].bufInd);      \
        AEmitExceptInfo(global);                                      \
    } while (0)

#define AEmitFinally(local) \
    do {                                                               \
        AEmitExceptInfo(A_FINALLY | ((local) << A_EXCEPT_CODE_SHIFT)); \
        AEmitExceptInfo(AGetCodeIndex() -                              \
                       ASection[ANumActiveSections - 1].bufInd);       \
    } while (0)


struct AToken_;


void AEmitExceptInfo(AOpcode opc);
void AEmitDebugInfo(AOpcode opc);
void AEmitLineNumber(struct AToken_ *tok);
void AEmitAbsoluteLineNumber(struct AToken_ *tok);


#endif
