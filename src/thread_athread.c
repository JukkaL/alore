/* thread_athread.c - thread module (implementation using athread API)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Thread module implementation using the cross-platform athread API (similar
   to pthread API). */

#include "alore.h"
#include "runtime.h"
#include "thread_module.h"
#include "thread_athread.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"
#include "debug_runtime.h"

#include <signal.h>
#include <errno.h>

/* FIX: Implement proper cleanup of mutexes etc. */


athread_mutex_t AHeapMutex;
athread_mutex_t AThreadMutex;
athread_mutex_t AHashMutex;
athread_mutex_t AStreamMutex;
athread_mutex_t AFinalizerMutex;
athread_mutex_t AInterpreterMutex; /* Protects the symbol table */

static athread_cond_t AllFrozenCond;
static athread_cond_t WakeUpCond;


volatile AAtomicInt AIsFreeze;
volatile AAtomicInt AIsInterrupt;
volatile AAtomicInt AIsKeyboardInterrupt;


static int NumFreezableThreads;


static int NumWaitingThreads;

static int ArgBufFirst;
static int ArgBufLast;

static athread_cond_t ArgBufRemoveCond;
static athread_cond_t ArgBufInsertCond;


/* Number of consecutive FreezeOtherThreads() calls. WakeOtherThreads() wakes
   threads only if FreezeDepth == 1, otherwise it decrements it. */
static int FreezeDepth;


/* Return the next index in the circular ThreadArgBuffer. */
#define NextInd(ind) (((ind) + 1) & (A_THREAD_ARG_BUFFER_SIZE - 1))


/* Size of the data in thread data block (not including the block header) */
#define THREAD_DATA_SIZE (sizeof(athread_mutex_t) + sizeof(athread_cond_t))

/* Return pointer to the thread mutex when given the pointer to the thread data
   block. */
#define GetThreadMutex(block) \
    ((athread_mutex_t *)APtrAdd(block, sizeof(AValue)))

/* Return pointer to the thread condition variable when given the pointer to
   the thread data block. */
#define GetThreadCond(block) \
    ((athread_cond_t *)APtrAdd(block, sizeof(AValue) + \
     sizeof(athread_mutex_t)))


/* Return a pointer to the Mutex object data area when given a Mutex value. */
#define GetMutexData(val) \
    APtrAdd(AValueToPtr(AValueToInstance(val)->member[1]), sizeof(AValue))

/* Return a pointer to the Condition object data area when given a Condition
   value. */
#define GetConditionData(val) \
    APtrAdd(AValueToPtr(AValueToInstance(val)->member[1]), sizeof(AValue))


static void *BeginNewThread(void *voidThread);
static void AllowFreezeNoLock(void);
static void DisallowFreezeNoLock(void);
static void PerformFreeze(void);


ABool AInitializeThreads(void)
{
    if (athread_init())
        return FALSE;

    if (athread_mutex_init(&AHeapMutex, NULL)
        || athread_mutex_init(&AThreadMutex, NULL)
        || athread_mutex_init(&AStreamMutex, NULL)
        || athread_mutex_init(&AHashMutex, NULL)
        || athread_mutex_init(&AFinalizerMutex, NULL)
        || athread_mutex_init(&AInterpreterMutex, NULL)
        || athread_cond_init(&AllFrozenCond, NULL)
        || athread_cond_init(&WakeUpCond, NULL)
        || athread_cond_init(&ArgBufRemoveCond, NULL)
        || athread_cond_init(&ArgBufInsertCond, NULL))
        return FALSE;

    ArgBufFirst = 0;
    ArgBufLast = 0;

    AIsInterrupt = FALSE;
    AIsFreeze = FALSE;
    AIsKeyboardInterrupt = FALSE;

    NumWaitingThreads = 0;
    NumFreezableThreads = 0;

    FreezeDepth = 0;

    return TRUE;
}


