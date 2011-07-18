/* time_module.c - __time module (used by time)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"

#include <time.h>
#include <stdio.h>

#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#endif


static AValue TimeNow(AThread *t, AValue *frame)
{
    int usecs;
    time_t t1;
    struct tm t2;
    
    frame[0] = AMakeArray(t, 7);
#ifdef HAVE_GETTIMEOFDAY
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) < 0)
            return ARaiseByNum(t, AErrorClassNum[EX_RESOURCE_ERROR], NULL);
        t1 = tv.tv_sec;
        usecs = tv.tv_usec;
    }
#else
    {
        t1 = time(NULL);
        usecs = 0;
    }
#endif

#ifdef A_HAVE_POSIX
    localtime_r(&t1, &t2); /* FIX: What if returns NULL? */
#else
    t2 = *localtime(&t1);
#endif
    
    ASetArrayItem(t, frame[0], 0, AIntToValue(t2.tm_year + 1900));
    ASetArrayItem(t, frame[0], 1, AIntToValue(t2.tm_mon + 1));
    ASetArrayItem(t, frame[0], 2, AIntToValue(t2.tm_mday));
    ASetArrayItem(t, frame[0], 3, AIntToValue(t2.tm_hour));
    ASetArrayItem(t, frame[0], 4, AIntToValue(t2.tm_min));
    ASetArrayItem(t, frame[0], 5, AIntToValue(t2.tm_sec));
    ASetArrayItem(t, frame[0], 6, AIntToValue(usecs));
    
    return frame[0];
}


A_MODULE(__time, "__time")
    A_DEF("Now", 0, 1, TimeNow)
A_END_MODULE()
