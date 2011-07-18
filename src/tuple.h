/* tuple.h - Tuple operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef TUPLE_H_INCL
#define TUPLE_H_INCL

#include "thread.h"
#include "operator.h"
#include "array.h"


#define ATupleLengthMethod AArrayTupleLength
AValue ATupleIter(AThread *t, AValue *frame);
AValue ATuple_get(AThread *t, AValue *frame);
#define ATuple_in AArrayTuple_in
#define ATuple_eq AArrayTuple_eq
#define ATuple_lt AArrayTuple_lt
#define ATuple_gt AArrayTuple_gt
AValue ATuple_str(AThread *t, AValue *frame);
AValue ATuple_hash(AThread *t, AValue *frame);

#define ATupleIterCreate AArrayTupleIterCreate
#define ATupleIterHasNext AArrayTupleIterHasNext
#define ATupleIterNext AArrayTupleIterNext


extern int ATupleIterNum;


#define AMakeUninitTuple_M(t, length, tuple, result) \
    AMakeUninitArrayOrTuple_M(t, length, tuple, ATupleClass, result)

#define ASetTupleItemNewGen(a, index, v) \
    (AArrayItem(a, index) = (v))


#endif
