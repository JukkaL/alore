/* std_array.c - std::Array related operations (plus some Tuple operations)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "array.h"
#include "tuple.h"
#include "int.h"
#include "str.h"
#include "std_module.h"
#include "runtime.h"
#include "memberid.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"


#define MAX_ARRAY_COMPARE_DEPTH 5
#define INITIAL_APPEND_ARRAY_SIZE 16


/* Global num of Array iterator class */
int AArrayIterNum;
/* Global num of Tuple iterator class */
int ATupleIterNum;


/* Return a pointer to the first element of an array. The argument should be
   an Array value. */
#define ARRAY_ELEM_PTR(a) \
    (AValueToFixArray(AValueToInstance(a)->member[A_ARRAY_A])->elem)


static AValue CompareArraysRecursive(AThread *t, AValue *a,
                                        AOperator operator, int index);


/* Slot ids of Array iterator objects */
#define ARRAY_ITER_I 0
#define ARRAY_ITER_LEN 1
#define ARRAY_ITER_A 2


/* Array #i()
   Initialize an Array instance. */
AValue AArrayInitialize(AThread *t, AValue *frame)
{
    AValue a;
    ASetMemberDirect(t, frame[0], A_ARRAY_LEN, AZero);
    ASetMemberDirect(t, frame[0], A_ARRAY_CAPACITY, AIntToValue(1));
    a = AMakeFixArray(t, 1, ANil);
    if (AIsError(a))
        return AError;
    ASetMemberDirect(t, frame[0], A_ARRAY_A, a);
    return frame[0];
}


/* Array create([iterable]) */
AValue AArrayCreate(AThread *t, AValue *frame)
{
    AValue v = AArrayInitialize(t, frame);
    if (frame[1] == ADefault || v == AError)
        return v;

    frame[2] = AIterator(t, frame[1]);
    if (AIsError(frame[2]))
        return AError;
    
    for (;;) {
        int res = ANext(t, frame[2], &frame[3]);
        if (res == 0)
            return frame[0];
        else if (res < 0)
            return AError;
        AAppendArray(t, frame[0], frame[3]);
    }    
}


/* Array _get(index) */
AValue AArray_get(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[1])) {
        AValue len = AMemberDirect(frame[0], A_ARRAY_LEN);
        AValue a;
        if (frame[1] >= len) {
            frame[1] = ANormIndexV(frame[1], len);
            if (frame[1] >= len)
                return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
        }
        a = AMemberDirect(frame[0], A_ARRAY_A);
        return AFixArrayItem(a, AValueToInt(frame[1]));
    } else if (AIsPair(frame[1])) {
        AValue loVal, hiVal;
        
        AGetPair(t, frame[1], &loVal, &hiVal);
        if (AIsNil(loVal))
            loVal = AZero;
        if (AIsNil(hiVal))
            hiVal = AIntToValue(A_SLICE_END);
        return ASubArrayND(t, frame[0], AGetInt_ssize_t(t, loVal),
                           AGetInt_ssize_t(t, hiVal));
    } else if (AIsLongInt(frame[1]))
        return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
    else
        return ARaiseTypeError(t, AMsgIntExpected);
}


/* Tuple _get(index) */
AValue ATuple_get(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[1])) {
        AValue len = AMemberDirect(frame[0], A_ARRAY_LEN);
        AValue a;
        if (frame[1] >= len) {
            frame[1] = ANormIndexV(frame[1], len);
            if (frame[1] >= len)
                return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
        }
        a = AMemberDirect(frame[0], A_ARRAY_A);
        return AFixArrayItem(a, AValueToInt(frame[1]));
    } else if (AIsLongInt(frame[1]))
        return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
    else
        return ARaiseTypeError(t, AMsgIntExpected);
}


/* Array _set(index, v) */
AValue AArray_set(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[1])) {
        AValue len = AMemberDirect(frame[0], A_ARRAY_LEN);
        AValue a;
        if (frame[1] >= len) {
            frame[1] = ANormIndexV(frame[1], len);
            if (frame[1] >= len)
                return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
        }
        a = AMemberDirect(frame[0], A_ARRAY_A);
        ASetFixArrayItem(t, a, AValueToInt(frame[1]), frame[2]);
        return ANil;
    } else if (AIsLongInt(frame[1]))
        return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
    else
        return ARaiseTypeError(t, AMsgIntExpected);
}


