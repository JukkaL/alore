/* thread_module.h - thread module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef THREAD_MODULE_H_INCL
#define THREAD_MODULE_H_INCL

#include "thread.h"


AValue AThreadCreate(AThread *t, AValue *frame);
AValue AThreadJoin(AThread *t, AValue *frame);
AValue AThreadStop(AThread *t, AValue *frame);
AValue AThreadFinalize(AThread *t, AValue *frame);

AValue AMutexCreate(AThread *t, AValue *frame);
AValue AMutexLock(AThread *t, AValue *frame);
AValue AMutexUnlock(AThread *t, AValue *frame);
AValue AMutexFinalize(AThread *t, AValue *frame);

AValue AConditionCreate(AThread *t, AValue *frame);
AValue AConditionWait(AThread *t, AValue *frame);
AValue AConditionSignal(AThread *t, AValue *frame);
AValue AConditionBroadcast(AThread *t, AValue *frame);
AValue AConditionFinalize(AThread *t, AValue *frame);


extern int AThreadMutexNum;


#endif
