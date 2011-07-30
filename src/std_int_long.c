/* std_int_long.c - Arbitrary precision Int operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "int.h"
#include "value.h"
#include "gc.h"
#include "globals.h"
#include "mem.h"
#include "float.h"
#include "internal.h"
#include "alore.h"
#include "runtime.h"

#include <math.h>


/* Many of the functions may return non-normalized long integer objects (i.e.
   objects which could be represented as short ints or objects with 0 digits
   as most significant digits). User code should never see these; high-level
   functions will normalize these into short integers.
   
   The comments do not always indicate whether the result is non-normalized. */

/* These functions do not raise direct exceptions, unless explicitly mentioned
   otherwise. Some parts of the interpreter may assume that no direct
   exceptions are raised, so be careful with this when updating the code. */

/* NOTE: These routines are not optimized for speed. */


/* Define some alias macros for commonly used constants and macros. */

#define BITS A_LONG_INT_DIGIT_BITS
#define BASE A_LONGINT_BASE
#define MASK A_LONGINT_DIGIT_MASK
#define DIGITS_IN_INT (A_VALUE_BITS / BITS)

#define Len(l) AGetLongIntLen(l)
#define Sign(l) AGetLongIntSign(l)
#define Size(n) AGetLongIntSize(n)
#define SetSign(l) ASetLongIntSign(l)

#define DoubleDigit ALongIntDoubleDigit
#define Digit ALongIntDigit


#if DIGITS_IN_INT > 2
#error ALongIntDigit requires more bits
#endif


static ALongInt *CreateLongInt(AThread *t, long len, ALongInt **a,
                               ALongInt **b);

static AValue AddAbsolute(AThread *t, ALongInt *a, ALongInt *b,
                          ABool isNorm);
static AValue SubAbsolute(AThread *t, ALongInt *a, ALongInt *b,
                          ABool isNorm);
static AValue AddAbsoluteSingle(AThread *t, ALongInt *a, unsigned add);
static AValue SubAbsoluteSingle(AThread *t, ALongInt *a, unsigned sub,
                                ABool isNorm);
static ALongInt *TruncateZeroDigits(AThread *t, ALongInt *li, ABool isNeg);


#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 32

/* NOTE: This alternative implementation (for 32-bit values and 32-bit
         long int digits) has not been tested! */

/* Convert a fixed-width integer to a long integer object. This function may
   create non-normalized long integers that could be represented as short
   ints. */ 
AValue ACreateLongIntFromIntND(AThread *t, ASignedValue intVal)
{
    ALongInt *li;
    ABool result;

    if (intVal >= -0x7fffffff) {
        AAlloc_M(t, AGetBlockSize(Size(1)), li, result);
        if (!result)
            return ARaiseMemoryErrorND(t);
        
        if (intVal < 0) {
            AInitLongIntBlock(li, 1, TRUE);
            intVal = -intVal;
        } else
            AInitLongIntBlock(li, 1, FALSE);
        
        li->digit[0] = intVal;
    } else {
        li = AAlloc(t, AGetBlockSize(Size(2)));
        if (li == NULL)
            return ARaiseMemoryErrorND(t);
        AInitLongIntBlock(li, 2, TRUE);
        li->digit[0] = 0;
        li->digit[1] = 1;
    }

    return ALongIntToValue(li);
}

#elif (A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16) || \
    (A_VALUE_BITS == 64 && A_LONG_INT_DIGIT_BITS == 32)

/* Convert a fixed-width integer to a long integer object. This function may
   create non-normalized long integers that could be represented as short
   ints. */ 
AValue ACreateLongIntFromIntND(AThread *t, ASignedValue intVal)
{
    ALongInt *li;
    ABool result;
    int len;
    ABool sign;

    /* The negative value -2^31 / -2^63 requires special processing, since the
       positive value with the same magnitude cannot be represented using a
       ASignedValue. */
    if (intVal == -1L << (A_VALUE_BITS - 1)) {
        li = AAlloc(t, AGetBlockSize(Size(2)));
        if (li == NULL)
            return ARaiseMemoryErrorND(t);
        AInitLongIntBlock(li, 2, TRUE);
        li->digit[0] = 0;
        li->digit[1] = 1L << (A_LONG_INT_DIGIT_BITS - 1);
    } else {
        if (intVal < 0) {
            intVal = -intVal;
            sign = TRUE;
        } else
            sign = FALSE;
        
        if (intVal > MASK)
            len = 2;
        else
            len = 1;
        
        AAlloc_M(t, AGetBlockSize(Size(len)), li, result);
        if (!result)
            return ARaiseMemoryErrorND(t);
        
        AInitLongIntBlock(li, len, sign);
        
        li->digit[0] = intVal & MASK;
        if (len > 1)
            li->digit[1] = intVal >> BITS;
    }
    
    return ALongIntToValue(li);
}

#else

/* Unsupported combination of A_VALUE_BITS and A_LONG_INT_DIGIT_BITS. */
error!;

#endif