/* Array _in(v) */
AValue AArray_in(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t n = AValueToInt(AMemberDirect(frame[0], A_ARRAY_LEN));
    frame[2] = AMemberDirect(frame[0], A_ARRAY_A);
    for (i = 0; i < n; i++) {
        int ret = AIsEq(t, frame[1], AFixArrayItem(frame[2], i));
        if (ret > 0)
            return ATrue;
        else if (ret < 0)
            return AError;
    }
    return AFalse;
}


/* Array length() */
AValue AArrayLengthMethod(AThread *t, AValue *frame)
{
    return AMemberDirect(frame[0], A_ARRAY_LEN);
}


/* Array _add(array) */
AValue AArray_add(AThread *t, AValue *frame)
{
    Assize_t len1, len2;
    Assize_t i;

    if (AIsArray(frame[0]) && AIsArray(frame[1]))
        return AConcatArrays(t, frame[0], frame[1]);

    if (!AIsArraySubType(frame[1]))
        return ARaiseTypeError(
            t, "Can only concatenate Array (not %T) to Array", frame[1]);

    len1 = AArrayLen(frame[0]);
    if (len1 < 0)
        return AError;
    len2 = ALen(t, frame[1]);
    if (len2 < 0)
        return AError;

    frame[2] = AMakeArray(t, len1 + len2);
    for (i = 0; i < len1; i++) {
        AValue item = AArrayItem(frame[0], i);
        if (AIsError(item))
            return AError;
        ASetArrayItem(t, frame[2], i, item);
    }
    for (i = 0; i < len2; i++) {
        AValue item = AGetItemAt(t, frame[1], i);
        if (AIsError(item))
            return AError;
        ASetArrayItem(t, frame[2], len1 + i, item);
    }
    
    return frame[2];
}


/* Array _mul(n) */
AValue AArray_mul(AThread *t, AValue *frame)
{
    Assize_t len;
    Assize_t n;
    Assize_t i, j;

    len = AArrayLen(frame[0]);
    n = AGetInt_ssize_t(t, frame[1]);

    if (n < 0)
        return ARaiseValueError(t, "Negative repetition count");
    
    if (len == 1) {
        /* Simple case of repeating an array of length 1 has an optimized
           implementation. */
        ABool result;
        AValue init;
        
        AMakeUninitArray_M(t, n, frame[2], result);
        if (!result)
            return AError;

        init = AArrayItem(frame[0], 0);
        for (i = 0; i < n; i++)
            ASetArrayItemNewGen(frame[2], i, init);
    } else {
        Assize_t len2 = len * n;

        frame[2] = AMakeArray(t, len2);

        j = 0;
        for (i = 0; i < len2; i++) {
            AValue item = AArrayItem(frame[0], j);
            ASetArrayItem(t, frame[2], i, item);
            j++;
            if (j == len)
                j = 0;
        }
    }

    return frame[2];
}


/* Compare an array or tuple (at frame[0]) to a sequence (at frame[1]). The op
   argument is the comparison operator (OPER_EQ, OPER_LT or OPER_GT). Return a
   boolean value. */
static AValue CompareArrayOrder(AThread *t, AValue *frame, int op)
{
    Assize_t len1 = AGetInt_ssize_t(t, AMemberDirect(frame[0], A_ARRAY_LEN));
    Assize_t len2 = ALen(t, frame[1]);
    Assize_t i;
    
    if (len2 < 0)
        return AError;

    frame[2] = AMemberDirect(frame[0], A_ARRAY_A);

    if (op == OPER_EQ && len1 != len2)
        return AFalse;

    for (i = 0; i < len1 && i < len2; i++) {
        int res;

        frame[3] = AGetItemAt(t, frame[1], i);
        if (AIsError(frame[3]))
            return AError;

        if (op == OPER_EQ) {
            res = AIsEq(t, AFixArrayItem(frame[2], i), frame[3]);
            if (res == 0)
                return AFalse;
            else if (res < 0)
                return AError;
        } else {
            if (op == OPER_LT)
                res = AIsLt(t, AFixArrayItem(frame[2], i), frame[3]);
            else
                res = AIsGt(t, AFixArrayItem(frame[2], i), frame[3]);
            if (res > 0)
                return ATrue;
            else if (res < 0)
                return AError;
            
            res = AIsEq(t, AFixArrayItem(frame[2], i), frame[3]);
            if (res == 0)
                return AFalse;
            else if (res < 0)
                return AError;
        }
    }

    return (op == OPER_LT ? len1 < len2 :
            op == OPER_GT ? len1 > len2 : 1) ? ATrue : AFalse;
}


