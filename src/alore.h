/* alore.h - Alore C API main header file

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ALORE_H_INCL
#define ALORE_H_INCL

#ifdef __cplusplus
extern "C" {
#endif

#include "value.h"
#include "module.h"
#include "thread.h"
#include "globals.h"
#include "errmsg.h"


/* AZero defined in value.h */


/* Global variable operations */
A_APIFUNC AValue AGlobal(AThread *t, const char *name);
/* AGlobalByNum defined in globals.h */
/* ASetGlobalByNum defined in globals.h */
A_APIFUNC void ASetConstGlobalByNum(AThread *t, int num, AValue v);
A_APIFUNC int AGetGlobalNum(AThread *t, const char *name);

/* Int operations */
/* A_VALUE_BITS defined in aconfig.h */
#define AIsInt(v) (AIsShortInt(v) || AIsLongInt(v))
/* AIsShortInt defined in value.h */
/* AIsLongInt defined in value.h */
A_APIFUNC int AGetInt(AThread *t, AValue v);
A_APIFUNC unsigned AGetIntU(AThread *t, AValue v);
A_APIFUNC AInt64 AGetInt64(AThread *t, AValue v);
A_APIFUNC AIntU64 AGetIntU64(AThread *t, AValue v);
#define AExpectInt(t, v) \
    do { \
        if (!AIsInt(v)) \
            ARaiseTypeError(t, AMsgIntExpectedBut, v);   \
    } while (0)
A_APIFUNC AValue AMakeInt(AThread *t, int i);
A_APIFUNC AValue AMakeIntU(AThread *t, unsigned i);
A_APIFUNC AValue AMakeInt64(AThread *t, AInt64 i);
A_APIFUNC AValue AMakeIntU64(AThread *t, AIntU64 i);

/* Helper definitions for creating Int from Asize_t/Assize_t value. */
/* IDEA: The #if might not work on all platforms. */
#if SIZEOF_SIZE_T == 8
#define AMakeInt_size_t AMakeIntU64
#define AMakeInt_ssize_t AMakeInt64
#define AGetInt_size_t AGetIntU64
#define AGetInt_ssize_t AGetInt64
#else
#define AMakeInt_size_t AMakeIntU
#define AMakeInt_ssize_t AMakeInt
#define AGetInt_size_t AGetIntU
#define AGetInt_ssize_t AGetInt
#endif

/* Long int operations (only for Int objects that do not fit in 30 bits signed
   in 32-bit architectures, or 62 bits signed in 64-bit architectures) */
/* A_LONG_INT_DIGIT_BITS defined in aconfig.h */
#define AIsNegLongInt(intVal) \
    AGetLongIntSign(AValueToLongInt(intVal))
#define ALongIntLen(intVal) \
    AGetLongIntLen(AValueToLongInt(intVal))
#define AGetLongIntDigit(intVal, n) \
    AValueToLongInt(intVal)->digit[n]
A_APIFUNC AValue AMakeLongInt(AThread *t, int len, ABool isNeg);
#define ASetLongIntDigit(intVal, n, digitValue) \
    AValueToLongInt(intVal)->digit[n] = (digitValue)

/* Float operations */
/* AIsFloat defined in value.h */
#define AGetFloat(t, v) (AIsFloat(v) ? AValueToFloat(v) : AGetFloat_(t, v))
#define AExpectFloat(t, v) \
    do { \
        if (!AIsFloat(v)) \
            ARaiseTypeError(t, AMsgFloatExpected); \
    } while (0)
A_APIFUNC AValue AMakeFloat(AThread *t, double f);
A_APIFUNC ABool AIsInf(double f);
A_APIFUNC ABool AIsNaN(double f);

/* Str operations */
/* AIsStr defined in value.h */
A_APIFUNC Assize_t AStrLen(AValue v);
A_APIFUNC Assize_t AStrLenUtf8(AValue v);
A_APIFUNC Assize_t AGetStr(AThread *t, AValue s, char *buf, Assize_t bufLen);
A_APIFUNC Assize_t AGetStrW(AThread *t, AValue s, AWideChar *buf,
                            Assize_t bufLen);
A_APIFUNC Assize_t AGetStrUtf8(AThread *t, AValue s, char *buf,
                               Assize_t bufLen);
#define AExpectStr(t, v) \
    do { \
        if (!AIsStr(v)) \
            ARaiseTypeError(t, AMsgStrExpectedBut, v); \
    } while (0)
