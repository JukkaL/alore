/* float.h - Float operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef FLOAT_H_INCL
#define FLOAT_H_INCL

#include "value.h"
#include "thread.h"


AValue AStdFloat(AThread *t, AValue *frame);


AValue ACreateFloat(AThread *t, double floatNum);
double ALongIntToFloat(AValue liVal);
#define ACreateFloatFromLongInt(t, liVal) \
    ACreateFloat(t, ALongIntToFloat(liVal))


AValue AFloatHashValue(double f);


#endif