/* Array _eq(x) and
   Tuple _eq(x)
   (they share an implementation) */
AValue AArrayTuple_eq(AThread *t, AValue *frame)
{
    if (AIsArrayOrTuple(frame[1])) {
        AValue res = ACompareArrays(t, frame[0], frame[1], OPER_EQ);
        return res == AZero ? AFalse : res == AIntToValue(1) ?
            ATrue : AError;
    } else if (AIsArraySubType(frame[1]))
        return CompareArrayOrder(t, frame, OPER_EQ);
    else
        return AFalse;
}


/* Array _lt(x) and
   Tuple _lt(x)
   (they share an implementation) */
AValue AArrayTuple_lt(AThread *t, AValue *frame)
{
    if (AIsArrayOrTuple(frame[1])) {
        AValue res = ACompareArrays(t, frame[0], frame[1], OPER_LT);
        return res == AZero ? AFalse : res == AIntToValue(1) ?
            ATrue : AError;
    } else if (AIsArraySubType(frame[1]))
        return CompareArrayOrder(t, frame, OPER_LT);
    else
        return ARaiseTypeError(t, "Array or Tuple expected (but %T given)",
                               frame[1]);
}


/* Array _gt(x) and
   Tuple _gt(x)
   (they share an implementation) */
AValue AArrayTuple_gt(AThread *t, AValue *frame)
{
    if (AIsArrayOrTuple(frame[1])) {
        AValue res = ACompareArrays(t, frame[0], frame[1], OPER_GT);
        return res == AZero ? AFalse : res == AIntToValue(1) ?
            ATrue : AError;
    } else if (AIsArraySubType(frame[1]))
        return CompareArrayOrder(t, frame, OPER_GT);
    else
        return ARaiseTypeError(t, "Array or Tuple expected (but %T given)",
                               frame[1]);
}


/* Array append(v) */
AValue AArrayAppend(AThread *t, AValue *frame)
{
    AValue len = AMemberDirect(frame[0], A_ARRAY_LEN);
    AValue capacity = AMemberDirect(frame[0], A_ARRAY_CAPACITY);

    if (len == capacity)
        AResizeArray(t, frame, 0);

    ASetFixArrayItem(t, AMemberDirect(frame[0], A_ARRAY_A), AValueToInt(len),
                     frame[1]);
    len += AIntToValue(1);
    AValueToInstance(frame[0])->member[A_ARRAY_LEN] = len;
    return ANil;
}


/* Array extend(sequence) */
AValue AArrayExtend(AThread *t, AValue *frame)
{
    Assize_t len = AArrayLen(frame[0]);
    Assize_t capacity = AArrayCapacity(frame[0]);
    Assize_t argLen;
    Assize_t i;

    if (AIsArray(frame[1])) {
        /* Separate implementation for Array argument. */
        argLen = AArrayLen(frame[1]);

        if (argLen > capacity - len)
            AResizeArray(t, frame, argLen);

        frame[2] = AMemberDirect(frame[0], A_ARRAY_A);
        for (i = 0; i < argLen; i++)
            ASetFixArrayItem(t, frame[2], len + i, AArrayItem(frame[1], i));
    } else {
        /* Separate implementation for non-Array argument. */
        argLen = ALen(t, frame[1]);
        if (argLen < 0)
            return AError;

        if (argLen > capacity - len)
            AResizeArray(t, frame, argLen);

        frame[2] = AMemberDirect(frame[0], A_ARRAY_A);
        for (i = 0; i < argLen; i++) {
            AValue item = AGetItemAt(t, frame[1], i);
            if (item == AError)
                return AError;
            ASetFixArrayItem(t, frame[2], len + i, item);
        }
    }
    
    AValueToInstance(frame[0])->member[A_ARRAY_LEN] =
        AIntToValue(len + argLen);
    
    return ANil;
}