A_APIFUNC AValue AMakeStr(AThread *t, const char *str);
A_APIFUNC AValue AMakeStrW(AThread *t, const AWideChar *str);
A_APIFUNC AValue AMakeStrUtf8(AThread *t, const char *str);
A_APIFUNC AValue AMakeEmptyStr(AThread *t, Assize_t len);
A_APIFUNC AValue AMakeEmptyStrW(AThread *t, Assize_t len);
A_APIFUNC AValue AToStr(AThread *t, AValue o);
#define AStrItem(v, index) \
    (AIsNarrowStr(v) ? AGetStrElem(v)[(index)] : AStrItem_(v, index))
A_APIFUNC AWideChar AStrItem_(AValue v, Assize_t index);
A_APIFUNC void ASetStrItem(AValue v, Assize_t index, AWideChar ch);
/* NOTE: Only works for strings just created using AMakeEmptyStr, and any
        function call potentially allocating memory invalidates the value. */
#define AStrPtr(s) (AValueToStr(s)->elem)
/* NOTE: Only works for strings just created using AMakeEmptyStrW, and any
         function call potentially allocating memory invalidates the value. */
#define AStrPtrW(s) (AValueToWideStr(s)->elem)
A_APIFUNC AValue ASubStr(AThread *t, AValue s, Assize_t i1, Assize_t i2);
A_APIFUNC AWideChar AGetCh(AThread *t, AValue ch);
A_APIFUNC AValue AMakeCh(AThread *t, AWideChar ch);

/* Array operations */
#define AIsArray(v) \
    (AIsInstance(v) && AGetInstanceType(AValueToInstance(v)) == AArrayClass)
#define AArrayLen(v) \
    (AValueToInt(AValueToInstance(v)->member[A_ARRAY_LEN]))
#define AArrayItem(v, i) \
    AFixArrayItem(AValueToInstance(v)->member[A_ARRAY_A], i)
#define AIsArraySubType(v) \
    (AIsArray(v) || AIsOfType(v, ATypeToValue(AArrayClass)) == A_IS_TRUE)
#define AExpectArray(t, v) \
    do { \
        if (!AIsArray(v)) \
            ARaiseTypeError(t, AMsgArrayExpected); \
    } while (0)
A_APIFUNC void ASetArrayItem(AThread *t, AValue a, Assize_t index, AValue v);
A_APIFUNC AValue AMakeArray(AThread *t, Assize_t len);
A_APIFUNC AValue ASubArray(AThread *t, AValue a, Assize_t i1, Assize_t i2);

/* FixArray operations */
/* IDEA: Several operations are missing... These are not very useful. */
#define AExpectFixArray(t, v) \
    do { \
        if (!AIsFixArray(v)) \
            ARaiseTypeError(t, NULL); \
    } while (0)
A_APIFUNC void ASetFixArrayItem(AThread *t, AValue a, Assize_t index,
                                AValue v);

/* Incremental array building operations */
A_APIFUNC void AAppendArray(AThread *t, AValue array, AValue item);

/* Tuple operations */
#define AIsTuple(v) \
    (AIsInstance(v) && AGetInstanceType(AValueToInstance(v)) == ATupleClass)
#define ATupleLen(v) \
    (AValueToInt(AValueToInstance(v)->member[A_ARRAY_LEN]))
#define ATupleItem(v, i) \
    AFixArrayItem(AValueToInstance(v)->member[A_ARRAY_A], i)
A_APIFUNC AValue AMakeTuple(AThread *t, Assize_t len);
A_APIFUNC AValue AInitTupleItem(AThread *t, AValue v, Assize_t index,
                                AValue item);

/* Array / Tuple operations */
#define AIsArrayOrTuple(v) (AIsTuple(v) || AIsArray(v))
#define AArrayOrTupleLen(v) AArrayLen(v)
#define AArrayOrTupleItem(v, i) AArrayItem(v, i)

/* Constant operations */
/* AIsConstant defined in value.h */

/* Pair operations */
/* AIsPair defined in value.h */
A_APIFUNC void AGetPair(AThread *t, AValue pair, AValue *v1, AValue *v2);
A_APIFUNC AValue AMakePair(AThread *t, AValue v1, AValue v2);

/* Range operations */
/* AIsRange defined in value.h */
A_APIFUNC void AGetRange(AThread *t, AValue range, AValue *lo, AValue *hi);
A_APIFUNC AValue AMakeRange(AThread *t, AValue lo, AValue hi);

