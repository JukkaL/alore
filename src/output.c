/* output.c - Generating compiled bytecode functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "output.h"
#include "error.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"


#define INITIAL_OUTPUT_BUFFER_SIZE 512
#define INITIAL_SAVE_BUFFER_SIZE   128
#define INITIAL_EXCEPT_BUFFER_SIZE 32
#define INITIAL_DEBUG_INFO_BUFFER_SIZE 32


AOpcode *ABuf;
unsigned ABufSize;
unsigned ABufInd;
unsigned APrevOpcodeInd;

unsigned ASaveBufSize;
unsigned ASaveSize;
AOpcode *ASaveBuf;

static AOpcode *ExceptBuf;
static unsigned ExceptBufSize;
static unsigned ExceptBufIndex;

static AOpcode *DebugInfoBuf;
static unsigned DebugInfoBufSize;
static unsigned DebugInfoBufIndex;

static unsigned LineIndex;
static unsigned PrevLineCodeIndex;
static unsigned PrevLine;

unsigned ANumActiveSections;
ASectionState ASection[A_MAX_ACTIVE_SECTIONS];


static ABool GrowOutputBuffer(void);
static ABool GrowBuffer(AOpcode **buffer, unsigned *length,
                        unsigned newLength);


ABool AInitializeOutput(void)
{
    ABuf = NULL;
    ABufSize = 0;
    if (!GrowBuffer(&ABuf, &ABufSize, INITIAL_OUTPUT_BUFFER_SIZE))
        return FALSE;
    
    ABufInd = 1;
    ABuf[0] = OP_NOP;

    ASaveBuf = NULL;
    ASaveBufSize = 0;
    if (!GrowBuffer(&ASaveBuf, &ASaveBufSize, INITIAL_SAVE_BUFFER_SIZE)) {
        AFreeStatic(ABuf);
        return FALSE;
    }

    ASaveSize = 0;

    ExceptBuf = NULL;
    ExceptBufSize = 0;
    if (!GrowBuffer(&ExceptBuf, &ExceptBufSize, INITIAL_EXCEPT_BUFFER_SIZE)) {
        /* FIX: free stuff */
        return FALSE;
    }

    ExceptBufIndex = 0;

    DebugInfoBuf = NULL;
    DebugInfoBufSize = 0;
    if (!GrowBuffer(&DebugInfoBuf, &DebugInfoBufSize,
                    INITIAL_DEBUG_INFO_BUFFER_SIZE)) {
        /* FIX: free stuff */
        return FALSE;
    }

    DebugInfoBufIndex = 0;
    
    ANumActiveSections = 0;

    return TRUE;
}


void ADeinitializeOutput(void)
{
    AFreeStatic(ABuf);
    AFreeStatic(ASaveBuf);
    AFreeStatic(ExceptBuf);
    AFreeStatic(DebugInfoBuf);
}


/* NOTE: Overflow is NOT checked. */
void AEnterSection(void)
{
    ASection[ANumActiveSections].bufInd = ABufInd;
    ASection[ANumActiveSections].debugInfoBufInd = DebugInfoBufIndex;
    ASection[ANumActiveSections].exceptInd = ExceptBufIndex;
    ASection[ANumActiveSections].lineIndex = LineIndex;
    ASection[ANumActiveSections].prevLineCodeIndex = PrevLineCodeIndex;
    ASection[ANumActiveSections].prevLine = PrevLine;

    ANumActiveSections++;
    
    PrevLineCodeIndex = ABufInd;
    LineIndex = A_OPCODE_BITS == 32 ? 3 : 1;
}