/* Array insertAt(index, v) */
AValue AArrayInsertAt(AThread *t, AValue *frame)
{
    Assize_t i = AGetInt_ssize_t(t, frame[1]);
    Assize_t n = AGetInt_ssize_t(t, AMemberDirect(frame[0], A_ARRAY_LEN));
    Assize_t j;

    if (i < 0 || i > n) {
        i = ANormIndex(i, n);
        if (i < 0 || i > n)
            return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
    }

    if (n == AArrayCapacity(frame[0]))
        AResizeArray(t, frame, 0);
    
    frame[3] = AMemberDirect(frame[0], A_ARRAY_A);
    for (j = n - 1; j >= i; j--)
        ASetFixArrayItem(t, frame[3], j + 1, AFixArrayItem(frame[3], j));

    ASetFixArrayItem(t, frame[3], i, frame[2]);
    AValueToInstance(frame[0])->member[A_ARRAY_LEN] += AIntToValue(1);
    
    return ANil;
}


/* Array remove(v) */
AValue AArrayRemove(AThread *t, AValue *frame)
{
    Assize_t i, numDel, len;

    frame[3] = AMemberDirect(frame[0], A_ARRAY_A);
    len = AArrayLen(frame[0]);
    numDel = 0;
    
    for (i = 0; i < len; i++) {
        frame[2] = AFixArrayItem(frame[3], i);
        int ret = AIsEq(t, frame[2], frame[1]);
        if (ret < 0)
            return AError;
        if (ret)
            numDel++;
        else if (numDel != 0)
            ASetFixArrayItem(t, frame[3], i - numDel, frame[2]);
    }

    AValueToInstance(frame[0])->member[A_ARRAY_LEN] -= AIntToValue(numDel);

    return ANil;
}


/* Array removeAt(index) */
AValue AArrayRemoveAt(AThread *t, AValue *frame)
{
    Assize_t i = AGetInt_ssize_t(t, frame[1]);
    Assize_t n = AGetInt_ssize_t(t, AMemberDirect(frame[0], A_ARRAY_LEN));
    Assize_t j;

    if (i >= n || i < 0) {
        i = ANormIndex(i, n);
        if (i >= n || i < 0)
            return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
    }

    frame[2] = AMemberDirect(frame[0], A_ARRAY_A);
    frame[3] = AFixArrayItem(frame[2], i);
    for (j = i; j < n - 1; j++)
        ASetFixArrayItem(t, frame[2], j, AFixArrayItem(frame[2], j + 1));
    ASetFixArrayItem(t, frame[2], n - 1, AZero);

    AValueToInstance(frame[0])->member[A_ARRAY_LEN] -= AIntToValue(1);
    
    return frame[3];
}


/* Helper used by Array _str() and Tuple _str(). */
AValue AArrayTuple_str(AThread *t, AValue *frame, const char *startDelim,
                       const char *endDelim)
{
    Assize_t i;
    Assize_t n;
    AValue repr;
    AValue str;

    repr = AGlobal(t, "std::Repr");
    
    frame[1] = AMemberDirect(frame[0], A_ARRAY_A);
    n = AGetInt_ssize_t(t, AMemberDirect(frame[0], A_ARRAY_LEN));
    frame[2] = AMakeArray(t, 0);
    str = AMakeStr(t, startDelim);
    AAppendArray(t, frame[2], str);

    for (i = 0; i < n; i++) {
        if (i > 0) {
            str = AMakeStr(t, ", ");
            AAppendArray(t, frame[2], str);
        }
        frame[3] = AFixArrayItem(frame[1], i);
        frame[3] = ACallValue(t, repr, 1, frame + 3);
        if (AIsError(frame[3]))
            return AError;
        AAppendArray(t, frame[2], frame[3]);
    }

    if (n == 1 && endDelim[0] == ')') {
        str = AMakeStr(t, ",");
        AAppendArray(t, frame[2], str);
    }

    str = AMakeStr(t, endDelim);
    AAppendArray(t, frame[2], str);
    
    return AJoin(t, frame[2], ADefault);
}


