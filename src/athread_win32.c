/* athread_win32.c - Windows API implementation of the athread API

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "athread.h"

#if defined(A_HAVE_THREADS) && defined(A_HAVE_WINDOWS)

/* MinGW-specific definitions */
#ifndef _MSC_VER
#include <stdint.h>
#endif

#include <windows.h>
#include <process.h>
#include <string.h>


struct athread_local_t_ {
    HANDLE event;
    struct athread_local_t_ *next;
};


struct athread_mutex_t_ {
    LONG state;
    HANDLE event;
};


struct athread_win32_events_t_ {
    HANDLE event;
    struct athread_win32_events_t_ *next;
};


typedef struct {
    void *(*func)(void *);
    void *param;
    athread_local_t *data;
} athread_args_t;


static void __cdecl thread_func(void *ptr);
static athread_local_t *alloc_thread_local_data(void);

static int athread_tls_index;


/* Initialize the athread module. */
int athread_init(void)
{
    athread_tls_index = TlsAlloc();
    if (athread_tls_index == TLS_OUT_OF_INDEXES)
        return -1;

    athread_local_t *data = alloc_thread_local_data();
    if (data == NULL)
        return -1;

    TlsSetValue(athread_tls_index, data);

    return 0;
}


/* Create a thread. */
int athread_create(athread_t *ptr, void *(*func)(void *), void *param)
{
    athread_args_t *args = malloc(sizeof(*args));
    if (args == NULL)
        return -1;

    args->func = func;
    args->param = param;
    args->data = alloc_thread_local_data();

    if (args->data == NULL) {
        free(args);
        return -1;
    }

    ptr->id = (void *)_beginthread(thread_func, 0, args);
    if ((uintptr_t)ptr->id == (uintptr_t)-1L) {
        free(args->data);
        free(args);
        return -1;
    }

    return 0;
}


/* Initialize a mutex. */
int athread_mutex_init(athread_mutex_t *mutex, void *arg)
{
    *mutex = malloc(sizeof(**mutex));
    if (*mutex == NULL)
        return -1;

    (*mutex)->state = 0;
    /* Create event (no manual reset needed, initially not signaled). */
    (*mutex)->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if ((*mutex)->event == 0) {
        free(*mutex);
        return -1;
    }

    return 0;
}


/* Lock a mutex. */
void athread_mutex_lock(athread_mutex_t *mutex)
{
    if (InterlockedExchange(&(*mutex)->state, 1) != 0) {
        while (InterlockedExchange(&(*mutex)->state, -1) != 0)
            WaitForSingleObject((*mutex)->event, INFINITE);
    }
}


/* Unlock a mutex. */
void athread_mutex_unlock(athread_mutex_t *mutex)
{
    LONG state = InterlockedExchange(&(*mutex)->state, 0);
    if (state < 0) {
        /* Signal any waiters. */
        SetEvent((*mutex)->event);
    }
}


/* Lock a mutex and return 0 if it is unlocked; otherwise return EBUSY. */
int athread_mutex_trylock(athread_mutex_t *mutex)
{
    if (InterlockedCompareExchange(&(*mutex)->state, 1, 0) == 0)
        return 0;
    else
        return EBUSY;
}


/* Free a mutex. */
void athread_mutex_destroy(athread_mutex_t *mutex)
{
    CloseHandle((*mutex)->event);
    free(*mutex);
}


/* Initialize a condition variable. */
int athread_cond_init(athread_cond_t *cond, void *arg)
{
    if (athread_mutex_init(&cond->mutex, NULL) < 0)
        return -1;
    cond->events = NULL;
    return 0;
}


/* Unlock mutex and wait the condition variable. The mutex must be locked on
   entrance. Lock mutex again before returning. */
void athread_cond_wait(athread_cond_t *cond, athread_mutex_t *mutex)
{
    athread_local_t *data;

    athread_mutex_lock(&cond->mutex);

    /* Add the pre-allocated event specific to this thread to the events list
       of the condition variable. The value returned by TlsGetValue is local
       to this thread, and no other thread may access it. Therefore no
       synchronization is needed below. */
    data = TlsGetValue(athread_tls_index);
    data->next = cond->events;
    cond->events = data;

    athread_mutex_unlock(&cond->mutex);
    athread_mutex_unlock(mutex);

    /* Wait for a signal or broadcast event. */
    WaitForSingleObject(data->event, INFINITE);

    athread_mutex_lock(mutex);
}


/* Signal a condition variable. */
void athread_cond_signal(athread_cond_t *cond)
{
    athread_mutex_lock(&cond->mutex);

    /* Set and remove the first event in the list. */
    if (cond->events != NULL) {
        athread_local_t *prev = cond->events;
        cond->events = prev->next;
        prev->next = NULL;
        SetEvent(prev->event);
    }

    athread_mutex_unlock(&cond->mutex);
}


/* Broadcast a condition variable. */
void athread_cond_broadcast(athread_cond_t *cond)
{
    athread_mutex_lock(&cond->mutex);

    /* Set all the events in the list. */
    while (cond->events != NULL) {
        athread_local_t *prev = cond->events;
        cond->events = prev->next;
        prev->next = NULL;
        SetEvent(prev->event);
    }

    athread_mutex_unlock(&cond->mutex);
}


/* Free a condition variable. */
void athread_cond_destroy(athread_cond_t *cond)
{
    /* The event list must be empty when a condition variable is destoyed.
       Only the mutex need to be freed. */
    athread_mutex_destroy(&cond->mutex);
}


/* This function is called within each newly created thread. */
static void __cdecl thread_func(void *ptr)
{
    athread_args_t args;

    /* Copy the received thread arguments and free the original pointer. */
    memcpy(&args, ptr, sizeof(args));
    free(ptr);

    /* Store the thread-local event data structure. */
    TlsSetValue(athread_tls_index, args.data);

    /* Call the function to perform this thread. */
    args.func(args.param);

    /* Free the event and thread-local data. */
    CloseHandle(args.data->event);
    free(args.data);
}


static athread_local_t *alloc_thread_local_data(void)
{
    athread_local_t *data = malloc(sizeof(athread_local_t));
    if (data == NULL)
        return NULL;

    data->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (data->event == 0) {
        free(data);
        return NULL;
    }
    data->next = NULL;

    return data;
}

#endif
