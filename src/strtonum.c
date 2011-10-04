/* strtonum.c - Str -> Int/Float conversion routines

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "int.h"
#include "float.h"
#include "internal.h"

#include <stdlib.h>
#include <errno.h>


/* A special value to indicate integer overflow. */
#define OVERFLOW (A_SHORT_INT_MAX + 1)

/* Maximum length of the string representation of a floating point number. */
#define MAX_FLOAT_LEN 2048


/* Converts a string to an integer. Returns a pointer to the first character
   not processed. Stores the resulting value in *VAL. If there was an error
   during conversion, *valPtr is AError and t contains the exception. */
unsigned char *AStrToInt(AThread *t, const unsigned char *beg,
                         const unsigned char *end, AValue *valPtr)
{
    AValue num;
    ABool isNeg;
    const unsigned char *str;

    num = 0;
    isNeg = FALSE;

    /* Skip whitespace at the beginning of the string. */
    while (beg != end && (*beg == ' ' || *beg == '\t'))
        beg++;

    /* Handle sign. */
    if (beg < end - 1) {
        if (*beg == '-') {
            isNeg = TRUE;
            beg++;
        } else if (*beg == '+')
            beg++;
    }

    /* Convert to integer. Check overflow. */
    for (str = beg; str != end && AIsDigit(*str); str++) {
        int digit = *str - '0';

        if (num >= (A_SHORT_INT_MAX + 1) / 10
            && (num > (A_SHORT_INT_MAX + 1) / 10
                || digit > ((A_SHORT_INT_MAX + 1) -
                            ((A_SHORT_INT_MAX + 1) / 10) * 10)))
            goto ConvertLongInt;

        num = num * 10 + digit;
    }

    if (str == beg)
        *valPtr = ARaiseValueErrorND(t, NULL);
    else if (isNeg)
        *valPtr = AIntToValue(-num);
    else if (num == A_SHORT_INT_MAX + 1)
        *valPtr = ACreateLongIntFromIntND(t, num);
    else
        *valPtr = AIntToValue(num);

    return (unsigned char *)str;

  ConvertLongInt:

    {
        ALongInt *li;
        AValue val;

        val = ACreateLongIntFromIntND(t, num);
        if (AIsError(val))
            goto Error;

        li = AValueToLongInt(val);

        do {
            li = AMulAddSingle(t, li, 10, *str - '0');
            if (li == NULL)
                goto Error;
            str++;
        } while (str != end && AIsDigit(*str));

        *valPtr = ALongIntToValue(li);
        return (unsigned char *)str;
    }

  Error:

    while (str != end && AIsDigit(*str))
        str++;

    *valPtr = AError;
    return (unsigned char *)str;
}


/* Converts a string to a float. Returns a pointer to the first character not
   processed. Stores the resulting value in *val. If there was an error during
   conversion, *val is AError. A prefix of the string must represent a valid
   Alore floating point literal. */
unsigned char *AStrToFloat(AThread *t, const unsigned char *beg,
                           const unsigned char *end, AValue *val)
{
    ABool isErr = FALSE;
    const unsigned char *numBeg;
    const unsigned char *str;

    /* Check that the input is a valid Alore floating point literal. */

    str = beg;

    /* Skip whitespace at the beginning of the string. */
    while (str != end && (*str == ' ' || *str == '\t'))
        str++;

    /* Skip sign. */
    if (str < end - 1) {
        if (*str == '-' || *str == '+')
            str++;
    }

    numBeg = str;

    /* Skip integer part. */
    while (str != end && AIsDigit(*str))
        str++;

    /* Skip fractional part. */
    if (str < end - 1 && *str == '.' && AIsDigit(*(str + 1))) {
        str++;
        do {
            str++;
        } while (str != end && AIsDigit(*str));
    }

    /* Skip exponent. */
    if (str < end - 1 && (*str == 'e' || *str == 'E')) {
        str++;

        if (str < end - 1) {
            if (*str == '-' || *str == '+')
                str++;
        }

        if (AIsDigit(*str)) {
            do {
                str++;
            } while (str != end && AIsDigit(*str));
        } else
            isErr = TRUE;
    }

    if (str == numBeg || isErr)
        *val = AError;
    else {
        char buf[MAX_FLOAT_LEN + 1];

        /* We have a maximum length for floating point strings. */
        if (str - beg > MAX_FLOAT_LEN)
            *val = AError;
        else {
            double num;

            /* Copy the floating point number string to a temporary buffer
               as a null-terminated string. */
            memcpy(buf, beg, str - beg);
            buf[str - beg] = '\0';

            /* Convert the string to a floating point number. We have already
               checked the string to be valid, so we don't bother checking that
               the call succeeds. */
            num = strtod(buf, NULL);

            /* strtod may update errno. */
            errno = 0;

            *val = ACreateFloat(t, num);
        }
    }

    return (unsigned char *)str;
}