/* Compare two long integers. Assume that the arguments are *normalized*
   long integers. The return value is
     * positive if aVal > bVal
     * 0 if aVal == bVal
     * negative if aVal < bVal. */
ALongIntSignedDoubleDigit ACompareLongInt(AValue aVal, AValue bVal)
{
    ALongInt *a;
    ALongInt *b;
    long lenA;
    long lenB;
    int signA;
    ALongIntSignedDoubleDigit result;

    a = AValueToLongInt(aVal);
    b = AValueToLongInt(bVal);

    lenA = Len(a);
    lenB = Len(b);

    signA = Sign(a);

    /* Perform non-trivial comparison if the signs are equal. */
    if (signA == Sign(b)) {
        /* If lengths are different, use a shortcut. */
        if (lenA != lenB)
            result = lenA - lenB;
        else {
            /* Perform digit-wise comparison when signs and lengths are
               equal. */
            int i;

            result = 0;

            for (i = lenA - 1; i >= 0; i--) {
                if (a->digit[i] != b->digit[i]) {
                    result = (ALongIntSignedDoubleDigit)a->digit[i] -
                             b->digit[i];
                    break;
                }
            }
        }      
    } else
        result = 1;

    return signA ? -result : result;
}


/* Multiply two long integers. Assume that arguments are long integers. */
AValue AMultiplyLongInt(AThread *t, AValue aVal, AValue bVal)
{
    ALongInt *a;
    ALongInt *b;
    ALongInt *d;
    long lenA;
    long lenB;
    long lenD;
    long i, j;

    a = AValueToLongInt(aVal);
    b = AValueToLongInt(bVal);
    
    lenA = Len(a);
    lenB = Len(b);
    lenD = lenA + lenB;

    d = CreateLongInt(t, lenD, &a, &b);
    if (d == NULL)
        return AError;

    for (i = 0; i < lenB; i++)
        d->digit[i] = 0;
    
    for (i = 0; i < lenA; i++) {
        ALongIntDoubleDigit carry;
        
        carry = 0;
        for (j = 0; j < lenB; j++) {
            carry += d->digit[i + j] +
                (ALongIntDoubleDigit)a->digit[i] * b->digit[j];
            d->digit[i + j] = carry & A_LONGINT_DIGIT_MASK;
            carry >>= A_LONG_INT_DIGIT_BITS;
        }
        
        d->digit[i + j] = carry;
    }

    return ANormalize(t, d, AGetLongIntSign(a) ^ AGetLongIntSign(b));
}


/* Add two long integers. Assume that the arguments are long integers. */
AValue AAddLongInt(AThread *t, AValue aVal, AValue bVal)
{
    ALongInt *a;
    ALongInt *b;
    
    a = AValueToLongInt(aVal);
    b = AValueToLongInt(bVal);
    
    if (Sign(a) == Sign(b))
        return AddAbsolute(t, a, b, TRUE);
    else
        return SubAbsolute(t, a, b, TRUE);
}


/* Subtract two long integers. Assume that the arguments are long integers. */
AValue ASubLongInt(AThread *t, AValue aVal, AValue bVal)
{
    ALongInt *a;
    ALongInt *b;

    a = AValueToLongInt(aVal);
    b = AValueToLongInt(bVal);
    
    if (Sign(a) == Sign(b))
        return SubAbsolute(t, a, b, TRUE);
    else
        return AddAbsolute(t, a, b, TRUE);
}


/* Add the absolute values of two long integers which have the same signs.
   This can be used to add two positive/negative numbers or to subtract
   numbers with different signs. */
static AValue AddAbsolute(AThread *t, ALongInt *a, ALongInt *b,
                          ABool isNorm)
{
    unsigned long lenA;
    unsigned long lenB;
    unsigned long lenD;
    ALongInt *d;
    unsigned long carry;
    unsigned long i;

    /* We want that b is not longer than a. Swap a and b if needed to satisfy
       this condition. */
    if (Len(a) < Len(b)) {
        ALongInt *tmp = a;
        a = b;
        b = tmp;
    }

    lenA = Len(a);
    lenB = Len(b);
    /* The length of the result is the length of the longest integer + 1 extra
       digit (for carry). */
    lenD = lenA + 1;

    d = CreateLongInt(t, lenD, &a, &b);
    if (d == NULL)
        return AError;

    carry = 0;
    for (i = 0; i < lenB; i++) {
        carry += (ALongIntDoubleDigit)a->digit[i] + b->digit[i];
        d->digit[i] = carry & MASK;
        carry >>= BITS;
    }

    for (; i < lenA; i++) {
        carry += a->digit[i];
        d->digit[i] = carry & MASK;
        carry >>= BITS;
    }

    d->digit[i] = carry;

    if (isNorm)
        return ANormalize(t, d, Sign(a));
    else
        return ALongIntToValue(d);
}


/* Subtract the absolute values of two long integers. This can be used for
   subtracting two integers with the same sign or adding two integers with
   different signs. */
