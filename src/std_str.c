/* std_str.c - std::Str related operations 

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "memberid.h"
#include "mem.h"
#include "str.h"
#include "int.h"
#include "str_internal.h"
#include "internal.h"
#include "debug_runtime.h"
#include "wrappers.h"
#include "gc.h"
#include "util.h"
#include "symtable.h"
#include "std_module.h"
#include "encodings_module.h"


/* Note that the method of Str must wrap the self instance in their bodies,
   since Str objects have a special representation. */


#define INT_BUF_SIZE (1 + ((A_VALUE_INT_BITS - 1) * 4 + 12) / 13)


/* Global number of Str iterator class. */
int AStrIterNum;


/* String with the Unicode replacement character. */
static const AWideChar ReplChar[2] = { 0xfffd, '\0' };


static AValue SymbolStr(AThread *t, ASymbolInfo *sym);
static ABool IsMatchAt(AValue src, Assize_t i, AValue dst, Assize_t dstLen);
static Assize_t Find(AValue str, Assize_t i, AValue substr);
static Assize_t Count(AValue str, AValue substr, Assize_t max);


/* String iterator slot numbers */
#define STR_ITER_I 0
#define STR_ITER_LEN 1
#define STR_ITER_S 2


/* std::Str(x)
   Construct a Str object. Frame contains exactly one value. Note that this is
   implemented internally as a function, not a type.
   
   NOTE: frame may point almost anywhere! Be careful. */
AValue AStdStr(AThread *t, AValue *frame)
{
    /* IDEA: Implement repetive type checking "if" checks using a switch
             statement -- it might be a lot faster in some cases. */
    
    if (AIsShortInt(frame[0])) {
        /* Short int to Str */
        unsigned char buf[INT_BUF_SIZE];
        Assize_t ind;
        ASignedValue n;
        ABool isNeg;
        AString *s;
        Assize_t len;
        ABool result;
        unsigned char *dst;

        ind = INT_BUF_SIZE - 1;
        
        n = AValueToInt(frame[0]);
        if (n < 0) {
            n = -n;
            isNeg = TRUE;
        } else
            isNeg = FALSE;
        
        while (n >= 10) {
            buf[ind--] = n % 10 + '0';
            n /= 10;
        }
        
        buf[ind] = n + '0';

        if (isNeg)
            buf[--ind] = '-';

        len = INT_BUF_SIZE - ind;

        AAlloc_M(t, AGetBlockSize(sizeof(AValue) + len), s, result);
        if (!result)
            return ARaiseMemoryErrorND(t);

        AInitNonPointerBlock(&s->header, len);
        
        dst = s->elem;
        
        do {
            *dst++ = buf[ind++];
        } while (ind < INT_BUF_SIZE);

        return AStrToValue(s);
    } else if (AIsStr(frame[0])) {
        /* Str to Str */
        return frame[0];
    } else if (AIsInstance(frame[0])) {
        /* Generic object to Str: call the _str() method. */
        AValue v = ACallMethodByNum(t, AM__STR, 0, frame);
        /* Expect a Str object as the return value. */
        if (AIsStr(v))
            return v;
        else if (AIsError(v)) {
            if (AMemberByNum(t, frame[0], AM__STR) == AError) {
                char buf[500];
                AFormatMessage(buf, sizeof(buf), "<%q instance>",
                               AGetInstanceType(
                                   AValueToInstance(frame[0]))->sym);
                v = ACreateStringFromCStr(t, buf);
            }
            return v;
        } else
            return ARaiseTypeError(
                t, "_str of %T returned non-string (%T)", frame[0], v);
    } else if (AIsFloat(frame[0])) {
        /* Float to Str */
        /* IDEA: sprintf might produce inconsistent results compared with
                 format() of Str. Verify that they are reasonable. */
        /* IDEA: Verify that the length of the buffer is reasonable. */
        char buf[100];
        sprintf(buf, "%.10g", AValueToFloat(frame[0]));
        /* If the float is an infinity or a NaN, the result of sprintf varies
           from system to system. Normalize any non-standard values. */
        AFixInfAndNanStrings(buf);
        return ACreateStringFromCStr(t, buf);
    } else if (AIsLongInt(frame[0]))
        return ALongIntToStr(t, frame[0], 10, 0);
    else if (AIsGlobalFunction(frame[0]))
        return SymbolStr(t, AValueToFunction(frame[0])->sym);
    else if (AIsNonSpecialType(frame[0]))
        return SymbolStr(t, AValueToType(frame[0])->sym);
    else if (AIsConstant(frame[0])) {
        return SymbolStr(t, AValueToConstant(frame[0])->sym);
    } else
        return AStdRepr(t, frame); /* Fallback for other object types */
}


/* Str length() */
AValue AStrLengthMethod(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0]);
    return AMakeInt_ssize_t(t, AStrLen(A_UNWRAP(frame[0])));
}


/* Str count(str) */
AValue AStrCount(AThread *t, AValue *frame)
{
    Assize_t n;

    frame[0] = AWrapObject(t, frame[0]);
    AExpectStr(t, frame[1]);
    frame[2] = A_UNWRAP(frame[0]);

    n = Count(frame[2], frame[1], A_SSIZE_T_MAX);
    return AMakeInt_ssize_t(t, n);
}


/* Str upper() */
AValue AStrUpper(AThread *t, AValue *frame)
{
    Assize_t begIndex;
    Assize_t len;

    frame[0] = AWrapObject(t, frame[0]);
    frame[1] = A_UNWRAP(frame[0]);

    /* Use optimized alternative implementations for different string
       representations. */
      
    if (AIsNarrowStr(frame[1])) {
        AString *s;
        unsigned char *src;
        Assize_t i;
        
        begIndex = 0;
        len = AGetStrLen(AValueToStr(frame[1]));

      Narrow:

        /* FIX: perhaps inline.. */
        s = AAlloc(t, sizeof(AValue) + len);
        if (s == NULL)
            return AError;
        
        AInitNonPointerBlock(&s->header, len);

        src = AValueToStr(frame[1])->elem + begIndex;

        for (i = 0; i < len; i++) {
            if (src[i] == 255)
                goto ConvertToWide;
            s->elem[i] = AUpperCase[src[i]];
        }

        return AStrToValue(s);

      ConvertToWide:

        frame[1] = ANarrowStringToWide(t, frame[0], frame);
        if (AIsError(frame[1]))
            return AError;
        goto Wide;      
    } else if (AIsWideStr(frame[1])) {
        AWideString *s;
        AWideChar *src;
        Assize_t i;
        
        begIndex = 0;
        len = AGetWideStrLen(AValueToWideStr(frame[1]));

      Wide:

        s = AAlloc(t, sizeof(AValue) + sizeof(AWideChar) * len);
        if (s == NULL)
            return AError;
        
        AInitNonPointerBlock(&s->header, sizeof(AWideChar) * len);
        
        src = AValueToWideStr(frame[1])->elem + begIndex;

        for (i = 0; i < len; i++) {
            unsigned ch = src[i];
            s->elem[i] = AUpper(ch);
        }

        return AWideStrToValue(s);
    } else if (AIsSubStr(frame[1])) {
        ASubString *ss = AValueToSubStr(frame[1]);

        begIndex = AValueToInt(ss->ind);
        len = AValueToInt(ss->len);

        frame[1] = ss->str;
        if (AIsNarrowStr(frame[1]))
            goto Narrow;
        else
            goto Wide;
    } else
        return ARaiseTypeErrorND(t, NULL);

    /* Not reached */
}


/* Str lower() */
AValue AStrLower(AThread *t, AValue *frame)
{
    Assize_t begIndex;
    Assize_t len;    

    frame[0] = AWrapObject(t, frame[0]);
    frame[1] = A_UNWRAP(frame[0]);

    /* Use optimized alternative implementations for different string
       representations. */
    
    if (AIsNarrowStr(frame[1])) {
        AString *s;
        unsigned char *src;
        Assize_t i;
        
        begIndex = 0;
        len = AGetStrLen(AValueToStr(frame[1]));

      Narrow:

        s = AAlloc(t, sizeof(AValue) + len);
        if (s == NULL)
            return AError;
        
        AInitNonPointerBlock(&s->header, len);

        src = AValueToStr(frame[1])->elem + begIndex;

        for (i = 0; i < len; i++)
            s->elem[i] = ALowerCase[src[i]];

        return AStrToValue(s);
    } else if (AIsWideStr(frame[1])) {
        AWideString *s;
        AWideChar *src;
        Assize_t i;
        
        begIndex = 0;
        len = AGetWideStrLen(AValueToWideStr(frame[1]));

      Wide:

        s = AAlloc(t, sizeof(AValue) + sizeof(AWideChar) * len);
        if (s == NULL)
            return AError;
        
        AInitNonPointerBlock(&s->header, sizeof(AWideChar) * len);
        
        src = AValueToWideStr(frame[1])->elem + begIndex;

        for (i = 0; i < len; i++) {
            unsigned ch = src[i];
            s->elem[i] = ALower(ch);
        }

        return AWideStrToValue(s);
    } else if (AIsSubStr(frame[1])) {
        ASubString *ss = AValueToSubStr(frame[1]);

        begIndex = AValueToInt(ss->ind);
        len = AValueToInt(ss->len);

        frame[1] = ss->str;
        if (AIsNarrowStr(frame[1]))
            goto Narrow;
        else
            goto Wide;
    } else
        return ARaiseTypeErrorND(t, NULL);

    /* Not reached */
}


