/* bitop_module.c - bitop module (bitwise operations)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "int.h"
#include "gc.h"
#include "mem.h"
#include "internal.h"
#include "runtime.h"


static long IntLen(AValue v);
static AValue GetLongIntDigits(AThread *t, AValue *v, long len);
static AValue MakeIntFromBinary(AThread *t, AValue *buf, long len);
static void NegateSign(AValue v, long len);
static AValue RaiseIntExpected(AThread *t, AValue *frame);


#define MASK A_LONGINT_DIGIT_MASK
#define BITS A_LONG_INT_DIGIT_BITS


#define DIGIT(v, n) \
    (*(ALongIntDigit *)APtrAdd(AValueToStr(v)->elem, \
                                   (n) * sizeof(ALongIntDigit)))


#if A_VALUE_BITS == 64
#define AMakeIntFromValue AMakeInt64
#elif A_VALUE_BITS == 32
#define AMakeIntFromValue AMakeInt
#else
/* Unsupported value type */
error!;
#endif


/* bitop::And(n, m) */
static AValue BitopAnd(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0]) && AIsShortInt(frame[1]))
        return AMakeIntFromValue(t, AValueToInt(frame[0]) &
                                 AValueToInt(frame[1]));
    else if (AIsInt(frame[0]) && AIsInt(frame[1])) {
        long i, len;

        len = AMax(IntLen(frame[0]), IntLen(frame[1]));

        frame[0] = GetLongIntDigits(t, &frame[0], len);
        frame[1] = GetLongIntDigits(t, &frame[1], len);

        for (i = 0; i < len; i++)
            DIGIT(frame[0], i) &= DIGIT(frame[1], i);

        return MakeIntFromBinary(t, &frame[0], len);
    } else
        return RaiseIntExpected(t, frame);
}


/* bitop::Or(n, m) */
static AValue BitopOr(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0]) && AIsShortInt(frame[1]))
        return AMakeIntFromValue(t, AValueToInt(frame[0]) |
                                 AValueToInt(frame[1]));
    else if (AIsInt(frame[0]) && AIsInt(frame[1])) {
        long i, len;

        len = AMax(IntLen(frame[0]), IntLen(frame[1]));

        frame[0] = GetLongIntDigits(t, &frame[0], len);
        frame[1] = GetLongIntDigits(t, &frame[1], len);

        for (i = 0; i < len; i++)
            DIGIT(frame[0], i) |= DIGIT(frame[1], i);

        return MakeIntFromBinary(t, &frame[0], len);
    } else
        return RaiseIntExpected(t, frame);
}


/* bitop::Xor(n, m) */
static AValue BitopXor(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0]) && AIsShortInt(frame[1]))
        return AMakeIntFromValue(t, AValueToInt(frame[0]) ^
                                 AValueToInt(frame[1]));
    else if (AIsInt(frame[0]) && AIsInt(frame[1])) {
        long i, len;

        len = AMax(IntLen(frame[0]), IntLen(frame[1]));

        frame[0] = GetLongIntDigits(t, &frame[0], len);
        frame[1] = GetLongIntDigits(t, &frame[1], len);

        for (i = 0; i < len; i++)
            DIGIT(frame[0], i) ^= DIGIT(frame[1], i);

        return MakeIntFromBinary(t, &frame[0], len);
    } else
        return RaiseIntExpected(t, frame);
}


/* bitop::Xor(n) */
static AValue BitopNeg(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0]))
        return AMakeIntFromValue(t, ~AValueToInt(frame[0]));
    else if (AIsLongInt(frame[0])) {
        long i, len;
        
        len = IntLen(frame[0]);
        frame[0] = GetLongIntDigits(t, &frame[0], len);
        
        for (i = 0; i < len; i++)
            DIGIT(frame[0], i) = ~DIGIT(frame[0], i);
        
        return MakeIntFromBinary(t, &frame[0], len);
    } else
        return ARaiseIntExpected(t, frame[0]);
}


#define IsShlOverflow(i, s) \
    ((s) >= A_VALUE_BITS || ((ASignedValue)(i) << (s) >> (s)) != (i))


/* bitop::Shl(n, m) */
static AValue BitopShl(AThread *t, AValue *frame)
{
    long shift, len;
    long digitShift, i;
    
    if (AIsShortInt(frame[1])) {
        shift = AValueToInt(frame[1]);
        if (shift < 0)
            ARaiseValueError(t, "Negative shift count");
    } else if (AIsLongInt(frame[1])) {
        if (AGetLongIntSign(AValueToLongInt(frame[1])))
            ARaiseValueError(t, "Negative shift count");
        shift = AGetInt(t, frame[1]);
    } else
        return ARaiseIntExpected(t, frame[1]);
    
    if (AIsShortInt(frame[0]) && !IsShlOverflow(frame[0], shift))
        return frame[0] << shift;
    else if (!AIsInt(frame[0]))
        return ARaiseIntExpected(t, frame[0]);

    digitShift = shift / BITS;
    shift &= (BITS - 1);
    len = IntLen(frame[0]) + digitShift;
    frame[0] = GetLongIntDigits(t, &frame[0], len);

    for (i = len - 1; i >= digitShift; i--) {
        long v = (ALongIntDoubleDigit)(DIGIT(frame[0],
                                            i - digitShift) << shift) & MASK;
        if (i > digitShift)
            v |= (ALongIntDoubleDigit)DIGIT(
                frame[0],  i - digitShift - 1) >> (BITS - shift);
        DIGIT(frame[0], i) = v;
    }
    for (; i >= 0; i--)
        DIGIT(frame[0], i) = 0;

    return MakeIntFromBinary(t, &frame[0], len);
}


