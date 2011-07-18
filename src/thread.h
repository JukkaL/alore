/* thread.h - Thread related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef THREAD_H_INCL
#define THREAD_H_INCL

#include "aconfig.h"

#include <setjmp.h>
#include "value.h"


#define A_TEMP_STACK_SIZE 1024

#define A_GC_LIST_BLOCK_LENGTH 128


#define A_NUM_CACHED_REGEXPS 6


typedef struct AGCListBlock_ {
    AValue header;
    struct AGCListBlock_ *next;
    unsigned long size;
    union {
        AValue val[A_GC_LIST_BLOCK_LENGTH];
        AValue *valPtr[A_GC_LIST_BLOCK_LENGTH];
    } data;
} AGCListBlock;


typedef struct {
    jmp_buf env;
    AValue *stackPtr;
    AValue *tempStackPtr;
} AExceptionContext;


typedef struct AThread_ {
    /* Next thread in the list of all threads (or NULL if last) */
    struct AThread_ *next;

    /* Top and end pointers of the thread-local heap (which is part of the new
       generation). Space between these pointers is available for allocation by
       the thread. */
    char *heapPtr;
    char *heapEnd;

    /* Old geneneration -> new generation references */
    AGCListBlock *newRef;
    AValue **newRefPtr;
    AValue **newRefEnd;

    /* New geneneration objects referenced from old geneneration */
    AGCListBlock *newRefValues;
    AGCListBlock *curNewRefValues;

    /* List of values that the mark-sweep gc may not have seen and that it
       might not find unless it traces this list */
    AGCListBlock *untraced;
    AValue *untracedPtr;
    AValue *untracedEnd;

    AValue *stack;       /* Thread Alore-level stack (grows down) */
    
    AValue *stackTop;    /* Top of stack */
    AValue *stackPtr;    /* Current stack pointer */

    /* Stack for holding temporary values that are visible to the gc. */
    AValue *tempStack;
    AValue *tempStackPtr;
    AValue *tempStackEnd;

    AValue *markStackBottom; /* Stack pointer at beginning of mark */

    /* Raised exception instance (valid while the exception is being raised) */
    AValue exception;

    AValue *uncaughtExceptionStackPtr;
    void *filler; /* FIX remove */
    /* Is the current exception raised again after leaving a finally block? */
    ABool isExceptionReraised;

    AExceptionContext *context;
    int contextSize;
    int contextIndex;

    int stackSize;     /* Current stack size (bytes) */

    void *finallyNextIp; /* Used in finally handling; stores pointer to next
                            compiled code offset to execute */
    int finallyStackSize; /* Used in finally handling; size indicator of the
                             current compiled function low-level stack */
    
    /* Cache for compiled regular expressions. regExp[2n] is a string object
       and regExp[2n + 1] is the corresponding regular expression object
       compiled with flags in regExpFalgs[n]. */
    AValue regExp[A_NUM_CACHED_REGEXPS * 2];
    AValue regExpFlags[A_NUM_CACHED_REGEXPS];
} AThread;


#define AHandleException(t) \
    (AUpdateContext(t) ? \
     (setjmp(t->context[t->contextIndex - 1].env) ?                  \
      (/*(t->contextIndex--), */                                      \
       (t->stackPtr = t->context[t->contextIndex - 1].stackPtr),         \
       (t->tempStackPtr = t->context[t->contextIndex - 1].tempStackPtr), \
       1) : 0) : 2)


A_APIFUNC ABool AUpdateContext(AThread *t);


AThread *ACreateMainThread(void);

/* Routines for traversing all active threads */
AThread *AGetFirstThread(void);
AThread *AGetNextThread(AThread *t);


enum {
    A_STACK_OUT_OF_MEMORY,
    A_STACK_OVERFLOW
};


ABool AAdvanceNewRefList(AThread *t);
ABool AAdvanceUntracedList(AThread *t);

AThread *ACreateThread(AValue *temp);

A_APIFUNC void AFreezeOtherThreads(void);
A_APIFUNC void AFreezeOtherThreadsSilently(void);
A_APIFUNC void AWakeOtherThreads(void);
A_APIFUNC void AWakeOtherThreadsSilently(void);

A_APIFUNC void ALockInterpreter(void);
A_APIFUNC void AUnlockInterpreter(void);


extern AThread *AMainThread;


/* FIX: doesn't work necessarily.. should be more portable */
#define AGetGCListBlock(end) \
    ((AGCListBlock *)APtrSub((end), sizeof(AGCListBlock)))


/* IDEA: Don't raise memory error. Use something more descriptive. */
#define AAllocTempStack_M(t, n) \
    ((t)->tempStackEnd - (t)->tempStackPtr >= (n) + 3 ? \
     ((t)->tempStackPtr += (n), TRUE)                        \
      : (ARaisePreallocatedMemoryErrorND(t), FALSE))


/* At the bottommost stack of frame of each thread other that the main thread,
   this value is stored instead of the function value. */
#define A_THREAD_BOTTOM_FUNCTION AIntToValue(995)

#define A_THREAD_ARG_BUFFER_SIZE 8

/* Buffer for communicating arguments for threads waiting in the thread pool */
extern AValue AThreadArgBuffer[A_THREAD_ARG_BUFFER_SIZE * 3];


ABool AAllocTempStack(AThread *t, int n);


extern int ANumThreads;


extern volatile AAtomicInt AIsInterrupt;

A_APIFUNC ABool AHandleInterrupt(AThread *t);


/* Number of values reserved at the start of thread->tempStack for the run-time
   system (i.e. these are not usable by extension modules). The third one is
   currently reserved for AdvanceNewRefList. */
#define A_NUM_FIXED_THREAD_TEMPS 3


#endif
