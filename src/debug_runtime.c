/* debug_runtime.c - Runtime debugging functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "alore.h"
#include "runtime.h"
#include "debug_runtime.h"
#include "internal.h"
#include "array.h"
#include "token.h" /* FIX?? */
#include "util.h"
#include "heap.h"
#include "str.h"
#include "gc.h"
#include "gc_internal.h"


#ifdef A_DEBUG

static char FmtBuffer[128];
#define FMT(fmt, val) (sprintf(FmtBuffer, fmt, val), FmtBuffer)


static void PrintValueRecursive(AValue v, int depth);


unsigned long ADebugInstructionCounter = 0;
unsigned long ADebugAllocationCounter = 0;
unsigned long ADebugCheckEveryNth = 1;
unsigned long ADebugCheckFirst = 0x7fffffffUL;
unsigned long ADebugCheckLast =  0xffffffffUL;


void ADebugNextInstruction(void)
{
    ADebugInstructionCounter++;
    ADebugVerifyMemory();
    if (ADebugIsOn())
        ADebugPeriodicCheck();
}


#ifdef A_DEBUG_GC_EVERY_NTH
void ADebugNextAllocation(AThread *t)
{
    if (ADebugIsOn()) {
        if (ADebugAllocationCounter % A_DEBUG_GC_EVERY_NTH == 0) {
            if (!ACollectAllGarbage())
                ADebugError_F("Garbage collection failed!\n");
        }
        ADebugAllocationCounter++;
    }
}
#endif


void ADebugPrint_F(char *format, ...)
{
    va_list args;

    if (ADebugIsOn()) {
        char buf[4096];
        va_start(args, format);
        AFormatMessageVa(buf, 4096, format, args);
        fprintf(stderr, "%s", buf);
        va_end(args);
    }
}


void ADebugError_F(char *format, ...)
{
    va_list args;
    char buf[4096];

    fprintf(stderr, A_ERR_HEADER);
    
    va_start(args, format);
    AFormatMessageVa(buf, 4096, format, args);
    fprintf(stderr, "%s", buf);
    va_end(args);

    exit(1);
}


void ADebug_Trace(AValue *stack, AOpcode *ip)
{
    char buf[4096];
    AFunction *func;
    
    ADebugPrint_F("(%d) ", (int)ADebugInstructionCounter);

    func = AValueToFunction(stack[1]);
    sprintf(buf, "%3ld: ", (long)(ip - func->code.opc));
    ADebugPrint_F(buf);
    AFormatInstruction(buf, func->code.opc, ip - func->code.opc);
    ADebugPrint_F(buf);

    ADebugPrint_F(" (");
    ADebugPrintValue(stack[1]);
    ADebugPrint_F(")");

    ADebugPrint_F("\n");

#ifdef A_DEBUG_SHOW_HEAP_INFO
    ADebugPrint_F("ACurFreeBlock: %s, ", FMT("%p", ACurFreeBlock));
    ADebugPrint_F("ALastBlock: %s\n", FMT("%p", ALastBlock));
#endif
}


void ADebugPrintValue(AValue v)
{
    PrintValueRecursive(v, 0);
}


#define MAX_VALUE_PRINT_DEPTH 2


static void PrintSubStr(AValue v, int i1, int i2)
{
    for (; i1 <= i2; i1++) {
        AWideChar ch = AStrItem(v, i1);
        if ((ch >= 32 && ch < 128 && ch != '\\')
            || (ch > 128+32 && ch < 256))
            ADebugPrint_F("%s", FMT("%c", ch));
        else
            ADebugPrint_F("%s", FMT("\\u%.4x", ch));
    }
}