/* Leave current section and discard all the output. */
void AQuitSection(void)
{
    ANumActiveSections--;
    
    ABufInd = ASection[ANumActiveSections].bufInd;
    DebugInfoBufIndex = ASection[ANumActiveSections].debugInfoBufInd;
    ExceptBufIndex = ASection[ANumActiveSections].exceptInd;
    
    /* Set previous opcode to NOP (Buf[0] == NOP). */
    APrevOpcodeInd = 0;

    LineIndex = ASection[ANumActiveSections].lineIndex;
    PrevLineCodeIndex = ASection[ANumActiveSections].prevLineCodeIndex;
    PrevLine = ASection[ANumActiveSections].prevLine;

    /* FIX do we need to update NumLocals? */
    /* IDEA: Make LeaveSection() call this function to share code. */    
}


/* Leave current section and build a function based on the output. Return the
   function AValue. */
AValue ALeaveSection(ASymbolInfo *sym, int minArgs, int maxArgs, int num)
{
    int codeLen;
    int blockLen;
    int debugInfoLen;
    int exceptInfoLen;
    AFunction *func;
    AFunction dummy;
    int fixedSize;

    ANumActiveSections--;

    if (DebugInfoBufIndex == ASection[ANumActiveSections].debugInfoBufInd) {
        AEmitDebugInfo(A_LINE_NUMBER); /* FIX */
        /* FIX: what if EmitDebugInfo fails? */
    }
    
    codeLen = ABufInd - ASection[ANumActiveSections].bufInd;
    ABufInd = ASection[ANumActiveSections].bufInd;

    debugInfoLen = DebugInfoBufIndex -
                   ASection[ANumActiveSections].debugInfoBufInd;
    DebugInfoBufIndex = ASection[ANumActiveSections].debugInfoBufInd;

    exceptInfoLen = ExceptBufIndex - ASection[ANumActiveSections].exceptInd;
    ExceptBufIndex = ASection[ANumActiveSections].exceptInd;

    /* Calculate the fixed size of a Functin object (all the data before the
       opcodes). */
    fixedSize = APtrDiff(&dummy.code, &dummy.header);
    
    blockLen = fixedSize + (codeLen + exceptInfoLen +
                            debugInfoLen) * sizeof(AOpcode);
    func = AAllocUnmovable(blockLen);
    if (func == NULL) {
        AGenerateOutOfMemoryError();
        return AZero;
    }
    
    AInitNonPointerBlockOld(&func->header, blockLen - sizeof(AValue));
    /* Set previous opcode to NOP (Buf[0] == NOP). */
    APrevOpcodeInd = 0;

    ACopyMem(func->code.opc, ABuf + ABufInd, codeLen * sizeof(AOpcode));
    ACopyMem(func->code.opc + codeLen, ExceptBuf + ExceptBufIndex,
            exceptInfoLen * sizeof(AOpcode));
    ACopyMem(func->code.opc + codeLen + exceptInfoLen, DebugInfoBuf +
            DebugInfoBufIndex, debugInfoLen * sizeof(AOpcode));

    func->sym = sym;
    func->minArgs = minArgs;
    func->maxArgs = maxArgs;
    func->stackFrameSize = ANumLocals * sizeof(AValue);
    func->codeLen = codeLen;
    func->fileNum = ACurFileNum;
#ifdef HAVE_JIT_COMPILER
    func->num = num;
    func->isJitFunction = AIsJitModule;
#endif
    
    /* Is the stack frame of this code section too large? */
    if (ANumLocals >= A_MAX_FRAME_DEPTH)
        AGenerateError(-1, ErrInternalOverflow, IOF_STACK_FRAME_TOO_LARGE);

    LineIndex = ASection[ANumActiveSections].lineIndex;
    PrevLineCodeIndex = ASection[ANumActiveSections].prevLineCodeIndex;
    PrevLine = ASection[ANumActiveSections].prevLine;

    /* FIX do we need to update NumLocals? */
    
    return AFunctionToValue(func);
}


void ASaveCode(int size)
{
    int newSize = ASaveSize + size;

    if (newSize > ASaveBufSize) {
        if (!GrowBuffer(&ASaveBuf, &ASaveBufSize, newSize))
            return;
    }

    ABufInd -= size;
    ACopyMem(ASaveBuf + ASaveSize, ABuf + ABufInd, size * sizeof(AOpcode));
    ASaveSize = newSize;
}


