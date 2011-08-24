/* std_map.c - std::Map related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"
#include "array.h"
#include "tuple.h"
#include "int.h"
#include "str.h"
#include "gc.h"
#include "mem.h"
#include "internal.h"
#include "runtime.h"


/* Size of an empty map after construction */
#define INITIAL_MAP_CAPACITY 4


/* Global num of Map iterator type */
int AMapIterNum;
/* Global num of constant that marks empty locations in the hash table */
int AEmptyMarkerNum;
/* Global num of constant that marks removed locations in the hash table */
int ARemovedMarkerNum;


/* Slot ids for std::Map instances */
#define MAP_A 0            /* The hash table */
#define MAP_LEN 1          /* Number of items + removed items */
#define MAP_SIZE 2
#define MAP_NUM_REMOVED 3  /* Number of removed items */

/* Slot ids for Map iterator instances */
#define ITER_I 0
#define ITER_LEN 1
#define ITER_A 2


/* Calculate next index in a hash chain. */
#define NextIndex(i, size) (((i) * 5 + 1) & (size))
/* Conveniences */
#define RemovedMarker AGlobalByNum(ARemovedMarkerNum)
#define EmptyMarker AGlobalByNum(AEmptyMarkerNum)
/* Return the number of items in a map. */
#define MapLen_M(v) \
    (AMemberDirect(v, MAP_LEN) - AMemberDirect(v, MAP_NUM_REMOVED))


static void ResizeMap(AThread *t, AValue *frame);
static int LongIntHash(AValue v);


/* Map create(*items) */
AValue AMapCreate(AThread *t, AValue *frame)
{
    Assize_t i;

    if (AIsError(AMapInitialize(t, frame)))
        return AError;
    
    frame[2] = frame[0];
    for (i = 0; i < AArrayLen(frame[1]); i++) {
        AGetPair(t, AArrayItem(frame[1], i), frame + 3, frame + 4);
        AMap_set(t, frame + 2);
    }
    
    return frame[0];
}


/* Map #i()
   Initialize map object when create may not have been called. */
AValue AMapInitialize(AThread *t, AValue *frame)
{
    AValue a;
    
    /* NOTE: Assume that the AMakeInt call doesn't cause gc. */
    ASetMemberDirect(t, frame[0], MAP_SIZE, AMakeInt(t, INITIAL_MAP_CAPACITY));
    a = AMakeFixArray(t, INITIAL_MAP_CAPACITY * 2, EmptyMarker);
    if (AIsError(a))
        return AError;
    ASetMemberDirect(t, frame[0], MAP_A, a);
    ASetMemberDirect(t, frame[0], MAP_LEN, AZero);
    ASetMemberDirect(t, frame[0], MAP_NUM_REMOVED, AZero);

    return frame[0];
}


/* Convert a hash value to a C integer. Raise direct exception if v is not an
   integer value. */
#define HashToInt(t, v) \
    (AIsShortInt(v) ? AValueToInt(v) : AIsLongInt(v) ? \
     LongIntHash(v) : ARaiseTypeError(t, AMsgIntExpected))


/* Get the value associated with a key in a Map. Assume that frame[0] is a Map
   instance, frame[1] is the key and frame[2..3] are available for
   temporaries. If def is AError, raise an exception if the key is missing.
   Otherwise, return def if the key is missing. */
static AValue MapLookup(AThread *t, AValue *frame, AValue def)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = frame[1];
    hash = AStdHash(t, frame + 2);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE)) - 1;
    i &= size;

    frame[2] = AMemberDirect(frame[0], MAP_A);
    while (AFixArrayItem(frame[2], i * 2) != EmptyMarker) {
        int res = AIsEq(t, AFixArrayItem(frame[2], i * 2), frame[1]);
        if (res == 1)
            return AFixArrayItem(frame[2], i * 2 + 1);
        else if (res < 0)
            return AError;
        i = NextIndex(i, size);
    }

    /* Key did not exist. Either raise an exception or return a default
       value. Include the representation of the key in the exception
       message. */
    if (def == AError)
        return ARaiseKeyErrorWithRepr(t, frame[1]);
    else
        return def;
}


/* Map _get(key) */
AValue AMap_get(AThread *t, AValue *frame)
{
    return MapLookup(t, frame, AError);
}


/* Map get(key, default) */
AValue AMapGet(AThread *t, AValue *frame)
{
    return MapLookup(t, frame, frame[2]);
}


/* SetFixArray_M is a faster macro replacement for ASetFixArrayItem. */
#define SetFixArray_M(t, a, index, valuePtr) \
    do {                                  \
        AFixArray *a__ = AValueToFixArray(a); \
        ABool result__;                     \
        AModifyObject_M(t, &(a__)->header, (a__)->elem + (index), (valuePtr), \
                       result__); \
        if (!result__)            \
            ARaiseMemoryError(t); \
    } while (0)


