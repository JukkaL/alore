/* random_module.c - random module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"

#include <stdlib.h>
#include <time.h>

#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#include <unistd.h>
#endif


/* If the return value of rand() is divided by RAND_DIVISOR, the result is a
   floating point value v so that 0 <= v < 1. */
/* If we are working with 64-bit integers where RAND_MAX might be around
   1 << 63, it is possible that RAND_MAX + 1.0 == RAND_MAX, so we add some
   extra to RAND_DIVISOR so that it is not equal to RAND_MAX. */
#ifdef A_HAVE_INT_64_BITS
#define RAND_DIVISOR (RAND_MAX + 1.0 + (RAND_MAX >> 50))
#else
#define RAND_DIVISOR (RAND_MAX + 1.0)
#endif


static AValue Random(AThread *t, AValue *frame)
{
    int value;
    int max;
    
    /* FIX: Make this work with long integers. */
    max = AGetInt(t, frame[0]);
    if (max <= 0)
        return ARaiseValueError(t, "Positive argument expected");
    
    value = (unsigned int)((double)max * rand() / RAND_DIVISOR);
    return AMakeInt(t, value);
}


static AValue RandomFloat(AThread *t, AValue *frame)
{
    double value = rand() / RAND_DIVISOR;
    return AMakeFloat(t, value);
}


static AValue RandomSeed(AThread *t, AValue *frame)
{
    unsigned int seed;
    
    if (AIsDefault(frame[0])) {
#ifdef HAVE_GETTIMEOFDAY
        struct timeval t;

        if (gettimeofday(&t, NULL) < 0)
            seed = time(NULL);
        else
            seed = t.tv_usec + t.tv_sec * 1000000;
#else
        seed = time(NULL);
#endif        
    } else
        seed = AGetInt(t, frame[0]);

    srand(seed);
    
    return ANil;
}


static AValue RandomMain(AThread *t, AValue *frame)
{
    frame[0] = ADefault;
    RandomSeed(t, frame);
    return ANil;
}


A_MODULE(random, "random")
    A_DEF("Main", 0, 1, RandomMain)
    A_DEF("Random", 1, 0, Random)
    A_DEF("RandomFloat", 0, 0, RandomFloat)
    A_DEF_OPT("Seed", 0, 1, 0, RandomSeed)
A_END_MODULE()
