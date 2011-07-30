/* debug_runtime.h - Definitions for Alore run-time debugging

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef DEBUG_RUNTIME_H_INCL
#define DEBUG_RUNTIME_H_INCL


#include "common.h"
#include "debug_params.h"


#ifdef A_DEBUG

/* Headers used in debugging messages */
#define A_ERR_HEADER "RUNTIME ERROR: "
#define A_MSG_HEADER "DEBUG: "


#ifdef A_DEBUG_STATUS_MESSAGES
#define ADebugStatusMsg(args) \
  do { \
      ADebugPrint_F("(%d) ", (int)ADebugInstructionCounter); \
      ADebugPrint_F args;                                    \
  } while (0)
#else
#define ADebugStatusMsg(args) /* NOP */
#endif

#ifdef A_DEBUG_COMPILER
#define ADebugCompilerMsg(args) \
  do { \
      ADebugPrint(("(%d) ", (int)ADebugInstructionCounter)); \
      ADebugPrint(args);                                     \
      ADebugPrint(("\n"));                                   \
      ADebugNextInstruction();                               \
  } while (0)
#else
#define ADebugCompilerMsg(args) /* NOP */
#endif

#ifdef A_DEBUG_VERIFY_MEMORY
#define ADebugVerifyMemory() ADebugVerifyMemory_F()
#else
#define ADebugVerifyMemory() /* NOP */
#endif

#define ADebugPrint(args) ADebugPrint_F args
#define ADebugError(args) ADebugError_F args

void ADebugNextInstruction(void);

void ADebugPrint_F(char *str, ...);
void ADebugError_F(char *str, ...);

void ADebugPrintValue(AValue v);
void ADebugPrintValueWithType(AValue v);
void ADebugVerifyMemory_F(void);
void ADebug_MemoryEvent(void *ptr, char *msg); /* FIX: call convention etc */
void ADebug_Trace(AValue *stack, AOpcode *ip); /* FIX: name, call conv etc */

#ifdef A_DEBUG_CALL_PERIODIC_CHECK
/* This function is called every now and then. Insert checking code inside
   this function. */
void ADebugPeriodicCheck(void);
#else
#define ADebugPeriodicCheck() /* NOP */
#endif

#ifdef A_DEBUG_GC_EVERY_NTH
void ADebugNextAllocation(struct AThread_ *t);
#else
#define ADebugNextAllocation(t)
#endif

extern void *ADebug_WatchLocation;
extern ABool ADebug_MemoryTrace;
extern ABool ADebug_MemoryDump;
extern unsigned long ADebugInstructionCounter;
extern unsigned long ADebugAllocationCounter;
extern unsigned long ADebugCheckEveryNth;
extern unsigned long ADebugCheckFirst;
extern unsigned long ADebugCheckLast;

#define ADebugIsOn() \
    (ADebugInstructionCounter % ADebugCheckEveryNth == 0 \
     && ADebugInstructionCounter >= ADebugCheckFirst \
     && ADebugInstructionCounter <= ADebugCheckLast)

void ADisplayCode(void);
int AFormatInstruction(char *buf, AOpcode *code, int ip);

void AShowGCStats(void);

#else /* !defined(A_DEBUG) */

/* Debugging is not active; include empty definitions for debugging macros. */

#define ADebugPrint(args) /* NOP */
#define ADebugError(args) /* NOP */
#define ADebugStatusMsg(args) /* NOP */
#define ADebugCompilerMsg(args) /* NOP */
#define ADebugVerifyMemory() /* NOP */
#define ADebugNextInstruction() /* NOP */
#define ADebugNextAllocation(t) /* NOP */

#define A_NO_INLINE_ALLOC 0

#endif /* A_DEBUG */

#endif
