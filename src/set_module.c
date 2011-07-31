/* set_module.c - set module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"
#include "internal.h"
#include "array.h"
#include "str.h"
#include "int.h"
#include "mem.h"
#include "gc.h"


/* Initial capacity for empty sets */
#define INITIAL_SET_CAPACITY 4


/* Global num of the Set iterator class */
static int SetIterNum;


/* Slot ids for Set objects */
#define SET_A 0
#define SET_LEN 1          /* Number of items + removed items */
#define A_SET_SIZE 2
#define SET_NUM_REMOVED 3  /* Number of removed items */

/* Slot ids for Set iterator objects */
#define ITER_I 0
#define ITER_LEN 1
#define ITER_A 2


/* Next index in hash chain */
#define NextIndex(i, size) (((i) * 5 + 1) & (size))
/* Conveniences */
#define RemovedMarker AGlobalByNum(ARemovedMarkerNum)
#define EmptyMarker AGlobalByNum(AEmptyMarkerNum)


/* SetFixArray_M is a faster macro replacement for ASetFixArrayItem. */
#define SetFixArray_M(t, a, index, valuePtr) \
    do {                                  \
        AFixArray *a__ = AValueToFixArray(a); \
        ABool result__;                     \
        AModifyObject_M((t), &(a__)->header, (a__)->elem + (index), \
                        (valuePtr), result__);                      \
        if (!result__)            \
            ARaiseMemoryError(t); \
    } while (0)


static AValue SetInitialize(AThread *t, AValue *frame);
static AValue SetAdd(AThread *t, AValue *frame);
static void ResizeSet(AThread *t, AValue *frame);
static int LongIntHash(AValue v);


/* Set create([iterable]) */
static AValue SetCreate(AThread *t, AValue *frame)
{
    Assize_t i;

    if (AIsError(SetInitialize(t, frame)))
        return AError;

    frame[3] = frame[0];
    if (!AIsDefault(frame[1])) {
        if (AIsArray(frame[1])) {
            /* Optimized special case for array argument. */
            for (i = 0; i < AArrayLen(frame[1]); i++) {
                frame[4] = AArrayItem(frame[1], i);
                SetAdd(t, frame + 3);
            }
        } else {
            /* Iterate arbitrary collection. */
            frame[2] = AIterator(t, frame[1]);
            if (AIsError(frame[2]))
                return AError;
            for (;;) {
                int status = ANext(t, frame[2], &frame[4]);
                if (status == 0)
                    break;
                else if (status < 0)
                    return AError;
                SetAdd(t, frame + 3);
            }
        }
    }
    
    return frame[0];

}


/* Set #i()
   Initialize an empty set. */
static AValue SetInitialize(AThread *t, AValue *frame)
{
    AValue a;
    
    /* NOTE: Assume that the AMakeInt call doesn't cause gc. */
    ASetMemberDirect(t, frame[0], A_SET_SIZE,
                     AMakeInt(t, INITIAL_SET_CAPACITY));
    a = AMakeFixArray(t, INITIAL_SET_CAPACITY * 2, EmptyMarker);
    if (AIsError(a))
        return AError;
    
    ASetMemberDirect(t, frame[0], SET_A, a);
    ASetMemberDirect(t, frame[0], SET_LEN, AZero);
    ASetMemberDirect(t, frame[0], SET_NUM_REMOVED, AZero);

    return frame[0];
}


/* Convert a hash value (as AValue) to a C integer. Raise a direct exception
   on errors. */
#define HashToInt(t, v) \
    (AIsShortInt(v) ? AValueToInt(v) : AIsLongInt(v) ? \
     LongIntHash(v) : ARaiseTypeError(t, AMsgIntExpected))