void PrintValueRecursive(AValue v, int depth)
{
    if (AIsShortInt(v))
        ADebugPrint_F("%d", AValueToInt(v));
    else if (AIsStr(v) || AIsWideStr(v) || AIsSubStr(v)) {
        ADebugPrint_F("\"");
        if (AStrLen(v) < 60)
            PrintSubStr(v, 1, AStrLen(v));
        else {
            PrintSubStr(v, 1, 15);
            ADebugPrint_F(" ... ");
            PrintSubStr(v, AStrLen(v) - 14, AStrLen(v));
        }
        ADebugPrint_F("\""); /* FIX show strings */
    } else if (AIsArray(v)) {
        int i;
        int len = AArrayLen(v);
        if (depth >= MAX_VALUE_PRINT_DEPTH)
            ADebugPrint_F("(...)", len);
        else {
            ADebugPrint_F("(");
            for (i = 0; i < len; i++) {
                if (i < 9 || i > len - 3) {
                    PrintValueRecursive(AArrayItem(v, i), depth + 1);
                    if (i < len)
                        ADebugPrint_F(", ");
                } else if (i == 10)
                    ADebugPrint_F("..., ");
            }
            ADebugPrint_F(")");
        }
    } else if (AIsGlobalFunction(v)) {
        AFunction *func = AValueToFunction(v);
        ADebugPrint_F("%F", func);
    } else if (AIsNonSpecialType(v)) {
        ATypeInfo *type = AValueToType(v);
        ADebugPrint_F("%q", type->sym);
    } else if (AIsMixedValue(v)) {
        AMixedObject *rng = AValueToMixedObject(v);
        if (rng->type == A_RANGE_ID) {
            PrintValueRecursive(rng->data.range.start, depth + 1);
            ADebugPrint_F(" to ");
            PrintValueRecursive(rng->data.range.stop, depth + 1);
        } else if (rng->type == A_BOUND_METHOD_ID) {
            AValue inst;
            AValue func;

            inst = rng->data.boundMethod.instance;
            if (AIsInstance(inst))
                PrintValueRecursive(inst, depth + 1);
            else
                ADebugPrint_F("INVALID");
            ADebugPrint_F(".");
            func = rng->data.boundMethod.method;
            if (AIsGlobalFunction(func))
                PrintValueRecursive(func, depth + 1);
            else
                ADebugPrint_F("INVALID");
        } else
            ADebugPrint_F("INVALID");
    } else if (AIsConstant(v)) {
        if (AIsNil(v))
            ADebugPrint_F("nil");
        else if (AIsError(v))
            ADebugPrint_F("AError");
        else if (AIsDefault(v))
            ADebugPrint_F("ADefaultArg");
        else
            ADebugPrint_F("%i", AValueToConstant(v)->sym);
    } else if (AIsInstance(v))
        ADebugPrint_F("%q", AGetInstanceType(AValueToInstance(v))->sym);
    else
        ADebugPrint_F("unknown"); /* FIX: support more stuff */
}


void ADebugPrintValueWithType(AValue v)
{
    if (AIsShortInt(v))
        ADebugPrint_F("Int(");
    else if (AIsStr(v) || AIsWideStr(v) || AIsSubStr(v))
        ADebugPrint_F("Str(");
    else if (AIsArray(v))
        ADebugPrint_F("Array(");
    else if (AIsGlobalFunction(v))
        ADebugPrint_F("Function(");
    else if (AIsNonSpecialType(v))
        ADebugPrint_F("Type(");
    else if (AIsInstance(v))
        ADebugPrint_F("Instance(");
    else if (AIsConstant(v))
        ADebugPrint_F("Constant(");
    else if (AIsMixedValue(v))
        ADebugPrint_F("MixedObject(");
    else if (AIsLongInt(v))
        ADebugPrint_F("LongInt(");
    else if (AIsFloat(v))
        ADebugPrint_F("Float(");
    else
        ADebugPrint_F("unknown(");

    ADebugPrintValue(v);

    ADebugPrint_F(")");
}


void AShowGCStats(void)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "*** Gargage collector stats ***\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Sizes\n");
    fprintf(stderr, "-----\n");
    fprintf(stderr, "  allocCount:   %lld\n", AGCStat.allocCount);
    fprintf(stderr, "  retireCount:  %lld\n", AGCStat.retireCount);
    fprintf(stderr, "  heapSize:     %lu\n", AGCStat.heapSize);
    fprintf(stderr, "  nurserySize:  %lu\n", AGCStat.nurserySize);
    fprintf(stderr, "  oldGenSize:   %lu\n", AGCStat.oldGenSize);
    fprintf(stderr, "  lastLiveSize: %lu\n", AGCStat.lastLiveSize);
    fprintf(stderr, "\n");
    fprintf(stderr, "Collection counters\n");
    fprintf(stderr, "-------------------\n");
    fprintf(stderr,
            "  newGenCollectCount:  %lu\n", AGCStat.newGenCollectCount);
    fprintf(stderr, "  fullCollectCount:    %lu\n", AGCStat.fullCollectCount);
    fprintf(stderr,
            "  forcedCollectCount:  %lu\n", AGCStat.forcedCollectCount);
    fprintf(stderr,
            "  fullIncrementCount:  %lu\n", AGCStat.fullIncrementCount);
    fprintf(stderr, "\n");
}

#endif


#ifdef A_DEBUG_CALL_PERIODIC_CHECK
void ADebugPeriodicCheck(void)
{
    /* Insert periodic ad-hoc debugging code here. */
}
#endif