static AValue SubAbsolute(AThread *t, ALongInt *a, ALongInt *b,
                          ABool isNorm)
{
    unsigned long lenA;
    unsigned long lenB;
    ALongInt *d;
    int sign;
    unsigned long borrow;
    long i;

    lenA = Len(a);
    lenB = Len(b);

    sign = Sign(a);

    if (lenA < lenB) {
        ALongInt *tmpInt = a;
        unsigned long tmpLen = lenA;
        a = b;
        lenA = lenB;
        b = tmpInt;
        lenB = tmpLen;
        
        sign = !sign;
    } else if (lenA == lenB) {
        for (i = lenA - 1; i >= 0 && a->digit[i] == b->digit[i]; i--);
        
        if (i >= 0 && a->digit[i] < b->digit[i]) {
            ALongInt *tmpInt = a;
            a = b;
            b = tmpInt;
            
            sign = !sign;
        }
    }

    /* Now a is larger than b. */

    d = CreateLongInt(t, lenA, &a, &b);
    if (d == NULL)
        return AError;

    borrow = 0;
    for (i = 0; i < lenB; i++) {
        borrow = (ALongIntSignedDoubleDigit)a->digit[i] -
                 (ALongIntSignedDoubleDigit)b->digit[i] - borrow;
        d->digit[i] = borrow & MASK;
        borrow >>= 2 * BITS - 1;
    }

    for (; i < lenA; i++) {
        borrow = (ALongIntSignedDoubleDigit)a->digit[i] - borrow;
        d->digit[i] = borrow & MASK;
        borrow >>= 2 * BITS - 1;
    }

    if (isNorm)
        return ANormalize(t, d, sign);
    else
        return ALongIntToValue(d);
}


/* Negate a long integer. Assume that the argument is a long integer. */
AValue ANegateLongInt(AThread *t, AValue aVal)
{
    ALongInt *a;
    ALongInt *d;
    unsigned long lenA;
    long i;
    ABool signA;

    a = AValueToLongInt(aVal);
    lenA = Len(a);
    signA = Sign(a);

#if DIGITS_IN_INT == 1
    if (!signA && a->digit[0] == A_SHORT_INT_MAX + 1)
        return AIntToValue(A_SHORT_INT_MIN);
#else
    if (!signA && a->digit[0] == 0
        && a->digit[1] == (A_SHORT_INT_MAX + 1) >> BITS)
        return AIntToValue(A_SHORT_INT_MIN);
#endif

    d = CreateLongInt(t, lenA, &a, NULL);
    if (d == NULL)
        return AError;

    /* FIX: use block copy? */
    for (i = 0; i < lenA; i++)
        d->digit[i] = a->digit[i];
    
    if (!Sign(a))
        SetSign(d);

    return ALongIntToValue(d);
}


/* Add a single digit to a long integer. Assume that aVal is a long integer.
   NOTE: i must be positive (FIX: why??) */
AValue AAddLongIntSingle(AThread *t, AValue aVal, unsigned i)
{
    ALongInt *a;

    a = AValueToLongInt(aVal);

    if (Sign(a))
        return SubAbsoluteSingle(t, a, i, TRUE);
    else
        return AddAbsoluteSingle(t, a, i);
}


/* Subtract a single digit from a long integer. Assume that aVal is a long
   integer.
   NOTE: i must be positive (FIX: why??) */
AValue ASubLongIntSingle(AThread *t, AValue aVal, unsigned i)
{
    ALongInt *a;

    a = AValueToLongInt(aVal);

    if (Sign(a))
        return AddAbsoluteSingle(t, a, i);
    else
        return SubAbsoluteSingle(t, a, i, TRUE);
}


/* Add a digit to the absolute value of a long integer, preserving sign. */
static AValue AddAbsoluteSingle(AThread *t, ALongInt *a, unsigned add)
{
    unsigned long lenA;
    unsigned long lenD;
    ALongInt *d;
    Digit carry;
    long i;

    lenA = Len(a);
    lenD = lenA + 1;

    d = CreateLongInt(t, lenD, &a, NULL);
    if (d == NULL)
        return AError;

    carry = add;
    for (i = 0; i < lenA; i++) {
        Digit res = carry + a->digit[i];

        d->digit[i] = res;
        
        if (res >= a->digit[i]) {
            i++;
            carry = 0;
            break;
        }

        carry = 1;
    }

    for (; i < lenA; i++)
        d->digit[i] = a->digit[i];

    d->digit[i] = carry;

    return ANormalize(t, d, Sign(a));
}


/* Subtract a digit from the absolute value of a long integer. */
static AValue SubAbsoluteSingle(AThread *t, ALongInt *a, unsigned sub,
                                ABool isNorm)
{
    unsigned long lenA;
    ALongInt *d;
    Digit borrow;
    long i;

    lenA = Len(a);

    d = CreateLongInt(t, lenA, &a, NULL);
    if (d == NULL)
        return AError;

    borrow = sub;
    for (i = 0; i < lenA; i++) {
        Digit res = a->digit[i] - borrow;

        d->digit[i] = res;

        if (res <= a->digit[i]) {
            i++;
            borrow = 0;
            break;
        }

        borrow = 1;
    }

    for (; i < lenA; i++)
        d->digit[i] = a->digit[i];

    if (isNorm)
        return ANormalize(t, d, Sign(a));
    else
        return ALongIntToValue(d);
}