/* Str strip() */
AValue AStrStrip(AThread *t, AValue *frame)
{
    Assize_t i1, i2, len;
    AValue str;
    
    frame[0] = AWrapObject(t, frame[0]);
    str = A_UNWRAP(frame[0]);

    len = AStrLen(str);
    i1 = 0;
    while (i1 < len) {
        int ch = AStrItem(str, i1);
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
            break;
        i1++;
    }
    
    i2 = len - 1;
    while (i2 > i1) {
        int ch = AStrItem(str, i2);
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
            break;
        i2--;
    }

    return ASubStr(t, str, i1, i2 + 1);
}


/* Str find(str[, start]) */
AValue AStrFind(AThread *t, AValue *frame)
{
    Assize_t index;
    Assize_t startIndex;
    
    frame[0] = AWrapObject(t, frame[0]);
    
    AExpectStr(t, frame[1]);

    if (!AIsDefault(frame[2])) {
        startIndex = AGetInt(t, frame[2]);
        if (startIndex < 0)
            return ARaiseValueError(t, "Negative start index");
    } else
        startIndex = 0;

    index = Find(A_UNWRAP(frame[0]), startIndex, frame[1]);

    if (index >= 0)
        return AMakeInt_ssize_t(t, index);
    else
        return AMakeInt(t, -1);
}


/* Str index(str) */
AValue AStrIndex(AThread *t, AValue *frame)
{
    Assize_t index;
    
    frame[0] = AWrapObject(t, frame[0]);
    
    AExpectStr(t, frame[1]);

    index = Find(A_UNWRAP(frame[0]), 0, frame[1]);

    if (index >= 0)
        return AMakeInt_ssize_t(t, index);
    else
        return ARaiseValueError(t, "Substring not found");
}


/* Str format(fmt, ...) */
AValue AStrFormat(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0]);
    frame[2] = frame[1];
    frame[1] = A_UNWRAP(frame[0]);
    return AFormat(t, frame + 1);
}


/* Str startsWith(prefix) */
AValue AStrStartsWith(AThread *t, AValue *frame)
{
    Assize_t len, prefixLen, i;
    AValue str;
    
    frame[0] = AWrapObject(t, frame[0]);
    str = A_UNWRAP(frame[0]);
    AExpectStr(t, frame[1]);

    len = AStrLen(str);
    prefixLen = AStrLen(frame[1]);

    if (len < prefixLen)
        return AFalse;

    for (i = 0; i < prefixLen; i++) {
        if (AStrItem(frame[1], i) != AStrItem(str, i))
            return AFalse;
    }
    
    return ATrue;
}


/* Str endsWith(suffix) */
AValue AStrEndsWith(AThread *t, AValue *frame)
{
    Assize_t len, suffixLen, i;
    AValue str;
    
    frame[0] = AWrapObject(t, frame[0]);
    str = A_UNWRAP(frame[0]);
    AExpectStr(t, frame[1]);

    len = AStrLen(str);
    suffixLen = AStrLen(frame[1]);

    if (len < suffixLen)
        return AFalse;

    for (i = 1; i <= suffixLen; i++) {
        if (AStrItem(frame[1], suffixLen - i) != AStrItem(str, len - i))
            return AFalse;
    }
    
    return ATrue;
}


/* Str replace(old, new[, max]) */
AValue AStrReplace(AThread *t, AValue *frame)
{
    Assize_t origLen, newLen;
    Assize_t srcLen, dstLen;
    Assize_t i, j;
    Assize_t max;
    int ch;
    Assize_t n;

    frame[0] = AWrapObject(t, frame[0]);
    
    AExpectStr(t, frame[1]);
    AExpectStr(t, frame[2]);

    if (AIsDefault(frame[3]))
        max = A_SSIZE_T_MAX;
    else
        max = AGetInt(t, frame[3]);
    
    frame[3] = A_UNWRAP(frame[0]);
    origLen = AStrLen(frame[3]);
    srcLen = AStrLen(frame[1]);
    dstLen = AStrLen(frame[2]);

    /* Do nothing if the source string is empty. */
    if (srcLen == 0 || max <= 0)
        return frame[3];

    /* Calculate the length of the new string. */
    newLen = origLen;
    if (srcLen != dstLen) {
        Assize_t c = Count(frame[3], frame[1], max);
        Assize_t x;
        if (c == 0)
            return frame[3];

        /* Perform the calculation. Verify that there is no overflow. */
        x = (dstLen - srcLen) * c;
        if (x / c != dstLen - srcLen)
            return ARaiseRuntimeError(t, "Replace result too long");
        newLen += x;
        if (newLen < 0)
            return ARaiseRuntimeError(t, "Replace result too long");
    }
    
    /* Setup the new string. */
    if ((AIsNarrowStr(frame[3]) || AIsNarrowSubStr(frame[3]))
        && (AIsNarrowStr(frame[1]) || AIsNarrowSubStr(frame[1]))
        && (AIsNarrowStr(frame[2]) || AIsNarrowSubStr(frame[2])))
        frame[4] = AMakeEmptyStr(t, newLen);
    else
        frame[4] = AMakeEmptyStrW(t, newLen);

    i = j = 0;
    ch = AStrItem(frame[1], 0);
    n = 0;
    while (i <= origLen - srcLen && n < max) {
        Assize_t i0 = i;

        while (i <= origLen - srcLen
               && AStrItem(frame[3], i) != ch)
            i++;

        ACopySubStr(frame[4], j, frame[3], i0, i - i0);
        j += i - i0;

        if (i > origLen - srcLen)
            break;
        
        if (IsMatchAt(frame[3], i, frame[1], srcLen)) {
            Assize_t k;
            for (k = 0; k < dstLen; k++) {
                ASetStrItem(frame[4], j, AStrItem(frame[2], k));
                j++;
            }
            i += srcLen;
            n++;
        } else {
            ASetStrItem(frame[4], j, ch);
            i++;
            j++;
        }
    }

    ACopySubStr(frame[4], j, frame[3], i, origLen - i);
    
    return frame[4];
}


static ABool IsMatchAt(AValue src, Assize_t i, AValue dst, Assize_t dstLen)
{
    Assize_t j;
    for (j = 1; j < dstLen; j++) {
        if (AStrItem(src, i + j) !=
            AStrItem(dst, j))
            return 0;
    }
    return 1;
}


#define IS_SPLIT_SPACE(ch) \
    ((ch) == ' ' || (ch) == '\n' || (ch) == '\r' || (ch) == '\t')


