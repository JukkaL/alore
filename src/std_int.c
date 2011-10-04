/* std_int.c - std::Int related operations (see also std_int_long.c)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "memberid.h"
#include "int.h"
#include "str.h"
#include "mem.h"
#include "internal.h"
#include "gc.h"

#include <math.h>


/* NOTE: There are more Int-related operations in std_int_long.c. */


static AValue RadixInt(AThread *t, AValue *frame);
static AValue RaiseValueErrorForInvalidIntStr(AThread *t, AValue str);


/* std::Int(obj[, radix])
   Construct an Int object. Note that this is implemented internally as a
   function, not a type. */
AValue AStdInt(AThread *t, AValue *frame)
{
    int ind;
    int end;
    unsigned char *buf;
    ABool isNeg;
    ASignedValue num;

    /* If the second argument is present, the perform string-to-int conversion
       with a custom radix (base). */
    if (!AIsDefault(frame[1]))
        return RadixInt(t, frame);

    if (AIsNarrowStr(frame[0])) {
        /* Convert a narrow string to Int. */

        ind = 0;
        buf = AValueToStr(frame[0])->elem;
        end = AGetStrLen(AValueToStr(frame[0]));

      TryConvert:

        /* Skip initial whitespace. */
        while (ind < end && (buf[ind] == ' ' || buf[ind] == '\t'))
            ind++;

        /* Check sign. */
        isNeg = FALSE;
        if (ind < end) {
            if (buf[ind] == '-') {
                isNeg = TRUE;
                ind++;
            } else if (buf[ind] == '+')
                ind++;
        }

        num = 0;

        if (ind < end && AIsDigit(buf[ind])) {
            /* Process digits until we find a non-digit character or until
               the int is too long to be represented as a short int. */
            do {
                int digit = buf[ind] - '0';

                if (num >= (A_SHORT_INT_MAX + 1) / 10
                    && (num > (A_SHORT_INT_MAX + 1) / 10
                        || digit > ((A_SHORT_INT_MAX + 1) -
                                    ((A_SHORT_INT_MAX + 1) / 10) * 10)))
                    goto HandleLongInt;

                num = num * 10 + digit;

                ind++;
            } while (ind < end && AIsDigit(buf[ind]));
        } else
            return RaiseValueErrorForInvalidIntStr(t, frame[0]);

        /* Skip whitespace at the end of the string. */
        for (; ind < end; ind++) {
            if (buf[ind] != ' ' && buf[ind] != '\t')
                return RaiseValueErrorForInvalidIntStr(t, frame[0]);
        }

        /* Construct and return an Int object. */
        if (isNeg)
            return AIntToValue(-num);
        else if (num == A_SHORT_INT_MAX + 1)
            return ACreateLongIntFromIntND(t, num);
        else
            return AIntToValue(num);
    } else if (AIsSubStr(frame[0])) {
        /* Substring object */
        ASubString *ss = AValueToSubStr(frame[0]);

        ind = AValueToInt(ss->ind);
        end = ind + AValueToInt(ss->len);

        if (AIsNarrowStr(ss->str)) {
            /* Narrow substring; reuse the narrow string implementation. */
            frame[0] = ss->str;
            buf = AValueToStr(frame[0])->elem;
            goto TryConvert;
        } else {
            /* Wide substring */
            frame[0] = ss->str;
            goto Wide;
        }
    } else if (AIsFloat(frame[0])) {
        /* Convert Float to Int. */
        double f = AValueToFloat(frame[0]);
        if (f < A_SHORT_INT_MAX + 1 && f >= A_SHORT_INT_MIN)
            return AIntToValue((ASignedValue)f);
        else
            return AFloatToLongInt(t, f);
    } else if (AIsWideStr(frame[0])) {
        /* A wide string object. If it is a valid argument it can be converted
           to a narrow string. */
        AValue n;

        ind = 0;
        end = AGetWideStrLen(AValueToWideStr(frame[0]));

      Wide:

        n = ASubStr(t, frame[0], ind, end);

        end -= ind;
        ind = 0;

        /* Try to convert the string to a narrow one. */
        frame[0] = n;
        n = AWideStringToNarrow(t, frame[0], frame);
        if (AIsError(n))
            return AError; /* Not a valid string argument */

        /* Now we have a narrow string -- convert it to Int. */
        frame[0] = n;
        buf = AValueToStr(frame[0])->elem;
        goto TryConvert;
    } else if (AIsInstance(frame[0]) || !AIsInt(frame[0])) {
        /* Call the _int method of an object. */
        AValue v = ACallMethodByNum(t, AM__INT, 0, frame);
        /* Expect an Int object. */
        if (AIsShortInt(v) || AIsLongInt(v))
            return v;
        else if (AIsError(v))
            return v;
        else
            return ARaiseTypeErrorND(t, "_int of %T returned non-integer (%T)",
                                     frame[0], v);
    } else {
        /* Int argument, which is trivial. Note that reusing the argument is
           fine since integers are immutable. */
        return frame[0];
    }

  HandleLongInt:

    {
        /* Perform string-to-int conversion when the result is a long int. */
        ALongInt *li;
        AValue val;

        val = ACreateLongIntFromIntND(t, num);
        if (AIsError(val))
            return AError;

        li = AValueToLongInt(val);

        do {
            unsigned char ch = AValueToStr(frame[0])->elem[ind];

            if (!AIsDigit(ch))
                break;

            li = AMulAddSingle(t, li, 10, ch - '0');
            if (li == NULL)
                return AError;

            ind++;
        } while (ind < end);

        if (isNeg)
            ASetLongIntSign(li);

        return ALongIntToValue(li);
    }
}


