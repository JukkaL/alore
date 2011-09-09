/* encodings_module.c - encodings module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "encodings_module.h"
#include "str.h"
#include "gc.h"


/* Size of the reverse mapping (unicode => 8-bit) hash table of 8-bit
   encodings. Chosen to be twice the maximum number of items to avoid long
   chains. */
#define REVERSE_MAP_SIZE 256
#define REVERSE_MAP_MASK (REVERSE_MAP_SIZE - 1)

/* Hash function used for reverse mapping hash tables of 8-bit encodings */
#define HASH(n) ((n) * 3)


/* Runtime representation of of a 8-bit encoding. These are built on demand
   from more compact binary representations (encodings_tables.c). */
typedef struct {
    /* Mapping from codes 128..255 to Unicode */
    AWideChar map[128];
    /* Hash table from Unicode to codes 128..255 */
    struct {
        AWideChar src;
        AWideChar dst;
    } reverseMap[REVERSE_MAP_SIZE];
} EightBitCodecInfo;


/* These are used for indexing the ACodecInfo array. The definitions must
   match the order of the ACodecInfo array. */
enum {
    CODEC_Iso8859_2,
    CODEC_Iso8859_3,
    CODEC_Iso8859_4,
    CODEC_Iso8859_5,
    CODEC_Iso8859_6,
    CODEC_Iso8859_7,
    CODEC_Iso8859_8,
    CODEC_Iso8859_9,
    CODEC_Iso8859_10,
    CODEC_Iso8859_11,
    CODEC_Iso8859_13,
    CODEC_Iso8859_14,
    CODEC_Iso8859_15,
    CODEC_Iso8859_16,
    CODEC_Ascii,
    CODEC_Cp437,
    CODEC_Cp850,
    CODEC_Windows1250,
    CODEC_Windows1251,
    CODEC_Windows1252,
    CODEC_Koi8R,
    CODEC_Koi8U,
    NUM_8_BIT_CODECS
};


/* Realized 8-bit encodings. Initially all are NULL, and this array is
   populated as 8-bit encodings are used.

   NOTE: Currently assume access to this array does not need to be
         synchronized; this might not be a safe assumption. Note that
         initializing an encoding simultenaously by multiple threads is
         acceptable; it only leaks (a little) memory. */
static EightBitCodecInfo *Codecs[NUM_8_BIT_CODECS];


/* Global numbers of various definitions in the module */

int AStrictNum;   /* encodings::Strict */
int AUnstrictNum; /* encodings::Unstrict */

int AUtf8Num;     /* encodings::Utf8 */

int ALatin1Num;   /* encodings::LatinN */
int ALatin2Num;
int ALatin3Num;
int ALatin4Num;
int ALatin5Num;
int ALatin6Num;
int ALatin7Num;
int ALatin8Num;
int ALatin9Num;
int ALatin10Num;

int AIso8859_1Num;   /* encodings::Iso8859_N */
int AIso8859_2Num;
int AIso8859_3Num;
int AIso8859_4Num;
int AIso8859_5Num;
int AIso8859_6Num;
int AIso8859_7Num;
int AIso8859_8Num;
int AIso8859_9Num;
int AIso8859_10Num;
int AIso8859_11Num;
int AIso8859_13Num;
int AIso8859_14Num;
int AIso8859_15Num;
int AIso8859_16Num;
int AAsciiNum;        /* encodings::Ascii */
int AKoi8RNum;        /* encodings::Koi8R */
int AKoi8UNum;        /* encodings::Koi8U */

int ADecodeErrorNum;
int AEncodeErrorNum;

static int ACp437Num;
static int ACp850Num;
static int AWindows1250Num;
static int AWindows1251Num;
static int AWindows1252Num;


/* The replacement character as a string */
static const AWideChar ReplChar[2] = { 0xfffd, '\0' };


/* Strictness constants */
enum {
    MODE_STRICT,
    MODE_UNSTRICT
};


#define ENC_DATA_SIZE 8


/* encodings::Decode(str, encoding[, strictness])
   [Deprecated] This is almost identical to decode method of std::Str. */
