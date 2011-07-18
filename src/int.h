/* int.h - Int operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef INT_H_INCL
#define INT_H_INCL

#include "thread.h"
#include "value.h"


AValue AStdInt(AThread *t, AValue *frame);


/* Add two Int / LongInt values. (NOTE: no type checking) */
AValue AAddInts(AThread *t, AValue a, AValue b);

/* Subtract two Int / LongInt values. (NOTE: no type checking) */
AValue ASubInts(AThread *t, AValue a, AValue b);

#define AIsAddOverflow(sum, left, right) \
    ((ASignedValue)((sum) ^ (left)) < 0 && (ASignedValue)((sum) ^ (right)) < 0)

#define AIsSubOverflow(dif, left, right) \
    ((ASignedValue)((dif) ^ (left)) < 0 && \
     (ASignedValue)((dif) ^ (right)) >= 0)


AValue ALongIntHashValue(AValue val);


#define A_LONGINT_DIGIT_SIZE (A_LONG_INT_DIGIT_BITS / 8)


/* FIX: perhaps not always like this? */
#define AGetLongIntSign(ptr) \
    ((ptr)->header & 4)
#define AGetLongIntLen(ptr) \
    ((((ptr)->header & ~A_NEW_GEN_FLAG) + 4) >> \
     (2 + ALog2(A_LONGINT_DIGIT_SIZE)))

#define AGetLongIntSize(numDigits) \
    (sizeof(AValue) + (numDigits) * sizeof(ALongIntDigit))

#define AInitLongIntBlock(li, numDigs, isNegative) \
    AInitNonPointerBlock(&(li)->header, (numDigs) * \
        sizeof(ALongIntDigit) - ((isNegative) != 0))
#define AInitLongIntBlockOld(li, numDigs, isNegative) \
    AInitNonPointerBlockOld(&(li)->header, (numDigs) * \
        sizeof(ALongIntDigit) - ((isNegative) != 0))

#define ASetLongIntSign(li) \
    ((li)->header -= 4)


#define A_LONGINT_BASE ((ALongIntDoubleDigit)1 << A_LONG_INT_DIGIT_BITS)
#define A_LONGINT_DIGIT_MASK (ALongIntDigit)(A_LONGINT_BASE - 1)


#define AHi(val) ((val) >> (A_VALUE_INT_BITS / 2 + A_VALUE_INT_SHIFT))
#define ALo(val) ((val) & ((1L << (A_VALUE_INT_BITS / 2)) - 1))


/* Decide whether the multiplication of two short Int values results in an
   overflow. full and half must be non-negative, and half must not be larger
   than x (FIX). If isNeg is true, the result of the multiplication will be
   negative (i.e. full * -half). */
#define AIsIntTimesHalfIntOverflow(full, half, isNeg) \
    ((AHi(full) * AValueToInt(half) >= 1L << (A_VALUE_INT_BITS / 2 - 1) \
      || AValueToInt(full) * AValueToInt(half) >=                       \
         1L << (A_VALUE_INT_BITS - 1))                                  \
     && (!(isNeg) || (AHi(full) * AValueToInt(half) >                   \
                      1L << (A_VALUE_INT_BITS / 2 - 1) || ALo(full) != 0)))


AValue ACreateLongIntFromIntND(AThread *t, ASignedValue intVal);
ALongIntSignedDoubleDigit ACompareLongInt(AValue aVal, AValue bVal);
AValue AAddLongInt(AThread *t, AValue aVal, AValue bVal);
AValue ASubLongInt(AThread *t, AValue aVal, AValue bVal);
AValue AAddLongIntSingle(AThread *t, AValue aVal, unsigned i);
AValue ASubLongIntSingle(AThread *t, AValue aVal, unsigned i);
AValue ANegateLongInt(AThread *t, AValue aVal);
AValue AMultiplyLongInt(AThread *t, AValue aVal, AValue bVal);
AValue AMultiplyShortInt(AThread *t, AValue left, AValue right);
AValue ADivModLongInt(AThread *t, AValue oldAVal, AValue oldBVal,
                    AValue *mod);
AValue APowInt(AThread *t, AValue baseVal, AValue expVal);

AValue ALongIntToStr(AThread *t, AValue a, int radix, int minWidth);

AValue AFloatToLongInt(AThread *t, double f);
AValue ANormalize(AThread *t, ALongInt *li, ABool isNeg);


/* FIX: following two return value? */
ALongInt *AMulAddSingle(AThread *t, ALongInt *a, unsigned mul,
                      unsigned add);
ALongInt *ADivModSingle(AThread *t, ALongInt *a, unsigned b,
                      unsigned *remPtr);


AValue AIntDivMod(AThread *t, ASignedValue left, ASignedValue right,
                AValue *modPtr);


extern const char ADigits[];


#endif