/* Raise value error related to invalid Str argument to std::Int. */
static AValue RaiseValueErrorForInvalidIntStr(AThread *t, AValue str)
{
    char repr[200];
    AGetRepr(t, repr, sizeof(repr), str);
    return ARaiseValueError(t, "Invalid string argument: %s", repr);
}


/* Add two Int objects. Assume arguments are integers. */
AValue AAddInts(AThread *t, AValue a, AValue b)
{
    if (AIsShortInt(a) && AIsShortInt(b)) {
        AValue sum = a + b;
        if (!AIsAddOverflow(sum, a, b))
            return sum;
        else
            return ACreateLongIntFromIntND(t, AValueToInt(a) +
                                       AValueToInt(b));
    } else if (AIsLongInt(a) && AIsLongInt(b))
        return AAddLongInt(t, a, b);
    else {
        a = ACoerce(t, OPER_PLUS, a, b, &b);
        if (AIsError(a))
            return a;
        return AAddInts(t, a, b);
    }
}


/* Subtract two Int objects. Assume arguments are integers. */
AValue ASubInts(AThread *t, AValue a, AValue b)
{
    if (AIsShortInt(a) && AIsShortInt(b)) {
        AValue dif = a - b;
        if (!AIsSubOverflow(dif, a, b))
            return dif;
        else
            return ACreateLongIntFromIntND(t, AValueToInt(a) -
                                        AValueToInt(b));
    } else if (AIsLongInt(a) && AIsLongInt(b))
        return ASubLongInt(t, a, b);
    else {
        a = ACoerce(t, OPER_MINUS, a, b, &b);
        if (AIsError(a))
            return a;
        return ASubInts(t, a, b);
    }
}


/* Convert a string to an Int with a specific radix. Share frame format with
   std::Int, but the second argument is always present. */
