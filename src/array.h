/* array.h - Array operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ARRAY_H_INCL
#define ARRAY_H_INCL

#include "thread.h"
#include "operator.h"


/* Shared Array/Tuple operations. */
AValue AArrayTupleLength(AThread *t, AValue *frame);
AValue AArrayTuple_in(AThread *t, AValue *frame);
AValue AArrayTuple_eq(AThread *t, AValue *frame);
AValue AArrayTuple_lt(AThread *t, AValue *frame);
AValue AArrayTuple_gt(AThread *t, AValue *frame);

AValue AArrayCreate(AThread *t, AValue *frame);
AValue AArrayInitialize(AThread *t, AValue *frame);
AValue AArray_get(AThread *t, AValue *frame);
AValue AArray_set(AThread *t, AValue *frame);
#define AArray_in AArrayTuple_in
#define AArray_eq AArrayTuple_eq
#define AArray_lt AArrayTuple_lt
#define AArray_gt AArrayTuple_gt
#define AArrayLengthMethod AArrayTupleLength
AValue AArray_add(AThread *t, AValue *frame);
AValue AArray_mul(AThread *t, AValue *frame);
AValue AArrayAppend(AThread *t, AValue *frame);
AValue AArrayExtend(AThread *t, AValue *frame);
AValue AArrayInsertAt(AThread *t, AValue *frame);
AValue AArrayRemove(AThread *t, AValue *frame);
AValue AArrayRemoveAt(AThread *t, AValue *frame);
AValue AArray_str(AThread *t, AValue *frame);
AValue AArrayIter(AThread *t, AValue *frame);
AValue AArrayFind(AThread *t, AValue *frame);
AValue AArrayIndex(AThread *t, AValue *frame);
AValue AArrayCount(AThread *t, AValue *frame);
AValue AArray_hash(AThread *t, AValue *frame);
AValue AArrayCopy(AThread *t, AValue *frame);

AValue AArrayTupleIterCreate(AThread *t, AValue *frame);
AValue AArrayTupleIterHasNext(AThread *t, AValue *frame);
AValue AArrayTupleIterNext(AThread *t, AValue *frame);

#define AArrayIterCreate AArrayTupleIterCreate
#define AArrayIterHasNext AArrayTupleIterHasNext
#define AArrayIterNext AArrayTupleIterNext


extern int AArrayIterNum;


#define AMakeUninitArrayOrTuple_M(t, length, target, class_, result)     \
    do {                              \
        unsigned long instSize, size; \
        AInstance *inst;              \
        AFixArray *fixArray;          \
                                      \
        instSize = (class_)->instanceSize; \
        size = instSize + AGetBlockSize(((length) + 1) * sizeof(AValue)); \
        AAlloc_M(t, size, inst, (result));  \
        if (!(result)) {                    \
            ARaisePreallocatedMemoryErrorND(t); \
            break;                          \
        }                                   \
        fixArray = APtrAdd(inst, instSize); \
                                            \
        AInitInstanceBlock(&inst->type, (class_)); \
        inst->member[A_ARRAY_LEN] = AIntToValue(length);      \
        inst->member[A_ARRAY_CAPACITY] = AIntToValue(length); \
        inst->member[A_ARRAY_A] = AFixArrayToValue(fixArray); \
        AInitValueBlock(&fixArray->header, (length) * sizeof(AValue)); \
        (target) = AInstanceToValue(inst); \
    } while (0)


AValue AMakeArrayND(AThread *t, Assize_t length);
#define AMakeUninitArray_M(t, length, array, result) \
    AMakeUninitArrayOrTuple_M(t, length, array, AArrayClass, result)
ABool ASetArrayTupleItemND(AThread *t, AValue arrVal, Assize_t index,
                           AValue item);
#define ASetArrayItemND ASetArrayTupleItemND
#define ASetArrayItemNewGen(a, index, v) \
    (AArrayItem(a, index) = (v))


#define AArrayCapacity(v) \
    (AValueToInt(AValueToInstance(v)->member[A_ARRAY_CAPACITY]))


AValue AMakeFixArray(AThread *t, Assize_t length, AValue init);

AValue AConcatArrays(AThread *t, AValue arrVal1, AValue arrVal2);
AValue ASubArrayND(AThread *t, AValue arrVal, Assize_t begInd,
                   Assize_t endInd);
AValue ACompareArrays(AThread *t, AValue a1, AValue a2, AOperator operator);

AValue AArrayTupleHashValue(AThread *t, AValue *frame);

void AResizeArray(AThread *t, AValue *frame, Assize_t amount);


#endif