/* Set add(object) */
static AValue SetAdd(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = AZero;
    frame[3] = frame[1];
    hash = AStdHash(t, frame + 3);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], A_SET_SIZE)) - 1;
    i &= size;

    frame[3] = AMemberDirect(frame[0], SET_A);
    while (AFixArrayItem(frame[3], i * 2) != EmptyMarker) {
        int res = AIsEq(t, AFixArrayItem(frame[3], i * 2) , frame[1]);
        if (res == 1) {
            SetFixArray_M(t, frame[3], i * 2 + 1, frame + 2);
            return ANil;
        } else if (res < 0)
            return AError;
        i = NextIndex(i, size);
    }

    SetFixArray_M(t, frame[3], i * 2, frame + 1);
    SetFixArray_M(t, frame[3], i * 2 + 1, frame + 2);
    
    AValueToInstance(frame[0])->member[SET_LEN] += AIntToValue(1);
    
    if (AValueToInt(AMemberDirect(frame[0], SET_LEN)) * 3 >= size * 2 + 2)
        ResizeSet(t, frame);
    
    return ANil;
}


/* Resize a set. Determine the new size automatically based on current level
   of emptiness. */
static void ResizeSet(AThread *t, AValue *frame)
{
    Assize_t oldSize;
    Assize_t numItems;
    ASignedValue newSize;
    Assize_t i;
    AValue a;
    AValue *oldA = AAllocTemp(t, AMemberDirect(frame[0], SET_A));

    oldSize = AGetInt(t, AMemberDirect(frame[0], A_SET_SIZE));

    numItems = AValueToInt(AMemberDirect(frame[0], SET_LEN) -
                           AMemberDirect(frame[0], SET_NUM_REMOVED));
    if (numItems * 3 > oldSize)
        newSize = 2 * oldSize;
    else if (numItems * 6 <= oldSize)
        newSize = AMax(INITIAL_SET_CAPACITY, oldSize / 2);
    else
        newSize = oldSize;

    if (newSize > A_SHORT_INT_MAX)
        ARaiseValueError(t, "Set size overflow");
    
    ASetMemberDirect(t, frame[0], A_SET_SIZE, AMakeInt(t, newSize));
    a = AMakeFixArray(t, 2 * newSize, EmptyMarker);
    if (AIsError(a))
        ADispatchException(t);
    ASetMemberDirect(t, frame[0], SET_A, a);
    ASetMemberDirect(t, frame[0], SET_LEN, AZero);
    ASetMemberDirect(t, frame[0], SET_NUM_REMOVED, AZero);

    for (i = 0; i < oldSize * 2; i += 2) {
        AValue v = AFixArrayItem(*oldA, i);
        if (v != EmptyMarker && v != RemovedMarker) {
            frame[1] = v;
            SetAdd(t, frame);
        }
    }

    AFreeTemp(t);
}


/* Set _in(object) */
static AValue Set_in(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = frame[1];
    hash = AStdHash(t, frame + 2);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], A_SET_SIZE)) - 1;
    i &= size;

    frame[2] = AMemberDirect(frame[0], SET_A);
    while (AFixArrayItem(frame[2], i * 2) != EmptyMarker) {
        int res = AIsEq(t, AFixArrayItem(frame[2], i * 2), frame[1]);
        if (res == 1)
            return ATrue;
        else if (res < 0)
            return AError;
        i = NextIndex(i, size);
    }
    return AFalse;
}


/* Set length() */
static AValue SetLength(AThread *t, AValue *frame)
{
    return AMemberDirect(frame[0], SET_LEN) -
        AMemberDirect(frame[0], SET_NUM_REMOVED);
}


/* Set remove(object) */
static AValue SetRemove(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = frame[1];
    hash = AStdHash(t, frame + 2);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], A_SET_SIZE)) - 1;
    i &= size;

    frame[2] = AMemberDirect(frame[0], SET_A);
    while (AFixArrayItem(frame[2], i * 2) != EmptyMarker) {
        int res = AIsEq(t, AFixArrayItem(frame[2], i * 2), frame[1]);
        if (res == 1) {
            /* Set the key to ADefault to mark a removed item. */
            ASetFixArrayItem(t, frame[2], i * 2, RemovedMarker);
            AValueToInstance(frame[0])->member[SET_NUM_REMOVED] +=
                AIntToValue(1);
            return ANil;
        } else if (res < 0)
            return AError;
        i = NextIndex(i, size);
    }

    /* Object not found in the set. Do nothing. */
    return ANil;
}


