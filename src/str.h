/* str.h - Str operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef STR_H_INCL
#define STR_H_INCL

#include "thread.h"
#include "mem.h"


AValue AStdStr(AThread *t, AValue *frame);

AValue AStrLengthMethod(AThread *t, AValue *frame);
AValue AStrIter(AThread *t, AValue *frame);
AValue AStrCount(AThread *t, AValue *frame);
AValue AStrUpper(AThread *t, AValue *frame);
AValue AStrLower(AThread *t, AValue *frame);
AValue AStrStrip(AThread *t, AValue *frame);
AValue AStrFind(AThread *t, AValue *frame);
AValue AStrIndex(AThread *t, AValue *frame);
AValue AStrFormat(AThread *t, AValue *frame);
AValue AStrStartsWith(AThread *t, AValue *frame);
AValue AStrEndsWith(AThread *t, AValue *frame);
AValue AStrEncode(AThread *t, AValue *frame);
AValue AStrDecode(AThread *t, AValue *frame);

AValue AStrIterCreate(AThread *t, AValue *frame);
AValue AStrIterHasNext(AThread *t, AValue *frame);
AValue AStrIterNext(AThread *t, AValue *frame);


extern int AStrIterNum;


#define AGetStrLen(s) \
    (AGetNonPointerBlockDataLength(&(s)->header))

#define AGetStrLenAsInt(s) \
    (AGetNonPointerBlockDataLengthAsInt(&(s)->header))

#define AGetWideStrLen(s) \
    (AGetNonPointerBlockDataLength(&(s)->header) / sizeof(AWideChar))

#define AGetWideStrElem(s) \
    (AValueToWideStr(s)->elem)


#define AGetSubStrLen(s) \
    AValueToInt((s)->len)

#define AGetSubStrIndex(s) \
    AValueToInt((s)->ind)

#define AGetSubStrElem(s) \
    (AValueToStr(AValueToSubStr(s)->str)->elem + \
     AValueToInt(AValueToSubStr(s)->ind))

#define AGetWideSubStrElem(s) \
    (AValueToWideStr(AValueToSubStr(s)->str)->elem + \
     AValueToInt(AValueToSubStr(s)->ind))

#define ANarrowStrItems(s) \
    (AIsNarrowStr(s) ? AGetStrElem(s) : AGetSubStrElem(s))

#define AWideStrItems(s) \
    (AIsWideStr(s) ? AGetWideStrElem(s) : AGetWideSubStrElem(s))


#define ASetStrItemW(v, index, ch) \
    (AGetWideStrElem(v)[i] = (ch))


AValue AConcatStrings(AThread *t, AValue left, AValue right);
AValue AConcatStringAndCStr(AThread *t, AValue str, const char *cstr);
AValue ACreateSubStr(AThread *t, AValue strVal, Assize_t begInd,
                     Assize_t endInd);

AValue AConcatWideStrings(AThread *t, AValue left, AValue right);
ABool AIsNarrowStringContents(AValue str);
AValue AWideStringToNarrow(AThread *t, AValue wide, AValue *keep);
AValue ANormalizeNarrowString(AThread *t, AValue strVal);
AValue ANarrowStringToWide(AThread *t, AValue narrow, AValue *keep);

int ACheckChrStr(AThread *t, AValue v);

int ACompareStrings(AValue s1, AValue s2);
#define A_CMP_FAIL 0x10000

AValue ARepeatString(AThread *t, AValue strVal, Assize_t num);

AValue ACreateString(AThread *t, const char *buf, Assize_t len);
AValue ACreateStringFromCStr(AThread *t, const char *str);
AValue ACreateWideString(AThread *t, const AWideChar *buf, Assize_t len);

Assize_t AGetCStrFromString(AThread *t, AValue strVal, char *buf,
                            Assize_t bufLen);
char *AAllocCStrFromString(AThread *t, AValue strVal, char *buf,
                           Assize_t bufLen);
void AFreeCStrFromString(const char *str);

void ACopySubStr(AValue dst, ssize_t dstIndex, AValue src, ssize_t srcIndex,
                 ssize_t srcLen);

AValue AIsInString(AThread *t, AValue strVal, AValue item);
AValue AStringHashValue(AValue str);
AValue AStrRepr(AThread *t, AValue *strPtr);


AValue AStringJoin(AThread *t, AValue *frame); /* FIX remove */
AValue AJoin(AThread *t, AValue sequence, AValue separator);
AValue AFormat(AThread *t, AValue *frame);
AValue AStrReplace(AThread *t, AValue *frame);
AValue AStrSplit(AThread *t, AValue *frame);
AValue AStrJoin(AThread *t, AValue *frame);


#ifdef A_NEWLINE_CHAR2
#define AIsNewLineChar(c) ((c) == A_NEWLINE_CHAR1 || (c) == A_NEWLINE_CHAR2)
#else
#define AIsNewLineChar(c) ((c) == A_NEWLINE_CHAR1)
#endif


#define A_SUBSTR_SIZE AGetBlockSize(sizeof(ASubString))

#define A_MIN_SUBSTR_LEN (AGetBlockSize(A_VALUE_SIZE + 1) - A_VALUE_SIZE + 1)
#define A_COPY_SUBSTR_SIZE AGetBlockSize(A_VALUE_SIZE + 1)


#define A_UTF8_SKIP(ch) \
    ((ch) < 0xc2 ? 1 : (ch) < 0xe0 ? 2 : 3)
#define A_UTF8_SKIP_W(ch) \
    ((ch) < 0xc2 ? 1 : (ch) < 0xe0 ? 2 : (ch) <= 0xff ? 3 : 1)
#define A_UTF8_LEN(ch) ((ch) <= 0x7f ? 1 : (ch) <= 0x7ff ? 2 : 3)


#endif