/* Exception operations */
A_APIFUNC AValue ARaise(AThread *t, const char *type, const char *msg);
A_APIFUNC AValue ARaiseByType(AThread *t, AValue type, const char *msg);
A_APIFUNC AValue ARaiseByNum(AThread *t, int classNum,
                                 const char *message); /* FIX rename */
A_APIFUNC AValue ARaiseValue(AThread *t, AValue v);
A_APIFUNC AValue ARaiseMemoryError(AThread *t);
A_APIFUNC AValue ARaiseTypeError(AThread *t, const char *fmt, ...);
A_APIFUNC AValue ARaiseValueError(AThread *t, const char *fmt, ...);
A_APIFUNC AValue ARaiseKeyError(AThread *t, const char *fmt, ...);
A_APIFUNC AValue ARaiseIoError(AThread *t, const char *msg);
A_APIFUNC AValue ARaiseRuntimeError(AThread *t, const char *msg);
A_APIFUNC AValue ARaiseIndexError(AThread *t, const char *msg);
A_APIFUNC AValue ARaiseErrnoException(AThread *t, const char *type);
A_APIFUNC AValue ARaiseErrnoExceptionByNum(AThread *t, int classNum,
                                           const char *path);
A_APIFUNC void ADispatchException(AThread *t);
#define ATry(t) \
    (AHandleException(t) ? (t)->contextIndex--, TRUE : FALSE)
#define AEndTry(t) ((t)->contextIndex--)
#define AGetException(t) ((t)->exception)
A_APIFUNC ABool AIsExceptionType(AThread *t, const char *type);

/* Temporary value operations */
A_APIFUNC AValue *AAllocTemps(AThread *t, int n);
#define AFreeTemps(t, n) \
    ((t)->tempStackPtr -= (n))
#define AAllocTemp(t, v) \
    ((t)->tempStackEnd - (t)->tempStackPtr >= 1 + 3 \
     ? (*(t)->tempStackPtr = (v), (t)->tempStackPtr++) \
     : (ARaiseMemoryError(t), (AValue *)NULL))
#define AFreeTemp(t) AFreeTemps(t, 1)

/* Function and call operations */
A_APIFUNC AValue ACall(AThread *t, const char *callable, int numArgs,
                       AValue *args);
#define ACallVarArg(t, f, fixedArgs, args) \
    ACall(t, f, (fixedArgs + 1) | A_VAR_ARG_FLAG, args)
A_APIFUNC AValue ACallValue(AThread *t, AValue funcVal, int numArgs,
                               AValue *args);
#define ACallValueVarArg(t, f, fixedArgs, args) \
    ACallValue(t, f, (fixedArgs + 1) | A_VAR_ARG_FLAG, args)
#define AIsFunction(v) (AIsGlobalFunction(v) || AIsMethod(v))

/* Type operations */
/* Test type, return boolean (0/1) */
A_APIFUNC ABool AIsType(AValue v);
A_APIFUNC ABool AIsValue(AThread *t, AValue v, AValue type);

/* Basic gc-aware memory allocation operations for C data */
A_APIFUNC AValue AAllocMem(AThread *t, Asize_t size);
A_APIFUNC AValue AAllocMemFixed(AThread *t, Asize_t size);
A_APIFUNC AValue AReallocMem(AThread *t, AValue mem, Asize_t newSize);
A_APIFUNC void AFreeMem(AThread *t, AValue mem);
A_APIFUNC void *AMemPtr(AValue mem);

/* Gc-aware growable buffer for C data */
A_APIFUNC AValue AAllocBuf(AThread *t, Asize_t initReserve);
A_APIFUNC AValue AAllocBufFixed(AThread *t, Asize_t initReserve);
A_APIFUNC void AAppendBuf(AThread *t, AValue buf, const char *data,
                          Asize_t len);
A_APIFUNC void *AReserveBuf(AThread *t, AValue buf, Asize_t len);
A_APIFUNC void AFreeBuf(AThread *t, AValue buf);
A_APIFUNC void *ABufPtr(AValue buf);

A_APIFUNC AValue AAllocContainer(AThread *t, AValue initValue);
A_APIFUNC void *AContainerPtr(AValue container);
A_APIFUNC AValue AContainerValue(void *ptr);
A_APIFUNC void ASetContainerValue(AThread *t, void *ptr, AValue item);

