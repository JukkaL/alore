/* athread.h - athread portable thread API

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ATHREAD_H_INCL
#define ATHREAD_H_INCL

#include "aconfig.h"


#if defined(HAVE_PTHREADS) && defined(A_HAVE_THREADS)

/* pthread implementation of the API */

#include <pthread.h>
#include <errno.h>

#define athread_t pthread_t

int athread_init(void);

int athread_create(athread_t *ptr, void *(*func)(void *), void *param);

#define athread_mutex_t pthread_mutex_t
#define athread_mutex_init pthread_mutex_init
#define athread_mutex_trylock pthread_mutex_trylock
#define athread_mutex_lock pthread_mutex_lock
#define athread_mutex_unlock pthread_mutex_unlock
#define athread_mutex_destroy pthread_mutex_destroy

#define athread_cond_t pthread_cond_t
#define athread_cond_init pthread_cond_init
#define athread_cond_wait pthread_cond_wait
#define athread_cond_signal pthread_cond_signal
#define athread_cond_broadcast pthread_cond_broadcast
#define athread_cond_destroy pthread_cond_destroy

#elif defined(A_HAVE_WINDOWS) && defined(A_HAVE_THREADS)

/* Windows implementation of the API */

/* We need EBUSY */
#include <errno.h>


typedef struct {
    void *id;
} athread_t;

typedef struct athread_mutex_t_ *athread_mutex_t;

typedef struct athread_local_t_ athread_local_t;

typedef struct {
    athread_mutex_t mutex;
     /* This represents the list of threads waiting for this condition
        variable. Each item in the list is the thread-local item of a waiting
        thread. This way condition waiting does not need to allocate any
        resources. */
    athread_local_t *events;
} athread_cond_t;


int athread_init(void);

int athread_create(athread_t *ptr, void *(*func)(void *), void *param);

A_APIFUNC int athread_mutex_init(athread_mutex_t *mutex, void *arg);
A_APIFUNC int athread_mutex_trylock(athread_mutex_t *mutex);
A_APIFUNC void athread_mutex_lock(athread_mutex_t *mutex);
A_APIFUNC void athread_mutex_unlock(athread_mutex_t *mutex);
A_APIFUNC void athread_mutex_destroy(athread_mutex_t *mutex);

int athread_cond_init(athread_cond_t *cond, void *arg);
void athread_cond_wait(athread_cond_t *cond, athread_mutex_t *mutex);
void athread_cond_signal(athread_cond_t *cond);
void athread_cond_broadcast(athread_cond_t *cond);
void athread_cond_destroy(athread_cond_t *cond);

#else

/* No thread support available - define an empty implementation of the API */

/* We need EBUSY */
#include <errno.h>


typedef void *athread_t;

typedef void *athread_mutex_t;

typedef void *athread_cond_t;

#define athread_init() 0

/* Thread creation always fails. */
#define athread_create(ptr, func, param) (-1)

#define athread_mutex_init(mutext, arg) 0
#define athread_mutex_trylock(mutex) 0
#define athread_mutex_lock(mutex) 0
#define athread_mutex_unlock(mutex) 0
#define athread_mutex_destroy(mutex) 0

#define athread_cond_init(cond, arg) 0
/* Since we only have a single thread, waiting on a condition blocks
   indefinitely.
   IDEA: Somehow signal an error? This is not very productive... */
#define athread_cond_wait(cond, mutex) do { for (;;); } while (0)
#define athread_cond_signal(cond) 0
#define athread_cond_broadcast(cond) 0
#define athread_cond_destroy(cond) 0

#endif


#endif
