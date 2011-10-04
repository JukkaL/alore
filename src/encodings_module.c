/* encodings_module.c - encodings module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "encodings_module.h"
#include "str.h"
#include "gc.h"
#include "util.h"


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

int ADecodeErrorNum;
int AEncodeErrorNum;

/* Global numbers of encoder/decoder classes (all private). */
static int Iso8859_1EncoderDecoderNum;
static int EightBitEncoderDecoderNum;
static int Utf8EncoderDecoderNum;
static int Utf16LeEncoderDecoderNum;
static int Utf16BeEncoderDecoderNum;


int AUtf8Num;        /* encodings::Utf8 */
int AUtf16LeNum;     /* encodings::Utf16Le */
int AUtf16BeNum;     /* encodings::Utf16Be */
int AUtf16Num;       /* encodings::Utf16 */

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



/* The replacement character as a string */
static const AWideChar ReplChar[2] = { 0xfffd, '\0' };


/* Strictness constants */
enum {
    MODE_STRICT,
    MODE_UNSTRICT
};


/* Slot ids of Encoder/Decoder objects (NOTE: not for Encoding objects) */
enum {
    SLOT_MODE,      /* Strictness; MODE_STRICT or MODE_UNSTRICT */
    SLOT_ENCODING   /* CODEC_x number; only in EightBitEncoderDecoder */
};


#define ENC_DATA_SIZE 8


/* Return pointer to the encoding buffer of a Encoder/Decoder object.
   Assume that the object has exactly 2 slots. */
#define ENC_BUF(value) \
    ((unsigned char *)APtrAdd(AValueToInstance(value), 3 * sizeof(AValue)))


/* encodings::Decode(str, encoding[, strictness])
   [Deprecated] This is just calls the decode method of the first argument. */
static AValue Decode(AThread *t, AValue *frame)
{
    if (AIsDefault(frame[2]))
        return ACallMethod(t, "decode", 1, frame);
    else
        return ACallMethod(t, "decode", 2, frame);
}


/* encodings::Encode(str, encoding[, strictness])
   [Deprecated] This just call the encode method of the first argument. */
static AValue Encode(AThread *t, AValue *frame)
{
    if (AIsDefault(frame[2]))
        return ACallMethod(t, "encode", 1, frame);
    else
        return ACallMethod(t, "encode", 2, frame);
}


/* Initialize an encoder/decoder object. The argument encoding should point to
   a reference to the object, and strictness is the a strictness constant or
   ADefault. Num is the number of a regular 8-bit encoding (CODEC_x); it is -1
   for other kinds of encoding. Return the encoder/decoder object. */
static AValue InitializeCodec(AThread *t, AValue *encoding,
                              AValue strictness, int num)
{
    /* Just initialize the state (encoding buffer and strictness). */

    if (strictness == ADefault || strictness == AGlobalByNum(AStrictNum))
        ASetMemberDirect(t, *encoding, SLOT_MODE, AMakeInt(t, MODE_STRICT));
    else if (strictness == AGlobalByNum(AUnstrictNum))
        ASetMemberDirect(t, *encoding, SLOT_MODE, AMakeInt(t, MODE_UNSTRICT));
    else
        ARaiseValueError(t, "Invalid mode");

    if (num != -1)
        ASetMemberDirect(t, *encoding, SLOT_ENCODING, AMakeInt(t, num));

    ENC_BUF(*encoding)[0] = 0;

    return *encoding;
}


/* The shared "name" getter of Encoding classes. */
static AValue EncodingName(AThread *t, AValue *frame)
{
    return AMemberDirect(frame[0], 0);
}


/* The shared _str() method of Encoding classes. */
static AValue Encoding_str(AThread *t, AValue *frame)
{
    char name[128];
    char result[128];
    AGetStr(t, AMemberDirect(frame[0], 0), name, sizeof(name));
    AFormatMessage(result, sizeof(result), "<%s encoding>", name);
    return AMakeStr(t, result);
}


/* The shared unprocessed() method of all Decoder classes. */
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