/* The comparison functions below return 0 for false, 1 for true, and negative
   for error. A negative value can only be returned if at least one of the
   parameters is a class instance; exceptions for built-in types are raised as
   direct exceptions. */

/* Is (a == b)? */
A_APIFUNC int AIsEq(AThread *t, AValue a, AValue b);
/* Is (a <> b)? */
#define AIsNeq(t, a, b) (AIsEq(t, a, b) ^ 1)
/* Is (a < b)? */
A_APIFUNC int AIsLt(AThread *t, AValue a, AValue b);
/* Is (a > b)? */
A_APIFUNC int AIsGt(AThread *t, AValue a, AValue b);
/* Is (a >= b)? */
#define AIsGte(t, a, b) (AIsLt(t, a, b) ^ 1)
/* Is (a <= b)? */
#define AIsLte(t, a, b) (AIsGt(t, a, b) ^ 1)

/* Sequence / collection operations */

/* Return the length of s. Return a negative value on error. */
A_APIFUNC Assize_t ALen(AThread *t, AValue s);
/* Return the item at the specified integer index. */
A_APIFUNC AValue AGetItemAt(AThread *t, AValue s, Assize_t i);
/* Set the item at the specified integer index. */
A_APIFUNC AValue ASetItemAt(AThread *t, AValue s, Assize_t i, AValue o);
/* Same as v.iterator() */
A_APIFUNC AValue AIterator(AThread *t, AValue v);
/* If the iteration e has items left, store the next value in the iteration
   in *next and return 1. If there are no items left, return 0. If there was
   an error, return -1. */
A_APIFUNC int ANext(AThread *t, AValue e, AValue *next);
/* Concatenate two sequences. */
A_APIFUNC AValue AConcat(AThread *t, AValue s1, AValue s2);

/* Instance operations */
/* AIsInstance defined in value.h */
A_APIFUNC AValue AMember(AThread *t, AValue o, const char *member);
A_APIFUNC AValue ASetMember(AThread *t, AValue o, const char *member,
                            AValue v);
A_APIFUNC AValue ASuperMember(AThread *t, AValue *frame, const char *member);
A_APIFUNC AValue ASetSuperMember(AThread *t, AValue *frame, const char *member,
                                 AValue v);
/* A_APIFUNC ABool AHasMember(AValue o, const char *member); */

A_APIFUNC AValue ACallMethod(AThread *t, const char *member, int nargs,
                             AValue *args);
#define ACallMethodVarArg(t, member, fixedArgs, args) \
    ACallMethod(t, member, (fixedArgs + 1) | A_VAR_ARG_FLAG, args)
/* IDEA: Add ACallMethodByNum[VarArg] or similar */

#define AMemberDirect(inst, memberIndex) \
    (AValueToInstance(inst)->member[memberIndex])
A_APIFUNC void ASetMemberDirect(AThread *t, AValue o, int num, AValue v);

#define ADataPtr(inst, offset) \
    APtrAdd(AValueToInstance(inst), \
           AGetInstanceType(AValueToInstance(inst))->dataOffset + (offset))
#define ASetData_M(inst, offset, type, value) \
    (*(type *)ADataPtr(inst, offset) = (value))
#define AGetData_M(inst, offset, type) \
    (*(type *)ADataPtr(inst, offset))

A_APIFUNC int AMemberNum(AThread *t, const char *member);
A_APIFUNC AValue AMemberByNum(AThread *t, AValue inst, int member);
A_APIFUNC AValue ASetMemberByNum(AThread *t, AValue o, int member, AValue v);
A_APIFUNC ABool AHasMemberByNum(AValue o, int member);

A_APIFUNC AValue AMakeUninitializedObject(AThread *t, AValue type);

A_APIFUNC ABool ASetExternalDataSize(AThread *t, AValue obj, Asize_t size);

/* Generic operations */
A_APIFUNC AValue AAdd(AThread *t, AValue a, AValue b);
A_APIFUNC AValue ASub(AThread *t, AValue a, AValue b);
A_APIFUNC AValue AMul(AThread *t, AValue a, AValue b);
A_APIFUNC AValue ADiv(AThread *t, AValue a, AValue b);
A_APIFUNC AValue AIntDiv(AThread *t, AValue a, AValue b);
A_APIFUNC AValue AMod(AThread *t, AValue a, AValue b);
A_APIFUNC AValue APow(AThread *t, AValue a, AValue b);
A_APIFUNC AValue ANeg(AThread *t, AValue a);
A_APIFUNC AValue AGetItem(AThread *t, AValue s, AValue index);
A_APIFUNC AValue ASetItem(AThread *t, AValue s, AValue index, AValue o);

