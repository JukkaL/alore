/* base64_module.c - base64 module (base64 encoding/decoding)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"


static const char Base64Alpha[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const signed char Base64DecMap[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1,  0, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1
};


/* Encode a string to base64 encoding. */
static AValue Base64Encode(AThread *t, AValue *frame)
{
    /* FIX: Raise exception if argument is a wide string. */
    
    int srcLen;
    int dstLen;
    AValue res;
    int i, j, resInd;

    AExpectStr(t, frame[0]);
    
    srcLen = AStrLen(frame[0]);
    dstLen = (srcLen + 2) / 3 * 4;

    res = AMakeEmptyStr(t, dstLen);

    resInd = 0;

    /* Process input in up to 3 byte chunks (each resulting in 4 target encoded
       characters). */
    for (i = 0; i < srcLen; i += 3) {
        unsigned char b1, b2, b3;
        char out[4];

        b1 = AStrItem(frame[0], i);
        b2 = i + 1 < srcLen ? AStrItem(frame[0], i + 1) : 0;
        b3 = i + 2 < srcLen ? AStrItem(frame[0], i + 2) : 0;

        /* Convert 3x8 bits to base64. */
        out[0] = Base64Alpha[b1 >> 2];
        out[1] = Base64Alpha[((b1 & 3) << 4) | (b2 >> 4)];
        out[2] = Base64Alpha[((b2 & 15) << 2) | (b3 >> 6)];
        out[3] = Base64Alpha[b3 & 63];

        /* Add padding if needed. */
        if (i + 2 >= srcLen)
            out[3] = '=';
        if (i + 1 >= srcLen)
            out[2] = '=';

        /* Output encoded characters. */
        for (j = 0; j < 4; j++)
            ASetStrItem(res, resInd + j, out[j]);

        resInd += 4;
    }    
    
    return res;
}


/* Decode base64 encoded strings. The input must be valid; for example, it
   may not contain extra padding characters or any whitespace. */
static AValue Base64Decode(AThread *t, AValue *frame)
{
    int srcLen;
    int dstLen;
    AValue res;
    int i, j, resInd;

    AExpectStr(t, frame[0]);

    srcLen = AStrLen(frame[0]);

    /* Calculate exact result length. */
    dstLen = srcLen / 4 * 3;
    if (srcLen > 0 && AStrItem(frame[0], srcLen - 1) == '=')
        dstLen--;
    if (srcLen > 0 && AStrItem(frame[0], srcLen - 2) == '=')
        dstLen--;

    res = AMakeEmptyStr(t, dstLen);

    resInd = 0;

    /* Process data in 4-character chunks, each representing up to 3 result
       bytes. */
    for (i = 0; i + 3 < srcLen; i += 4) {
        signed char in[4];
        char out[3];
        int pad = 0; /* Bit field marking pad characters in the input chunk */

        /* Convert input chunk to 4 6-bit integers. */
        for (j = 0; j < 4; j++) {
            int c = AStrItem(frame[0], i + j);
            if (c == '=')
                pad |= 1 << j;
            if (c < 128)
                in[j] = Base64DecMap[c];
            else
                in[j] = -1;

            if (in[j] == -1)
                return ARaiseValueError(t, "Non-alphabet input character");
        }

        /* Convert 4x6 bits to 3x8 bits. */
        out[0] = (in[0] << 2) | (in[1] >> 4);
        out[1] = ((in[1] & 15) << 4) | (in[2] >> 2);
        out[2] = ((in[2] & 3) << 6) | in[3];

        /* Output result bytes and check padding. */
        ASetStrItem(res, resInd, out[0]);
        if (resInd + 1 < dstLen) {
            ASetStrItem(res, resInd + 1, out[1]);
            if (resInd + 2 < dstLen) {
                ASetStrItem(res, resInd + 2, out[2]);
                if (pad != 0)
                    ARaiseValueError(t, "Padding before input end");
            } else if (pad != 8)
                ARaiseValueError(t, "Incorrect padding");
        } else if (pad != 12)
            ARaiseValueError(t, "Incorrect padding");

        resInd += 3;
    }
    
    if ((srcLen & 3) != 0)
        return ARaiseValueError(t, "Incorrect padding");
    
    return res;
}


A_MODULE(base64, "base64")
    A_DEF("Base64Encode", 1, 0, Base64Encode)
    A_DEF("Base64Decode", 1, 0, Base64Decode)
A_END_MODULE()
