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


/* Only include debugging functions if compiling in debugging mode. */
#ifdef A_DEBUG

static char FmtBuffer[128];
/* Do sprintf with format string and a single additional argument, and return
   a pointer to the target buffer. Note that the buffer has fixed size and this
   function is not thread-safe as there is only a single global buffer. */
#define FMT(fmt, val) (sprintf(FmtBuffer, fmt, val), FmtBuffer)


static void PrintValueRecursive(AValue v, int depth);


/* Number of "instructions" executed since program start. If the value is
   1000000 or greater, this counts interpreter bytecode instructions. If the
   value is less than 1M, count different stages of compilation (the
   instructions in this case are not very well defined). This is used to
   restrict runtime debugging to certain parts of program execution. */
unsigned long ADebugInstructionCounter = 0;
/* Number of allocation operations since program start (approximately), but
   only for checked instructions. */
unsigned long ADebugAllocationCounter = 0;
/* Perform runtime checks every n'th instruction, where this variable is n. */
unsigned long ADebugCheckEveryNth = 1;
/* First and last instructions to check (as measured by
   ADebugInstructionCounter). By default, the values are such that no debugging
   is performed. */
unsigned long ADebugCheckFirst = 0x7fffffffUL;
unsigned long ADebugCheckLast =  0xffffffffUL;


/* The functions below are called for certain events, but they only perform
   non-trivial actions only for checked instructions (ADebugIsOn() returns
   TRUE). */


/* Called for each interpreted instruction. */
void ADebugNextInstruction(void)
{
    ADebugInstructionCounter++;
    ADebugVerifyMemory();
    if (ADebugIsOn())
        ADebugPeriodicCheck();
}


/* Called for each allocation. Perform GC every nth allocation. */
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


/* Display a message if debugging is active. The arguments are similar to
   AFormatMessage, but this function uses a fixed-length internal buffer. */
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


/* Display a message and terminate program. Arguments are similar to
   ADebugPrint_F. */
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


/* Display a debugging message useful for tracing the execution of a program.
   Display the name of the current function and the current opcode. The stack
   argument should point to the topmost stack frame and the ip to the current
   opcode being executed. */
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


/* Pretty-print the contents of a value. Use a compact format that only
   displays fragments of large values (e.g. strings). */
void ADebugPrintValue(AValue v)
{
    PrintValueRecursive(v, 0);
}


/* For nested values, only print values up to this nesting depth. This is used
   to avoid overly long output. */
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


/* Recursively pretty-print a value. */
void PrintValueRecursive(AValue v, int depth)
{
    if (AIsShortInt(v))
        ADebugPrint_F("%d", AValueToInt(v));
    else if (AIsStr(v) || AIsWideStr(v) || AIsSubStr(v)) {
        ADebugPrint_F("\"");
        /* Display short strings entirely. For long strings, display a prefix
           and a suffix (and ... between them). */
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
            /* Display array items. For long arrays, only display a
               fragment. */
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


/* Pretty-print a value. Also include the internal representation type. */
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


/* Display garbage collector statistics. */
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


/* This function is called every nth instructions if
   A_DEBUG_CALL_PERIODIC_CHECK is defined. Modify this function to perform
   custom debugging operations. */
#ifdef A_DEBUG_CALL_PERIODIC_CHECK
void ADebugPeriodicCheck(void)
{
    /* Insert periodic ad-hoc debugging code here. */
}
#endif