/* Str split([sep[, max]]) */
AValue AStrSplit(AThread *t, AValue *frame)
{
    ssize_t strLen;
    ssize_t i, i0;
    ssize_t max;
    ssize_t n;
    AValue ss;
    
    frame[3] = frame[0];
    frame[0] = AWrapObject(t, frame[0]);

    frame[4] = AMakeArray(t, 0);

    strLen = AStrLen(frame[3]);
    if (!AIsDefault(frame[2]))
        max = AGetInt(t, frame[2]);
    else
        max = A_SSIZE_T_MAX;
    
    n = 0;
    
    if (frame[1] != ADefault && frame[1] != ANil) {
        /* Explicit separator string */
        ssize_t sepLen;
        int ch;
        
        AExpectStr(t, frame[1]);
        
        sepLen = AStrLen(frame[1]);
        if (sepLen == 0)
            return ARaiseValueError(t, "Empty separator");
        
        ch = AStrItem(frame[1], 0);
        i0 = 0;
        for (i = 0; i <= strLen - sepLen;) {
            if (AStrItem(frame[3], i) == ch
                && IsMatchAt(frame[3], i, frame[1], sepLen) && n < max) {
                ss = ASubStr(t, frame[3], i0, i);
                AAppendArray(t, frame[4], ss);
                i += sepLen;
                i0 = i;
                n++;
            } else
                i++;
        }
    } else {
        /* Run of whitespace as a separator. Whitespace at the start and the
           end of the string is ignored. */
        for (i0 = 0; i0 < strLen; i0++) {
            int ch = AStrItem(frame[3], i0);
            if (!IS_SPLIT_SPACE(ch))
                break;
        }
        if (i0 >= strLen)
            goto End;
        for (i = i0; i < strLen;) {
            int ch = AStrItem(frame[3], i);
            if (IS_SPLIT_SPACE(ch) && n < max) {
                ss = ASubStr(t, frame[3], i0, i);
                AAppendArray(t, frame[4], ss);
                for (i++; i < strLen; i++) {
                    ch = AStrItem(frame[3], i);
                    if (!IS_SPLIT_SPACE(ch))
                        break;
                }
                if (i >= strLen)
                    goto End;
                n++;
                i0 = i;
            } else
                i++;
        }
    }

    ss = ASubStr(t, frame[3], i0, strLen);
    AAppendArray(t, frame[4], ss);

  End:
    
    return frame[4];
}


/* Str join(a[, separator]) */
AValue AStrJoin(AThread *t, AValue *frame)
{
    ssize_t len;
    ssize_t strLen;
    ssize_t i;
    ssize_t ti;
    ABool isWide;
    ssize_t sepLen;

    frame[3] = frame[0];
    frame[0] = AWrapObject(t, frame[0]);

    sepLen = AStrLen(frame[3]);
    isWide = AIsWideStr(frame[3]) || AIsWideSubStr(frame[3]);

    /* Calculate the total length of the result. */
    len = ALen(t, frame[1]);
    if (len > 0)
        strLen = (len - 1) * sepLen;
    else if (len < 0)
        return AError;
    else
        strLen = 0;
    for (i = 0; i < len; i++) {
        AValue s = AGetItemAt(t, frame[1], i);
        if (!AIsNarrowStr(s) && !AIsNarrowSubStr(s)) {
            if (AIsStr(s))
                isWide = TRUE;
            else if (AIsError(s))
                return AError;
            else
                ARaiseTypeError(t, AMsgStrExpected);
        }
        strLen += AStrLen(s);
    }

    /* Optimize for the special case of a single-element sequence. */
    if (len == 1)
        return AGetItemAt(t, frame[1], 0);
    
    /* Create a new string. */
    if (isWide)
        frame[2] = AMakeEmptyStrW(t, strLen);
    else
        frame[2] = AMakeEmptyStr(t, strLen);
    
    /* Copy strings. */
    ti = 0;
    for (i = 0; i < len; i++) {
        ssize_t slen;
        AValue s = AGetItemAt(t, frame[1], i);
        
        if (!AIsStr(s)) {
            if (AIsError(s))
                return AError;
            else
                ARaiseTypeError(t, AMsgStrExpected);
        }

        slen = AStrLen(s);
        ACopySubStr(frame[2], ti, s, 0, slen);
        ti += slen;

        if (sepLen > 0 && i < len - 1) {
            ACopySubStr(frame[2], ti, frame[3], 0, sepLen);
            ti += sepLen;
        }
    }
    
    return frame[2];
}


/* Str decode(encoding[, strictness]) */
AValue AStrDecode(AThread *t, AValue *frame)
{
    ABool isStrict = TRUE;

    frame[0] = AWrapObject(t, frame[0]);
    
    /* Shift arguments by 1. */
    frame[3] = frame[2];
    frame[2] = frame[1];
    frame[1] = A_UNWRAP(frame[0]);
    
    if (AIsDefault(frame[3]))
        frame[3] = ACallValue(t, frame[2], 0, frame + 3);
    else {
        if (frame[3] == AGlobalByNum(AUnstrictNum))
            isStrict = FALSE;
        frame[3] = ACallValue(t, frame[2], 1, frame + 3);
    }
        
    if (AIsError(frame[3]))
        return AError;

    frame[4] = frame[1];
    frame[1] = ACallMethod(t, "decode", 1, frame + 3);
    if (AIsError(frame[1]))
        return AError;

    frame[2] = ACallMethod(t, "unprocessed", 0, frame + 3);
    if (AIsError(frame[2]))
        return AError;
    if (!AIsStr(frame[2]))
        ARaiseTypeError(t, AMsgStrExpected);
    if (AStrLen(frame[2]) != 0) {
        if (isStrict)
            return ARaiseByNum(t, ADecodeErrorNum, "Partial character");
        else {
            frame[3] = AMakeStrW(t, ReplChar);
            frame[1] = AConcat(t, frame[1], frame[3]);
        }
    }
    
    return frame[1];
}


/* Str encode(encoding[, strictness]) */
AValue AStrEncode(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0]);
    
    if (AIsDefault(frame[2]))
        frame[2] = ACallValue(t, frame[1], 0, frame + 2);
    else
        frame[2] = ACallValue(t, frame[1], 1, frame + 2);
    
    if (AIsError(frame[2]))
        return AError;

    frame[3] = A_UNWRAP(frame[0]);
    return ACallMethod(t, "encode", 1, frame + 2);
}


/* Str iterator() */
AValue AStrIter(AThread *t, AValue *frame)
{
    frame[1] = frame[0];
    frame[0] = AWrapObject(t, frame[0]);
    
    return ACallValue(t, AGlobalByNum(AStrIterNum), 1, frame + 1);
}


/* StrIterator create(str) */
AValue AStrIterCreate(AThread *t, AValue *frame)
{
    AValue len;
    
    AExpectStr(t, frame[1]);
    ASetMemberDirect(t, frame[0], STR_ITER_I, AZero);
    len = AIntToValue(AStrLen(frame[1]));
    ASetMemberDirect(t, frame[0], STR_ITER_LEN, len);
    ASetMemberDirect(t, frame[0], STR_ITER_S, frame[1]);
    return frame[0];
}


/* StrIterator hasNext() */
AValue AStrIterHasNext(AThread *t, AValue *frame)
{
    AValue i = AMemberDirect(frame[0], STR_ITER_I);
    AValue n = AMemberDirect(frame[0], STR_ITER_LEN);
    if (i < n)
        return ATrue;
    else
        return AFalse;
}


/* StrIterator next() */
AValue AStrIterNext(AThread *t, AValue *frame)
{
    AValue i = AMemberDirect(frame[0], STR_ITER_I);
    AValue n = AMemberDirect(frame[0], STR_ITER_LEN);

    if (i < n) {
        int ch;
        
        ch = AStrItem(AMemberDirect(frame[0], STR_ITER_S), AValueToInt(i));
        
        i += AIntToValue(1);
        AValueToInstance(frame[0])->member[STR_ITER_I] = i;
        
        return AMakeCh(t, ch);
    } else
        return ARaiseValueError(t, "No items left");
}


/* C API functions */


/* Return the length of a string. Assume v is a string. */
Assize_t AStrLen(AValue v)
{
    if (AIsNarrowStr(v))
        return AGetStrLen(AValueToStr(v));
    else if (AIsWideStr(v))
        return AGetWideStrLen(AValueToWideStr(v));
    else
        return AGetSubStrLen(AValueToSubStr(v));
}


/* Return the string item at index. This internal function only supports wide
   strings and substrings; the AStrItem macro contains a special case for
   narrow strings and should be used instead. */
AWideChar AStrItem_(AValue v, Assize_t index)
{
    if (AIsWideStr(v))
        return AGetWideStrElem(v)[index];
    else {
        ASubString *subStr = AValueToSubStr(v);
        if (AIsWideStr(subStr->str))
            return AGetWideSubStrElem(v)[index];
        else
            return AGetSubStrElem(v)[index];
    }
}


/* Set a character within a string. This should only used for newly allocated,
   uninitialized string objects, and this must be never used with shared
   Str references as Str objects (as seen by Alore code) are immutable! */
void ASetStrItem(AValue v, Assize_t index, AWideChar ch)
{
    if (AIsNarrowStr(v))
        AGetStrElem(v)[index] = ch;
    else if (AIsWideStr(v))
        AGetWideStrElem(v)[index] = ch;
    else {
        ASubString *subStr = AValueToSubStr(v);
        if (AIsWideStr(subStr->str))
            AGetWideSubStrElem(v)[index] = ch;
        else
            AGetSubStrElem(v)[index] = ch;
    }
}


/* Return the number of characters taken by the encoding of the string to
   UTF-8, without the null terminator. */