static AValue Decode(AThread *t, AValue *frame)
{
    ABool isStrict = TRUE;
    
    if (AIsDefault(frame[2]))
        frame[2] = ACallValue(t, frame[1], 0, frame + 2);
    else {
        if (frame[2] == AGlobalByNum(AUnstrictNum))
            isStrict = FALSE;
        frame[2] = ACallValue(t, frame[1], 1, frame + 2);
    }
        
    if (AIsError(frame[2]))
        return AError;

    frame[3] = frame[0];
    frame[0] = ACallMethod(t, "decode", 1, frame + 2);
    if (AIsError(frame[0]))
        return AError;

    frame[1] = ACallMethod(t, "unprocessed", 0, frame + 2);
    if (AIsError(frame[1]))
        return AError;
    if (!AIsStr(frame[1]))
        ARaiseTypeError(t, AMsgStrExpected);
    if (AStrLen(frame[1]) != 0) {
        if (isStrict)
            return ARaiseByNum(t, ADecodeErrorNum, "Partial character");
        else {
            frame[2] = AMakeStrW(t, ReplChar);
            frame[0] = AConcat(t, frame[0], frame[2]);            
        }
    }
    
    return frame[0];
}


/* encodings::Encode(str, encoding[, strictness])
   [Deprecated] This is almost identical to encode method of std::Str. */
static AValue Encode(AThread *t, AValue *frame)
{
    if (AIsDefault(frame[2]))
        frame[2] = ACallValue(t, frame[1], 0, frame + 2);
    else
        frame[2] = ACallValue(t, frame[1], 1, frame + 2);
    
    if (AIsError(frame[2]))
        return AError;

    frame[3] = frame[0];
    return ACallMethod(t, "encode", 1, frame + 2);
}


#define ENC_BUF(value) \
    ((unsigned char *)APtrAdd(AValueToInstance(value), 2 * sizeof(AValue)))


/* The constructor for a 8-bit encoding; this is shared by all the 8-bit
   encoding classes. */
static AValue EncodingCreate(AThread *t, AValue *frame)
{
    /* Just initialize the state (encoding buffer and strictness). */
    
    if (frame[1] == ADefault || frame[1] == AGlobalByNum(AStrictNum))
        ASetMemberDirect(t, frame[0], 0, AMakeInt(t, MODE_STRICT));
    else if (frame[1] == AGlobalByNum(AUnstrictNum))
        ASetMemberDirect(t, frame[0], 0, AMakeInt(t, MODE_UNSTRICT));
    else
        ARaiseValueError(t, "Invalid mode");

    ENC_BUF(frame[0])[0] = 0;
    
    return frame[0];
}


/* The unprocessed method of 8-bit encodings. */
static AValue EncodingUnprocessed(AThread *t, AValue *frame)
{
    int n;
    int i;
    AValue v;

    n = ENC_BUF(frame[0])[0];
    v = AMakeEmptyStr(t, n);

    for (i = 0; i < n; i++)
        ASetStrItem(v, i, ENC_BUF(frame[0])[i + 1]);

    return v;
}


#define ITEM(s, i) \
    (AIsNarrowSubStr(s) ? AGetSubStrElem(s)[(i)] : AStrItem(s, i))


#define ChAt(self, str, i, unp) \
    ((i) < 0 ? ENC_BUF(self)[(i) + (unp) + 1] : ITEM((str), (i)))


