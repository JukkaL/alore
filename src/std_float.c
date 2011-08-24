/* std_float.c - std::Float related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "memberid.h"
#include "float.h"
#include "int.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"
#include "str.h"
#include "util.h"

#include <stdlib.h>


static ABool TryConvertStrToSpecialFloat(char *s, double *f);
static AValue RaiseValueErrorForInvalidFloatStr(AThread *t, AValue str);


AValue AStdFloat(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0]))
        return ACreateFloat(t, (double)AValueToInt(frame[0]));
    else if (AIsStr(frame[0])) {
        char buf[64];
        char *s;
        char *end;
        double f;
        AValue ret;

        s = AAllocCStrFromString(t, frame[0], buf, 64);
        if (s  == NULL)
            return AError;

        /* FIX: strtod is too relaxed about the whitespace that it accepts. */
        /* FIX: Denormalized numbers may not be converted properly, at least in
                mingw. Perhaps if the result is 0.0, use a custom strtod
                replacement that works better (for example, one that used to
                be implemented in strtonum.c). */
        f = strtod(s, &end);
        
        if (end == s) {
            /* Even though strtod failed, s might represent a valid special
               float value such as "inf". */
            if (TryConvertStrToSpecialFloat(s, &f))
                ret = ACreateFloat(t, f);
            else
                ret = RaiseValueErrorForInvalidFloatStr(t, frame[0]);
        } else {
            while (*end == ' ' || *end == '\t')
                end++;
            if (*end != '\0')
                ret = RaiseValueErrorForInvalidFloatStr(t, frame[0]);
            else
                ret = ACreateFloat(t, f);
        }
        
        AFreeCStrFromString(s);

        return ret;
    } else if (AIsFloat(frame[0]))
        return frame[0];
    else if (AIsLongInt(frame[0]))
        return ACreateFloatFromLongInt(t, frame[0]);
    else {
        /* Call the _float method. */
        AValue v = ACallMethodByNum(t, AM__FLOAT, 0, frame);
        /* Expect a Float object. */
        if (AIsFloat(v))
            return v;
        else if (AIsError(v))
            return v;
        else
            return ARaiseTypeErrorND(t, "_float of %T returned non-float (%T)",
                                     frame[0], v);
    }
}


/* Raise value error related to invalid Str argument to std::Float. */
static AValue RaiseValueErrorForInvalidFloatStr(AThread *t, AValue str)
{
    char repr[200];
    AGetRepr(t, repr, sizeof(repr), str);
    return ARaiseValueError(t, "Invalid string argument: %s", repr);
}


/* C API functions */


double AGetFloat_(AThread *t, AValue v)
{
    if (AIsShortInt(v))
        return AValueToInt(v);
    else if (AIsLongInt(v))
        return ALongIntToFloat(v);
    else
        return ARaiseTypeError(t, AMsgFloatExpected);
}


AValue AMakeFloat(AThread *t, double f)
{
    /* IDEA: Optimize this. */
    AValue v = ACreateFloat(t, f);
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* Determine whether f is IEEE 754 positive or negative infinity. */
ABool AIsInf(double f)
{
    return (f > 0.0 || f < 0.0) && f + f == f;
}


/* Determine whether f is IEEE NaN (Not-a-Number). */
ABool AIsNaN(double f)
{
    return f != f;
}


/* Helpers */


/* NOTE: Never raises a direct exception.
   IDEA: Rename to AMakeFloatND for consistency. */
AValue ACreateFloat(AThread *t, double floatNum)
{
    double *floatPtr = AAlloc(t, sizeof(double));

    if (floatPtr == NULL)
        return AError;

    *floatPtr = floatNum;

    return AFloatPtrToValue(floatPtr);
}


double ALongIntToFloat(AValue liVal)
{
    ALongInt *l;
    int i;
    double d;

    l = AValueToLongInt(liVal);
    i = AGetLongIntLen(l) - 1;
    d = 0.0;
    do {
        d = d * A_LONGINT_BASE + l->digit[i];
        i--;
    } while (i >= 0);

    if (AGetLongIntSign(l))
        return -d;
    else
        return d;
}


AValue AFloatHashValue(double f)
{
    unsigned long hash;
    int i;
    
    union {
        double f;
        unsigned long arr[sizeof(double) / sizeof(unsigned long)];
    } v;
    v.f = f;
    
    hash = v.arr[0];
    for (i = 1; i < sizeof(double) / sizeof(unsigned long); i++)
        hash = 33 * hash + v.arr[i];

    return AIntToValue(hash);
}


/* If s is a valid representation of an infinity or a nan, store the relevant
   value (infinity or nan) to *f and return TRUE. Otherwise, return FALSE.
   
   NOTE: May modify the contents of s. */
static ABool TryConvertStrToSpecialFloat(char *s, double *f)
{
    int l;
    
    /* Spaces and tabs are ignored at the start and at the end of the
       string so strip them. */
    while (*s == ' ' || *s == '\t')
        s++;
    l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t')) {
        s[l - 1] = '\0';
        l--;
    }

    if (strcmp(s, "inf") == 0) {
        /* Positive infinity */
        double a = 1e300;
        *f = a * a; /* Overflow */
        return TRUE;
    } else if (strcmp(s, "-inf") == 0) {
        /* Negative infinity */
        double a = 1e300;
        *f = -a * a; /* Overflow (negative)*/
        return TRUE;
    } else if (strcmp(s, "nan") == 0) {
        /* Not-a-number */
        double a = 1e300;
        *f = a * a - a * a;
        return TRUE;
    }
    
    return FALSE;
}