/* Utf8Encoding encoder([strictness])
   Utf8Encoding decoder([strictness])
   (the implementation is shared) */
static AValue Utf8EncodingFactoryMethod(AThread *t, AValue *frame)
{
    AValue codecType = AGlobalByNum(Utf8EncoderDecoderNum);
    frame[2] = ACallValue(t, codecType, 0, frame + 2);
    return InitializeCodec(t, &frame[2], frame[1], -1);
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

        if (AMemberDirect(frame[0], SLOT_MODE) == AMakeInt(t, MODE_STRICT))
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


/* Iso8859_1Encoding encoder([strictness])
   Iso8859_1Encoding decoder([strictness])
   (the implementation is shared) */
static AValue Iso8859_1EncodingFactoryMethod(AThread *t, AValue *frame)
{
    AValue codecType = AGlobalByNum(Iso8859_1EncoderDecoderNum);
    frame[2] = ACallValue(t, codecType, 0, frame + 2);
    return InitializeCodec(t, &frame[2], frame[1], -1);
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
                if (AMemberDirect(frame[0], SLOT_MODE) ==
                        AMakeInt(t, MODE_STRICT))
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
                if (AMemberDirect(frame[0], SLOT_MODE) ==
                        AMakeInt(t, MODE_STRICT))
                    return ARaiseByNum(t, AEncodeErrorNum, NULL);
                else
                    ASetStrItem(s, i, '?');
            }
        }

        return s;
    } else
        return ARaiseTypeError(t, AMsgStrExpected);
}


/* Utf16LeEncoding encoder([strictness])
   Utf16LeEncoding decoder([strictness])
   (the implementation is shared) */
static AValue Utf16LeEncodingFactoryMethod(AThread *t, AValue *frame)
{
    AValue codecType = AGlobalByNum(Utf16LeEncoderDecoderNum);
    frame[2] = ACallValue(t, codecType, 0, frame + 2);
    return InitializeCodec(t, &frame[2], frame[1], -1);
}


/* Utf16BeEncoding encoder([strictness])
   Utf16BeEncoding decoder([strictness])
   (the implementation is shared) */
static AValue Utf16BeEncodingFactoryMethod(AThread *t, AValue *frame)
{
    AValue codecType = AGlobalByNum(Utf16BeEncoderDecoderNum);
    frame[2] = ACallValue(t, codecType, 0, frame + 2);
    return InitializeCodec(t, &frame[2], frame[1], -1);
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


/* Decode method of Utf16LeEncoderDecoder */
static AValue Utf16LeDecode(AThread *t, AValue *frame)
{
    return Utf16Decode(t, frame, FALSE);
}


/* Encode method of Utf16LeEncoderDecoder */
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


/* Decode method of Utf16BeEncoderDecoder */
static AValue Utf16BeDecode(AThread *t, AValue *frame)
{
    return Utf16Decode(t, frame, TRUE);
}


/* Encode method of Utf16BeEncoderDecoder */
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


/* EightBitEncoding encoder([strictness])
   EightBitEncoding decoder([strictness])
   (the implementation is shared)
   Create a decoder/encoder object for a regular 8-bit encoding which uses
   a table-based description of character mapping. */
AValue EightBitEncodingFactoryMethod(AThread *t, AValue *frame)
{
    AValue codecType = AGlobalByNum(EightBitEncoderDecoderNum);
    int num;
    frame[2] = ACallValue(t, codecType, 0, frame + 2);
    num = AValueToInt(AMemberDirect(frame[0], 1));
    return InitializeCodec(t, &frame[2], frame[1], num);
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


/* EightBitEncoderDecoder encode(s)
   Encode a string using a 8-bit encoding. Realize the encoding first, if
   needed. */
static AValue EightBitCodecEncode(AThread *t, AValue *frame)
{
    int len;
    AValue s;
    int i;
    EightBitCodecInfo *info;
    int codec;

    /* Look up the internal encoding number (CODEC_x). */
    codec = AValueToInt(AMemberDirect(frame[0], SLOT_ENCODING));

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
                if (AMemberDirect(frame[0], SLOT_MODE) ==
                        AMakeInt(t, MODE_STRICT))
                    return ARaiseByNum(t, AEncodeErrorNum, NULL);
                else
                    ASetStrItem(s, i, '?');
            }
        }
    }

    return s;
}