void ARestoreCode(int size)
{
    if (size > ASaveSize)
        return;

    while (ABufSize - ABufInd < size)
        if (!GrowOutputBuffer())
            return;

    ASaveSize -= size;
    ACopyMem(ABuf + ABufInd, ASaveBuf + ASaveSize, size * sizeof(AOpcode));
    ABufInd += size;

    /* Set previous opcode to NOP. */
    APrevOpcodeInd = 0;
}


void AEmitOpcode(AOpcode opc)
{
     if (ABufInd >= ABufSize)
        if (!GrowOutputBuffer())
            return;

    APrevOpcodeInd = ABufInd;

    ABuf[ABufInd] = opc;
    ABufInd += 1;
}


void AEmitOpcodeArg(AOpcode opc, AOpcode arg)
{
     if (ABufInd + 1 >= ABufSize)
        if (!GrowOutputBuffer())
            return;

    APrevOpcodeInd = ABufInd;

    ABuf[ABufInd]     = opc;
    ABuf[ABufInd + 1] = arg;
    ABufInd += 2;
}


void AEmitOpcode2Args(AOpcode opc, AOpcode arg1, AOpcode arg2)
{
    if (ABufInd + 2 >= ABufSize)
        if (!GrowOutputBuffer())
            return;

    APrevOpcodeInd = ABufInd;

    ABuf[ABufInd]     = opc;
    ABuf[ABufInd + 1] = arg1;
    ABuf[ABufInd + 2] = arg2;
    ABufInd += 3;
}


void AEmitArg(AOpcode arg)
{
    if (ABufInd >= ABufSize)
        if (!GrowOutputBuffer())
            return;

    ABuf[ABufInd] = arg;
    ABufInd += 1;
}


void AEmit2Args(AOpcode arg1, AOpcode arg2)
{
    if (ABufInd + 1 >= ABufSize)
        if (!GrowOutputBuffer())
            return;

    ABuf[ABufInd]     = arg1;
    ABuf[ABufInd + 1] = arg2;
    ABufInd += 2;
}


static ABool GrowOutputBuffer(void)
{
    ABool result = GrowBuffer(&ABuf, &ABufSize, ABufSize * 2);
    if (!result)
        ABufInd = ABufSize;

    return result;
}


void AEmitExceptInfo(AOpcode opc)
{
    if (ExceptBufIndex >= ExceptBufSize)
        if (!GrowBuffer(&ExceptBuf, &ExceptBufSize, 2 * ExceptBufSize))
            return;

    ExceptBuf[ExceptBufIndex] = opc;
    ExceptBufIndex++;
}


void AEmitDebugInfo(AOpcode opc)
{
    if (DebugInfoBufIndex >= DebugInfoBufSize)
        if (!GrowBuffer(&DebugInfoBuf, &DebugInfoBufSize,
                        2 * DebugInfoBufSize))
            return;

    DebugInfoBuf[DebugInfoBufIndex] = opc;
    DebugInfoBufIndex++;

    LineIndex = A_OPCODE_BITS == 32 ? 3 : 1;
}


#if A_OPCODE_BITS == 32

void AEmitLineNumber(AToken *tok)
{
    unsigned lineDelta = tok->lineNumber - PrevLine;
    unsigned codeDelta = AGetCodeIndex() - PrevLineCodeIndex;

    PrevLine = tok->lineNumber;
    
    PrevLineCodeIndex = AGetCodeIndex();

    while (codeDelta > 15) {
        if (LineIndex != 3) {
            LineIndex++;
            DebugInfoBuf[DebugInfoBufIndex - 1] |= 15 << (4 + LineIndex * 7);
        } else
            AEmitDebugInfo(A_LINE_NUMBER | (15 << 4));

        codeDelta -= 15;
    }

    if (lineDelta > 7) {
        AEmitAbsoluteLineNumber(tok);
        lineDelta = 0;
    }

    if (LineIndex != 3) {
        LineIndex++;
        DebugInfoBuf[DebugInfoBufIndex - 1] |=
            (codeDelta | (lineDelta << 4)) << (4 + LineIndex * 7);
    } else
        AEmitDebugInfo(A_LINE_NUMBER | (codeDelta << 4) | (lineDelta << 8));
}