/* Decode method of Utf8 */
static AValue Utf8Decode(AThread *t, AValue *frame)
{
    int i, j;
    int len;
    int resultLen;
    int unprocessed;
    int newUnprocessed;
    AValue s;

    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    
    resultLen = 0;

    unprocessed = ENC_BUF(frame[0])[0];
    newUnprocessed = 0;
    if (unprocessed != 0) {
        i = A_UTF8_SKIP(ENC_BUF(frame[0])[1]) - unprocessed;
        if (i <= len)
            resultLen++;
        else
            newUnprocessed = unprocessed;            
    } else
        i = 0;
    
    if (i < len) {
        for (;;) {
            AWideChar ch = ITEM(frame[1], i);
            i += A_UTF8_SKIP_W(ch);
            if (i < len)
                resultLen++;
            else if (i == len) {
                resultLen++;
                break;
            } else
                break;
        }
    }

    s = AMakeEmptyStrW(t, resultLen);
    j = -unprocessed;
    for (i = 0; i < resultLen; i++) {
        AWideChar ch1 = ChAt(frame[0], frame[1], j, unprocessed);
        AWideChar result;
        
        if (ch1 <= 0x7f) {
            result = ch1;
            j++;
        } else if (ch1 < 0xc2) {
            j++;
            goto DecodeError;
        } else if (ch1 < 0xe0) {
            AWideChar ch2 = AStrItem(frame[1], j + 1);
            j += 2;
            if (ch2 < 0x80 || ch2 > 0xbf)
                goto DecodeError;
            result = ((ch1 & 0x1f) << 6) | (ch2 & 0x3f);
        } else if (ch1 < 0x100) {
            AWideChar ch2 = ChAt(frame[0], frame[1], j + 1, unprocessed);
            AWideChar ch3 = AStrItem(frame[1], j + 2);
            j += 3;
            if (ch2 < 0x80 || ch2 > 0xbf || ch3 < 0x80 || ch3 > 0xbf
                || (ch1 == 0xe0 && ch2 < 0xa0))
                goto DecodeError;
            result = ((ch1 & 0xf) << 12) | ((ch2 & 0x3f) << 6) | (ch3 & 0x3f);
        } else {
            j++;
            goto DecodeError;
        }

        ASetStrItemW(s, i, result);
        continue;

      DecodeError:

        if (AMemberDirect(frame[0], 0) == AMakeInt(t, MODE_STRICT))
            return ARaiseByNum(t, ADecodeErrorNum, NULL);

        ASetStrItem(s, i, 0xfffd);
    }

    if (j < 0)
        j = 0;

    ENC_BUF(frame[0])[0] = newUnprocessed + len - j;
    for (i = 0; i < len - j; i++)
        ENC_BUF(frame[0])[i + 1 + newUnprocessed] = AStrItem(frame[1], j + i);
    
    return s;
}


/* Encode method of Utf8 */
static AValue Utf8Encode(AThread *t, AValue *frame)
{
    int i, j;
    int len;
    int resultLen;
    AValue s;

    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    resultLen = 0;

    for (i = 0; i < len; i++)
        resultLen += A_UTF8_LEN(AStrItem(frame[1], i));

    s = AMakeEmptyStr(t, resultLen);

    j = 0;
    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(frame[1], i);
        if (ch <= 0x7f) {
            ASetStrItem(s, j, ch);
            j++;
        } else if (ch <= 0x7ff) {
            ASetStrItem(s, j, 0xc0 | (ch >> 6));
            ASetStrItem(s, j + 1, 0x80 | (ch & 0x3f));
            j += 2;
        } else {
            ASetStrItem(s, j, 0xe0 | (ch >> 12));
            ASetStrItem(s, j + 1, 0x80 | ((ch >> 6) & 0x3f));
            ASetStrItem(s, j + 2, 0x80 | (ch & 0x3f));
            j += 3;
        }
    }
    
    return s;
}


/* Decode method of Iso8859_1 */
static AValue Iso8859_1Decode(AThread *t, AValue *frame)
{
    if (AIsNarrowStr(frame[1]) || AIsNarrowSubStr(frame[1]))
        return frame[1];
    else if (AIsStr(frame[1])) {
        int len = AStrLen(frame[1]);
        AValue s = AMakeEmptyStrW(t, AStrLen(frame[1]));
        int i;

        for (i = 0; i < len; i++) {
            AWideChar ch = AStrItem(frame[1], i);
            if (ch <= 0xff)
                ASetStrItem(s, i, AStrItem(frame[1], i));
            else {
                if (AMemberDirect(frame[0], 0) == AMakeInt(t, MODE_STRICT))
                    return ARaiseByNum(t, ADecodeErrorNum, NULL);
                else
                    ASetStrItem(s, i, 0xfffd);
            }
        }

        return s;
    } else    
        return ARaiseTypeError(t, AMsgStrExpected);
}