static AValue RadixInt(AThread *t, AValue *frame)
{
    int i, j;
    int radix = AGetInt(t, frame[1]);
    ABool isNeg;

    if (radix < 2 || radix > 36)
        return ARaiseValueError(t, "Invalid radix");

    frame[1] = AZero;

    AExpectStr(t, frame[0]);

    /* Skip initial whitespace. */
    for (i = 0; i < AStrLen(frame[0]); i++) {
        int ch = AStrItem(frame[0], i);
        if (ch != ' ' && ch != '\t')
            break;
    }

    /* Process sign. */
    if (i < AStrLen(frame[0]) && AStrItem(frame[0], i) == '-') {
        isNeg = TRUE;
        i++;
    } else
        isNeg = FALSE;

    /* Process the number. */
    j = i;
    for (; i < AStrLen(frame[0]); i++) {
        int ch = AStrItem(frame[0], i);
        int digit;

        if (AIsDigit(ch))
            digit = ch - '0';
        else if (ch >= 'a' && ch <= 'z')
            digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z')
            digit = ch - 'A' + 10;
        else
            break;

        if (digit >= radix)
            ARaiseValueError(t, "Invalid character");

        frame[1] = AMul(t, frame[1], AMakeShortInt(radix));
        frame[1] = AAdd(t, frame[1], AMakeShortInt(digit));
    }

    if (i == j)
        return ARaiseValueError(t, "Argument not number");

    /* Skip trailing whitespace. */
    for (; i < AStrLen(frame[0]); i++) {
        int ch = AStrItem(frame[0], i);
        if (ch != ' ' && ch != '\t')
            return ARaiseValueError(t, "Invalid character");
    }

    if (!isNeg)
        return frame[1];
    else
        return ANeg(t, frame[1]);
}


/* Calculate the division (left / right) and remainder (left mod right) of two
   short integer values. Return the division and store modulus in *modPtr.

   NOTE: The result may be a long integer if left is A_SHORT_INT_MIN and right
         is -1. */
AValue AIntDivMod(AThread *t, ASignedValue left, ASignedValue right,
                  AValue *modPtr)
{
    ASignedValue quot;
    ASignedValue mod;

    if (right == AZero)
        return ARaiseArithmeticErrorND(t, AMsgDivisionByZero);

    if (left < 0) {
        if (right < 0) {
            if (left == AIntToValue(A_SHORT_INT_MIN)
                    && right == AIntToValue(-1)) {
                *modPtr = AZero;
                return ACreateLongIntFromIntND(t, -A_SHORT_INT_MIN);
            }
            quot = AIntToValue(-AValueToInt(left) / -AValueToInt(right));
        } else
            quot = -AIntToValue(-AValueToInt(left) / AValueToInt(right));

    } else {
        if (right < 0)
            quot = -(left / -AValueToInt(right));
        else
            quot = left / AValueToInt(right);
    }

    quot &= ~A_INTEGER_MASK;

    mod = left - quot * AValueToInt(right);
    if ((mod < 0 && right > 0) || (mod > 0 && right < 0)) {
        mod += right;
        quot -= AIntToValue(1);
    }

    *modPtr = mod;
    return quot;
}


/* Int-related C API functions */


int AGetInt(AThread *t, AValue val)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    /* IDEA: Use AGetLongIntDigit and other API macros instead of LongInt *. */
    if (AIsShortInt(val))
        return AValueToInt(val);
    else if (AIsLongInt(val)) {
        ALongInt *li = AValueToLongInt(val);
        ALongIntDoubleDigit dd = li->digit[0] + (li->digit[1] << 16);
        if ((AGetLongIntSign(li) && dd > (1U << 31)) ||
            (!AGetLongIntSign(li) && dd > INT_MAX) ||
            AGetLongIntLen(li) > 2)
            return ARaiseValueError(t, AMsgOutOfRange);
        return AGetLongIntSign(li) ? -dd : dd;
    } else
        return ARaiseIntExpected(t, val);
#elif A_VALUE_BITS == 64
    if (AIsShortInt(val)) {
        ASignedValue v = AValueToInt(val);
        if (v > 0x7fffffff || v < -0x80000000L)
            ARaiseValueError(t, AMsgOutOfRange);
        return (int)v;
    } else if (AIsLongInt(val))
        return ARaiseValueError(t, AMsgOutOfRange);
    else
        return ARaiseIntExpected(t, val);
#else
    /* Not supported */
    error!;
#endif
}


unsigned AGetIntU(AThread *t, AValue val)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    if (AIsShortInt(val)) {
        if ((ASignedValue)val < 0)
            return ARaiseValueError(t, AMsgNegativeValueNotExpected);
        return AValueToInt(val);
    } else if (AIsLongInt(val)) {
        if (AIsNegLongInt(val) || ALongIntLen(val) > 2)
            return ARaiseValueError(t, AMsgOutOfRange);
        return AGetLongIntDigit(val, 0) + (AGetLongIntDigit(val, 1) << 16);
    } else
        return ARaiseIntExpected(t, val);