/* Array _str() */
AValue AArray_str(AThread *t, AValue *frame)
{
    return AArrayTuple_str(t, frame, "[", "]");
}


/* Tuple _str() */
AValue ATuple_str(AThread *t, AValue *frame)
{
    return AArrayTuple_str(t, frame, "(", ")");
}


/* Array iterator() */
AValue AArrayIter(AThread *t, AValue *frame)
{
    frame[2] = AMemberDirect(frame[0], A_ARRAY_LEN);
    frame[1] = AMemberDirect(frame[0], A_ARRAY_A);
    return ACallValue(t, AGlobalByNum(AArrayIterNum), 2, frame + 1);
}


/* Tuple iterator() */
AValue ATupleIter(AThread *t, AValue *frame)
{
    frame[2] = AMemberDirect(frame[0], A_ARRAY_LEN);
    frame[1] = AMemberDirect(frame[0], A_ARRAY_A);
    return ACallValue(t, AGlobalByNum(ATupleIterNum), 2, frame + 1);
}


/* Array find(v) */
AValue AArrayFind(AThread *t, AValue *frame)
{
    Assize_t len, i;

    frame[2] = AMemberDirect(frame[0], A_ARRAY_A);
    
    len = AArrayLen(frame[0]);
    for (i = 0; i < len; i++) {
        int ret = AIsEq(t, AFixArrayItem(frame[2], i), frame[1]);
        if (ret < 0)
            return AError;
        if (ret > 0)
            return AMakeInt(t, i);
    }

    return AIntToValue(-1);
}


/* Array index(v) */
AValue AArrayIndex(AThread *t, AValue *frame)
{
    AValue ret = AArrayFind(t, frame);
    if (ret == AIntToValue(-1))
        return ARaiseValueError(t, "Item not found");
    else
        return ret;
}


/* Array count(v) */
AValue AArrayCount(AThread *t, AValue *frame)
{
    Assize_t len, i, n;

    frame[2] = AMemberDirect(frame[0], A_ARRAY_A);

    n = 0;
    len = AArrayLen(frame[0]);
    for (i = 0; i < len; i++) {
        int ret = AIsEq(t, AFixArrayItem(frame[2], i), frame[1]);
        if (ret < 0)
            return AError;
        if (ret > 0)
            n++;
    }

    return AMakeInt(t, n);
}


/* Array _hash() */
AValue AArray_hash(AThread *t, AValue *frame)
{
    return AArrayTupleHashValue(t, frame);
}


/* Tuple _hash() */
AValue ATuple_hash(AThread *t, AValue *frame)
{
    return AArrayTupleHashValue(t, frame);
}


/* Array copy() */
AValue AArrayCopy(AThread *t, AValue *frame)
{
    AValue newArray;
    Assize_t len;
    Assize_t i;
    
    len = AArrayLen(frame[0]);
    newArray = AMakeArray(t, len);
    for (i = 0; i < len; i++)
        ASetArrayItemNewGen(newArray, i, AArrayItem(frame[0], i));
    
    return newArray;
}


/* The create method of Array and Tuple iterators. */
AValue AArrayTupleIterCreate(AThread *t, AValue *frame)
{
    AExpectFixArray(t, frame[1]);
    AExpectInt(t, frame[2]);
    ASetMemberDirect(t, frame[0], ARRAY_ITER_I, AZero);
    ASetMemberDirect(t, frame[0], ARRAY_ITER_LEN, frame[2]);
    ASetMemberDirect(t, frame[0], ARRAY_ITER_A, frame[1]);
    return frame[0];
}


/* The hasNext() method of Array and Tuple iterators. */
AValue AArrayTupleIterHasNext(AThread *t, AValue *frame)
{
    Assize_t i = AMemberDirect(frame[0], ARRAY_ITER_I);
    Assize_t n = AMemberDirect(frame[0], ARRAY_ITER_LEN);
    if (i < n)
        return ATrue;
    else
        return AFalse;
}