/* bitop::Shr(n, m) */
static AValue BitopShr(AThread *t, AValue *frame)
{
    long shift, len;
    long digitShift, i;
    ABool isNeg;
    
    if (AIsShortInt(frame[1])) {
        shift = AValueToInt(frame[1]);
        if (shift < 0)
            ARaiseValueError(t, "Negative shift count");
    } else if (AIsLongInt(frame[1])) {
        if (AGetLongIntSign(AValueToLongInt(frame[1])))
            ARaiseValueError(t, "Negative shift count");
        /* If we have REALLY large integers, Shr might not work with them (the
           correct result below might be larger than 0). But since it is
           completely impractical to work with integers having 64MB+ length on
           32-bit systems where this is an issue, the problem is trivial. */
        if (AIsInt(frame[0]))
            return AZero;
        shift = 0; /* The value does not matter since a TypeError will be
                      raised. */
    } else
        return ARaiseIntExpected(t, frame[1]);

    if (AIsShortInt(frame[0]) && shift < A_VALUE_BITS)
        return AIntToValue(AValueToInt(frame[0]) >> shift);
    else if (!AIsInt(frame[0]))
        return ARaiseIntExpected(t, frame[0]);

    digitShift = shift / BITS;
    shift &= (BITS - 1);
    len = IntLen(frame[0]);
    frame[0] = GetLongIntDigits(t, &frame[0], len);
    isNeg = DIGIT(frame[0], len - 1) & (1UL << (BITS - 1));

    for (i = 0; i < len - digitShift; i++) {
        long v = (DIGIT(frame[0], i + digitShift) >> shift) & MASK;
        if (i < len - digitShift - 1)
            v |= (ALongIntDoubleDigit)DIGIT(
                frame[0], i + digitShift + 1) << (BITS - shift);
        else if (isNeg)
            v |= (ALongIntDoubleDigit)MASK << (BITS - shift);
        DIGIT(frame[0], i) = v;
    }
    for (; i < len; i++)
        DIGIT(frame[0], i) = isNeg ? MASK : 0;

    return MakeIntFromBinary(t, &frame[0], len);
}


#define DIGITS_IN_INT (A_VALUE_BITS / BITS)


/* Return the number of digits needed to represent an Int value in 2's
   complement form. */
static long IntLen(AValue v)
{
    if (AIsShortInt(v))
        return DIGITS_IN_INT;
    else {
        /* A long Int. Reserve an extra digit for the sign (the Int object
           representation has a separate sign bit).
           IDEA: This is really only be needed if the most significant bit is
                 1? */ 
        return AGetLongIntLen(AValueToLongInt(v)) + 1;
    }
}


/* Return a narrow string object containing a 2's complement representation of
   an Int object *v with len "digits" (each A_LONG_INT_DIGIT_BITS in size).
   Note: len must be large enough for *v, including one bit for the sign. */
static AValue GetLongIntDigits(AThread *t, AValue *v, long len)
{
    AValue dst = AMakeEmptyStr(t, sizeof(ALongIntDigit) * len);
    long i;
    
    if (AIsShortInt(*v)) {
        ASignedValue ii = AValueToInt(*v);
        
        for (i = 0; i < DIGITS_IN_INT; i++)
            DIGIT(dst, i) = (ii >> (i * BITS)) & MASK;
        for (; i < len; i++)
            DIGIT(dst, i) = (ASignedValue)*v < 0 ? MASK : 0;
    } else {
        ALongInt *li = AValueToLongInt(*v);
        long ilen = AGetLongIntLen(li);
        for (i = 0; i < ilen; i++)
            DIGIT(dst, i) = li->digit[i];
        for (i = 0; i < len - ilen; i++)
            DIGIT(dst, ilen + i) = 0;

        if (AGetLongIntSign(li))
            NegateSign(dst, len);
    }

    return dst;
}


/* Create an Int object based on a binary Str 2's complement representation. */
static AValue MakeIntFromBinary(AThread *t, AValue *v, long len)
{
    ABool isNeg = FALSE;
    ALongInt *ptr;
    long i;
    
    if (DIGIT(*v, len - 1) & (1UL << (BITS - 1))) {
        /* Negative result. */
        isNeg = TRUE;
        NegateSign(*v, len);
    }

    ptr = AAlloc(t, AGetBlockSize(AGetLongIntSize(len)));
    if (ptr == NULL)
        ARaiseMemoryError(t);
    
    AInitLongIntBlock(ptr, len, FALSE);
    
    for (i = 0; i < len; i++)
        ptr->digit[i] = DIGIT(*v, i);
    
    return ANormalize(t, ptr, isNeg);
}


/* Perform a 2's complement negation for a positive long integer (represented
   as a Str buffer). */
static void NegateSign(AValue v, long len)
{
    long i = 0;
    /* Decrement by 1. Note that since we assume the integer to be positive,
       this will terminate. */
    while (DIGIT(v, i)-- == 0)
        i++;
    /* Perform bitwise negation. */
    for (i = 0; i < len; i++)
        DIGIT(v, i) = ~DIGIT(v, i);
}


/* Raise a TypeError if frame[0] or frame[1] is not an Int. */
static AValue RaiseIntExpected(AThread *t, AValue *frame)
{
    if (!AIsInt(frame[0]))
        return ARaiseIntExpected(t, frame[0]);
    else
        return ARaiseIntExpected(t, frame[1]);
}



A_MODULE(bitop, "bitop")
    A_DEF("And", 2, 0, BitopAnd)
    A_DEF("Or", 2, 0, BitopOr)
    A_DEF("Xor", 2, 0, BitopXor)
    A_DEF("Neg", 1, 0, BitopNeg)
    A_DEF("Shl", 2, 0, BitopShl)
    A_DEF("Shr", 2, 0, BitopShr)
A_END_MODULE()