/* Encode method of Iso8859_1 */
static AValue Iso8859_1Encode(AThread *t, AValue *frame)
{
    if (AIsNarrowStr(frame[1]) || AIsNarrowSubStr(frame[1]))
        return frame[1];
    else if (AIsStr(frame[1])) {
        int len = AStrLen(frame[1]);
        AValue s = AMakeEmptyStr(t, AStrLen(frame[1]));
        int i;

        for (i = 0; i < len; i++) {
            AWideChar ch = AStrItem(frame[1], i);
            if (ch <= 0xff)
                ASetStrItem(s, i, AStrItem(frame[1], i));
            else {
                if (AMemberDirect(frame[0], 0) == AMakeInt(t, MODE_STRICT))
                    return ARaiseByNum(t, AEncodeErrorNum, NULL);
                else
                    ASetStrItem(s, i, '?');
            }
        }

        return s;
    } else    
        return ARaiseTypeError(t, AMsgStrExpected);
}


/* Decode a UTF-16 encoded string, using either big endian or little endian
   encoding.

   NOTE: Currently actually only supports UCS-2. */
static AValue Utf16Decode(AThread *t, AValue *frame, ABool isBigEndian)
{
    int len;
    int resultLen;
    int unprocessed;
    AValue s;
    int i, j;

    AExpectStr(t, frame[1]);

    unprocessed = ENC_BUF(frame[0])[0];
    len = AStrLen(frame[1]);
    resultLen = (len + unprocessed) >> 1;

    s = AMakeEmptyStrW(t, resultLen);
    
    i = 0;
    j = 0;

    if (unprocessed && len > 0) {
        if (isBigEndian)
            ASetStrItem(s, 0, (ENC_BUF(frame[0])[1] << 8) |
                        AStrItem(frame[1], 0));
        else
            ASetStrItem(s, 0, ENC_BUF(frame[0])[1] |
                        (AStrItem(frame[1], 0) << 8));
        i++;
        j++;
    }

    if (isBigEndian) {
        for (; i < resultLen; i++, j += 2)
            ASetStrItem(s, i, (AStrItem(frame[1], j) << 8) |
                        AStrItem(frame[1], j + 1));
    } else {
        for (; i < resultLen; i++, j += 2)
            ASetStrItem(s, i, AStrItem(frame[1], j) |
                        (AStrItem(frame[1], j + 1) << 8));
    }

    ENC_BUF(frame[0])[0] = j == len - 1;
    if (j == len - 1)
        ENC_BUF(frame[0])[1] = AStrItem(frame[1], j);
    
    return s;
}


/* Decode method of Utf16Le */
static AValue Utf16LeDecode(AThread *t, AValue *frame)
{
    return Utf16Decode(t, frame, FALSE);
}


/* Encode method of Utf16Le */
static AValue Utf16LeEncode(AThread *t, AValue *frame)
{
    int len;
    AValue s;
    int i;
    
    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    s = AMakeEmptyStr(t, len * 2);

    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(frame[1], i);
        ASetStrItem(s, i * 2, ch & 0xff);
        ASetStrItem(s, i * 2 + 1, ch >> 8);
    }
    
    return s;
}


/* Decode method of Utf16Be */
static AValue Utf16BeDecode(AThread *t, AValue *frame)
{
    return Utf16Decode(t, frame, TRUE);
}


/* Encode method of Utf16Be */
static AValue Utf16BeEncode(AThread *t, AValue *frame)
{
    int len;
    AValue s;
    int i;
    
    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    s = AMakeEmptyStr(t, len * 2);

    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(frame[1], i);
        ASetStrItem(s, i * 2, ch >> 8);
        ASetStrItem(s, i * 2 + 1, ch & 0xff);
    }
    
    return s;
}


/* Initialize a 8-bit encoding. Parse a compact binary description of
   the encoding, and use it to build encoding and decoding tables for efficient
   encoding and decoding. */