/* The next() method of Array and Tuple iterators. */
AValue AArrayTupleIterNext(AThread *t, AValue *frame)
{
    AValue i = AMemberDirect(frame[0], ARRAY_ITER_I);
    AValue n = AMemberDirect(frame[0], ARRAY_ITER_LEN);

    if (i < n) {
        AValueToInstance(frame[0])->member[ARRAY_ITER_I] = i + AIntToValue(1);
        return AFixArrayItem(AMemberDirect(frame[0], ARRAY_ITER_A),
                            AValueToInt(i));
    } else
        return ARaiseValueError(t, "No items left");
}


/* Create an Array with length elements. The elements are uninitialized to
   nil. Return AError if run out of memory. */
AValue AMakeArrayND(AThread *t, Assize_t length)
{
    AValue array;
    ABool result;
    Assize_t i;

    AAvoidWarning_M(array = AZero);

    AMakeUninitArray_M(t, length, array, result);
    if (!result)
        return AError;

    for (i = 0; i < length; i++)
        ASetArrayItemNewGen(array, i, ANil);

    return array;
}


/* Set an item of an Array or Tuple object (val) at the given index. Assume
   val is a Tuple or an Array, and assume that the index is valid and
   non-negative. Return FALSE if failed (out of memory). */
ABool ASetArrayTupleItemND(AThread *t, AValue val, Assize_t index,
                           AValue item)
{
    ABool result;
    AValue fixArray;

    *t->tempStack = item;
    fixArray = AValueToInstance(val)->member[A_ARRAY_A];
    AModifyObject_M(t, &AValueToFixArray(fixArray)->header,
                   AGetFixArrayItemPtr(AValueToFixArray(fixArray),
                                      AIntToValue(index)),
                   t->tempStack, result);
    *t->tempStack = AZero;
    return result;
}


/* Concatenates two arrays into a new array and returns that array
   (or AError if the operation was unsuccessful). */
AValue AConcatArrays(AThread *t, AValue arrVal1, AValue arrVal2)
{
    Assize_t arr1Len;
    Assize_t arr2Len;
    Assize_t length;
    AValue cat;

    t->tempStack[0] = arrVal1;
    t->tempStack[1] = arrVal2;
    
    arr1Len = AArrayLen(arrVal1);
    arr2Len = AArrayLen(arrVal2);
    
    length = arr1Len + arr2Len;    
    cat = AMakeArrayND(t, length);
    if (AIsError(cat))
        return AError;

    ACopyMem(ARRAY_ELEM_PTR(cat), ARRAY_ELEM_PTR(t->tempStack[0]),
            arr1Len * sizeof(AValue));
    ACopyMem(ARRAY_ELEM_PTR(cat) + arr1Len,
            ARRAY_ELEM_PTR(t->tempStack[1]), arr2Len * sizeof(AValue));

    t->tempStack[0] = AZero;
    t->tempStack[1] = AZero;

    return cat;
}


/* Doesn't touch thread->tempStack[1+]. Assumes arrVal is an array.
   Indices are 0-based. The index range is open at the end. Use A_SLICE_END as
   endInd to include all items until array end. Return AError on error. */
AValue ASubArrayND(AThread *t, AValue arrVal, Assize_t begInd, Assize_t endInd)
{
    AValue sub;
    Assize_t length;
    Assize_t arrLen = (ASignedValue)AArrayLen(arrVal);

    if (begInd < 0) {
        begInd = arrLen + begInd;
        if (begInd < 0)
            begInd = 0;
    }

    if (endInd < 0)
        endInd = arrLen + endInd;
    else if (endInd > arrLen)
        endInd = arrLen;

    if (endInd < begInd)
        endInd = begInd;

    t->tempStack[0] = arrVal;

    length = endInd - begInd;

    sub = AMakeArrayND(t, length);
    if (AIsError(sub))
        return AError;

    ACopyMem(ARRAY_ELEM_PTR(sub),
            ARRAY_ELEM_PTR(t->tempStack[0]) + begInd,
            length * sizeof(AValue));

    t->tempStack[0] = AZero;

    return sub;
}


#define BUF_SIZE (MAX_ARRAY_COMPARE_DEPTH * 2)