/* Calculate the division of a long integer and a digit. Return the result of
   the division and store the remainder at *remPtr.
   NOTE: b is assumed to be positive. Sign bit of a is ignored. */
ALongInt *ADivModSingle(AThread *t, ALongInt *a, unsigned b, unsigned *remPtr)
{
    long i;
    unsigned long lenA;
    ALongIntDoubleDigit rem;
    ALongInt *d;

    lenA = Len(a);

    d = CreateLongInt(t, lenA, &a, NULL);
    if (d == NULL)
        return NULL;

    rem = 0;
    for (i = lenA - 1; i >= 0; i--) {
        rem = (rem << BITS) + a->digit[i];
        d->digit[i] = rem / b;
        rem %= b;
    }

    *remPtr = rem;

    return TruncateZeroDigits(t, d, 0);
}


/* Multiply a long integer by a digit and add a digit to the result.
   NOTE: Both a, mul and add are assumed to be positive. */
ALongInt *AMulAddSingle(AThread *t, ALongInt *a, unsigned mul, unsigned add)
{
    unsigned long lenA;
    unsigned long lenD;
    ALongInt *d;
    ALongIntDoubleDigit carry;
    long i;

    lenA = Len(a);
    lenD = lenA + 1;
        
    d = CreateLongInt(t, lenD, &a, NULL);
    if (d == NULL)
        return NULL;

    carry = add;
    for (i = 0; i < lenA; i++) {
        carry += (ALongIntDoubleDigit)a->digit[i] * mul;
        d->digit[i] = carry & MASK;
        carry >>= BITS;
    }

    d->digit[i] = carry;

    return TruncateZeroDigits(t, d, 0);
}


/* Shift a long integer left by shift bits. Store the result at dst, modifying
   it in-place. Assume that dst is long enough to hold the result. */
static void Shl(ALongInt *src, ALongInt *dst, unsigned long shift)
{
    unsigned long wordShift;
    unsigned long bitShift;
    unsigned long srcLen;
    unsigned long dstLen;
    unsigned long carry;
    long i;
    
    wordShift = shift / BITS;
    bitShift = shift & (BITS - 1);

    srcLen = Len(src);
    dstLen = srcLen + wordShift;

    carry = (ALongIntDoubleDigit)src->digit[srcLen - 1] << bitShift; 
    if (carry > MASK) {
        dst->digit[dstLen] = carry >> BITS;
        dstLen++;
    }

    for (i = srcLen - 1; i > 0; i--) {
        carry <<= BITS;
        carry |= (ALongIntDoubleDigit)src->digit[i - 1] << bitShift;
        dst->digit[i + wordShift] = carry >> BITS;
    }

    dst->digit[wordShift] = carry & MASK;

    for (i = 0; i < wordShift; i++)
        dst->digit[i] = 0;

    if (dstLen < Len(dst))
        dst->digit[dstLen] = 0; /* FIX: ??? */
}


/* Shift a long integer right by shift bits. Store the result at dst, modifying
   it in-place. Assume that dst is long enough to hold the result. */
static void Shr(ALongInt *src, ALongInt *dst, unsigned long shift)
{
    unsigned long wordShift;
    unsigned long bitShift;
    unsigned long srcLen;
    unsigned long dstLen;
    unsigned long carry;
    long i;

    wordShift = shift / BITS;
    bitShift = shift & (BITS - 1);

    srcLen = Len(src);
    dstLen = srcLen - wordShift;

    carry = (ALongIntDoubleDigit)src->digit[wordShift] >> bitShift;
    for (i = 0; i < dstLen - 1; i++) {
        carry |= (ALongIntDoubleDigit)src->digit[i + wordShift + 1]
            << (BITS - bitShift);
        dst->digit[i] = carry & MASK;
        carry >>= BITS;
    }

    if (carry > 0)
        dst->digit[dstLen - 1] = carry;
    else
        dst->digit[dstLen - 1] = 0;
}


/* Divide a long integer by another. Return the result of the division and
   store the remainder at *mod.
   NOTE: b should be at least 2 digits long for decent performance. */