Assize_t AStrLenUtf8(AValue v)
{
    Assize_t i;
    Assize_t len;
    Assize_t utf8len;

    len = AStrLen(v);
    utf8len = 0;
    for (i = 0; i < len; i++)
        utf8len += A_UTF8_LEN(AStrItem(v, i));
    
    return utf8len;
}


/* Copy the contents of a string to a narrow buffer, including null terminator.
   Raise a direct exception if the value is not a string or if the string (+
   null) is too long to fit in the buffer. */
Assize_t AGetStr(AThread *t, AValue s, char *buf, Assize_t bufLen)
{
    Assize_t len = AGetCStrFromString(t, s, buf, bufLen);
    if (len < 0)
        ADispatchException(t);
    return len;
}


/* Copy the contents of a string to a wide buffer, including null terminator.
   Raise a direct exception if the string (+ null) is too long to fit in the
   buffer. */
Assize_t AGetStrW(AThread *t, AValue s, AWideChar *buf, Assize_t bufLen)
{
    Assize_t i;
    Assize_t len;
    
    if (!AIsStr(s))
        return ARaiseTypeError(t, AMsgStrExpected);

    len = AStrLen(s);
    if (len >= bufLen)
        return ARaiseValueError(t, "Str too long");
    for (i = 0; i < len; i++)
        buf[i] = AStrItem(s, i);
    buf[i] = '\0';
    return len;
}


/* Copy the contents of a string to a buffer, encoded in UTF-8 (including null
   terminator). Raise a direct exception if the value is not a string or if
   the result is too long to fit within the buffer. */
Assize_t AGetStrUtf8(AThread *t, AValue s, char *buf, Assize_t bufLen)
{
    Assize_t i, j;
    Assize_t len;
    Assize_t resultLen;
    
    AExpectStr(t, s);

    len = AStrLen(s);

    /* Calculate the length of the UTF-8 encoded string. */
    resultLen = 0;
    for (i = 0; i < len; i++)
        resultLen += A_UTF8_LEN(AStrItem(s, i));
    
    if (resultLen >= bufLen)
        ARaiseValueError(t, "Str too long");

    /* UTF-8 encode the input string into buf. */
    j = 0;
    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(s, i);
        if (ch <= 0x7f) {
            buf[j] = ch;
            j++;
        } else if (ch <= 0x7ff) {
            buf[j] = 0xc0 | (ch >> 6);
            buf[j + 1] = 0x80 | (ch & 0x3f);
            j += 2;
        } else {
            buf[j] = 0xe0 | (ch >> 12);
            buf[j + 1] = 0x80 | ((ch >> 6) & 0x3f);
            buf[j + 2] = 0x80 | (ch & 0x3f);
            j += 3;
        }
    }
    buf[j] = '\0';
    
    return resultLen;
}