void AEmitAbsoluteLineNumber(AToken *tok)
{
    unsigned line = tok->lineNumber;

    AEmitDebugInfo(A_LINE_NUMBER | (1 << 3) | (line << 4));
    PrevLine = line;
}

#else

void AEmitLineNumber(unsigned lineNum)
{
    unsigned lineDelta = lineNum - SectionPrevLineNum[CurActiveSections];

    if (lineDelta > 0) {
        unsigned codeDelta = AGetCodeIndex() -
                         SectionPrevOpcodeIndex[CurActiveSection];
        /* FIX */
    }
}

#endif


static ABool GrowBuffer(AOpcode **buffer, unsigned *length, unsigned newLength)
{
    AOpcode *newBuf;

    newBuf = AGrowStatic(*buffer, newLength * sizeof(AOpcode));

    if (newBuf == NULL) {
        AGenerateOutOfMemoryError();
        return FALSE;
    } else {
        *length = newLength;
        *buffer = newBuf;
        return TRUE;
    }
}


/* Functions dealing with specific opcodes. */


/* Toggle the order of the assignment of an opcode that targets a local
   variable, for example ASSIGN_GL -> ASSIGN_LG. */
void AToggleInstruction(int index)
{
    /* We only need to switch the opcode - the operands can always be
       reused without modification. */
    
    int prevOpcode = AGetOpcode(index);
    int newOpcode = prevOpcode;
    
    switch (prevOpcode) {
    case OP_ASSIGN_LL:
        newOpcode = OP_ASSIGN_LL_REV;
        break;
    case OP_ASSIGN_GL:
        newOpcode = OP_ASSIGN_LG;
        break;
    case OP_ASSIGN_ML:
        newOpcode = OP_ASSIGN_LM;
        break;
    case OP_ASSIGN_VL:
        newOpcode = OP_ASSIGN_LV;
        break;
    case OP_ASSIGN_MDL:
        newOpcode = OP_ASSIGN_LMD;
        break;
    case OP_ASSIGN_EL:
        newOpcode = OP_ASSIGN_LE;
        break;
    case OP_AGET_LLL:
        newOpcode = OP_ASET_LLL;
        break;
    case OP_AGET_GLL:
        newOpcode = OP_ASET_GLL;
        break;
    default:
        /* This case should be impossible. */
        AGenerateError(0, ErrInternalError);
        break;
    }
    
    ASetOpcode(index, newOpcode);
}


void AToggleMultipleAssignment(AExpressionType type, int num,
                               int oldCodeIndex, int saveSize)
{
    unsigned codeIndex;
    int i;

    codeIndex = AGetCodeIndex() - saveSize;

    if ((type == ET_ARRAY_LOCAL_LVALUE ||
         type == ET_TUPLE_LOCAL_LVALUE)
        && AGetPrevOpcode() == OP_EXPAND) {
        /* Get rid of ASSIGN_LL instructions. */
        AEmitBack(saveSize);

        for (i = 0; i < num; i++)
            ASetOpcode(codeIndex - num + i,
                       AGetOpcode(codeIndex + 1 + 3 * i));
    } else {
        for (i = 0; i < num; i++) {
            int index = AAssignPos[i] + codeIndex - oldCodeIndex;
            AOpcode opcode = AGetOpcode(index);

            AToggleInstruction(index);
            ASetOpcode(index + AOpcodeSizes[opcode] - 1,
                       ANumLocalsActive + i +
                       (AIsSpecialAssignmentOpcode(opcode) ?
                       A_NO_RET_VAL : 0));
        }
    }
}