#elif A_VALUE_BITS == 64
    if (AIsShortInt(val)) {
        ASignedValue v = AValueToInt(val);
        if (v < 0 || v > 0xffffffff)
            return ARaiseValueError(t, AMsgOutOfRange);
        return (int)v;
    } else if (AIsLongInt(val))
        return ARaiseValueError(t, AMsgOutOfRange);
    else
        return ARaiseIntExpected(t, val);
#else
    /* Not supported */
    error!;
#endif
}


AInt64 AGetInt64(AThread *t, AValue val)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    if (AIsShortInt(val))
        return AValueToInt(val);
    else if (AIsLongInt(val)) {
        AInt64 i;
        int n;

        if (ALongIntLen(val) > 4
            || (ALongIntLen(val) == 4 &&
                (AGetLongIntDigit(val, 3) > 0x8000)))
            return ARaiseValueError(t, AMsgOutOfRange);
        if (ALongIntLen(val) == 4 && AGetLongIntDigit(val, 3) == 0x8000) {
            n = 2;
            while (n > 0 && AGetLongIntDigit(val, n) == 0)
                n--;
            if (AGetLongIntDigit(val, n) > 0 || !AIsNegLongInt(val))
                return ARaiseValueError(t, AMsgOutOfRange);
        }

        i = 0;
        for (n = ALongIntLen(val) - 1; n >= 0; n--)
            i = (i << 16) + AGetLongIntDigit(val, n);
        return AIsNegLongInt(val) ? -i : i;
    } else
        return ARaiseIntExpected(t, val);
#elif A_VALUE_BITS == 64 && A_LONG_INT_DIGIT_BITS == 32
    if (AIsShortInt(val))
        return AValueToInt(val);
    else if (AIsLongInt(val)) {
        AInt64 i;
        int n;

        if (ALongIntLen(val) > 2
            || (ALongIntLen(val) == 2 &&
                (AGetLongIntDigit(val, 1) > 0x80000000U)))
            return ARaiseValueError(t, AMsgOutOfRange);
        if (ALongIntLen(val) == 2 && AGetLongIntDigit(val, 1) == 0x80000000U) {
            if (AGetLongIntDigit(val, 0) > 0 || !AIsNegLongInt(val))
                return ARaiseValueError(t, AMsgOutOfRange);
        }

        i = 0;
        for (n = ALongIntLen(val) - 1; n >= 0; n--)
            i = (i << 32) + AGetLongIntDigit(val, n);
        return AIsNegLongInt(val) ? -i : i;
    } else
        return ARaiseIntExpected(t, val);
#else
    /* Not supported */
    error!;
#endif
}


AIntU64 AGetIntU64(AThread *t, AValue val)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    if (AIsShortInt(val)) {
        if ((ASignedValue)val < 0)
            return ARaiseValueError(t, AMsgNegativeValueNotExpected);
        return AValueToInt(val);
    } else if (AIsLongInt(val)) {
        AIntU64 i;
        int n;

        if (AIsNegLongInt(val) || ALongIntLen(val) > 4)
            return ARaiseValueError(t, AMsgOutOfRange);

        i = 0;
        for (n = ALongIntLen(val) - 1; n >= 0; n--)
            i = (i << 16) + AGetLongIntDigit(val, n);
        return i;
    } else
        return ARaiseIntExpected(t, val);
#elif A_VALUE_BITS == 64 && A_LONG_INT_DIGIT_BITS == 32
    if (AIsShortInt(val)) {
        if ((ASignedValue)val < 0)
            return ARaiseValueError(t, AMsgNegativeValueNotExpected);
        return AValueToInt(val);
    } else if (AIsLongInt(val)) {
        AIntU64 i;
        int n;

        if (AIsNegLongInt(val) || ALongIntLen(val) > 2)
            return ARaiseValueError(t, AMsgOutOfRange);

        i = 0;
        for (n = ALongIntLen(val) - 1; n >= 0; n--)
            i = (i << 32) + AGetLongIntDigit(val, n);
        return i;
    } else
        return ARaiseIntExpected(t, val);
