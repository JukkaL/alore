/* string_module.c - string module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "str.h"
#include "str_internal.h"
#include "int.h"
#include "re.h"


/* string::IsDigit(ch) */
static AValue StringIsDigit(AThread *t, AValue *frame)
{
    /* IDEA: Maybe this should be implemented for unicode. */
    
    int ch = AGetCh(t, frame[0]);
    if (ch >= '0' && ch <= '9')
        return ATrue;
    else
        return AFalse;
}


/* string::IsLetter(ch) */
static AValue StringIsLetter(AThread *t, AValue *frame)
{
    int ch = AGetCh(t, frame[0]);

    if (ch < 256) {
        if (AReIsInSet(ALetterCharSet, ch))
            return ATrue;
        else
            return AFalse;
    } else {
        if (AReIsInSet(ACharSets + A_RE_SET_SIZE * ALetterLookupTable[ch >> 8],
                      ch & 255))
            return ATrue;
        else
            return AFalse;
    }
}


/* string::IsWordChar(ch) */
static AValue StringIsWordChar(AThread *t, AValue *frame)
{
    int ch = AGetCh(t, frame[0]);

    if (ch < 256) {
        if (AReIsInSet(AWordCharSet, ch))
            return ATrue;
        else
            return AFalse;
    } else {
        if (AReIsInSet(ACharSets + A_RE_SET_SIZE *
                       AWordCharLookupTable[ch >> 8], ch & 255))
            return ATrue;
        else
            return AFalse;
    }
}


/* string::IsSpace(ch) */
static AValue StringIsSpace(AThread *t, AValue *frame)
{
    int ch = AGetCh(t, frame[0]);

    if (ch <= 32 && (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t'))
        return ATrue;
    else
        return AFalse;
}


#if 0
AValue GetRegularExpression(AThread *t, AValue regExpStr, AValue *flagVal,
                           int flags)
{
    AValue regExp;
    int i;

    /* Check if any regular exprsesion flags are given and verify their
       correctness. */
    if (!AIsDefault(flagVal[0])) {
        if (flagVal[0] == AGlobalByNum(IgnoreCaseNum))
            flags |= A_RE_NOCASE;
        else if (flagVal[0] == AGlobalByNum(LiteralNum))
            flags |= A_RE_NOSPECIAL;
        else
            return ARaiseValueErrorND(t, NULL);

        if (!AIsDefault(flagVal[1])) {
            if (flagVal[1] == AGlobalByNum(IgnoreCaseNum))
                flags |= A_RE_NOCASE;
            else if (flagVal[1] == AGlobalByNum(LiteralNum))
                flags |= A_RE_NOSPECIAL;
            else
                return ARaiseValueErrorND(t, NULL);
        }
    }
            
    /* First try to find the regular expression from the t with given
       flags and regExpStr. */
    for (i = 0; i < A_NUM_CACHED_REGEXPS; i++) {
        if (t->regExp[i * 2] == regExpStr
            && t->regExpFlags[i] == flags) {
            regExp = t->regExp[i * 2 + 1];
            goto FoundRegExp;
        }
    }
    
    /* Otherwise compile the regular expression. */
    regExp = ACompileRegExp(t, regExpStr, flags);
    if (AIsError(regExp))
        return AError;

    i--;

  FoundRegExp:

    /* Insert regular expression into the list of regular expressions. */
    for (; i > 0; i--) {
        t->regExp[i * 2]     = t->regExp[i * 2 - 2];
        t->regExp[i * 2 + 1] = t->regExp[i * 2 - 1];
        t->regExpFlags[i]    = t->regExpFlags[i - 1];
    }
    
    t->regExp[0] = regExpStr;
    t->regExp[1] = regExp;
    t->regExpFlags[0] = flags;
    
    return regExp;
}
#endif


/* This should equal or greater than the maximum length of the string
   representation of a short int (base 2); not including zero terminator and
   sign. */
#define N 100


/* string::IntToStr(i, base[, minWidth]) */
static AValue IntToStr(AThread *t, AValue *frame)
{
    int radix = AGetInt(t, frame[1]);
    int minWidth;

    if (radix < 2 || radix > 36)
        return ARaiseValueError(t, "Invalid radix");
    
    minWidth = 0;
    if (!AIsDefault(frame[2]))
        minWidth = AGetInt(t, frame[2]);

    if (AIsShortInt(frame[0])) {
        int n = AGetInt(t, frame[0]);
        ABool sign = n < 0;
        char buf[N + 2];
        int i = 0;
        
        if (sign)
            n = -n;

        do {
            buf[N - i++] = ADigits[n % radix];
            n /= radix;
        } while (n != 0);

        while (i < minWidth)
            buf[N - i++] = '0';

        if (sign)
            buf[N - i++] = '-';

        buf[N + 1] = '\0';

        return AMakeStr(t, buf + N - i + 1);
    } else {
        AExpectInt(t, frame[0]);
        return ALongIntToStr(t, frame[0], radix, minWidth);
    }
}


A_MODULE(__string, "__string")
    A_DEF("IsLetter", 1, 0, StringIsLetter)
    A_DEF("IsDigit", 1, 0, StringIsDigit)
    A_DEF("IsWordChar", 1, 0, StringIsWordChar)
    A_DEF("IsSpace", 1, 0, StringIsSpace)
    A_DEF_OPT("IntToStr", 2, 3, 0, IntToStr)
A_END_MODULE()