/* Map _set(key, value) */
AValue AMap_set(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[3] = frame[1];
    hash = AStdHash(t, frame + 3);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE)) - 1;
    i &= size;

    frame[3] = AMemberDirect(frame[0], MAP_A);
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
    
    AValueToInstance(frame[0])->member[MAP_LEN] += AIntToValue(1);
    
    if (AValueToInt(AMemberDirect(frame[0], MAP_LEN)) * 3 >= size * 2 + 2)
        ResizeMap(t, frame);
    
    return ANil;
}


/* Resize a Map object to a new size. Automatically determine the new size
   based on the current level of emptiness. */
static void ResizeMap(AThread *t, AValue *frame)
{
    Assize_t oldSize;
    Assize_t numItems;
    ASignedValue newSize;
    Assize_t i;
    AValue a;
    AValue *oldA = AAllocTemp(t, AMemberDirect(frame[0], MAP_A));

    oldSize = AGetInt(t, AMemberDirect(frame[0], MAP_SIZE));

    numItems = AValueToInt(MapLen_M(frame[0]));
    /* Determine the new size. */
    if (numItems * 3 > oldSize)
        newSize = 2 * oldSize;
    else if (numItems * 6 <= oldSize)
        newSize = AMax(INITIAL_MAP_CAPACITY, oldSize / 2);
    else
        newSize = oldSize;

    if (newSize > A_SHORT_INT_MAX)
        ARaiseValueError(t, "Map size overflow");
    
    ASetMemberDirect(t, frame[0], MAP_SIZE, AMakeInt(t, newSize));
    a = AMakeFixArray(t, 2 * newSize, EmptyMarker);
    if (AIsError(a))
        ADispatchException(t);
    ASetMemberDirect(t, frame[0], MAP_A, a);
    ASetMemberDirect(t, frame[0], MAP_LEN, AZero);
    ASetMemberDirect(t, frame[0], MAP_NUM_REMOVED, AZero);

    /* Populate the new hash table by copying items from the original one. */
    for (i = 0; i < oldSize * 2; i += 2) {
        AValue v = AFixArrayItem(*oldA, i);
        if (v != EmptyMarker && v != RemovedMarker) {
            frame[1] = v;
            frame[2] = AFixArrayItem(*oldA, i + 1);
            AMap_set(t, frame);
        }
    }

    AFreeTemp(t);
}


/* Map hasKey(key) */
AValue AMapHasKey(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = frame[1];
    hash = AStdHash(t, frame + 2);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE)) - 1;
    i &= size;

    frame[2] = AMemberDirect(frame[0], MAP_A);
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


/* Map length() */
AValue AMapLength(AThread *t, AValue *frame)
{
    return MapLen_M(frame[0]);
}


/* Map remove(key) */
AValue AMapRemove(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t size;
    AValue hash;

    frame[2] = frame[1];
    hash = AStdHash(t, frame + 2);
    if (AIsError(hash))
        return AError;
        
    i = HashToInt(t, hash);
    size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE)) - 1;
    i &= size;

    frame[2] = AMemberDirect(frame[0], MAP_A);
    while (AFixArrayItem(frame[2], i * 2) != EmptyMarker) {
        int res = AIsEq(t, AFixArrayItem(frame[2], i * 2), frame[1]);
        if (res == 1) {
            /* Set the key to ADefault to mark a removed item. */
            ASetFixArrayItem(t, frame[2], i * 2, RemovedMarker);
            AValueToInstance(frame[0])->member[MAP_NUM_REMOVED] +=
                AIntToValue(1);
            return ANil;
        } else if (res < 0)
            return AError;
        i = NextIndex(i, size);
    }

    /* Key does not exist. Raise an exception with the representation of the
       key in the message. */
    return ARaiseKeyErrorWithRepr(t, frame[1]);
}


/* Map items() */
AValue AMapItems(AThread *t, AValue *frame)
{
    Assize_t n = AValueToInt(MapLen_M(frame[0]));
    Assize_t size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE));
    Assize_t i, j;
    
    frame[1] = AMakeArray(t, n);
    frame[2] = AMemberDirect(frame[0], MAP_A);

    j = 0;
    for (i = 0; i < 2 * size; i += 2) {
        frame[4] = AFixArrayItem(frame[2], i);
        if (frame[4] != EmptyMarker && frame[4] != RemovedMarker) {
            frame[3] = AMakeTuple(t, 2);
            AInitTupleItem(t, frame[3], 0, frame[4]);
            AInitTupleItem(t, frame[3], 1, AFixArrayItem(frame[2], i + 1));
            ASetArrayItem(t, frame[1], j, frame[3]);
            j++;
        }
    }
    
    return frame[1];
}