#else
    /* Not supported */
    error!;
#endif
}


AValue AMakeInt(AThread *t, int i)
{
#if A_VALUE_BITS != 32 && A_VALUE_BITS != 64
    error! /* FIX */;
#endif

#if A_VALUE_BITS == 32
    /* 32-bit values */
    if (i <= A_SHORT_INT_MAX && i >= A_SHORT_INT_MIN)
        return AIntToValue(i);
    else {
        AValue v = ACreateLongIntFromIntND(t, i);
        if (AIsError(v))
            ADispatchException(t);
        return v;
    }
#else
    /* 64-bit values */
    return AIntToValue(i);
#endif
}


AValue AMakeIntU(AThread *t, unsigned i)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    /* 32-bit values */
    if (i <= A_SHORT_INT_MAX)
        return AIntToValue(i);
    else {
        AValue v = AMakeLongInt(t, 2, FALSE);
        ASetLongIntDigit(v, 0, i & 0xffff);
        ASetLongIntDigit(v, 1, i >> 16);
        return v;
    }
#elif A_VALUE_BITS == 64
    /* 64-bit values */
    return AIntToValue(i);
#else
    /* Not supported */
    error!;
#endif
}


AValue AMakeInt64(AThread *t, AInt64 i)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    if (i <= A_SHORT_INT_MAX && i >= A_SHORT_INT_MIN)
        return AIntToValue(i);
    else {
        AValue v = AMakeLongInt(t, 4, FALSE);
        ABool neg = i < 0;
        AInt64 ui = i < 0 ? -i : i;

        ASetLongIntDigit(v, 0, ui & 0xffff);
        ASetLongIntDigit(v, 1, (ui >> 16) & 0xffff);
        ASetLongIntDigit(v, 2, (ui >> 32) & 0xffff);
        ASetLongIntDigit(v, 3, (ui >> 48) & 0xffff);
        return ANormalize(t, AValueToLongInt(v), neg);
    }
#elif A_VALUE_BITS == 64 && A_LONG_INT_DIGIT_BITS == 32
    if (i <= A_SHORT_INT_MAX && i >= A_SHORT_INT_MIN)
        return AIntToValue(i);
    else {
        AValue v = AMakeLongInt(t, 2, FALSE);
        ABool neg = i < 0;
        AInt64 ui = i < 0 ? -i : i;

        ASetLongIntDigit(v, 0, ui & 0xffffffffU);
        ASetLongIntDigit(v, 1, ui >> 32);
        return ANormalize(t, AValueToLongInt(v), neg);
    }
#else
    /* Not supported */
    error!;
#endif
}


AValue AMakeIntU64(AThread *t, AIntU64 i)
{
#if A_VALUE_BITS == 32 && A_LONG_INT_DIGIT_BITS == 16
    if (i <= A_SHORT_INT_MAX)
        return AIntToValue(i);
    else {
        AValue v = AMakeLongInt(t, 4, FALSE);
        ASetLongIntDigit(v, 0, i & 0xffff);
        ASetLongIntDigit(v, 1, (i >> 16) & 0xffff);
        ASetLongIntDigit(v, 2, (i >> 32) & 0xffff);
        ASetLongIntDigit(v, 3, (i >> 48) & 0xffff);
        return ANormalize(t, AValueToLongInt(v), 0);
    }
#elif A_VALUE_BITS == 64
    if (i <= A_SHORT_INT_MAX)
        return AIntToValue(i);
    else {
        AValue v = AMakeLongInt(t, 2, FALSE);
        ASetLongIntDigit(v, 0, i & 0xffffffffU);
        ASetLongIntDigit(v, 1, i >> 32);
        return ANormalize(t, AValueToLongInt(v), 0);
    }
#else
    /* Not supported */
    error!;
#endif
}


AValue AMakeLongInt(AThread *t, int len, ABool isNeg)
{
    ALongInt *li = AAlloc(t, AGetLongIntSize(len));
    if (li == NULL)
        return ARaiseMemoryError(t);
    AInitLongIntBlock(li, len, isNeg);
    return ALongIntToValue(li);
}
