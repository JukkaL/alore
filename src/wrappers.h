/* wrappers.h - Wrappers for representing methods of built-in types

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef WRAPPERS_H_INCL
#define WRAPPERS_H_INCL

#include "alore.h"


/* Generic wrapper methods */
AValue AAddWrapper(AThread *t, AValue *frame);
AValue ASubWrapper(AThread *t, AValue *frame);
AValue ANegWrapper(AThread *t, AValue *frame);
AValue AConcatWrapper(AThread *t, AValue *frame);
AValue AMulWrapper(AThread *t, AValue *frame);
AValue ADivWrapper(AThread *t, AValue *frame);
AValue AIdivWrapper(AThread *t, AValue *frame);
AValue AModWrapper(AThread *t, AValue *frame);
AValue APowWrapper(AThread *t, AValue *frame);
AValue AGetItemWrapper(AThread *t, AValue *frame);
AValue ASetItemWrapper(AThread *t, AValue *frame);
AValue AEqWrapper(AThread *t, AValue *frame);
AValue ALtWrapper(AThread *t, AValue *frame);
AValue AGtWrapper(AThread *t, AValue *frame);
AValue AInWrapper(AThread *t, AValue *frame);
AValue ACallWrapper(AThread *t, AValue *frame);
AValue AStrWrapper(AThread *t, AValue *frame);
AValue AHashWrapper(AThread *t, AValue *frame);
AValue AReprWrapper(AThread *t, AValue *frame);
AValue AIntWrapper(AThread *t, AValue *frame);
AValue AFloatWrapper(AThread *t, AValue *frame);
AValue AIterWrapper(AThread *t, AValue *frame);


/* Range methods */
AValue AStdRangeStart(AThread *t, AValue *frame);
AValue AStdRangeStop(AThread *t, AValue *frame);


/* Pair methods */
AValue APairLeft(AThread *t, AValue *frame);
AValue APairRight(AThread *t, AValue *frame);


/* Misc methods  */
AValue AHiddenCreate(AThread *t, AValue *frame);


#define A_UNWRAP(value) (AMemberDirect((value), 0))


#endif