AValue AThreadCreate(AThread *t, AValue *frame)
{
    AThread *newThread;
    athread_t threadId;
    ABool isWaiting;
    AValue *threadData;

    /* Call the thread function always with no arguments. */
    frame[2] = AMakeArray(t, 0);

    threadData = AAllocUnmovable(sizeof(AValue) + THREAD_DATA_SIZE);
    if (threadData == NULL)
        return ARaiseMemoryErrorND(t);

    AInitNonPointerBlockOld(threadData, THREAD_DATA_SIZE);

    *t->tempStack = ANonPointerBlockToValue(threadData);
    ASetMemberDirect(t, frame[0], A_THREAD_DATA, *t->tempStack);
    threadData = AValueToPtr(*t->tempStack);

    if (athread_mutex_init(GetThreadMutex(threadData), NULL)
        || athread_cond_init(GetThreadCond(threadData), NULL))
        return ARaiseMemoryErrorND(t);

    athread_mutex_lock(&AThreadMutex);

    if (NumWaitingThreads > 0) {
        NumWaitingThreads--;
        isWaiting = TRUE;
        AAvoidWarning_M(newThread = NULL);
    } else {
        athread_mutex_unlock(&AThreadMutex);

        newThread = ACreateThread(frame + 3);
        if (newThread == NULL)
            return ARaiseMemoryErrorND(t);

        isWaiting = FALSE;

        athread_mutex_lock(&AThreadMutex);
    }

    /* Wait until the argument buffer is not full. */
    while (ArgBufFirst == NextInd(ArgBufLast)) {
        AllowFreezeNoLock();
        athread_cond_wait(&ArgBufRemoveCond, &AThreadMutex);
        DisallowFreezeNoLock();
    }

    /* Store thread object, function to be executed and arguments. */
    AThreadArgBuffer[3 * ArgBufLast    ] = frame[0];
    AThreadArgBuffer[3 * ArgBufLast + 1] = frame[1];
    AThreadArgBuffer[3 * ArgBufLast + 2] = frame[2];

    ArgBufLast = NextInd(ArgBufLast);

    athread_mutex_unlock(&AThreadMutex);

    if (isWaiting)
        athread_cond_signal(&ArgBufInsertCond);
    else {
        int result = athread_create(&threadId, BeginNewThread, newThread);
        if (result)
            return ARaiseMemoryErrorND(t); /* FIX: bad */

        /* Perhaps store new thread id somewhere. */
    }

    return frame[0];
}


/* Free the mutex and the condition variable associated with the t. */
AValue AThreadFinalize(AThread *t, AValue *frame)
{
    AValue dataVal;

    dataVal = AValueToInstance(frame[0])->member[A_THREAD_DATA];
    if (!AIsNil(dataVal)) {
        AValue *threadData = AValueToPtr(dataVal);
        athread_mutex_destroy(GetThreadMutex(threadData));
        athread_cond_destroy(GetThreadCond(threadData));
    }

    return AZero;
}