static void RealizeCodec(AThread *t, int codec)
{
    EightBitCodecInfo *info;
    const unsigned char *data;
    int i, j;
    int prev;

    info = AAllocStatic(sizeof(EightBitCodecInfo));
    if (info == NULL)
        ARaiseMemoryError(t);

    data = ACodecInfo[codec];

    /* First build the 8-bit -> unicode decoding table. */
    i = 0;
    j = 0;
    prev = -1;
    while (data[i] != 255) {
        if (data[i] < 128) {
            prev += (data[i] - 63);
            info->map[j] = prev;
            i++;
            j++;
        } else if (data[i] < 252) {
            prev = ((data[i] - 128) << 8) | data[i + 1];
            info->map[j] = prev;
            i += 2;
            j++;
        } else if (data[i] == 252) {
            int n = data[i + 1];
            for (; n > 0; n--) {
                info->map[j] = ++prev;
                j++;
            }
            i += 2;
        } else if (data[i] == 253) {
            int n = data[i + 1];
            for (; n > 0; n--) {
                info->map[j] = j + 128;
                j++;
            }
            i += 2;
        } else if (data[i] == 254) {
            int n = data[i + 1];
            for (; n > 0; n--) {
                info->map[j] = 0xfffd; /* Unicode replacement character */
                j++;
            }
            i += 2;
        }
    }

    /* Build the unicode -> 8-bit encoding (reverse) hash table. */
    
    for (i = 0; i < REVERSE_MAP_SIZE; i++)
        info->reverseMap[i].src = 0;

    for (i = 0; i < 128; i++) {
        if (info->map[i] != 0xfffd) {
            int j = HASH(info->map[i]) & REVERSE_MAP_MASK;
            while (info->reverseMap[j].src != 0)
                j = (j + 1) & REVERSE_MAP_MASK;
            info->reverseMap[j].src = info->map[i];
            info->reverseMap[j].dst = i + 128;
        }
    }

    /* Store the initialized encoding tables. */
    Codecs[codec] = info;
}


/* Encode a string using a 8-bit encoding. Realize the encoding first, if
   needed. */
static AValue EightBitEncode(AThread *t, AValue *frame, int codec)
{
    int len;
    AValue s;
    int i;
    EightBitCodecInfo *info;

    /* Realize codec if it is uninitialized. */
    if (Codecs[codec] == NULL)
        RealizeCodec(t, codec);

    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    s = AMakeEmptyStr(t, AStrLen(frame[1]));
    info = Codecs[codec];

    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(frame[1], i);
        if (ch <= 0x7f)
            ASetStrItem(s, i, ch); /* Assume direct mapping for 7-bit codes */
        else {
            /* Codes larger than 0x7f are looked up using a hash table. */

            /* Find the initial index. */
            int j = HASH(ch) & REVERSE_MAP_MASK;

            /* Look for a match. */
            while (info->reverseMap[j].src != ch
                   && info->reverseMap[j].src != 0)
                j = (j + 1) & REVERSE_MAP_MASK;

            if (info->reverseMap[j].src != 0)
                ASetStrItem(s, i, info->reverseMap[j].dst); /* Found it */
            else {
                /* Invalid input character */
                if (AMemberDirect(frame[0], 0) == AMakeInt(t, MODE_STRICT))
                    return ARaiseByNum(t, AEncodeErrorNum, NULL);
                else
                    ASetStrItem(s, i, '?');
            }
        }
    }                

    return s;
}


/* Decode a string using a 8-bit encoding. Realize the encoding first, if
   needed. */
static AValue EightBitDecode(AThread *t, AValue *frame, int codec)
{
    int len;
    AValue s;
    int i;
    AWideChar *map;
    
    /* Realize codec if it is uninitialized. */
    if (Codecs[codec] == NULL)
        RealizeCodec(t, codec);

    AExpectStr(t, frame[1]);

    len = AStrLen(frame[1]);
    s = AMakeEmptyStrW(t, AStrLen(frame[1]));
    map = Codecs[codec]->map;

    for (i = 0; i < len; i++) {
        AWideChar ch = AStrItem(frame[1], i);
        if (ch <= 0x7f)
            ASetStrItemW(s, i, ch); /* Assume direct mapping for 7-bit codes */
        else if (ch <= 0xff) {
            /* Look up characters in range 0x80..0xff from the decoding map. */
            AWideChar dst = map[ch - 128];
            if (dst != 0xfffd)
                ASetStrItemW(s, i, dst);
            else
                goto Error;
        } else {
            /* The input is invalid, as it contained a character code larger
               than 0xff. */
            
          Error:

            if (AMemberDirect(frame[0], 0) == AMakeInt(t, MODE_STRICT))
                return ARaiseByNum(t, ADecodeErrorNum, NULL);
            else
                ASetStrItemW(s, i, 0xfffd);
        }
    }                

    return s;
}