/* Return 1 for true, 0 for false and -1 for error. */
A_APIFUNC int AIn(AThread *t, AValue a, AValue b);

/* Return the hash value of an object as an Int value. May raise normal
   exceptions. */
A_APIFUNC AValue AHash(AThread *t, AValue object);

/* Unofficial operations that rely on implementation details - these may not be
   portable! */
#define AMakeShortInt(i) AIntToValue(i)

/* Thread operations */
A_APIFUNC void AAllowBlocking(void);
A_APIFUNC void AEndBlocking(void);


/* Internal functions and definitions - not part of the public API! 
   IDEA: These could be moved to a separate header file for clarity. */


/* FIX: Document this, and decide if this is part of the public API. Maybe
        this functionality could be provided by an Alore-accessible module? */
A_APIFUNC AValue AAddExitHandler(AThread *t, AValue *args);

A_APIFUNC AValue ACallMethodByNum(AThread *t, int member, int numArgs,
                                  AValue *args);

/* Report an unrecoverable error. Write an error message to stderr and halt the
   process immediately. */
A_APIFUNC AValue AEpicInternalFailure(const char *message);

/* Return TRUE if the program currently being executed is standalone, i.e.
   compiled using alorec. */
A_APIFUNC ABool AIsStandalone(void);

A_APIFUNC AValue ARaiseMemoryErrorND(AThread *t);
A_APIFUNC AValue ARaisePreallocatedMemoryErrorND(AThread *t);
A_APIFUNC AValue ARaiseTypeErrorND(AThread *t, const char *fmt, ...);
A_APIFUNC AValue ARaiseValueErrorND(AThread *t, const char *fmt, ...);
A_APIFUNC AValue ARaiseMemberErrorND(AThread *t, AValue inst, int member);
A_APIFUNC AValue ARaiseArithmeticErrorND(AThread *t, const char *msg);
A_APIFUNC AValue ARaiseArgumentErrorND(AThread *t, AValue func, int nargs,
                                       int numHidden);

A_APIFUNC AValue AWriteRepr(AThread *t, AValue val);


#define ACheckInterrupt(t) \
    (AIsInterrupt && AHandleInterrupt(t) ? TRUE : FALSE)


/* The following *private* definitions are needed by some API macros. */


A_APIFUNC double AGetFloat_(AThread *t, AValue v);

/* Pointer to the ATypeInfo structure of std::Array */
A_APIVAR extern ATypeInfo *AArrayClass;
/* Pointer to the ATypeInfo structure of std::Tuple */
A_APIVAR extern ATypeInfo *ATupleClass;


/* Macros for dealing with FixArray objects */

/* IDEA: Refactor these. Maybe some of these macros are not required to be
   defined here? */
#if A_VALUE_BITS == 32

#define AGetFixArrayItemPtr(a, intVal) \
    ((AValue *)APtrAdd(a, sizeof(AValue) + (intVal)))
#define AGetFixArrayItem(a, intVal) \
    (*AGetFixArrayItemPtr(a, intVal))
#define AGetFixArrayLen(a) \
    (((a)->header & ~A_NEW_GEN_FLAG) >> 2)

#elif A_VALUE_BITS == 64    

#define AGetFixArrayItemPtr(a, intVal) \
    ((AValue *)APtrAdd(a, sizeof(AValue) + (intVal) * 2))
#define AGetFixArrayItem(a, intVal) \
    (*AGetFixArrayItemPtr(a, intVal))
#define AGetFixArrayLen(a) \
    (((a)->header & ~A_NEW_GEN_FLAG) >> 3)
    
#else
    
#error not supported
    
#endif

#define AFixArrayLen(v) AGetFixArrayLen(AValueToFixArray(v))
#define AFixArrayItem(v, ind) \
    AGetFixArrayItem(AValueToFixArray(v), AIntToValue(ind))
#define AFixArrayItemV(v, indVal) \
    AGetFixArrayItem(AValueToFixArray(v), indVal)


/* Macros for dealing with Str objects */

#define AGetStrElem(s) \
    (AValueToStr(s)->elem)


#ifdef __cplusplus
}
#endif

#endif