/* Set _str() */
static AValue Set_str(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t n;
    AValue repr;
    AValue str;
    ABool first = TRUE;

    repr = AGlobal(t, "std::Repr");
    
    frame[1] = AMemberDirect(frame[0], SET_A);
    n = AFixArrayLen(frame[1]);
    frame[2] = AMakeArray(t, 0);
    str = AMakeStr(t, "Set(");
    AAppendArray(t, frame[2], str);

    for (i = 0; i < n; i += 2) {
        frame[3] = AFixArrayItem(frame[1], i);
        if (frame[3] != EmptyMarker && frame[3] != RemovedMarker) {
            if (!first) {
                str = AMakeStr(t, ", ");
                AAppendArray(t, frame[2], str);
            }
            frame[3] = ACallValue(t, repr, 1, frame + 3);
            if (AIsError(frame[3]))
                return AError;
            AAppendArray(t, frame[2], frame[3]);
            first = FALSE;
        }
    }

    str = AMakeStr(t, ")");
    AAppendArray(t, frame[2], str);

    return AJoin(t, frame[2], ADefault);
}


/* Set _eq(object) */
static AValue Set_eq(AThread *t, AValue *frame)
{
    int status;
    Assize_t len;
    
    if (!AIsValue(t, frame[1], AGlobal(t, "set::Set")))
        return AFalse;

    len = ALen(t, frame[1]);
    if (len < 0)
        return AError;
    if (len != AValueToInt(AMemberDirect(frame[0], SET_LEN) -
        AMemberDirect(frame[0], SET_NUM_REMOVED)))
        return AFalse;

    frame[2] = AIterator(t, frame[1]);
    if (AIsError(frame[2]))
        return AError;
    frame[3] = frame[0];
    while ((status = ANext(t, frame[2], frame + 4)) == 1) {
        AValue in = Set_in(t, frame + 3);
        if (AIsFalse(in) || AIsError(in))
            return in;
    }
    
    return status == 0 ? ATrue : AError;
}


/* Set _hash() */
static AValue Set_hash(AThread *t, AValue *frame)
{
    AValue hash;
    Assize_t i;
    Assize_t len;

    hash = 0;
    len = AValueToInt(AMemberDirect(frame[0], A_SET_SIZE)) * 2;
    frame[1] = AMemberDirect(frame[0], SET_A);
    
    for (i = 0; i < len; i += 2) {
        frame[2] = AFixArrayItem(frame[1], i);
        if (frame[2] != EmptyMarker && frame[2] != RemovedMarker) {
            /* The hash value of a set is constructed by xoring the hash
               values of set items. This way the value does not depend on the
               order the items are evaluated (equal sets can have their items
               in different orders). Before xoring, the item hash values are
               multiplied by a constant to increase spread. */

            /* IDEA: It might be a better idea to use multiplication modulo
                     prime instead of xoring. */
            frame[2] = AStdHash(t, frame + 2);
            if (AIsError(frame[2]))
            return AError;
            hash ^= HashToInt(t, frame[2]) * (32768 + 512 + 32 + 1);
        }
    }
    
    return AMakeInt(t, hash);
}


/* Set copy() */
static AValue SetCopy(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t len;

    frame[2] = ACallValue(t, AGlobal(t, "set::Set"), 0, frame + 1);
    
    len = AValueToInt(AMemberDirect(frame[0], A_SET_SIZE)) * 2;
    frame[1] = AMemberDirect(frame[0], SET_A);
    
    for (i = 0; i < len; i += 2) {
        frame[3] = AFixArrayItem(frame[1], i);
        if (frame[3] != EmptyMarker && frame[3] != RemovedMarker) {
            if (AIsError(SetAdd(t, frame + 2)))
                return AError;
        }
    }
        
    return frame[2];
}