/* Compare two array/tuple values using the specified comparison operator
   (OPER_EQ, OPER_NEQ, OPER_LT, OPER_GTE, OPER_GT or OPER_GTE). Return
   AIntToValue(1) if the comparison is true, ZERO if false, or AError if an
   exeption was raised. */
AValue ACompareArrays(AThread *t, AValue a1, AValue a2,
                        AOperator operator)
{
    /* IDEA: Test interruption every now and then, since otherwise we might
             freeze indefinitely if given strange input (self-referential
             arrays). */
    /* FIX: MAX_ARRAY_COMPARE_DEPTH should be made larger by dynamically
            allocating the buffer. */

    AValue res;
    Assize_t i;

    if (!AAllocTempStack_M(t, BUF_SIZE))
        return AError;

    i = -BUF_SIZE;
    t->tempStackPtr[i++] = a1;
    t->tempStackPtr[i++] = a2;
    for (; i < 0; i++)
        t->tempStackPtr[i] = AZero;
    res = CompareArraysRecursive(t, t->tempStackPtr - BUF_SIZE,
                                    operator, 0);
    t->tempStackPtr -= BUF_SIZE;

    return res;
}


static AValue CompareArraysRecursive(AThread *t, AValue *a,
                                     AOperator operator, int index)
{
    Assize_t i;
    Assize_t l1, l2;
    int len;

    if (index > 0 && (!AIsArrayOrTuple(a[index])
                      || !AIsArrayOrTuple(a[index + 1])))
        return ACompareOrder(t, a[index], a[index + 1], operator);

    /* Limit maximum comparison depth to avoid stack overflows and endless
       loops. */
    if (index >= BUF_SIZE - 2)
        return ARaiseValueErrorND(t, NULL); /* IDEA: Add error message and
                                               perhaps raise RuntimeError. */
    
    l1 = AArrayOrTupleLen(a[index]);
    l2 = AArrayOrTupleLen(a[index + 1]);
    len = AMin(l1, l2);

    /* Find longest common prefix. */
    for (i = 0; i < len; i++) {
        AValue res;
        a[index + 2] = AArrayOrTupleItem(a[index], i);
        a[index + 3] = AArrayOrTupleItem(a[index + 1], i);
        res = CompareArraysRecursive(t, a, OPER_EQ, index + 2);
        if (AIsError(res))
            return res;
        if (res != AIntToValue(1))
            break;
    }

    if (i >= len)
        return ACompareOrder(t, AIntToValue(l1), AIntToValue(l2), operator);
    else
        return CompareArraysRecursive(t, a, operator, index + 2);
}


/* Grow array buffer (capacity) by at least amount. The capacity is always
   at least doubled independent of the value of amount.

   Accesses frame[0] but does not modify it. */
void AResizeArray(AThread *t, AValue *frame, Assize_t amount)
{
    Assize_t capacity = AArrayCapacity(frame[0]);
    Assize_t newCapacity;
    AValue *tmp = AAllocTemps(t, 2);
    Assize_t i;

    newCapacity = AMax(capacity * 2, capacity + amount);
    if (newCapacity < 4)
        newCapacity = 4; /* IDEA: Don't use constants. */

    tmp[0] = AMakeFixArray(t, newCapacity, ANil);
    if (AIsError(tmp[0]))
        ADispatchException(t);
    tmp[1] = AMemberDirect(frame[0], A_ARRAY_A);
    for (i = 0; i < capacity; i++)
        ASetFixArrayItem(t, tmp[0], i, AFixArrayItem(tmp[1], i));

    ASetMemberDirect(t, frame[0], A_ARRAY_CAPACITY, AIntToValue(newCapacity));
    ASetMemberDirect(t, frame[0], A_ARRAY_A, tmp[0]);

    AFreeTemps(t, 2);
}


/* Return the hash value of an Array or a Tuple (frame[0]). */
AValue AArrayTupleHashValue(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t len;
    AValue hashValue;

    len = AArrayLen(frame[0]);
    hashValue = AZero;

    for (i = 0; i < len; i++) {
        AValue ret;
        
        frame[1] = AArrayItem(frame[0], i);
        ret = ACallValue(t, AGlobalByNum(AStdHashNum), 1,
                            frame + 1);
        if (AIsShortInt(ret))
            hashValue += hashValue * 32 + ret;
        else if (AIsError(ret))
            return AError;
        else if (AIsLongInt(ret))
            hashValue += hashValue * 32 + ALongIntHashValue(ret);
        else
            return ARaiseTypeErrorND(t, NULL);
    }

    return hashValue;
}