static void *BeginNewThread(void *voidThread)
{
    AValue *stack;
    AThread *t;
    AInstance *inst;
    void *threadData;
    athread_mutex_t *mutex;
    athread_cond_t *cond;

    /* NOTE: This code is fragile! Be careful when doing modifications because
       trivial changes in ordering of statements etc. may cause rare
       synchronization problems, especially with garbage collection. */

    t = voidThread;

    athread_mutex_lock(&AThreadMutex);

    ADebugStatusMsg(("New os thread (%ld)\n", (long)t));

    /* Each physical thread loops forever and waits for new arguments that
       signal the creation of a new Alore (logical) thread. */
    for (;;) {
        ABool broadcast;

        /* FIX: perhaps gotta wake up something at some place.. no? */

        /* Wait until we are given some arguments that indicate that we should
           start. The current thread is not active at this time, so NumThreads
           doesn't include it. Therefore don't call AAllowBlocking --
           essentially the thread is always frozen. */
        while (ArgBufFirst == ArgBufLast)
            athread_cond_wait(&ArgBufInsertCond, &AThreadMutex);

        /* From this point on, this thread is active. Keep track of the number
           of active threads. */
        ANumThreads++;

        /* Initially all the threads could be frozen. Make sure that the
           thread is not frozen after we start the execution of the thread,
           since otherwise the garbage collector might be active. */
        NumFreezableThreads++;
        DisallowFreezeNoLock(); /* This decrements NumFreezableThreads. */

        /* If another thread may have read the arguments meanwhile, back off
           and try again. */
        if (ArgBufFirst == ArgBufLast) {
            ANumThreads--;
            continue;
        }

        ADebugStatusMsg(("Alore thread started (%ld)\n", (long)t));

        /* Construct a dummy stack frame at the bottom of the thread stack that
           stores information on the thread. */
        stack = t->stackPtr - 7;
        /* The stack frame header is special, because it doesn't have a related
           Alore function. */
        stack[0] = 7 * sizeof(AValue); /* IDEA: Use a macro? */
        stack[1] = A_THREAD_BOTTOM_FUNCTION;
        stack[2] = A_COMPILED_FRAME_FLAG; /* FIX: Is this ok? */
        /* Read the arguments given during Thread object creation and store
           them for future reference. */
        stack[3] = AThreadArgBuffer[3 * ArgBufFirst + 0];
        stack[4] = AThreadArgBuffer[3 * ArgBufFirst + 1];
        stack[5] = AThreadArgBuffer[3 * ArgBufFirst + 2];
        stack[6] = AZero;
        t->stackPtr = stack;

        /* Clear the arguments after they have been read to allow them to be
           reclaimed by the garbage collector. */
        AThreadArgBuffer[3 * ArgBufFirst + 0] = AZero;
        AThreadArgBuffer[3 * ArgBufFirst + 1] = AZero;
        AThreadArgBuffer[3 * ArgBufFirst + 2] = AZero;

        /* Somebody might be waiting for a free slot in the argument buffer
           in order to create a new thread. Wake him up! */
        if (ArgBufFirst == NextInd(ArgBufLast))
            athread_cond_signal(&ArgBufRemoveCond);

        /* Mark a slot in the argument buffer as free. */
        ArgBufFirst = NextInd(ArgBufFirst);

        athread_mutex_unlock(&AThreadMutex);

        /* The thread has been fully initialized. Now start the execution by
           calling the function given as an argument to the thread. Also catch
           any direct exceptions. */
        if (AHandleException(t))
            stack[4] = AError;
        else
            stack[4] = ACallValue(t, stack[4], 1 | A_VAR_ARG_FLAG,
                                    stack + 5);
        t->contextIndex--;

        inst = AValueToInstance(stack[3]);
        threadData = AValueToPtr(inst->member[A_THREAD_DATA]);
        mutex = GetThreadMutex(threadData);
        cond = GetThreadCond(threadData);

        athread_mutex_lock(mutex);

        broadcast = inst->member[A_THREAD_STATE] == AZero;

        inst->member[A_THREAD_STATE] = AIntToValue(1);

        if (AIsError(stack[4])) {
            inst->member[A_THREAD_STATE] = AIntToValue(2);
            stack[4] = t->exception;
            ACreateTracebackArray(t); /* FIX: is this ok..? */
            inst = AValueToInstance(stack[3]);
        }

        if (!ASetInstanceMember(t, inst, A_THREAD_RET_VAL, stack + 4)) {
            inst = AValueToInstance(stack[3]);
            inst->member[A_THREAD_STATE] = AIntToValue(2);
            inst->member[A_THREAD_RET_VAL] = 0 /*MemoryErrorInstance*/;
            /* FIX!! */
        }

        t->stackPtr = stack + 7;

        athread_mutex_lock(&AThreadMutex);

        NumWaitingThreads++;
        ANumThreads--;

        if (AIsFreeze && NumFreezableThreads == ANumThreads - 1)
            athread_cond_signal(&AllFrozenCond);

        ADebugStatusMsg(("Alore thread ended   (%ld)\n", (long)t));

        athread_mutex_unlock(mutex);

        if (broadcast)
            athread_cond_broadcast(cond);
    }

    /* Never reached */

    /* Return a dummy NULL to get rid of a potential compiler warning. */
    return NULL;
}


void AWaitForHeap(void)
{
    AAllowBlocking();
    athread_mutex_lock(&AHeapMutex);
    AEndBlocking();
}


void AWaitForHash(void)
{
    AAllowBlocking();
    athread_mutex_lock(&AHashMutex);
    AEndBlocking();
}


void AWaitForStreams(void)
{
    AAllowBlocking();
    athread_mutex_lock(&AStreamMutex);
    AEndBlocking();
}


/* Request all the other threads than the current thread to stop execution
   until the current thread calls AWakeOtherThreads. The other threads will
   have to cooperate, i.e. they are never forcibly suspended. This function
   returns only after the current thread is the only executing thread. */
void AFreezeOtherThreads(void)
{
    athread_mutex_lock(&AThreadMutex);

    if (FreezeDepth == 0) {
        ADebugStatusMsg(("Freezing other threads\n"));
        PerformFreeze();
        ADebugStatusMsg(("Froze other threads\n"));
    }

    FreezeDepth++;

    athread_mutex_unlock(&AThreadMutex);
}


/* Like AFreeOtherThreads, but do not generate a status message in debug
   mode. */
void AFreezeOtherThreadsSilently(void)
{
    athread_mutex_lock(&AThreadMutex);

    if (FreezeDepth == 0)
        PerformFreeze();

    FreezeDepth++;

    athread_mutex_unlock(&AThreadMutex);
}