/* Set iterator() */
static AValue SetIter(AThread *t, AValue *frame)
{
    frame[0] = AMemberDirect(frame[0], SET_A);
    return ACallValue(t, AGlobalByNum(SetIterNum), 1, frame);
}


/* The create method of Set iterator. */
static AValue SetIterCreate(AThread *t, AValue *frame)
{
    AExpectFixArray(t, frame[1]);
    ASetMemberDirect(t, frame[0], ITER_I, AZero);
    ASetMemberDirect(t, frame[0], ITER_LEN,
                     AIntToValue(AFixArrayLen(frame[1])));
    ASetMemberDirect(t, frame[0], ITER_A, frame[1]);
    return frame[0];
}


/* Set iterator hasNext() */
static AValue SetIterHasNext(AThread *t, AValue *frame)
{
    Assize_t i = AValueToInt(AMemberDirect(frame[0], ITER_I));
    Assize_t n = AValueToInt(AMemberDirect(frame[0], ITER_LEN));
    AValue a = AMemberDirect(frame[0], ITER_A);

    for (; i < n - 1; i += 2) {
        if (AFixArrayItem(a, i) != RemovedMarker &&
            AFixArrayItem(a, i) != EmptyMarker) {
            AValueToInstance(frame[0])->member[ITER_I] = AIntToValue(i);
            return ATrue;
        }
    }
    
    AValueToInstance(frame[0])->member[ITER_I] = AIntToValue(i);
    return AFalse;
}


/* Set iterator next() */
static AValue SetIterNext(AThread *t, AValue *frame)
{
    Assize_t i = AValueToInt(AMemberDirect(frame[0], ITER_I));
    Assize_t n = AValueToInt(AMemberDirect(frame[0], ITER_LEN));

    if (i < n - 1) {
        AValue a = AMemberDirect(frame[0], ITER_A);
        AValue obj = AFixArrayItem(a, i);
        
        if (obj == EmptyMarker || obj == RemovedMarker) {
            SetIterHasNext(t, frame);
            return SetIterNext(t, frame);
        }

        AValueToInstance(frame[0])->member[ITER_I] += AIntToValue(2);
        return obj;
    } else
        return ARaiseValueError(t, "No items left");
}


/* Return a hash value of a long integer. */
static int LongIntHash(AValue v)
{
    ALongInt *li = AValueToLongInt(v);
    Assize_t l = AGetLongIntLen(li);
    Assize_t i;
    int hash = AGetLongIntSign(li);

    for (i = 0; i < l; i++)
        hash = 33 * hash + li->digit[i];

    return hash;
}


A_MODULE(set, "set")
    A_CLASS_PRIV("Set", 4)
        A_IMPLEMENT("std::Iterable")
        A_METHOD_OPT("create", 0, 1, 7, SetCreate)
        A_METHOD("#i", 0, 0, SetInitialize)
        A_METHOD("add", 1, 4, SetAdd)
        A_METHOD("_in", 1, 3, Set_in)
        A_METHOD("length", 0, 0, SetLength)
        A_METHOD("remove", 1, 3, SetRemove)
        A_METHOD("_str", 0, 5, Set_str)
        A_METHOD("_eq", 1, 6, Set_eq)
        A_METHOD("_hash", 0, 4, Set_hash)
        A_METHOD("copy", 0, 7, SetCopy)
        A_METHOD("iterator", 0, 1, SetIter)
    A_END_CLASS()
    
    A_CLASS_PRIV_P(A_PRIVATE("SetIter"), 3, &SetIterNum)
        A_METHOD("create", 1, 0, SetIterCreate)
        A_METHOD("hasNext", 0, 0, SetIterHasNext)
        A_METHOD("next", 0, 0, SetIterNext)
    A_END_CLASS()
A_END_MODULE()