AValue ADivModLongInt(AThread *t, AValue oldAVal, AValue oldBVal,
                      AValue *mod)
{
    long lenA;
    long lenB;
    int signOldA;
    int signOldB;
    ALongInt *oldA;
    ALongInt *oldB;
    ALongInt *d;
    ALongInt *a;
    ALongInt *b;
    long shift;
    unsigned long i;
    long aInd, dInd;
    AValue tmp;

    /* Keeping everything available for the garbage collector is a pain. */
    if (!AAllocTempStack(t, 2))
        return AError;
    
    t->tempStackPtr[-2] = oldAVal;
    t->tempStackPtr[-1] = oldBVal;
    
    oldA = AValueToLongInt(oldAVal);
    oldB = AValueToLongInt(oldBVal);
    
    signOldA = Sign(oldA);
    signOldB = Sign(oldB);
    
    lenB = Len(oldB);

    /* Calculate how many bits the B value need to be shifted left so that
       the most significant bit of the most significant digit is 1. */
    for (i = BASE / 2, shift = 0; oldB->digit[lenB - 1] < i; i >>= 1, shift++);

    if (signOldA ^ signOldB) {
        /* If the signs differ, tweak a to get the correct rounding
           behavior. */
        tmp = SubAbsoluteSingle(t, oldB, 1, FALSE);
        if (AIsError(tmp))
            goto OutOfMemory;

        oldA = AValueToLongInt(t->tempStackPtr[-2]);
        t->tempStackPtr[-2] = tmp;
        
        oldAVal = AddAbsolute(t, oldA, AValueToLongInt(tmp), FALSE);
        if (AIsError(oldAVal))
            goto OutOfMemory;;

        oldA = AValueToLongInt(oldAVal);
        oldB = AValueToLongInt(t->tempStackPtr[-1]);
    }

    /* Shift both operands left so that the divisor has 1 as the msb of the
       most significant digit. */
    
    a = CreateLongInt(t, Len(oldA) + 1, &oldA, &oldB);
    if (a == NULL)
        goto OutOfMemory;
    
    Shl(oldA, a, shift);
    
    b = CreateLongInt(t, lenB, &a, &oldB);
    if (b == NULL)
        goto OutOfMemory;
    
    Shl(oldB, b, shift);

    lenA = Len(a);

    d = CreateLongInt(t, lenA - lenB + 1, &a, &b);
    if (d == NULL)
        goto OutOfMemory;

    /* Calculate the digits of the result, one digit at a time. */
    for (aInd = lenA, dInd = lenA - lenB; dInd >= 0; aInd--, dInd--) {
        unsigned long aHi;
        unsigned long q;
        DoubleDigit carry;
        
        aHi = (aInd < lenA ? a->digit[aInd] : 0);

        /* Get an estimate for the next digit (as q). */
        if (aHi == b->digit[lenB - 1])
            q = MASK;
        else
            q = ((aHi << BITS) + a->digit[aInd - 1]) / b->digit[lenB - 1];

        /* q may be too large. Correct q so that the error is at most 1. */
        for (;;) {
            unsigned long t = (aHi << BITS) + a->digit[aInd - 1] -
                      q * b->digit[lenB - 1];
            unsigned bDig2 = lenB - 2 >= 0 ? b->digit[lenB - 2] : 0;
            if (t > MASK
                || bDig2 * q <= (t << BITS) + a->digit[aInd - 2])
                break;

            q--;
        }

        /* Subtract q * b * BASE**dInd from a. Since q may be too large, the
           result may underflow. */
        carry = 0;
        for (i = 0; i < lenB; i++) {
            DoubleDigit s = b->digit[i] * q;
            DoubleDigit t = a->digit[i + dInd];
            a->digit[i + dInd] = (t - s - carry) & MASK;
            carry = (s - t + carry + MASK) >> BITS;
        }
        if (lenB + dInd < lenA) {
            carry -= a->digit[lenB + dInd];
            a->digit[lenB + dInd] = 0;
        }

        /* If there was no carry, q is correct. Otherwise, q is too large and
           we have to add b to a to fix it. */
        if (carry == 0)
            d->digit[dInd] = q;
        else {
            d->digit[dInd] = q - 1;
            
            carry = 0;
            for (i = 0; i < lenB; i++) {
                carry += (ALongIntDoubleDigit)a->digit[i + dInd] + b->digit[i];
                a->digit[i + dInd] = carry & MASK;
                carry >>= BITS;
            }
        }
    }

    /* Normalize the results. */

    /* a contains the shifted remainder. Shift it back. */
    Shr(a, a, shift);

    t->tempStackPtr[-1] = ALongIntToValue(d);

    if (signOldA ^ signOldB) {
        /* Tweak the remainder if the operands had different signs. */
        AValue tmp;
        
        a = TruncateZeroDigits(t, a, 0);
        if (a == NULL)
            goto OutOfMemory;
        tmp = SubAbsolute(t, AValueToLongInt(t->tempStackPtr[-2]), a,
                          FALSE);
        if (AIsError(tmp))
            goto OutOfMemory;

        a = AValueToLongInt(tmp);
    }
    
    *mod = ANormalize(t, a, signOldB);

    d = AValueToLongInt(t->tempStackPtr[-1]);
    t->tempStackPtr -= 2;
    
    return ANormalize(t, d, signOldA ^ signOldB);

  OutOfMemory:
    
    t->tempStackPtr -= 2;
    
    return ARaiseMemoryErrorND(t);
}


/* Create a long int object with len digits. Return a pointer to the object or
   NULL on error. The objects *a and *b will by protected from garbage
   collection, but their value may be changed by copying gc. If b is NULL,
   it is ignored. */