/* Pre- and postcondition: AThreadMutex locked. */
static void PerformFreeze(void)
{
    /* Wait until nobody else wants to freeze others. */
    while (AIsFreeze) {
        athread_mutex_unlock(&AThreadMutex);
        AAllowBlocking();
        /* This call waits until IsFreeze is cleared. */
        AEndBlocking();
        athread_mutex_lock(&AThreadMutex);
    }

    AIsInterrupt = TRUE;
    AIsFreeze = TRUE;

    /* Wait until everybody can be frozen. */
    while (NumFreezableThreads < ANumThreads - 1)
        athread_cond_wait(&AllFrozenCond, &AThreadMutex);
}


/* Continue the execution of all other threads than the current thread. You
   must have called AFreezeOtherThreads before calling this function. */
void AWakeOtherThreads(void)
{
    athread_mutex_lock(&AThreadMutex);

    FreezeDepth--;
    if (FreezeDepth == 0) {
        ADebugStatusMsg(("Waking other threads\n"));

        AIsInterrupt = AIsKeyboardInterrupt;
        AIsFreeze = FALSE;

        athread_mutex_unlock(&AThreadMutex);
        athread_cond_broadcast(&WakeUpCond);
    } else
        athread_mutex_unlock(&AThreadMutex);
}


/* Like AWakeOtherThreads, but do not generate a status message in debug
   mode. */
void AWakeOtherThreadsSilently(void)
{
    athread_mutex_lock(&AThreadMutex);

    FreezeDepth--;
    if (FreezeDepth == 0) {
        AIsInterrupt = AIsKeyboardInterrupt;
        AIsFreeze = FALSE;

        athread_mutex_unlock(&AThreadMutex);
        athread_cond_broadcast(&WakeUpCond);
    } else
        athread_mutex_unlock(&AThreadMutex);
}


void AAllowBlocking(void)
{
    athread_mutex_lock(&AThreadMutex);

    /* Mark thread as freezable. */
    NumFreezableThreads++;

    if (AIsFreeze && NumFreezableThreads == ANumThreads - 1)
        athread_cond_signal(&AllFrozenCond);

    athread_mutex_unlock(&AThreadMutex);
}


void AllowFreezeNoLock(void)
{
    /* Mark thread as freezable. */
    NumFreezableThreads++;

    if (AIsFreeze && NumFreezableThreads == ANumThreads - 1)
        athread_cond_signal(&AllFrozenCond);
}


void AEndBlocking(void)
{
    athread_mutex_lock(&AThreadMutex);

    /* Check if gotta freeze. */
    while (AIsFreeze)
        athread_cond_wait(&WakeUpCond, &AThreadMutex);

    /* Mark thread as not freezable. */
    NumFreezableThreads--;

    athread_mutex_unlock(&AThreadMutex);
}


void DisallowFreezeNoLock(void)
{
    /* Check if gotta freeze. */
    while (AIsFreeze)
        athread_cond_wait(&WakeUpCond, &AThreadMutex);

    /* Mark thread as not freezable. */
    NumFreezableThreads--;
}


AValue AMutexCreate(AThread *t, AValue *frame)
{
    AValue *mutex;

    mutex = AAllocUnmovable(sizeof(AValue) + sizeof(athread_mutex_t));
    if (mutex == NULL)
        return ARaiseMemoryErrorND(t);

    AInitNonPointerBlock(mutex, sizeof(athread_mutex_t));

    *t->tempStack = AStrToValue(mutex);
    ASetMemberDirect(t, frame[0], 1, *t->tempStack);

    if (athread_mutex_init(APtrAdd(AValueToPtr(*t->tempStack), sizeof(AValue)),
                           NULL))
        return ARaiseMemoryErrorND(t);

    return frame[0];
}


AValue AMutexFinalize(AThread *t, AValue *frame)
{
    if (AValueToInstance(frame[0])->member[1] != ANil)
        athread_mutex_destroy(GetMutexData(frame[0]));
    return AZero;
}


AValue AConditionCreate(AThread *t, AValue *frame)
{
    AValue *cond;

    cond = AAllocUnmovable(sizeof(AValue) + sizeof(athread_cond_t));
    if (cond == NULL)
        return ARaiseMemoryErrorND(t);

    AInitNonPointerBlock(cond, sizeof(athread_cond_t));

    *t->tempStack = AStrToValue(cond);
    ASetMemberDirect(t, frame[0], 1, *t->tempStack);

    if (athread_cond_init(APtrAdd(AValueToPtr(*t->tempStack), sizeof(AValue)),
                          NULL))
        return ARaiseMemoryErrorND(t);

    return frame[0];
}


AValue AConditionFinalize(AThread *t, AValue *frame)
{
    if (AValueToInstance(frame[0])->member[1] != ANil)
        athread_cond_destroy(GetConditionData(frame[0]));
    return AZero;
}