/* EightBitEncoderDecoder decode(s)
   Decode a string using a 8-bit encoding. Realize the encoding first, if
   needed. */
static AValue EightBitCodecDecode(AThread *t, AValue *frame)
{
    int len;
    AValue s;
    int i;
    AWideChar *map;
    int codec;

    /* Look up the internal encoding number (CODEC_x). */
    codec = AValueToInt(AMemberDirect(frame[0], SLOT_ENCODING));

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

            if (AMemberDirect(frame[0], SLOT_MODE) ==
                    AMakeInt(t, MODE_STRICT))
                return ARaiseByNum(t, ADecodeErrorNum, NULL);
            else
                ASetStrItemW(s, i, 0xfffd);
        }
    }

    return s;
}


/* Create an Encoding object for an encoding. Create an instance based on the
   given fully qualified name of an encoding class. Assign the encoding object
   to the constant with the given name in the encodings module. The num
   argument must be one of CODEC_x constants for regular 8-bit encodings or
   -1 for other encodings. The frame argument should be able to hold at least
   one value. */
static void InitializeEncoding(AThread *t, const char *name,
                               const char *encodingClass, int num,
                               AValue *frame)
{
    char fullName[128];
    int targetNum;
    AValue nameObj;

    /* Build and initialize encoding object. */
    frame[0] = ACall(t, encodingClass, 0, frame);
    if (AIsError(frame[0]))
        ARaiseValueError(t, "Initialization failed");

    /* Initialize encoding name. */
    nameObj = AMakeStr(t, name);
    ASetMemberDirect(t, frame[0], 0, nameObj);

    /* Initialize encoding number of 8-bit encodings. */
    if (num >= 0)
        ASetMemberDirect(t, frame[0], 1, AMakeInt(t, num));

    /* Get the global num of the encoding constant. */
    AFormatMessage(fullName, sizeof(fullName), "encodings::%s", name);
    targetNum = AGetGlobalNum(t, fullName);

    /* Initialize the encoding constant. */
    ASetConstGlobalByNum(t, targetNum, frame[0]);
}


/* Create an Encoding object for a regular 8-bit encoding. Assign it to the
   constant with the given name in the encodings module. The num argument
   must be one of CODEC_x constants. The frame argument should be able to hold
   at least one value. */
static void Initialize8BitEncoding(AThread *t, const char *name, int num,
                                   AValue *frame)
{
    InitializeEncoding(t, name, "encodings::EightBitEncoding", num, frame);
}


/* Assign value of encodings::<codec> to constant encodings::<alias>. */
static void InitializeCodecAlias(AThread *t, const char *alias,
                                 const char *codec)
{
    char fullAlias[128];
    char fullCodec[128];
    int aliasNum;
    AFormatMessage(fullAlias, sizeof(fullAlias), "encodings::%s", alias);
    AFormatMessage(fullCodec, sizeof(fullCodec), "encodings::%s", codec);
    aliasNum = AGetGlobalNum(t, fullAlias);
    ASetConstGlobalByNum(t, aliasNum, AGlobal(t, fullCodec));
}


/* encodings::Main()
   Initialize encoding objects. */