/* C API functions */


/* Set array item at specified index. Assume a refers to an Array and the index
   is non-negative and valid. */
void ASetArrayItem(AThread *t, AValue a, Assize_t index, AValue v)
{
    ABool result;
    AInstance *i;
    AFixArray *aa;

    *t->tempStack = v;
    i = AValueToInstance(a);
    aa = AValueToFixArray(i->member[A_ARRAY_A]);
    AModifyObject_M(t, &aa->header, aa->elem + index, t->tempStack, result);
    *t->tempStack = AZero;
    if (!result)
        ARaiseMemoryError(t);
}


/* Make an Array object with given length. All items are initialized to nil. */
AValue AMakeArray(AThread *t, Assize_t len)
{
    AValue v = AMakeArrayND(t, len);
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* Construct a subarray of an array. */
AValue ASubArray(AThread *t, AValue a, Assize_t i1, Assize_t i2)
{
    AValue sub;
    AExpectArray(t, a);
    sub = ASubArrayND(t, a, i1, i2);
    if (AIsError(sub))
        ADispatchException(t);
    return sub;
}


/* Set the item of a FixArray object at given index. Assume index is
   non-negative and valid and that a refers to a FixArray instance. */
void ASetFixArrayItem(AThread *t, AValue a, Assize_t index, AValue v)
{
    ABool result;
    AFixArray *aa;

    *t->tempStack = v;
    aa = AValueToFixArray(a);
    AModifyObject_M(t, &aa->header, aa->elem + index, t->tempStack, result);
    *t->tempStack = AZero;
    if (result)
        return;
    else
        ARaiseMemoryError(t);
}


/* Append an item to an Array. Assume array refers to an Array. */
void AAppendArray(AThread *t, AValue array, AValue item)
{
    AValue len = AMemberDirect(array, A_ARRAY_LEN);
    AValue capacity = AMemberDirect(array, A_ARRAY_CAPACITY);

    if (len == capacity) {
        AValue *tmp = AAllocTemps(t, 2);
        tmp[0] = array;
        tmp[1] = item;
        AResizeArray(t, tmp, 0);
        array = tmp[0];
        item = tmp[1];
        AFreeTemps(t, 2);
    }

    AValueToInstance(array)->member[A_ARRAY_LEN] = len + AIntToValue(1);
    ASetFixArrayItem(t, AMemberDirect(array, A_ARRAY_A), AValueToInt(len),
                     item);
}


/* Create a FixArray with length elements. The elements are uninitialized to
   init. Return AError if run out of memory. */
AValue AMakeFixArray(AThread *t, Assize_t length, AValue init)
{
    AFixArray *array;
    Asize_t size;
    ABool result;
    Assize_t i;

    size = AGetBlockSize((length + 1) * sizeof(AValue));
    AAlloc_M(t, size, array, result);
    if (!result) {
        ARaisePreallocatedMemoryErrorND(t);
        return AError;
    }

    AInitValueBlock(&array->header, length * sizeof(AValue));
    for (i = 0; i < length; i++)
        array->elem[i] = init;

    return AFixArrayToValue(array);
}


/* Create a Tuple with length elements. The elements are uninitialized to
   nil. Raise a direct exception if run out of memory. */
AValue AMakeTuple(AThread *t, Assize_t length)
{
    AValue tuple;
    ABool result;
    Assize_t i;

    AAvoidWarning_M(tuple = AZero);

    AMakeUninitTuple_M(t, length, tuple, result);
    if (!result)
        ADispatchException(t);

    for (i = 0; i < length; i++)
        ASetTupleItemNewGen(tuple, i, ANil);

    return tuple;
}


/* Initialize the item of a tuple at the specified index. Assume v is a
   Tuple instance. */
AValue AInitTupleItem(AThread *t, AValue v, Assize_t index, AValue item)
{
    return ASetArrayTupleItemND(t, v, index, item);
}