/* Make a string from a null-terminated string. */
AValue AMakeStr(AThread *t, const char *str)
{
    AValue v = ACreateString(t, str, strlen(str));
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* Make a string from a null-terminated wide string. */
AValue AMakeStrW(AThread *t, const AWideChar *str)
{
    AValue v;
    int len;

    for (len = 0; str[len] != '\0'; len++);
    v = ACreateWideString(t, str, len);
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* Make a string from a null-terminated UTF-8 string. */
AValue AMakeStrUtf8(AThread *t, const char *str)
{
    int i, j;
    int len;
    int resultLen;
    AValue s;

    len = strlen(str);    
    resultLen = 0;

    for (i = 0; i < len; ) {
        i += A_UTF8_SKIP((unsigned char)str[i]);
        resultLen++;
    }

    if (i != len)
        goto DecodeError;
    
    s = AMakeEmptyStrW(t, resultLen);
    j = 0;
    for (i = 0; i < resultLen; i++) {
        unsigned char ch1 = str[j];
        AWideChar result;
        
        if (ch1 <= 0x7f) {
            result = ch1;
            j++;
        } else if (ch1 < 0xc2) {
            j++;
            goto DecodeError;
        } else if (ch1 < 0xe0) {
            unsigned char ch2 = str[j + 1];
            j += 2;
            if (ch2 < 0x80 || ch2 > 0xbf)
                goto DecodeError;
            result = ((ch1 & 0x1f) << 6) | (ch2 & 0x3f);
        } else {
            unsigned char ch2 = str[j + 1];
            unsigned char ch3 = str[j + 2];
            j += 3;
            if (ch2 < 0x80 || ch2 > 0xbf || ch3 < 0x80 || ch3 > 0xbf
                || (ch1 == 0xe0 && ch2 < 0xa0))
                goto DecodeError;
            result = ((ch1 & 0xf) << 12) | ((ch2 & 0x3f) << 6) | (ch3 & 0x3f);
        }
        
        ASetStrItemW(s, i, result);
    }
    
    return s;

  DecodeError:

    return ARaiseValueError(t, "Invalid UTF-8 sequence");
}


/* Make an uninitialized narrow string with the specified length. */
AValue AMakeEmptyStr(AThread *t, Assize_t len)
{
    AValue v = ACreateString(t, NULL, len);
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* Make an uninitialized wide string with the specified length. */
AValue AMakeEmptyStrW(AThread *t, Assize_t len)
{
    AWideString *s = AAlloc(t, sizeof(AValue) + sizeof(AWideChar) * len);
    if (s == NULL) {
        ADispatchException(t);
        return AError;
    }
    AInitNonPointerBlock(&s->header, sizeof(AWideChar) * len);
    return AWideStrToValue(s);
}


/* Make a substring of a string. */
AValue ASubStr(AThread *t, AValue s, Assize_t i1, Assize_t i2)
{
    AValue v = ACreateSubStr(t, s, i1, i2);
    if (AIsError(v))
        ADispatchException(t);
    return v;
}


/* If ch is a string with length 1, return the character code of the only
   character. Otherwise, raise a direct exception. */
AWideChar AGetCh(AThread *t, AValue ch)
{
    if (AIsStr(ch) && AStrLen(ch) == 1)
        return AStrItem(ch, 0);
    else {
        if (!AIsStr(ch)) 
            ARaiseTypeError(t, "Character expected (but %T found)", ch);
        else
            ARaiseValueError(t, "Character expected (but string of length "
                             "%d found)", (int)AStrLen(ch));
        return 0; /* Not reached */
    }
}


/* Make a single-character string. */
AValue AMakeCh(AThread *t, AWideChar ch)
{
    /* IDEA: Consider optimizing for speed. */
    if (ch < 256) {
        AValue s = AMakeEmptyStr(t, 1);
        ASetStrItem(s, 0, ch);
        return s;
    } else {
        AValue s = AMakeEmptyStrW(t, 1);
        ASetStrItem(s, 0, ch);
        return s;
    }
}


/* String helpers */


/* Create a narrow string with the given length. Copy the contents from the
   character buffer unless it is NULL. */
AValue ACreateString(AThread *t, const char *buf, Assize_t len)
{
    AString *str;

    str = AAlloc(t, sizeof(AValue) + len);
    if (str == NULL)
        return AError;

    AInitNonPointerBlock(&str->header, len);
    if (buf != NULL)
        ACopyMem(str->elem, buf, len);

    return AStrToValue(str);
}


/* Create a narrow string from a zero-terminated string. */
AValue ACreateStringFromCStr(AThread *t, const char *str)
{
    return ACreateString(t, str, strlen(str));
}


/* Create a wide/narrow string with the given length. Copy the contents from
   the character buffer. */
AValue ACreateWideString(AThread *t, const AWideChar *buf, Assize_t len)
{
    AWideString *wideStr;
    AString *narrowStr;
    Assize_t i;

    for (i = 0; i < len; i++) {
        if (buf[i] > 0xff)
            break;
    }

    if (i < len) {
        wideStr = AAlloc(t, sizeof(AValue) + sizeof(AWideChar) * len);
        if (wideStr == NULL)
            return AError;

        AInitNonPointerBlock(&wideStr->header, sizeof(AWideChar) * len);
        ACopyMem(wideStr->elem, buf, sizeof(AWideChar) * len);

        return AWideStrToValue(wideStr);
    } else {
        narrowStr = AAlloc(t, sizeof(AValue) + len);
        if (narrowStr == NULL)
            return AError;

        AInitNonPointerBlock(&narrowStr->header, len);
        for (i = 0; i < len; i++)
            narrowStr->elem[i] = buf[i];

        return AStrToValue(narrowStr);
    }
}


enum { BUF_STATIC, BUF_DYNAMIC };


/* Copy the contents of a string to a buffer as a C string.
   Return -1 if failed and raise an exception. Return the length of the string
   if successful. */
Assize_t AGetCStrFromString(AThread *t, AValue strVal, char *buf,
                            Assize_t bufLen)
{
    /* FIX: do not allocate memory?? */
    
    AString *str;
    Assize_t len;
    
    strVal = ANormalizeNarrowString(t, strVal);
    if (AIsError(strVal))
        return -1;

    str = AValueToStr(strVal);
    len = AGetStrLen(str);

    if (len + 1 <= bufLen) {
        ACopyMem(buf, str->elem, len);
        buf[len] = '\0';
        return len;
    }
    
    ARaiseValueErrorND(t, "String too long");
    return -1;
}


/* Allocate a buffer and copy the contents of a string to it as a C string.
   If buf != NULL, use buf to hold the contents if possible. Return a pointer
   to the buffer or NULL if unsuccessful. In that case, raise an exception. */
char *AAllocCStrFromString(AThread *t, AValue strVal, char *buf,
                           Assize_t bufLen)
{
    AString *str;
    Assize_t len;
    
    strVal = ANormalizeNarrowString(t, strVal);
    if (AIsError(strVal))
        return NULL;

    str = AValueToStr(strVal);
    len = AGetStrLen(str);

    if (len + 2 <= bufLen)
        buf[0] = BUF_STATIC;
    else {
        t->tempStack[0] = strVal;
        buf = AAllocStatic(AGetBlockSize(len + 2));
        if (buf == NULL) {
            ARaiseMemoryErrorND(t);
            return NULL;
        }
        buf[0] = BUF_DYNAMIC;
        str = AValueToStr(t->tempStack[0]);
    }

    buf++;
    ACopyMem(buf, str->elem, len);
    buf[len] = '\0';

    /* Did the original string have zero characters? */
    if (strlen(buf) < len) {
        ARaiseValueErrorND(t, NULL);
        return NULL;
    }

    return buf;
}


/* Free the buffer returned by AllocCStrFromString unless the caller-supplied
   buffer was used. */
void AFreeCStrFromString(const char *str)
{
    if (str[-1] == BUF_DYNAMIC)
        AFreeStatic((void *)(str - 1));
}


/* Convert a wide string to a narrow string. Assume wide to be either a wide
   string or a wide substring. Raise exception if unsuccessful. */
AValue AWideStringToNarrow(AThread *t, AValue wide, AValue *keep)
{
    Assize_t i;
    AWideChar *str;
    Assize_t len;
    AString *narrowStr;
    
    if (AIsSubStr(wide)) {
        str = AGetWideSubStrElem(wide);
        len = AGetSubStrLen(AValueToSubStr(wide));
    } else {
        str = AGetWideStrElem(wide);
        len = AGetWideStrLen(AValueToWideStr(wide));
    }
    
    for (i = 0; i < len; i++) {
        if (str[i] > 0xff)
            return ARaiseValueErrorND(t, NULL);
    }

    t->tempStack[0] = wide;
    if (keep != NULL)
        t->tempStack[1] = *keep;
    narrowStr = AAlloc(t, sizeof(AValue) + len);
    wide  = t->tempStack[0];
    if (keep != NULL)
        *keep = t->tempStack[1];

    if (narrowStr == NULL)
        return AError;
    
    AInitNonPointerBlock(&narrowStr->header, len);
    
    if (AIsSubStr(wide))
        str = AGetWideSubStrElem(wide);
    else
        str = AGetWideStrElem(wide);

    for (i = 0; i < len; i++)
        narrowStr->elem[i] = str[i];

    return AStrToValue(narrowStr);
}


/* Convert a narrow string to a wide string. Assume narrow to be a narrow
   string or a substring FIX???. May return a substring. FIX??? */
AValue ANarrowStringToWide(AThread *t, AValue narrow, AValue *keep)
{
    Assize_t i;
    unsigned char *str;
    Assize_t len;
    Assize_t blockSize;
    AWideString *wideStr;
    
    if (AIsSubStr(narrow)) {
        if (AIsWideStr(AValueToSubStr(narrow)->str))
            return narrow;
        len = AGetSubStrLen(AValueToSubStr(narrow));
    } else
        len = AGetStrLen(AValueToStr(narrow));

    /* FIX: Perhaps not like this.. call AllocKeep maybe??? */
    blockSize = AGetBlockSize(sizeof(AValue) + len * sizeof(AWideChar));
    if (t->heapPtr + blockSize > t->heapEnd || A_NO_INLINE_ALLOC) {
        t->tempStack[0] = narrow;
        t->tempStack[1] = *keep;
        
        wideStr = AAlloc(t, blockSize);
        
        narrow = t->tempStack[0];
        *keep  = t->tempStack[1];

        if (wideStr == NULL)
            return AError;
    } else {
        wideStr = (AWideString *)t->heapPtr;
        t->heapPtr += blockSize;
    }

    if (AIsSubStr(narrow))
        str = AGetSubStrElem(narrow);
    else
        str = AGetStrElem(narrow);

    AInitNonPointerBlock(&wideStr->header, len * sizeof(AWideChar));
    
    for (i = 0; i < len; i++)
        wideStr->elem[i] = str[i];

    return AWideStrToValue(wideStr);
}


/* Does the string only contain 8-bit character codes? Assume the argument is
   a string . */
ABool AIsNarrowStringContents(AValue str)
{
    if (AIsNarrowStr(str) || AIsNarrowSubStr(str))
        return TRUE;
    else {
        Assize_t i;
        Assize_t len;
        AWideChar *elem;

        len = AStrLen(str);
        elem = AWideStrItems(str);

        for (i = 0; i < len; i++) {
            if (elem[i] > 0xff)
                return FALSE;
        }

        return TRUE;
    }
}


/* Convert any string to a narrow string. The returned string is never a
   substring object. If the conversion is unsuccessful, raise exception. */
AValue ANormalizeNarrowString(AThread *t, AValue strVal)
{
    if (!AIsNarrowStr(strVal)) {
        if (AIsWideStr(strVal)) {
            /* Wide string to narrow string */
            strVal = AWideStringToNarrow(t, strVal, &strVal);
            if (AIsError(strVal))
                return AError;
        } else if (AIsSubStr(strVal)) {
            /* Substring to narrow string */
            ASubString *ss;
            
            ss = AValueToSubStr(strVal);
            if (AIsWideStr(ss->str)) {
                /* Wide substring */
                strVal = AWideStringToNarrow(t, strVal, &strVal);
                if (AIsError(strVal))
                    return AError;
            } else {
                /* Narrow substring */
                AString *dst;
                AString *src;
                Assize_t len;
                Assize_t ind;

                len = AValueToInt(ss->len);
                ind = AValueToInt(ss->ind);

                *t->tempStack = ss->str;
                
                dst = AAlloc(t, sizeof(AValue) + len);
                if (dst == NULL)
                    return AError;
                
                src = AValueToStr(*t->tempStack);

                AInitNonPointerBlock(&dst->header, len);
                ACopyMem(dst->elem, src->elem + ind, len);

                return AStrToValue(dst);
            }
        } else 
            return ARaiseTypeErrorND(t, AMsgStrExpectedBut, strVal);
    }

    return strVal;
}


/* Return the concatenation of two strings. */
AValue AConcatStrings(AThread *t, AValue left, AValue right)
{
    unsigned char *leftStr;
    unsigned char *rightStr;
    Assize_t leftLen;
    Assize_t rightLen;
    
    if (AIsNarrowStr(left)) {
        AString *res;
        Assize_t blockSize;
        Assize_t resultLen;
        
        leftLen = AGetStrLen(AValueToStr(left));
        leftStr = AValueToStr(left)->elem;
        
      NarrowLeft:
        
        if (AIsNarrowStr(right)) {
            rightLen = AGetStrLen(AValueToStr(right));
            rightStr = AValueToStr(right)->elem;
        } else if (AIsSubStr(right)) {
            ASubString *subStr;
            
            subStr = AValueToSubStr(right);
            if (AIsWideStr(subStr->str))
                return AConcatWideStrings(t, left, right);
            
            rightStr = AValueToStr(subStr->str)->elem +
                AValueToInt(subStr->ind);
            rightLen = AValueToInt(subStr->len);
        } else
            return AConcatWideStrings(t, left, right);
        
        resultLen = leftLen + rightLen;
        blockSize = AGetBlockSize(sizeof(AValue) + resultLen);
        
        if (t->heapPtr + blockSize > t->heapEnd || A_NO_INLINE_ALLOC) {
            t->tempStack[0] = left;
            t->tempStack[1] = right;
            
            res = AAlloc(t, blockSize);
            if (res == NULL)
                return AError;
            
            left = t->tempStack[0];
            if (AIsNarrowStr(left))
                leftStr = AGetStrElem(left);
            else
                leftStr = AGetSubStrElem(left);
            
            right = t->tempStack[1];
            if (AIsNarrowStr(right))
                rightStr = AGetStrElem(right);
            else
                rightStr = AGetSubStrElem(right);
        } else {
            res = (AString *)t->heapPtr;
            t->heapPtr += blockSize;
        }
        
        AInitNonPointerBlock(&res->header, resultLen);
        
        ACopyMem(res->elem, leftStr, leftLen);
        ACopyMem(res->elem + leftLen, rightStr, rightLen);
        
        return AStrToValue(res);
    }   
    
    if (AIsSubStr(left)) {
        ASubString *subStr;
        
        subStr = AValueToSubStr(left);
        
        if (AIsWideStr(subStr->str))
            return AConcatWideStrings(t, left, right);
        
        leftStr = AValueToStr(subStr->str)->elem +
            AValueToInt(subStr->ind);
        leftLen = AValueToInt(subStr->len);
        
        goto NarrowLeft;
    }
    
    if (AIsWideStr(left))
        return AConcatWideStrings(t, left, right);

    return ARaiseTypeErrorND(t, NULL);
}


/* Return the concatenation of two strings as a wide string. The arguments
   can be any string values. */
AValue AConcatWideStrings(AThread *t, AValue left, AValue right)
{
    Assize_t leftLen;
    Assize_t rightLen;
    Assize_t blockSize;
    AWideString *wideStr;
    AWideChar *leftStr;
    AWideChar *rightStr;

    if (AIsNarrowStr(left) || AIsNarrowSubStr(left))
        left = ANarrowStringToWide(t, left, &right);
    
    if (!AIsWideStr(right)) {
        if (AIsNarrowStr(right) || AIsNarrowSubStr(right))
            right = ANarrowStringToWide(t, right, &left);
        else if (!AIsSubStr(right))
            return ARaiseBinopTypeErrorND(t, OPER_PLUS, left, right);
    }

    if (AIsSubStr(left))
        leftLen = AGetSubStrLen(AValueToSubStr(left));
    else if (AIsWideStr(left))
        leftLen = AGetWideStrLen(AValueToWideStr(left));
    else
        return ARaiseBinopTypeErrorND(t, OPER_PLUS, left, right);

    if (AIsSubStr(right))
        rightLen = AGetSubStrLen(AValueToSubStr(right));
    else
        rightLen = AGetWideStrLen(AValueToWideStr(right));

    blockSize = AGetBlockSize(sizeof(AValue) + sizeof(AWideChar) *
                              (leftLen + rightLen));
    /* Can we use optimized inline memory allocation? */
    if (t->heapPtr + blockSize > t->heapEnd || A_NO_INLINE_ALLOC) {
        /* Ordinary allocation. */
        t->tempStack[0] = left;
        t->tempStack[1] = right;
        
        wideStr = AAlloc(t, blockSize);
        if (wideStr == NULL)
            return AError;
        
        left  = t->tempStack[0];
        right = t->tempStack[1];
    } else {
        /* Optimized allocation from nursery. */
        wideStr = (AWideString *)t->heapPtr;
        t->heapPtr += blockSize;
    }

    if (AIsSubStr(left))
        leftStr = AGetWideSubStrElem(left);
    else
        leftStr = AGetWideStrElem(left);

    if (AIsSubStr(right))
        rightStr = AGetWideSubStrElem(right);
    else
        rightStr = AGetWideStrElem(right);

    AInitNonPointerBlock(&wideStr->header,
                         (leftLen + rightLen) * sizeof(AWideChar));

    ACopyMem(wideStr->elem, leftStr, leftLen * sizeof(AWideChar));
    ACopyMem(wideStr->elem + leftLen, rightStr, rightLen * sizeof(AWideChar));

    return AWideStrToValue(wideStr);
}


#if A_MIN_SUBSTR_LEN != 5 && A_MIN_SUBSTR_LEN != 9
/* MIN_SUBSTR_LEN must be either 5 or 9 */
error
#endif


/* Create a substring of a string. Indices are 0-based. The index range is
   open at the end. */
AValue ACreateSubStr(AThread *t, AValue strVal, Assize_t begInd,
                     Assize_t endInd)
{
    Assize_t len;
    AString *str;
    AWideString *wideStr;

    if (begInd < 0) {
        begInd = AStrLen(strVal) + begInd;
        if (begInd < 0)
            begInd = 0;
    }

    if (endInd < 0)
        endInd = AStrLen(strVal) + endInd;
     else if (endInd > AStrLen(strVal))
         endInd = AStrLen(strVal);

    if (endInd < begInd) {
        begInd = 0;
        endInd = 0;
    }

    len = endInd - begInd;

    if (AIsNarrowStr(strVal)) {
        AString *newStr;
        
        str = AValueToStr(strVal);

      NarrowStr:
        
        if (len < A_MIN_SUBSTR_LEN) {
            /* The new string object is short; construct a narrow string object
               instead of substring object. */

            /* Do we use optimized inline memory allocation? */
            if (t->heapPtr + A_COPY_SUBSTR_SIZE > t->heapEnd
                || A_NO_INLINE_ALLOC) {
                /* Use generic allocation */
                *t->tempStack = strVal;
                newStr = AAlloc(t, A_COPY_SUBSTR_SIZE);
                if (newStr == NULL)
                    return AError;             
                strVal = *t->tempStack;
                str = AValueToStr(strVal);
            } else {
                /* Use inline allocation */
                newStr = (AString *)t->heapPtr;
                t->heapPtr += A_COPY_SUBSTR_SIZE;
            }

            AInitNonPointerBlock(&newStr->header, len);

            /* Round up to the nearest multiple of word size. The copying loop
               has been manually unrolled for higher efficiency.

               IDEA: Should we actually do some additional size checks to avoid
                     copying unnecessarily? */
            newStr->elem[0] = str->elem[begInd];
            newStr->elem[1] = str->elem[begInd + 1];
            newStr->elem[2] = str->elem[begInd + 2];
            newStr->elem[3] = str->elem[begInd + 3];
#if A_MIN_SUBSTR_LEN == 9
            if (len > 4) {
                newStr->elem[4] = str->elem[begInd + 4];
                newStr->elem[5] = str->elem[begInd + 5];
                newStr->elem[6] = str->elem[begInd + 6];
                newStr->elem[7] = str->elem[begInd + 7];
            }
#endif
            return AStrToValue(newStr);
        } else {
            /* Create a substr object */
            
            ASubString *ss;

          SubStr:
            
            if ((int)len < -1)
                goto IndexError;

            /* Use optimize inline memory allocation? */
            if (t->heapPtr + A_SUBSTR_SIZE > t->heapEnd
                || A_NO_INLINE_ALLOC) {
                *t->tempStack = strVal;
                ss = AAlloc(t, A_SUBSTR_SIZE);
                if (ss == NULL)
                    return AError;
                strVal = *t->tempStack;
            } else {
                ss = (ASubString *)t->heapPtr;
                t->heapPtr += A_SUBSTR_SIZE;
            }

            AInitValueBlock(&ss->header, sizeof(ASubString) - sizeof(AValue));

            ss->str = strVal;
            ss->ind = AIntToValue(begInd);
            ss->len = AIntToValue(len);

            return ASubStrToValue(ss);
        }
    } else if (AIsSubStr(strVal)) {
        ASubString *ss;

        ss = AValueToSubStr(strVal);

        begInd += AValueToInt(ss->ind);
        endInd += AValueToInt(ss->ind);

        strVal = ss->str;

        if (AIsNarrowStr(strVal)) {
            str = AValueToStr(strVal);
            goto NarrowStr;
        } else {
            wideStr = AValueToWideStr(strVal);
            goto WideStr;
        }
    } else if (AIsWideStr(strVal)) {
        AWideString *newWideStr;
        
        wideStr = AValueToWideStr(strVal);

      WideStr:

        if (len < (A_MIN_SUBSTR_LEN + 1) / 2) {
            /* Optimized inline memory allocation? */
            if (t->heapPtr + A_COPY_SUBSTR_SIZE > t->heapEnd
                || A_NO_INLINE_ALLOC) {
                /* No; generic allocation */
                *t->tempStack = strVal;
                newWideStr = AAlloc(t, A_COPY_SUBSTR_SIZE);
                if (newWideStr == NULL)
                    return AError;
                strVal = *t->tempStack;
                str = AValueToStr(strVal);
            } else {
                /* Yes; optimzied inline allocation */
                newWideStr = (AWideString *)t->heapPtr;
                t->heapPtr += A_COPY_SUBSTR_SIZE;
            }
            
            AInitNonPointerBlock(&newWideStr->header, sizeof(AWideChar) * len);

            /* Copy the characters to the target string; round the string
               length up a bit to avoid conditionals. Unrolled the loop for
               copying characters. */
            newWideStr->elem[0] = wideStr->elem[begInd];
            newWideStr->elem[1] = wideStr->elem[begInd + 1];
#if A_MIN_SUBSTR_LEN == 9
            if (len > 2) {
                newWideStr->elem[2] = wideStr->elem[begInd + 2];
                newWideStr->elem[3] = wideStr->elem[begInd + 3];
            }
#endif
            return AWideStrToValue(newWideStr);
        } else
            goto SubStr;
    } else
        return ARaiseTypeErrorND(t, NULL);

    /* Not reached */

  IndexError:    

    return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], NULL);
}


/* Compare two strings lexicographically using the character codes as keys. If
   the return value is < 0, s1 > s2. If the return value is > 0, s1 < s2.
   If the return value is 0, s1 == s2. However, if the return value is
   CMP_FAIL, there was a type error. */
int ACompareStrings(AValue s1, AValue s2)
{
    unsigned char *str1;
    AWideChar *wstr1;
    Assize_t len1;
    
    unsigned char *str2;
    AWideChar *wstr2;
    Assize_t len2;

    /* Use several optimized implementation variants for different
       representations of s1/s2. */
    
    if (AIsNarrowStr(s1)) {
        /* Narrow str as first operand */
        str1 = AValueToStr(s1)->elem;
        len1 = AGetStrLen(AValueToStr(s1));

      Narrow:
        
        if (AIsNarrowStr(s2)) {
            /* Narrow vs narrow compare */
            str2 = AValueToStr(s2)->elem;
            len2 = AGetStrLen(AValueToStr(s2));

          NarrowVsNarrow:

            while (len1 > 0 && len2 > 0) {
                if (*str1 != *str2)
                    return *str1 - *str2;
                len1--;
                len2--;
                str1++;
                str2++;
            }

            return len1 - len2;
        }

        if (AIsSubStr(s2)) {
            ASubString *ss;

            ss = AValueToSubStr(s2);
            len2 = AValueToInt(ss->len);

            if (AIsNarrowStr(ss->str)) {
                /* Narrow vs narrow substring compare */
                str2 = AValueToStr(ss->str)->elem + AValueToInt(ss->ind);
                goto NarrowVsNarrow;
            } else {
                /* Narrow vs wide substring compare */
                wstr2 = AValueToWideStr(ss->str)->elem + AValueToInt(ss->ind);
                goto NarrowVsWide;
            }
        }

        if (AIsWideStr(s2)) {
            /* Narrow vs wide compare */
            wstr2 = AValueToWideStr(s2)->elem;
            len2 = AGetWideStrLen(AValueToWideStr(s2));

          NarrowVsWide:

            while (len1 > 0 && len2 > 0) {
                if (*str1 != *wstr2)
                    return *str1 - *wstr2;
                len1--;
                len2--;
                str1++;
                wstr2++;
            }

            return len1 - len2;
        }

        return A_CMP_FAIL;
    }

    if (AIsSubStr(s1)) {
        /* Substr as first operand */
        ASubString *ss;

        ss = AValueToSubStr(s1);
        len1 = AValueToInt(ss->len);
        
        if (AIsNarrowStr(ss->str)) {
            /* Narrow substr as first operand */
            str1 = AValueToStr(ss->str)->elem + AValueToInt(ss->ind);
            goto Narrow;
        } else {
            /* Wide substr as first operand */
            wstr1 = AValueToWideStr(ss->str)->elem + AValueToInt(ss->ind);
            goto Wide;
        }
    }

    if (AIsWideStr(s1)) {
        /* Wide str as first operand */
        wstr1 = AValueToWideStr(s1)->elem;
        len1 = AGetWideStrLen(AValueToStr(s1));

      Wide:
        
        if (AIsWideStr(s2)) {
            /* Wide vs wide compare */
            wstr2 = AValueToWideStr(s2)->elem;
            len2 = AGetWideStrLen(AValueToWideStr(s2));

          WideVsWide:
            
            while (len1 > 0 && len2 > 0) {
                if (*wstr1 != *wstr2)
                    return *wstr1 - *wstr2;
                len1--;
                len2--;
                wstr1++;
                wstr2++;
            }

            return len1 - len2;
        }

        if (AIsSubStr(s2)) {
            ASubString *ss;

            ss = AValueToSubStr(s2);
            len2 = AValueToInt(ss->len);

            if (AIsNarrowStr(ss->str)) {
                /* Wide vs narrow substr compare */
                str2 = AValueToStr(ss->str)->elem + AValueToInt(ss->ind);
                goto WideVsNarrow;
            } else {
                /* Wide vs wide substr compare */
                wstr2 = AValueToWideStr(ss->str)->elem + AValueToInt(ss->ind);
                goto WideVsWide;
            }
        }

        if (AIsNarrowStr(s2)) {
            /* Wide vs narrow compare */
            str2 = AValueToStr(s2)->elem;
            len2 = AGetStrLen(AValueToStr(s2));

          WideVsNarrow:

            while (len1 > 0 && len2 > 0) {
                if (*wstr1 != *str2)
                    return *wstr1 - *str2;
                len1--;
                len2--;
                wstr1++;
                str2++;
            }

            return len1 - len2;
        }
    }

    return A_CMP_FAIL;
}


/* Create a string that is formed by concatenating the argument string num
   times. Asssume that strVal is a string. */
AValue ARepeatString(AThread *t, AValue strVal, Assize_t num)
{
    Assize_t len;
    Assize_t ind;

    *t->tempStack = strVal;

    if (AIsNarrowStr(strVal)) {
        AString *s;
        unsigned char *dst;
        unsigned char *src;
        Assize_t size;

        len = AGetStrLen(AValueToStr(strVal));
        ind = 0;

      Narrow:

        size = num * len;
        s = AAlloc(t, sizeof(AValue) + size);
        if (s == NULL)
            return AError;

        AInitNonPointerBlock(&s->header, size);

        if (len > 0) {
            dst = s->elem;
            src = AValueToStr(*t->tempStack)->elem + ind;

            if (len == 1) {
                /* Optimize the case where we are repeating a single
                   character. */
                memset(dst, src[0], num);
            } else {
                while (num > 0) {
                    Assize_t i;
                    
                    i = 0;
                    do {
                        dst[i] = src[i];
                        i++;
                    } while (i < len);
                    
                    dst += len;
                    num--;
                }
            }
        }
        
        return AStrToValue(s);
    }

    if (AIsWideStr(strVal)) {
        AWideString *s;
        AWideChar *dst;
        AWideChar *src;
        unsigned size;

        len = AGetWideStrLen(AValueToWideStr(strVal));
        ind = 0;

      Wide:

        size = num * len * sizeof(AWideChar);
        s = AAlloc(t, sizeof(AValue) + size);
        if (s == NULL)
            return AError;

        AInitNonPointerBlock(&s->header, size);

        if (len > 0) {
            dst = s->elem;
            src = AValueToWideStr(*t->tempStack)->elem + ind;

            while (num > 0) {
                Assize_t i;
                
                i = 0;
                do {
                    dst[i] = src[i];
                    i++;
                } while (i < len);

                dst += len;
                num--;
            }
        }
        
        return AWideStrToValue(s);
    }

    {
        /* Substring */

        ASubString *ss;

        ss = AValueToSubStr(strVal);
        ind = AValueToInt(ss->ind);
        len = AValueToInt(ss->len);
        strVal = ss->str;
        *t->tempStack = strVal;
        
        if (AIsNarrowStr(strVal))
            goto Narrow;
        else
            goto Wide;
    }
}


/* Concatenate a Str object and a C string. */
AValue AConcatStringAndCStr(AThread *t, AValue str, const char *cstr)
{
    AValue cstrVal;
    
    *t->tempStack = str;
    cstrVal = ACreateStringFromCStr(t, cstr);
    if (AIsError(cstrVal))
        return cstrVal;

    return AConcatStrings(t, *t->tempStack, cstrVal); 
}


/* Check if substr is included in a string. Assume str is string, but substr
   can be of any value. Return Int value 0 if the string could not be found,
   Int value 1 if it could be found. Raise a non-direct exception and return
   AError if there was a type error. */
AValue AIsInString(AThread *t, AValue str, AValue substr)
{
    if (!AIsStr(substr))
        return ARaiseBinopTypeErrorND(t, OPER_IN, substr, str);

    return Find(str, 0, substr) >= 0 ? AIntToValue(1) : AZero;
}


/* Return the hash value of a string. Assume str is a string. */
AValue AStringHashValue(AValue str)
{
    Assize_t len;
    unsigned char *s;
    AWideChar *ws;
    Assize_t i;
    unsigned hashValue = 0;

    len = AStrLen(str);
    
    if (AIsNarrowStr(str)) {
        s = AValueToStr(str)->elem;
        for (i = 0; i < len; i++)
            hashValue += hashValue * 32 + s[i];
    } else if (AIsWideStr(str)) {
        ws = AValueToWideStr(str)->elem;
        for (i = 0; i < len; i++)
            hashValue += hashValue * 32 + ws[i];
    } else {
        ASubString *subStr = AValueToSubStr(str);
        if (AIsWideStr(subStr->str)) {
            ws = AGetWideSubStrElem(str);
            for (i = 0; i < len; i++)
                hashValue += hashValue * 32 + ws[i];
        } else {
            s = AGetSubStrElem(str);
            for (i = 0; i < len; i++)
                hashValue += hashValue * 32 + s[i];
        }
    }

    return AIntToValue(hashValue);
}


/* Return the representation of a string, equivalent to std::Repr(str). Assume
   *strPtr is a string. */
AValue AStrRepr(AThread *t, AValue *strPtr)
{
    AValue str;
    Assize_t len;
    AValue resultStr;
    Assize_t resultLen;
    Assize_t i;
    Assize_t ri;
    unsigned char *resultElem;
    char quote;
    char hasSingleQuote;
    char hasDoubleQuote;

    str = *strPtr;
    len = AStrLen(str);

    hasDoubleQuote = FALSE;
    hasSingleQuote = FALSE;

    for (i = 0; i < len; i++) {
        int item = AStrItem(str, i);
        if (item == '\'')
            hasSingleQuote = TRUE;
        else if (item == '"')
            hasDoubleQuote = TRUE;
    }

    quote = (!hasSingleQuote || hasDoubleQuote) ? '\'' : '"';
    
    /* Calculate the length of the result string. */
    resultLen = 2;
    for (i = 0; i < len; i++) {
        int item = AStrItem(str, i);
        
        if (item < 32 || item >= 127)
            resultLen += 6;
        else if (item == quote)
            resultLen += 2;
        else
            resultLen++;
    }
    
    resultStr = AMakeEmptyStr(t, resultLen);

    resultElem = AGetStrElem(resultStr);
    resultElem[0] = quote;
    ri = 1;

    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(str, i);
        if (ch < 32 || ch >= 127) {
            char buf[7];
            sprintf(buf, "\\u%04x", ch);
            ACopyMem(resultElem + ri, buf, 6);
            ri += 6;
        } else {
            resultElem[ri++] = ch;
            if (ch == quote)
                resultElem[ri++] = ch;
        }
    }

    resultElem[ri] = quote;
    return resultStr;
}