/* Map keys() */
AValue AMapKeys(AThread *t, AValue *frame)
{
    Assize_t n = AValueToInt(MapLen_M(frame[0]));
    AValue dst = AMakeArray(t, n);
    Assize_t size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE));
    AValue a = AMemberDirect(frame[0], MAP_A);
    Assize_t i, j;

    j = 0;
    for (i = 0; i < 2 * size; i += 2) {
        AValue v = AFixArrayItem(a, i);
        if (v != EmptyMarker && v != RemovedMarker) {
            ASetArrayItemNewGen(dst, j, v);
            j++;
        }
    }
    
    return dst;
}


/* Map values() */
AValue AMapValues(AThread *t, AValue *frame)
{
    Assize_t n = AValueToInt(MapLen_M(frame[0]));
    AValue dst = AMakeArray(t, n);
    Assize_t size = AValueToInt(AMemberDirect(frame[0], MAP_SIZE));
    AValue a = AMemberDirect(frame[0], MAP_A);
    Assize_t i, j;

    j = 0;
    for (i = 0; i < 2 * size; i += 2) {
        AValue v = AFixArrayItem(a, i);
        if (v != EmptyMarker && v != RemovedMarker) {
            ASetArrayItemNewGen(dst, j, AFixArrayItem(a, i + 1));
            j++;
        }
    }
    
    return dst;
}


/* Map _str() */
AValue AMap_str(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t n;
    AValue repr;
    AValue str;
    ABool first = TRUE;

    repr = AGlobal(t, "std::Repr");
    
    frame[1] = AMemberDirect(frame[0], MAP_A);
    n = AFixArrayLen(frame[1]);
    frame[2] = AMakeArray(t, 0);
    str = AMakeStr(t, "Map(");
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
            str = AMakeStr(t, " : ");
            AAppendArray(t, frame[2], str);
            frame[3] = AFixArrayItem(frame[1], i + 1);
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


/* Map iterator() */
AValue AMapIter(AThread *t, AValue *frame)
{
    frame[0] = AMemberDirect(frame[0], MAP_A);
    return ACallValue(t, AGlobalByNum(AMapIterNum), 1, frame);
}


/* Map iterator constructor */
AValue AMapIterCreate(AThread *t, AValue *frame)
{
    AExpectFixArray(t, frame[1]);
    ASetMemberDirect(t, frame[0], ITER_I, AZero);
    ASetMemberDirect(t, frame[0], ITER_LEN,
                     AIntToValue(AFixArrayLen(frame[1])));
    ASetMemberDirect(t, frame[0], ITER_A, frame[1]);
    return frame[0];
}


/* Map iterator hasNext() */
AValue AMapIterHasNext(AThread *t, AValue *frame)
{
    Assize_t i = AValueToInt(AMemberDirect(frame[0], ITER_I));
    Assize_t n = AValueToInt(AMemberDirect(frame[0], ITER_LEN));
    AValue a = AMemberDirect(frame[0], ITER_A);

    for (; i < n - 1; i += 2) {
        if (AFixArrayItem(a, i) != EmptyMarker &&
            AFixArrayItem(a, i) != RemovedMarker) {
            AValueToInstance(frame[0])->member[ITER_I] = AIntToValue(i);
            return ATrue;
        }
    }
    
    return AFalse;
}


/* Map iterator next() */
AValue AMapIterNext(AThread *t, AValue *frame)
{
    Assize_t i = AValueToInt(AMemberDirect(frame[0], ITER_I));
    Assize_t n = AValueToInt(AMemberDirect(frame[0], ITER_LEN));
    AValue a = AMemberDirect(frame[0], ITER_A);

    /* Find the next non-empty item. */
    while (i < n - 1 && (AFixArrayItem(a, i) == EmptyMarker ||
                         AFixArrayItem(a, i) == RemovedMarker))
        i += 2;

    if (i < n - 1) {
        AValue res = AMakeTuple(t, 2);
        a = AMemberDirect(frame[0], ITER_A); /* Re-read, might have been gc */
        ASetTupleItemNewGen(res, 0, AFixArrayItem(a, i));
        ASetTupleItemNewGen(res, 1, AFixArrayItem(a, i + 1));
        AValueToInstance(frame[0])->member[ITER_I] = AMakeShortInt(i + 2);
        return res;
    } else
        return ARaiseValueError(t, "No items left");
}


/* Calculate the hash value of a long integer object. Assume v is a long
   integer. */
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