ALongInt *CreateLongInt(AThread *t, long len, ALongInt **a, ALongInt **b)
{
    long size;
    ALongInt *li;

    size = AGetBlockSize(Size(len));
    if (t->heapPtr + size > t->heapEnd || A_NO_INLINE_ALLOC) {
        t->tempStack[0] = ALongIntToValue(*a);
        if (b != NULL)
            t->tempStack[1] = ALongIntToValue(*b);

        li = AAlloc(t, size);

        *a = AValueToLongInt(t->tempStack[0]);
        if (b != NULL)
            *b = AValueToLongInt(t->tempStack[1]);
        
        t->tempStack[0] = AZero;
        t->tempStack[1] = AZero;
        
        if (li == NULL)
            return NULL;
    } else {
        li = (ALongInt *)t->heapPtr;
        t->heapPtr += size;
    }

    AInitLongIntBlock(li, len, 0);

    return li;
}


/* Return a copy of a long int with leading zero digits truncated. Try to
   perform the operation in-place whenever possible. If isNeg is TRUE, also
   set the sign bit of the result. */
static ALongInt *TruncateZeroDigits(AThread *t, ALongInt *li, ABool isNeg)
{
    long len = AGetLongIntLen(li);
    
    if (li->digit[len - 1] == 0) {
        long oldSize;
        long newSize;
        ABool isNewGen;

        oldSize = AGetBlockSize(AGetLongIntSize(len));
        
        do {
            len--;
        } while (len > 1 && li->digit[len - 1] == 0);

        newSize = AGetBlockSize(AGetLongIntSize(len));
        isNewGen = (li->header & A_NEW_GEN_FLAG) != 0;

        /* Truncate block if possible. This might be impossible if the
           difference in block sizes is too small. In that case, we need to
           allocate a new block and copy the contents there. */
        if (!isNewGen && newSize < oldSize
                && oldSize - newSize < A_MIN_BLOCK_SIZE) {
            /* Allocate a new block and copy the digits from the old block. */
            ALongInt *li2;
            long i;

            li2 = CreateLongInt(t, len, &li, NULL);
            if (li2 == NULL)
                return NULL;
            for (i = 0; i < len; i++)
                li2->digit[i] = li->digit[i];
            if (isNeg)
                ASetLongIntSign(li2);
            li = li2;
        } else {
            if (isNewGen)
                AInitLongIntBlock(li, len, isNeg);
            else
                AInitLongIntBlockOld(li, len, isNeg);

            if (newSize < oldSize)
                AInitNonPointerBlockOld((AValue *)APtrAdd(li, newSize),
                                        oldSize - newSize - sizeof(AValue));
        }
    } else if (isNeg)
        ASetLongIntSign(li);

    return li;
}


/* Normalize a long integer object. Remove leading zero digits and represent
   the integer as a short integer if possible. If isNeg is TRUE, assume that
   the integer is negative (and also set the sign bit of the result). */
AValue ANormalize(AThread *t, ALongInt *li, ABool isNeg)
{
    long len;

    li = TruncateZeroDigits(t, li, isNeg);
    if (li == NULL)
        return ARaiseMemoryError(t);
    
    len = AGetLongIntLen(li);
    
    if (len <= DIGITS_IN_INT) {
#if DIGITS_IN_INT == 1
        if (isNeg) {
            if (li->digit[0] <= A_SHORT_INT_MAX + 1)
                return AIntToValue(-li->digit[0]);
        } else {
            if (li->digit[0] <= A_SHORT_INT_MAX)
                return AIntToValue(-li->digit[0]);
        }
#else
        if (isNeg) {
            if (li->digit[1] < (A_SHORT_INT_MAX + 1) >> BITS
                || (li->digit[0] == 0
                    && li->digit[1] == (A_SHORT_INT_MAX + 1) >> BITS))
                return AIntToValue(-(li->digit[0] +
                                     ((ALongIntDoubleDigit)li->digit[1]
                                      << BITS)));
        } else {
            if (len == 1)
                return AIntToValue(li->digit[0]);
            else if (li->digit[1] < (A_SHORT_INT_MAX + 1) >> BITS)
                return AIntToValue(li->digit[0] +
                                   ((ALongIntDoubleDigit)li->digit[1]
                                    << BITS));
        }
#endif
    }

    return ALongIntToValue(li);
}