/* Return a Str object that represents the fully-qualified name of a symbol. */
static AValue SymbolStr(AThread *t, ASymbolInfo *sym)
{
    char buf[1025];
    if (sym == NULL)
        strcpy(buf, "nil");
    else
        AFormatMessage(buf, 1025, "%q", sym);
    /* FIX: Don't use a fixed-size buffer. */
    return AMakeStr(t, buf);
}


/* If buf points to a non-standard string representation of a floating point
   (-)infinity or NaN, convert it to the standard representation (inf, -inf or
   nan). Note that here "standard" refers to standard Alore behaviour, not
   standard C behaviour... */
void AFixInfAndNanStrings(char *buf)
{
    if (buf[0] == 'I' && strcmp(buf, "Infinity") == 0)
        strcpy(buf, "inf");
    else if (buf[0] == 'N' && strcmp(buf, "NaN") == 0)
        strcpy(buf, "nan");
    else if (buf[0] == '-') {
        if (buf[1] == 'I' && strcmp(buf, "-Infinity") == 0)
            strcpy(buf, "-inf");
        else if (buf[1] == 'N' && strcmp(buf, "-NaN") == 0)
            strcpy(buf, "nan");
        else if (buf[1] == 'n' && strcmp(buf, "-nan") == 0)
            strcpy(buf, "nan");
    }
#ifdef A_HAVE_WINDOWS
    /* At least mingw has rather peculiar representations for infinities and
       not-a-numbers. */
    if (buf[0] == '1' && buf[1] == '.' && buf[2] == '#') {
        if (strcmp(buf, "1.#INF") == 0)
            strcpy(buf, "inf");
        else if (strcmp(buf, "1.#IND") == 0)
            strcpy(buf, "nan");
    } else if (buf[0] == '-' && buf[1] == '1' && buf[2] == '.'
               && buf[3] == '#') {
        if (strcmp(buf, "-1.#INF") == 0)
            strcpy(buf, "-inf");
        else if (strcmp(buf, "-1.#IND") == 0)
            strcpy(buf, "nan");
    }
#endif
}


