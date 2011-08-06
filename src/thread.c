/* thread.c - Thread related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "thread.h"
#include "alore.h"
#include "runtime.h"
#include "thread_athread.h"
#include "heap.h"
#include "gc.h"
#include "mem.h"
#include "internal.h"
#include "debug_runtime.h"
#include "opcode.h"
#include "errmsg.h"


#define INITIAL_TEMP_STACK_SIZE 128
#define INITIAL_CONTEXT_STACK_SIZE 10


static AGCListBlock *AllocateGCListBlock(void);


/* Number of threads alive. */
int ANumThreads;
/* The list of all live threads. */
static AThread *Threads;


/* Circular buffer used for transmitting information to newly created threads.
   Contains three items for each thread: Thread instance, function to call,
   and arguments to the function.

   ArgBufFirst and ArgBufLast (in thread_athread.c) specify which portion
   of the buffer is in use. */
AValue AThreadArgBuffer[A_THREAD_ARG_BUFFER_SIZE * 3];


/* Create the AThread structure related to the main thread. */
AThread *ACreateMainThread(void)
{
    AValue temp[4];
    int i;

    for (i = 0; i < A_THREAD_ARG_BUFFER_SIZE * 3; i++)
        AThreadArgBuffer[i] = AZero;
    
    ANumThreads = 1;
    Threads = NULL;

    if (!AInitializeThreads())
        return NULL;

    return ACreateThread(temp);
}


/* Allocate and initialize an AThread structure. */
AThread *ACreateThread(AValue *temp)
{
    AThread *t;
    void *ptr;
    int i;

    ptr = AllocateGCListBlock();
    if (ptr == NULL)
        return NULL;
    
    temp[0] = ANonPointerBlockToValue(ptr);
    
    ptr = AllocateGCListBlock();
    if (ptr == NULL)
        return NULL;
    
    temp[1] = ANonPointerBlockToValue(ptr);

    ptr = AllocateGCListBlock();
    if (ptr == NULL)
        return NULL;

    temp[2] = ANonPointerBlockToValue(ptr);
    
    t = AAllocStatic(sizeof(AThread));
    if (t == NULL)
        return NULL;

    t->stack = AAllocStatic(A_ALORE_STACK_SIZE);
    if (t->stack == NULL) {
        AFreeStatic(t);
        return NULL;
    }

    /* FIX is this freed somewhere */
    t->tempStack = AAllocStatic(A_TEMP_STACK_SIZE);
    if (t->tempStack == NULL) {
        AFreeStatic(t->stack);
        AFreeStatic(t);
        return NULL;
    }

    t->contextIndex = 0;
    t->contextSize = INITIAL_CONTEXT_STACK_SIZE;
    t->context = AAllocStatic(t->contextSize *
                                  sizeof(AExceptionContext));
    if (t->context == NULL) {
        AFreeStatic(t->tempStack);
        AFreeStatic(t->stack);
        AFreeStatic(t);
        return NULL;
    }

    t->stackTop = APtrAdd(t->stack, A_ALORE_STACK_SIZE);
    t->stackPtr = t->stackTop; /* FIX ok? */
    t->stackSize = A_ALORE_STACK_SIZE;
    
    for (i = 0; i < A_NUM_FIXED_THREAD_TEMPS; i++)
        t->tempStack[i] = AZero;
    t->tempStackPtr = t->tempStack + A_NUM_FIXED_THREAD_TEMPS;
    t->tempStackEnd = t->tempStack + INITIAL_TEMP_STACK_SIZE;

    t->heapPtr = t->heapEnd = A_EMPTY_THREAD_HEAP_PTR;

    t->newRef = AValueToPtr(temp[0]);
    t->newRefValues = AValueToPtr(temp[1]);

    t->newRefPtr = t->newRef->data.valPtr;
    t->newRefEnd = t->newRefPtr + A_GC_LIST_BLOCK_LENGTH;

    t->curNewRefValues = t->newRefValues;
    
    t->untraced = AValueToPtr(temp[2]);
    t->untracedPtr = t->untraced->data.val;
    t->untracedEnd = t->untracedPtr + A_GC_LIST_BLOCK_LENGTH;

    /* Add a sentinel 0 value to mark the top of the stack. */
    t->stackPtr = t->stackTop - 1;
    *t->stackPtr = AZero;

    t->markStackBottom = NULL;

    t->exception = AZero;
    t->uncaughtExceptionStackPtr = NULL;
    t->isExceptionReraised = FALSE;

    for (i = 0; i < 2 * A_NUM_CACHED_REGEXPS; i++)
        t->regExp[i] = AZero;

    ALockThreads();
    t->next = Threads;
    Threads = t;
    AReleaseThreads();

    return t;
}


ABool AAllocTempStack(AThread *t, int n)
{
    return AAllocTempStack_M(t, n);
}


/* Return the first thread in the global list of live threads. */
AThread *AGetFirstThread(void)
{
    return Threads;
}


/* Return the next thread in the global list of threads, or NULL if at the
   last thread. */
AThread *AGetNextThread(AThread *t)
{
    return t->next;
}


/* Allocate a block that can be used as an untraced reference or new
   generation reference list block. */
static AGCListBlock *AllocateGCListBlock(void)
{
    AGCListBlock *block;

    block = AAllocUnmovable(sizeof(AGCListBlock));
    if (block != NULL) {
        AInitNonPointerBlockOld(&block->header,
                               sizeof(AGCListBlock) - sizeof(AValue));
        block->next = NULL;
    }

    return block;
}


/* Advance to the next block of untraced references within a thread. If there
   is no block available, allocate a new one. */
ABool AAdvanceUntracedList(AThread *t)
{
    AGCListBlock *untraced;

    if (AGetGCListBlock(t->untracedEnd)->next == NULL) {
        untraced = AllocateGCListBlock();
        if (untraced == NULL)
            return FALSE;
    } else
        untraced = AGetGCListBlock(t->untracedEnd)->next;

    if (t->untracedPtr == t->untracedEnd) {
        AGetGCListBlock(t->untracedEnd)->size = A_GC_LIST_BLOCK_LENGTH;
        AGetGCListBlock(t->untracedEnd)->next = untraced;
        
        t->untracedPtr = untraced->data.val;
        t->untracedEnd = untraced->data.val + A_GC_LIST_BLOCK_LENGTH;
    }

    return TRUE;
}


/* Advance to the next block of new generation references within a thread. If
   there is no block available, allocate a new one. */
ABool AAdvanceNewRefList(AThread *t)
{
    AGCListBlock *newRef;
    AGCListBlock *newRefValues;

    if (t->curNewRefValues->next == NULL) {
        newRef = AllocateGCListBlock();
        t->tempStack[2] = AStrToValue(newRef);
        newRefValues = AllocateGCListBlock();
        t->tempStack[2] = AZero;
        if (newRef == NULL || newRefValues == NULL)
            return FALSE;
    } else {
        newRef = AGetGCListBlock(t->newRefEnd)->next;
        newRefValues = t->curNewRefValues->next;
    }

    if (t->newRefPtr == t->newRefEnd) {
        AGetGCListBlock(t->newRefEnd)->size = A_GC_LIST_BLOCK_LENGTH;
        AGetGCListBlock(t->newRefEnd)->next = newRef;
        t->curNewRefValues->size = A_GC_LIST_BLOCK_LENGTH;
        t->curNewRefValues->next = newRefValues;

        t->curNewRefValues = newRefValues;
        t->newRefPtr = newRef->data.valPtr;
        t->newRefEnd = newRef->data.valPtr + A_GC_LIST_BLOCK_LENGTH;
    }
    
    return TRUE;        
}
