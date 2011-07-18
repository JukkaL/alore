/* athread_pthread.c - pthread implementation of the athread API

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "athread.h"

#if defined(A_HAVE_THREADS) && defined(HAVE_PTHREADS)

int athread_init(void)
{
    return 0;
}


int athread_create(athread_t *ptr, void *(*func)(void *), void *param)
{
    pthread_attr_t attr;
    int result;
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    result = pthread_create(ptr, &attr, func, param);
    pthread_attr_destroy(&attr);

    return result;
}

#endif