/* Equivalent to separator.join(sequence). Separator may be ADefault for
   an empty separator. Assume separator is a string (or ADefault). */
AValue AJoin(AThread *t, AValue sequence, AValue separator)
{
    AValue *frame = AAllocTemps(t, 4);
    AValue result;

    frame[1] = sequence;
    if (!AIsDefault(separator))
        frame[0] = separator;
    else
        frame[0] = AMakeStr(t, "");
    result = AStrJoin(t, frame);

    AFreeTemps(t, 4);

    return result;
}


/* Find leftmost instance of sub-string in a string starting from the specified
   index (0 == start of string). Return -1 if not found, otherwise return the
   index at which the substring was found. Assume str and substr are Str
   values.*/
static Assize_t Find(AValue str, Assize_t i, AValue substr)
{
    Assize_t len1;
    Assize_t len2;
    int ch;
    Assize_t j;
    
    len1 = AStrLen(str);
    len2 = AStrLen(substr);

    if (len2 == 0) {
        if (i <= len1)
            return i;
        else
            return -1;
    }

    /* Get the string start character. */
    ch = AStrItem(substr, 0);

    for (; i <= len1 - len2; i++) {
        if (AStrItem(str, i) == ch) {
            /* Potential match. Verify it. */
            for (j = 1; j < len2; j++) {
                if (AStrItem(str, i + j) != AStrItem(substr, j))
                   break;
            }
            /* Found a match? */
            if (j >= len2)
                return i;
        }
    }
    
    return -1;
}