AValue AThreadJoin(AThread *t, AValue *frame)
{
    AInstance *inst;
    void *threadData;
    athread_mutex_t *mutex;
    athread_cond_t *cond;
    ABool isLocked;

    isLocked = FALSE;

    /* Wait until the thread has finished execution. */
    for (;;) {
        inst = AValueToInstance(frame[0]);
        if (inst->member[A_THREAD_DATA] == ANil)
            return ARaiseValueErrorND(t, NULL); /* FIX: msg? */

        threadData = AValueToPtr(inst->member[A_THREAD_DATA]);
        mutex = GetThreadMutex(threadData);
        cond = GetThreadCond(threadData);

        if (!isLocked) {
            athread_mutex_lock(mutex);
            isLocked = TRUE;
        }

        if (inst->member[A_THREAD_STATE] != ANil
            && inst->member[A_THREAD_STATE] != AZero)
            break;

        inst->member[A_THREAD_STATE] = AZero;
        AAllowBlocking();
        athread_cond_wait(cond, mutex);
        AEndBlocking();
    }

    athread_mutex_unlock(mutex);

    /* If there was an exception, raise it. Otherwise, return the thread
       return value or no return value. */
    if (inst->member[A_THREAD_STATE] == AIntToValue(2)) {
        t->exception = inst->member[A_THREAD_RET_VAL];
        t->uncaughtExceptionStackPtr = t->stackPtr;
        t->isExceptionReraised = FALSE;
        return AError;
    } else
        return inst->member[A_THREAD_RET_VAL];
}


AValue AThreadStop(AThread *t, AValue *frame)
{
    /* We should somehow raise an exception in the t.. */
    return ANil;
}


AValue AMutexLock(AThread *t, AValue *frame)
{
    if (athread_mutex_trylock(GetMutexData(frame[0])) == EBUSY) {
        AAllowBlocking();
        athread_mutex_lock(GetMutexData(frame[0]));
        AEndBlocking();
    }

    return ANil;
}


AValue AMutexUnlock(AThread *t, AValue *frame)
{
    athread_mutex_unlock(GetMutexData(frame[0]));
    return ANil;
}


AValue AConditionWait(AThread *t, AValue *frame)
{
    if (AIsOfType(frame[1], AGlobalByNum(AThreadMutexNum)) != A_IS_TRUE)
        return ARaiseTypeErrorND(t, NULL);

    AAllowBlocking();
    athread_cond_wait(GetConditionData(frame[0]),
                      GetMutexData(frame[1]));
    AEndBlocking();

    return ANil;
}


AValue AConditionSignal(AThread *t, AValue *frame)
{
    athread_cond_signal(GetConditionData(frame[0]));
    return ANil;
}


AValue AConditionBroadcast(AThread *t, AValue *frame)
{
    athread_cond_broadcast(GetConditionData(frame[0]));
    return ANil;
}


ABool AHandleInterrupt(AThread *t)
{
    ABool retVal;

    athread_mutex_lock(&AThreadMutex);

    if (AIsFreeze) {
        /* IDEA: Use AllowFreezeNoLock / DisallowFreezeNoLock. */
        NumFreezableThreads++;

        if (NumFreezableThreads == ANumThreads - 1)
            athread_cond_signal(&AllFrozenCond);

        while (AIsFreeze)
            athread_cond_wait(&WakeUpCond, &AThreadMutex);

        NumFreezableThreads--;
    }

    athread_mutex_unlock(&AThreadMutex);

    /* Check if we should raise an InterruptException. It is only raised in the
       main thread and only once per ctrl+c press. */
    if (t == AMainThread && AIsKeyboardInterrupt) {
        ARaiseByNum(t, AErrorClassNum[EX_INTERRUPT_EXCEPTION],
                    AMsgKeyboardInterrupt);
        AIsKeyboardInterrupt = FALSE;
        retVal = TRUE;
    } else
        retVal = FALSE;

    return retVal;
}


/* Lock mutable interpreter data structures (currently only the symbol
   table). Accessing a known SymbolInfo structure is allowed without locking,
   since the structures are mostly immutable while not compiling, and during
   compilation only a single thread can be active (thus no locking
   required). Note that the next member of SymbolInfo is an exception and it
   must not be accessed without locking, since it can be changed at any
   time! */
void ALockInterpreter(void)
{
    if (athread_mutex_trylock(&AInterpreterMutex) == EBUSY) {
        AAllowBlocking();
        athread_mutex_lock(&AInterpreterMutex);
        AEndBlocking();
    }
}


void AUnlockInterpreter(void)
{
    athread_mutex_unlock(&AInterpreterMutex);
}
