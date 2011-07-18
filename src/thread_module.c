/* thread_module.c - thread module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "thread_module.h"
#include "thread_athread.h"


AThread *AMainThread;

int AThreadMutexNum;


A_MODULE(thread, "thread")
    A_CLASS_PRIV("Thread", 3)
        A_METHOD("create", 1, 5, AThreadCreate)
        A_METHOD("join", 0, 0, AThreadJoin)
        A_METHOD("stop", 0, 0, AThreadStop)
        A_METHOD("#f", 0, 0, AThreadFinalize)
        /* FIX why not #i */
    A_END_CLASS()
    A_CLASS_PRIV_P("Mutex", 1, &AThreadMutexNum)
        /* NOTE: A_MUTEX_DATA_SIZE currently is 0. */
        A_BINARY_DATA(A_MUTEX_DATA_SIZE)
        A_METHOD("create", 0, 0, AMutexCreate)
        A_METHOD("lock", 0, 0, AMutexLock)
        A_METHOD("unlock", 0, 0, AMutexUnlock)
        A_METHOD("#i", 0, 0, AMutexCreate)
        A_METHOD("#f", 0, 0, AMutexFinalize)
    A_END_CLASS()
    A_CLASS_PRIV("Condition", 1)
        /* NOTE: A_CONDITION_DATA_SIZE currently is 0. */
        A_BINARY_DATA(A_CONDITION_DATA_SIZE)
        A_METHOD("create", 0, 0, AConditionCreate)
        A_METHOD("wait", 1, 1, AConditionWait)
        A_METHOD("signal", 0, 0, AConditionSignal)
        A_METHOD("broadcast", 0, 0, AConditionBroadcast)
        A_METHOD("#i", 0, 0, AConditionCreate)
        A_METHOD("#f", 0, 0, AConditionFinalize)
    A_END_CLASS()
A_END_MODULE()
