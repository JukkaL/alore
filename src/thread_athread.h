/* thread_athread.h - thread module (implementation using athread API)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef THREAD_ATHREAD_H_INCL
#define THREAD_ATHREAD_H_INCL

#include "common.h"


#if defined(A_HAVE_ATHREAD)
#include "athread.h"
#else
#error No athread implementation
#endif


A_APIVAR extern athread_mutex_t AHeapMutex;

/* Various mutexes used by the runtime to synchronize access to global data
   structures. */
extern athread_mutex_t AThreadMutex;
extern athread_mutex_t AHashMutex;
extern athread_mutex_t AStreamMutex;
extern athread_mutex_t AFinalizerMutex;

extern volatile AAtomicInt AIsFreeze;
A_APIVAR extern volatile AAtomicInt AIsInterrupt;
extern volatile AAtomicInt AIsKeyboardInterrupt;


ABool AInitializeThreads(void);


A_APIFUNC void AWaitForHeap(void);

#define ALockHeap() \
    do {                                                \
        if (athread_mutex_trylock(&AHeapMutex) == EBUSY) \
            AWaitForHeap();                              \
    } while (0)

#define AReleaseHeap() \
    (void)athread_mutex_unlock(&AHeapMutex)


#define ALockThreads() \
    (void)athread_mutex_lock(&AThreadMutex)

#define AReleaseThreads() \
    (void)athread_mutex_unlock(&AThreadMutex)


void AWaitForHash(void);

#define ALockHashMappings() \
    do {                                                \
        if (athread_mutex_trylock(&AHashMutex) == EBUSY) \
            AWaitForHash();                              \
    } while (0)

#define AUnlockHashMappings() \
    (void)athread_mutex_unlock(&AHashMutex)


void AWaitForStreams(void);

#define ALockStreams() \
    do {                                                  \
        if (athread_mutex_trylock(&AStreamMutex) == EBUSY) \
            AWaitForStreams();                             \
    } while (0)

#define AUnlockStreams() \
    (void)athread_mutex_unlock(&AStreamMutex)


#define A_MUTEX_DATA_SIZE 0
#define A_CONDITION_DATA_SIZE 0


/* Member indices of Thread object member variables. */
enum {
    A_THREAD_FINALIZER_LIST, /* Pointer to the next object with finalizer in
                              the global finalizable object list */
    A_THREAD_STATE,    /* State of the thread (0 = ?, 1 = thread returned a
                        value, 2 = thread raised an exception, 3 = thread
                        did not return a value) */
    A_THREAD_RET_VAL,  /* Return value or raised exception */
    A_THREAD_DATA,     /* Pointer to a non-pointer block that contains a
                        thread-specific mutex and a condition variable */
    A_NUM_THREAD_MEMBER_VARS
};


#endif