/* Multiple two short integers. The result may be a short or long integer. */
AValue AMultiplyShortInt(AThread *t, AValue left, AValue right)
{
    AValue prod;
    ABool isNeg;
    
    if (AHi(left) != 0 || AHi(right) != 0) {
        isNeg = FALSE;
        
        if ((ASignedValue)left < 0) {
            left = -left;
            if ((ASignedValue)left < 0) {
                if (right <= AIntToValue(1))
                    return left * AValueToInt(right);
                else
                    goto FullLongMul;
            }
            isNeg = TRUE;
        }
        
        if ((ASignedValue)right < 0) {
            right = -right;
            if ((ASignedValue)right < 0) {
                if (!isNeg && left <= AIntToValue(1))
                    return left * AValueToInt(right);
                else
                    goto FullLongMul;
            }
            isNeg ^= 1;
        }
        
        if (right > left) {
            AValue tmp = left;
            left = right;
            right = tmp;
        }
        
        /* Now left and right are positive. isNeg is true iff result
           is negative. */
        
        if (AHi(right) != 0) {
            
          FullLongMul:
            
            if (isNeg)
                left = -left;
                    
            left = ACreateLongIntFromIntND(t, AValueToInt(left));
            if (AIsError(left))
                return left;
            
            *t->tempStack = left;
            
            right = ACreateLongIntFromIntND(t, AValueToInt(right));
            
            left = *t->tempStack;
            *t->tempStack = AZero;
            
            if (AIsError(right))
                return right;
            
            prod = AMultiplyLongInt(t, left, right);
        } else if (AIsIntTimesHalfIntOverflow(left, right, isNeg)) {
            
          ShortLongMul:
            
            left = ACreateLongIntFromIntND(t, AValueToInt(left));
            if (AIsError(left))
                return left;
            
            prod = ALongIntToValue(
                AMulAddSingle(t, AValueToLongInt(left),
                             AValueToInt(right), 0));
            if (AIsError(prod))
                return prod;
            
            if (isNeg)
                ASetLongIntSign(AValueToLongInt(prod));
        } else {
            prod = AValueToInt(left) * (ASignedValue)right;
            if (isNeg)
                prod = -prod;
        }
    } else {
        prod = AValueToInt(left) * (ASignedValue)right;
        if ((ASignedValue)prod < 0) {
            isNeg = FALSE;
            goto ShortLongMul;
        }
    }

    return prod;
}


/* Return baseVal**expVAl.
   NOTE: baseVal and expVal are expected to be ints or longints */
AValue APowInt(AThread *t, AValue baseVal, AValue expVal)
{
    AValue val;
    long exp;
    
    if (AIsLongInt(expVal)) {
        /* Raise exception if exponent is negative. */
        if (Sign(AValueToLongInt(expVal)))
            return ARaiseArithmeticErrorND(t, NULL);/* FIX: msg, func? */

        if (baseVal == AZero)
            return AZero;
        
        if (baseVal == AIntToValue(1)) {
            if (Sign(AValueToLongInt(expVal)))
                return ACreateFloat(t, 1.0);
            else
                return baseVal;
        } else if (baseVal == AIntToValue(-1)) {
            if (Sign(AValueToLongInt(expVal)))
                return (AValueToLongInt(expVal)->digit[0] & 1) == 0 ?
                    ACreateFloat(t, 1.0) : ACreateFloat(t, -1.0);
            else
                return (AValueToLongInt(expVal)->digit[0] & 1) == 0 ?
                    AIntToValue(1) : AIntToValue(-1);
        } else
            return ARaiseValueErrorND(t, NULL); /* FIX: msg */
    }

    exp = AValueToInt(expVal);
    if (exp <= 0) {
        if (exp == 0)
            return AIntToValue(1);

        return ARaiseArithmeticErrorND(t, NULL); /* FIX: msg, func? */
    }

    if (!AAllocTempStack(t, 2))
        return AError;
        
    t->tempStackPtr[-2] = baseVal;
    t->tempStackPtr[-1] = baseVal;

    val = baseVal;
    exp--;
    
    if (AIsLongInt(val))
        goto LongIntLoop;
    
    /* Invariant: baseVal^exp * val == result. */
    while (exp > 0) {
        while ((exp & 1) == 0) {
            baseVal = AMultiplyShortInt(t, baseVal, baseVal);
            exp >>= 1;

            if (AIsError(baseVal))
                goto OutOfMemory;
            
            if (AIsLongInt(baseVal)) {
                t->tempStackPtr[-2] = baseVal;
                t->tempStackPtr[-1] = ACreateLongIntFromIntND(
                    t, AValueToInt(val));
                if (t->tempStackPtr[-1] == AError)
                    goto OutOfMemory;
                goto LongIntLoopMiddle;
            }
        }

        val = AMultiplyShortInt(t, val, baseVal);
        exp--;
        
        if (AIsError(val))
            goto OutOfMemory;
        
        if (AIsLongInt(val)) {
            t->tempStackPtr[-1] = val;
            t->tempStackPtr[-2] = ACreateLongIntFromIntND(
                t, AValueToInt(baseVal));
            if (t->tempStackPtr[-2] == AError)
                goto OutOfMemory;
            goto LongIntLoop;
        }
    }

    t->tempStackPtr -= 2;

    return val;

  LongIntLoop:

    while (exp > 0) {
        while ((exp & 1) == 0) {
            t->tempStackPtr[-2] = AMultiplyLongInt(
                t, t->tempStackPtr[-2], t->tempStackPtr[-2]);
            exp >>= 1;

            if (t->tempStackPtr[-2] == AError)
                goto OutOfMemory;
        }

      LongIntLoopMiddle:

        t->tempStackPtr[-1] = AMultiplyLongInt(
            t, t->tempStackPtr[-1], t->tempStackPtr[-2]);
        exp--;
        
        if (t->tempStackPtr[-1] == AError)
            goto OutOfMemory;
    }

    t->tempStackPtr -= 2;

    return t->tempStackPtr[1];

  OutOfMemory:

    t->tempStackPtr -= 2;
    
    return ARaiseMemoryErrorND(t);
}