static AValue EncodingsMain(AThread *t, AValue *frame)
{
    /* Initialize Iso8859_1 encoding. */
    InitializeEncoding(t, "Iso8859_1", "encodings::Iso8859_1Encoding", -1,
                       frame);

    /* Initialize Unicode encodings. */
    InitializeEncoding(t, "Utf8", "encodings::Utf8Encoding", -1, frame);
    InitializeEncoding(t, "Utf16Le", "encodings::Utf16LeEncoding", -1, frame);
    InitializeEncoding(t, "Utf16Be", "encodings::Utf16BeEncoding", -1, frame);

    /* Set encodings::Utf16 to match the platform byte order. */
#ifdef A_HAVE_LITTLE_ENDIAN
    ASetConstGlobalByNum(t, AUtf16Num, AGlobalByNum(AUtf16LeNum));
#else
    ASetConstGlobalByNum(t, AUtf16Num, AGlobalByNum(AUtf16BeNum));
#endif

    /* Initialize regular 8-bit encodings. */
    Initialize8BitEncoding(t, "Iso8859_2", CODEC_Iso8859_2, frame);
    Initialize8BitEncoding(t, "Iso8859_3", CODEC_Iso8859_3, frame);
    Initialize8BitEncoding(t, "Iso8859_4", CODEC_Iso8859_4, frame);
    Initialize8BitEncoding(t, "Iso8859_5", CODEC_Iso8859_5, frame);
    Initialize8BitEncoding(t, "Iso8859_6", CODEC_Iso8859_6, frame);
    Initialize8BitEncoding(t, "Iso8859_7", CODEC_Iso8859_7, frame);
    Initialize8BitEncoding(t, "Iso8859_8", CODEC_Iso8859_8, frame);
    Initialize8BitEncoding(t, "Iso8859_9", CODEC_Iso8859_9, frame);
    Initialize8BitEncoding(t, "Iso8859_10", CODEC_Iso8859_10, frame);
    Initialize8BitEncoding(t, "Iso8859_11", CODEC_Iso8859_11, frame);
    Initialize8BitEncoding(t, "Iso8859_13", CODEC_Iso8859_13, frame);
    Initialize8BitEncoding(t, "Iso8859_14", CODEC_Iso8859_14, frame);
    Initialize8BitEncoding(t, "Iso8859_15", CODEC_Iso8859_15, frame);
    Initialize8BitEncoding(t, "Iso8859_16", CODEC_Iso8859_16, frame);
    Initialize8BitEncoding(t, "Ascii", CODEC_Ascii, frame);
    Initialize8BitEncoding(t, "Cp437", CODEC_Cp437, frame);
    Initialize8BitEncoding(t, "Cp850", CODEC_Cp850, frame);
    Initialize8BitEncoding(t, "Windows1250", CODEC_Windows1250, frame);
    Initialize8BitEncoding(t, "Windows1251", CODEC_Windows1251, frame);
    Initialize8BitEncoding(t, "Windows1252", CODEC_Windows1252, frame);
    Initialize8BitEncoding(t, "Koi8R", CODEC_Koi8R, frame);
    Initialize8BitEncoding(t, "Koi8U", CODEC_Koi8U, frame);

    /* Initialize encoding alias constants. */
    InitializeCodecAlias(t, "Latin1", "Iso8859_1");
    InitializeCodecAlias(t, "Latin2", "Iso8859_2");
    InitializeCodecAlias(t, "Latin3", "Iso8859_3");
    InitializeCodecAlias(t, "Latin4", "Iso8859_4");
    InitializeCodecAlias(t, "Latin5", "Iso8859_9");
    InitializeCodecAlias(t, "Latin6", "Iso8859_10");
    InitializeCodecAlias(t, "Latin7", "Iso8859_13");
    InitializeCodecAlias(t, "Latin8", "Iso8859_14");
    InitializeCodecAlias(t, "Latin9", "Iso8859_15");
    InitializeCodecAlias(t, "Latin10", "Iso8859_16");

    return ANil;
}