/* Count the number of time substr appears within str. The max argument is an
   upper bound for the result. */
static Assize_t Count(AValue str, AValue substr, Assize_t max)
{
    Assize_t len1 = AStrLen(str);
    Assize_t len2 = AStrLen(substr);
    Assize_t count = 0;
    Assize_t i = 0;
    Assize_t j;
    int ch;

    ch = AStrItem(substr, 0);

    while (i <= len1 - len2) {
        if (AStrItem(str, i) == ch) {
            /* Potential match. Verify it. */
            for (j = 1; j < len2; j++) {
                if (AStrItem(str, i + j) !=
                    AStrItem(substr, j))
                   break;
            }
            /* Found a match? */
            if (j >= len2) {
                count++;
                if (count == max)
                    return count;
                i += len2;
            } else
                i++;
        } else
            i++;
    }
    return count;
}


void ACopySubStr(AValue dst, ssize_t dstIndex, AValue src, ssize_t srcIndex,
                 ssize_t srcLen)
{
    if (AIsNarrowStr(dst)) {
        /* narrow -> narrow (assume src is narrow) */
        const unsigned char *srcItems = ANarrowStrItems(src);
        unsigned char *dstItems = AGetStrElem(dst);
        memcpy(dstItems + dstIndex, srcItems + srcIndex, srcLen);
    } else {
        AWideChar *dstItems = AGetWideStrElem(dst);
        
        if (AIsWideStr(src) || AIsWideSubStr(src)) {
            /* wide -> wide */
            const AWideChar *srcItems = AWideStrItems(src);
            memcpy(dstItems + dstIndex, srcItems + srcIndex,
                   srcLen * sizeof(AWideChar));
        } else {
            /* narrow -> wide */
            ssize_t i;
            const unsigned char *srcItems = ANarrowStrItems(src);
            for (i = 0; i < srcLen; i++)
                dstItems[dstIndex + i] = srcItems[srcIndex + i];
        }
    }
}