/* encodings::Main() */
static AValue EncodingsMain(AThread *t, AValue *frame)
{
    /* Initialize encoding alias constants. */
    ASetConstGlobalByNum(t, ALatin1Num, AGlobalByNum(AIso8859_1Num));
    ASetConstGlobalByNum(t, ALatin2Num, AGlobalByNum(AIso8859_2Num));
    ASetConstGlobalByNum(t, ALatin3Num, AGlobalByNum(AIso8859_3Num));
    ASetConstGlobalByNum(t, ALatin4Num, AGlobalByNum(AIso8859_4Num));
    ASetConstGlobalByNum(t, ALatin5Num, AGlobalByNum(AIso8859_9Num));
    ASetConstGlobalByNum(t, ALatin6Num, AGlobalByNum(AIso8859_10Num));
    ASetConstGlobalByNum(t, ALatin7Num, AGlobalByNum(AIso8859_13Num));
    ASetConstGlobalByNum(t, ALatin8Num, AGlobalByNum(AIso8859_14Num));
    ASetConstGlobalByNum(t, ALatin9Num, AGlobalByNum(AIso8859_15Num));
    ASetConstGlobalByNum(t, ALatin10Num, AGlobalByNum(AIso8859_16Num));
    return ANil;
}


/* This macro defines the decode and encode functions and a static variable for
   a 8-bit codec. */