A_MODULE(encodings, "encodings")
    A_DEF("Main", 0, 1, EncodingsMain)

    A_SYMBOLIC_CONST_P("Strict", &AStrictNum)
    A_SYMBOLIC_CONST_P("Unstrict", &AUnstrictNum)

    A_DEF_OPT("Decode", 2, 3, 1, Decode)
    A_DEF_OPT("Encode", 2, 3, 1, Encode)

    A_CLASS_P("DecodeError", &ADecodeErrorNum)
        A_INHERIT("std::ValueError")
    A_END_CLASS()

    A_CLASS_P("EncodeError", &AEncodeErrorNum)
        A_INHERIT("std::ValueError")
    A_END_CLASS()

    A_INTERFACE("Encoding")
        A_METHOD_OPT("decoder", 0, 1, 0, NULL)
        A_METHOD_OPT("encoder", 0, 1, 0, NULL)
        A_GETTER("name", 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Decoder")
        A_METHOD("decode", 1, 0, NULL)
        A_METHOD("unprocesser", 0, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Encoder")
        A_METHOD("encode", 1, 0, NULL)
    A_END_INTERFACE()

    /* NOTE: The shared "name" getter assumes that Encoding classes have 1
       private slot that contains the name of the encoding. */

    /* NOTE: Some functions assume that all Encoder/Decoder objects have the
       same internal structure: 2 private slots + ENC_DATA_SIZE bytes of
       binary data. */

    /* Implementations of Unicode encodings */

    A_CLASS_PRIV(A_PRIVATE("Utf8Encoding"), 1)
        A_IMPLEMENT("::Encoding")
        A_METHOD_OPT("decoder", 0, 1, 1, Utf8EncodingFactoryMethod)
        A_METHOD_OPT("encoder", 0, 1, 1, Utf8EncodingFactoryMethod)
        A_GETTER("name", 0, EncodingName)
        A_METHOD("_str", 0, 0, Encoding_str)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("Utf8EncoderDecoder"), 2, &Utf8EncoderDecoderNum)
        A_IMPLEMENT("::Encoder")
        A_IMPLEMENT("::Decoder")
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD("decode", 1, 0, Utf8Decode)
        A_METHOD("encode", 1, 0, Utf8Encode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    A_CLASS_PRIV(A_PRIVATE("Utf16LeEncoding"), 1)
        A_IMPLEMENT("::Encoding")
        A_METHOD_OPT("decoder", 0, 1, 1, Utf16LeEncodingFactoryMethod)
        A_METHOD_OPT("encoder", 0, 1, 1, Utf16LeEncodingFactoryMethod)
        A_GETTER("name", 0, EncodingName)
        A_METHOD("_str", 0, 0, Encoding_str)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("Utf16LeEncoderDecoder"), 2,
                   &Utf16LeEncoderDecoderNum)
        A_IMPLEMENT("::Encoder")
        A_IMPLEMENT("::Decoder")
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD("decode", 1, 0, Utf16LeDecode)
        A_METHOD("encode", 1, 0, Utf16LeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    A_CLASS_PRIV(A_PRIVATE("Utf16BeEncoding"), 1)
        A_IMPLEMENT("::Encoding")
        A_METHOD_OPT("decoder", 0, 1, 1, Utf16BeEncodingFactoryMethod)
        A_METHOD_OPT("encoder", 0, 1, 1, Utf16BeEncodingFactoryMethod)
        A_GETTER("name", 0, EncodingName)
        A_METHOD("_str", 0, 0, Encoding_str)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("Utf16BeEncoderDecoder"), 2,
                   &Utf16BeEncoderDecoderNum)
        A_IMPLEMENT("::Encoder")
        A_IMPLEMENT("::Decoder")
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD("decode", 1, 0, Utf16BeDecode)
        A_METHOD("encode", 1, 0, Utf16BeEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    /* The ISO 8859-1 (Latin 1) encoding has a special, optimized
       implementation, unlike other 8-bit encodings. */

    A_CLASS_PRIV(A_PRIVATE("Iso8859_1Encoding"), 1)
        A_IMPLEMENT("::Encoding")
        A_METHOD_OPT("decoder", 0, 1, 1, Iso8859_1EncodingFactoryMethod)
        A_METHOD_OPT("encoder", 0, 1, 1, Iso8859_1EncodingFactoryMethod)
        A_GETTER("name", 0, EncodingName)
        A_METHOD("_str", 0, 0, Encoding_str)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("Iso8859_1EncoderDecoder"), 2,
                   &Iso8859_1EncoderDecoderNum)
        A_IMPLEMENT("::Encoder")
        A_IMPLEMENT("::Decoder")
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD("decode", 1, 0, Iso8859_1Decode)
        A_METHOD("encode", 1, 0, Iso8859_1Encode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    /* Most 8-bit (or 7-bit)  encodings share this implementation class. */

    A_CLASS_PRIV(A_PRIVATE("EightBitEncoding"), 2)
        A_IMPLEMENT("::Encoding")
        A_METHOD_OPT("decoder", 0, 1, 1, EightBitEncodingFactoryMethod)
        A_METHOD_OPT("encoder", 0, 1, 1, EightBitEncodingFactoryMethod)
        A_GETTER("name", 0, EncodingName)
        A_METHOD("_str", 0, 0, Encoding_str)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("EightBitEncoderDecoder"), 2,
                   &EightBitEncoderDecoderNum)
        A_IMPLEMENT("::Encoder")
        A_IMPLEMENT("::Decoder")
        A_BINARY_DATA(ENC_DATA_SIZE)
        A_METHOD("decode", 1, 0, EightBitCodecDecode)
        A_METHOD("encode", 1, 0, EightBitCodecEncode)
        A_METHOD("unprocessed", 0, 0, EncodingUnprocessed)
    A_END_CLASS()

    /* Unicode encoding constants */
    A_EMPTY_CONST_P("Utf8", &AUtf8Num)
    A_EMPTY_CONST_P("Utf16Le", &AUtf16LeNum)
    A_EMPTY_CONST_P("Utf16Be", &AUtf16BeNum)
    A_EMPTY_CONST_P("Utf16", &AUtf16Num)

    /* 8-bit encoding constants */

    A_EMPTY_CONST_P("Iso8859_1", &AIso8859_1Num)
    A_EMPTY_CONST_P("Iso8859_2", &AIso8859_2Num)
    A_EMPTY_CONST_P("Iso8859_3", &AIso8859_3Num)
    A_EMPTY_CONST_P("Iso8859_4", &AIso8859_4Num)
    A_EMPTY_CONST_P("Iso8859_5", &AIso8859_5Num)
    A_EMPTY_CONST_P("Iso8859_6", &AIso8859_6Num)
    A_EMPTY_CONST_P("Iso8859_7", &AIso8859_7Num)
    A_EMPTY_CONST_P("Iso8859_8", &AIso8859_8Num)
    A_EMPTY_CONST_P("Iso8859_9", &AIso8859_9Num)
    A_EMPTY_CONST_P("Iso8859_10", &AIso8859_10Num)
    A_EMPTY_CONST_P("Iso8859_11", &AIso8859_11Num)
    A_EMPTY_CONST_P("Iso8859_13", &AIso8859_13Num)
    A_EMPTY_CONST_P("Iso8859_14", &AIso8859_14Num)
    A_EMPTY_CONST_P("Iso8859_15", &AIso8859_15Num)
    A_EMPTY_CONST_P("Iso8859_16", &AIso8859_16Num)

    A_EMPTY_CONST_P("Ascii", &AAsciiNum)
    A_EMPTY_CONST_P("Koi8R", &AKoi8RNum)
    A_EMPTY_CONST_P("Koi8U", &AKoi8UNum)

    A_EMPTY_CONST("Cp437")
    A_EMPTY_CONST("Cp850")

    A_EMPTY_CONST("Windows1250")
    A_EMPTY_CONST("Windows1251")
    A_EMPTY_CONST("Windows1252")

    /* Encoding alias constants */
    A_EMPTY_CONST("Latin1")
    A_EMPTY_CONST("Latin2")
    A_EMPTY_CONST("Latin3")
    A_EMPTY_CONST("Latin4")
    A_EMPTY_CONST("Latin5")
    A_EMPTY_CONST("Latin6")
    A_EMPTY_CONST("Latin7")
    A_EMPTY_CONST("Latin8")
    A_EMPTY_CONST("Latin9")
    A_EMPTY_CONST("Latin10")
A_END_MODULE()