/* Array of digit characters (used for all supported bases) */
const char ADigits[36] = "0123456789abcdefghijklmnopqrstuvwxyz";


#define LONGINT_STACK_BUF_SIZE 128


/* Convert a long integer to a string with the given radix (base). The result
   will have at least minWidth digits; if necessary, pad it with zeroes. Return
   a Str value. Assume that the argument is a long integer. */
AValue ALongIntToStr(AThread *t, AValue liVal, int radix, int minWidth)
{
    ALongInt *li;
    long maxLen;
    unsigned char stackBuf[LONGINT_STACK_BUF_SIZE];
    unsigned char *buf;
    long ind;
    ABool isNeg;
    AString *s;
    long len;

    if (!AAllocTempStack(t, 1))
        return AError;
    
    t->tempStackPtr[-1] = liVal;
    
    li = AValueToLongInt(liVal);

    if (Sign(li))
        isNeg = TRUE;
    else
        isNeg = FALSE;

    if (radix < 8)
        maxLen = Len(li) * BITS + 1;
    else if (radix < 16)
        maxLen = Len(li) * BITS / 3 + 2;
    else
        maxLen = Len(li) * BITS / 4 + 2;
    if (maxLen < minWidth + 1)
        maxLen = minWidth + 1;
    
    /* Allocate space for the string if necessary, or use a fixed-length
       buffer allocated in the stack (quicker!) when possible. */
    if (maxLen > LONGINT_STACK_BUF_SIZE) {
        AValue *heapBuf;

        heapBuf = AAlloc(t, sizeof(AValue) + maxLen);
        if (heapBuf == NULL)
            goto MemoryError;

        li = AValueToLongInt(t->tempStackPtr[-1]);

        AInitNonPointerBlock(heapBuf, maxLen);

        buf = (unsigned char *)(heapBuf + 1);

        t->tempStackPtr[-1] = AStrToValue(heapBuf);
    } else
        buf = stackBuf;

    ind = maxLen;

    /* Process one digit at a time.
       IDEA: This could be faster by processing more than one digit at a
             time. */
    do {
        unsigned rem;
        char ch;
        
        li = ADivModSingle(t, li, radix, &rem);
        if (li == NULL)
            goto MemoryError;

        if (buf != stackBuf)
            buf = APtrAdd(AValueToPtr(t->tempStackPtr[-1]), sizeof(AValue));

        if (rem < 10)
            ch = rem + '0';
        else
            ch = rem - 10 + 'a';
        
        buf[--ind] = ch;
    } while (Len(li) > 0 || li->digit[0] != 0);

    /* Insert zeroes as padding if the result is too short. */
    while (maxLen - ind < minWidth)
        buf[--ind] = '0';

    if (isNeg)
        buf[--ind] = '-';

    len = maxLen - ind;
    
    /* Allocate the string. */
    s = AAlloc(t, sizeof(AValue) + len);
    if (s == NULL)
        goto MemoryError;

    if (buf != stackBuf)
        buf = APtrAdd(AValueToPtr(t->tempStackPtr[-1]), sizeof(AValue));

    AInitNonPointerBlock(&s->header, len);
    
    /* Copy stuff to the string */
    ACopyMem(s->elem, buf + ind, len);

    t->tempStackPtr--;
    
    return AStrToValue(s);

  MemoryError:

    t->tempStackPtr--;
    
    return ARaiseMemoryErrorND(t);
}


#define F2LI_BUF_SIZE 64


/* Return a long int that represents the truncation of a float. */
AValue AFloatToLongInt(AThread *t, double f)
{
    ALongIntDigit digits[F2LI_BUF_SIZE];
    ABool isNeg;
    int n;
    ALongInt *li;

    if (AIsNaN(f))
        return ARaiseValueError(t, "NaN");
    if (AIsInf(f))
        return ARaiseValueError(t, "Infinity");

    isNeg = (f < 0.0);
    f = floor(fabs(f));

    n = 0;
    while (f > 0.0) {
        digits[n] = fmod(f, 1L << A_LONG_INT_DIGIT_BITS);
        n++;
        f = floor(f / (1L << A_LONG_INT_DIGIT_BITS));
    }

    li = AAlloc(t, Size(n));
    if (li == NULL)
        return AError;
    
    AInitLongIntBlock(li, n, isNeg);
    ACopyMem(li->digit, digits, n * sizeof(ALongIntDigit));
    return ALongIntToValue(li);
}


/* Return the hash value of a long integer. Assume the argument is a long
   integer. */
AValue ALongIntHashValue(AValue val)
{
    ALongInt *l = AValueToLongInt(val);
    unsigned long hashValue = 0;
    long i;
    
    for (i = 0; i < AGetLongIntLen(l); i++)
        hashValue += hashValue * 32 + l->digit[i];
    
    return AIntToValue(hashValue);
}