#define EIGHT_BIT_CODEC_DEFS(name) \
static AValue name##Decode(AThread *t, AValue *frame) \
{                                                     \
    return EightBitDecode(t, frame, CODEC_##name);    \
}                                                     \
static AValue name##Encode(AThread *t, AValue *frame) \
{                                                     \
    return EightBitEncode(t, frame, CODEC_##name);    \
}


/* Define the decode and encode functions for a 8-bit codec. */
EIGHT_BIT_CODEC_DEFS(Iso8859_2)
EIGHT_BIT_CODEC_DEFS(Iso8859_3)
EIGHT_BIT_CODEC_DEFS(Iso8859_4)
EIGHT_BIT_CODEC_DEFS(Iso8859_5)
EIGHT_BIT_CODEC_DEFS(Iso8859_6)
EIGHT_BIT_CODEC_DEFS(Iso8859_7)
EIGHT_BIT_CODEC_DEFS(Iso8859_8)
EIGHT_BIT_CODEC_DEFS(Iso8859_9)
EIGHT_BIT_CODEC_DEFS(Iso8859_10)
EIGHT_BIT_CODEC_DEFS(Iso8859_11)
EIGHT_BIT_CODEC_DEFS(Iso8859_13)
EIGHT_BIT_CODEC_DEFS(Iso8859_14)
EIGHT_BIT_CODEC_DEFS(Iso8859_15)
EIGHT_BIT_CODEC_DEFS(Iso8859_16)
EIGHT_BIT_CODEC_DEFS(Ascii)
EIGHT_BIT_CODEC_DEFS(Cp437)
EIGHT_BIT_CODEC_DEFS(Cp850)
EIGHT_BIT_CODEC_DEFS(Windows1250)
EIGHT_BIT_CODEC_DEFS(Windows1251)
EIGHT_BIT_CODEC_DEFS(Windows1252)
EIGHT_BIT_CODEC_DEFS(Koi8R)
EIGHT_BIT_CODEC_DEFS(Koi8U)


A_MODULE(encodings, "encodings")
    A_DEF("Main", 0, 0, EncodingsMain)

    A_SYMBOLIC_CONST_P("Strict", &AStrictNum)
    A_SYMBOLIC_CONST_P("Unstrict", &AUnstrictNum)

    /* Encoding alias constants */
    A_EMPTY_CONST_P("Latin1", &ALatin1Num)
    A_EMPTY_CONST_P("Latin2", &ALatin2Num)
    A_EMPTY_CONST_P("Latin3", &ALatin3Num)
    A_EMPTY_CONST_P("Latin4", &ALatin4Num)
    A_EMPTY_CONST_P("Latin5", &ALatin5Num)
    A_EMPTY_CONST_P("Latin6", &ALatin6Num)
    A_EMPTY_CONST_P("Latin7", &ALatin7Num)
    A_EMPTY_CONST_P("Latin8", &ALatin8Num)
    A_EMPTY_CONST_P("Latin9", &ALatin9Num)
    A_EMPTY_CONST_P("Latin10", &ALatin10Num)
    
    A_DEF_OPT("Decode", 2, 3, 1, Decode)
    A_DEF_OPT("Encode", 2, 3, 1, Encode)

    A_CLASS_P("DecodeError", &ADecodeErrorNum)
        A_INHERIT("std::ValueError")
    A_END_CLASS()

    A_CLASS_P("EncodeError", &AEncodeErrorNum)
        A_INHERIT("std::ValueError")
    A_END_CLASS()

    A_CLASS_PRIV_P("Utf8", 1, &AUtf8Num)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Utf8Decode)
        A_METHOD("encode", 1, 0, Utf8Encode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    /* The ISO 8859-1 (Latin 1) encoding has a special, optimized
       implementation, unlike other 8-bit encodings. */
    A_CLASS_PRIV_P("Iso8859_1", 1, &AIso8859_1Num)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Iso8859_1Decode)
        A_METHOD("encode", 1, 0, Iso8859_1Encode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

/* Macro for defining a 8-bit codec class. They share most of the class
   structure. */
#define EIGHT_BIT_CODEC_CLASS(name) \
    A_CLASS_PRIV_P(#name, 1, &A##name##Num) \
        A_BINARY_DATA(ENC_DATA_SIZE)                         \
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)      \
        A_METHOD("decode", 1, 0, name##Decode)               \
        A_METHOD("encode", 1, 0, name##Encode)               \
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)   \
    A_END_CLASS()

    /* Definitions of 8-bit codec classes */
    EIGHT_BIT_CODEC_CLASS(Iso8859_2)
    EIGHT_BIT_CODEC_CLASS(Iso8859_3)
    EIGHT_BIT_CODEC_CLASS(Iso8859_4)
    EIGHT_BIT_CODEC_CLASS(Iso8859_5)
    EIGHT_BIT_CODEC_CLASS(Iso8859_6)
    EIGHT_BIT_CODEC_CLASS(Iso8859_7)
    EIGHT_BIT_CODEC_CLASS(Iso8859_8)
    EIGHT_BIT_CODEC_CLASS(Iso8859_9)
    EIGHT_BIT_CODEC_CLASS(Iso8859_10)
    EIGHT_BIT_CODEC_CLASS(Iso8859_11)
    EIGHT_BIT_CODEC_CLASS(Iso8859_13)
    EIGHT_BIT_CODEC_CLASS(Iso8859_14)
    EIGHT_BIT_CODEC_CLASS(Iso8859_15)
    EIGHT_BIT_CODEC_CLASS(Iso8859_16)
    EIGHT_BIT_CODEC_CLASS(Ascii)
    EIGHT_BIT_CODEC_CLASS(Cp437)
    EIGHT_BIT_CODEC_CLASS(Cp850)
    EIGHT_BIT_CODEC_CLASS(Windows1250)
    EIGHT_BIT_CODEC_CLASS(Windows1251)
    EIGHT_BIT_CODEC_CLASS(Windows1252)
    EIGHT_BIT_CODEC_CLASS(Koi8R)
    EIGHT_BIT_CODEC_CLASS(Koi8U)

    A_CLASS_PRIV("Utf16Le", 1)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Utf16LeDecode)
        A_METHOD("encode", 1, 0, Utf16LeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    A_CLASS_PRIV("Utf16Be", 1)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Utf16BeDecode)
        A_METHOD("encode", 1, 0, Utf16BeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    /* The definition of the Utf16 encoding is different for little and big
       endian architectures. */
#ifdef A_HAVE_LITTLE_ENDIAN
    A_CLASS_PRIV("Utf16", 1)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Utf16LeDecode)
        A_METHOD("encode", 1, 0, Utf16LeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()
#elif defined(HAVE_BIG_ENDIAN)
    A_CLASS_PRIV("Utf16", 1)
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD_OPT("create", 0, 1, 0, EncodingCreate)
        A_METHOD("decode", 1, 0, Utf16BeDecode)
        A_METHOD("encode", 1, 0, Utf16BeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()
#endif

A_END_MODULE()
