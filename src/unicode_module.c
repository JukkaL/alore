/* unicode_module.c - unicode module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "unicode_module.h"
#include "internal.h"

#include <stdio.h>


/* NOTE: The implementation only supports code points up to 0xffff. */

/* IDEA: Add additional features such as composing/decomposing code point
         sequences, classifying code points etc. */


/* This module implements a bidirectional mapping between Unicode characters
   (representing Unicode code points 0..0xffff) and standard Unicode names
   such as 'LATIN CAPITAL LETTER A WITH RIGN ABOVE'. To conserve space needed
   for the mapping, a rather elaborate encoding scheme is used.

   CJK unified ideographs and Hangul syllables have special handling, but the
   rest of the code points are handled using the method sketched below:

     * Names are encoded as sequences of words (e.g. 'DOLLAR SIGN' =>
       ['DOLLAR', 'SIGN']). Words are encoded using numeric ids.
     * The tables AUnicodeWordList and AUnicodeWordLUT map a word id to
       a sequence of characters (GetUnicodeWord).
     * The tables AUnicodeNameList and AUnicodeNameLUT map a code point to
       a sequence of word ids (GetGenericUnicodeName).
     * The hash table AUnicodeNameHashTable is used to map names to code
       points (LookupChr).
*/


#define WORD_BUCKET_SIZE 128
#define MAX_WORD_LEN 128


/* Look up the word (Unicode name component) corresponding to the word id n. */
static void GetUnicodeWord(int n, char *buf)
{
    int bi = n / WORD_BUCKET_SIZE;
    int wi = bi * WORD_BUCKET_SIZE;
    int ti = AUnicodeWordLUT[bi];

    strcpy(buf, "");
    for (;;) {
        int j = AUnicodeWordList[ti]; /* Shared prefix length */
        int k = 0;
        ti++;
        do {
            buf[j + k] = AUnicodeWordList[ti + k];
            k++;
        } while ((unsigned char)buf[j + k - 1] < 128);
        buf[j + k - 1] ^= 128;

        if (wi == n) {
            buf[j + k] = '\0';
            break;
        }

        ti += k;
        wi++;
    }
}


/* Look up the name of a code point using the generic algorithm. */
static void GetGenericUnicodeName(int c, char *buf)
{
    int i;
    int ci;
    unsigned short w[32];

    /* Find the start location in the name list using a lookup table. */
    i = 0;
    while (AUnicodeNameLUT[i] != 0 && c >= AUnicodeNameLUT[i + 2])
        i += 3;

    ci = AUnicodeNameLUT[i];

    /* Is this an invalid character? */
    if (ci == 0 || c < ci) {
        strcpy(buf, "");
        return;
    }

    /* Iterate words in the name list until the requested name is
       reached. */
    i = AUnicodeNameLUT[i + 1];
    for (;;) {
        int j = AUnicodeNameList[i]; /* Shared prefix length */
        int k = 0;
        i++;
        
        /* Read word numbers not part of the shared prefix. The last word
           has the highest bit set. */
        do {
            w[j + k] = (AUnicodeNameList[i + k * 2] |
                        (AUnicodeNameList[i + k * 2 + 1] << 8));
            k++;
        } while (w[j + k - 1] < 32768);
        w[j + k - 1] ^= 32768;

        /* Found the requested name? */
        if (ci == c) {
            /* Build the name from component word numbers. */
            k += j;
            j = 0;
            for (i = 0; i < k; i++) {
                GetUnicodeWord(w[i], buf + j);
                j = strlen(buf);
                buf[j++] = ' ';
            }
            buf[j - 1] = '\0';
            break;
        }

        /* Advance to the next name. */
        i += k * 2;
        ci++;
    }
}


/* Definitions for processing hangul syllables */

#define HS_Start 44032
#define HS_End 55203
#define HS_N1 19
#define HS_N2 21
#define HS_N3 28

static const char * const HS_Comp1[HS_N1] = {
    "G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS", "", "J", "JJ",
    "C", "K", "T", "P", "H"
};

static const char * const HS_Comp2[HS_N2] = {
    "A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE", "OE",
    "YO", "U", "WEO", "WE", "WI", "YU", "EU", "YI", "I"
};

static const char * const HS_Comp3[HS_N3] = {
    "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS",
    "LT", "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C", "K", "T",
    "P", "H"
};


/* Return the name of a Hangul syllable. Assume ch is code point for a Hangul
   syllable. */
static void GetHangulSyllableName(int ch, char *buf)
{
    int c1, c2, c3;

    ch -= HS_Start;
    c1 = ch / (HS_N2 * HS_N3);
    c2 = (ch / HS_N3) % HS_N2;
    c3 = ch % HS_N3;

    strcpy(buf, "HANGUL SYLLABLE ");
    strcat(buf, HS_Comp1[c1]);
    strcat(buf, HS_Comp2[c2]);
    strcat(buf, HS_Comp3[c3]);
}


static int FindPrefix(char *s, const char *const*a, int n)
{
    int i;
    int found = -1;

    for (i = 0; i < n; i++) {
        if (strncmp(s, a[i], strlen(a[i])) == 0) {
            if (found == -1 || strlen(a[i]) > strlen(a[found]))
                found = i;
        }
    }
    return found;
}


static int ParseHangulSyllable(char *name)
{
    int i1, i2, i3;

    i1 = FindPrefix(name, HS_Comp1, HS_N1);
    if (i1 >= 0) {
        name += strlen(HS_Comp1[i1]);
        i2 = FindPrefix(name, HS_Comp2, HS_N2);
        if (i2 >= 0) {
            name += strlen(HS_Comp2[i2]);
            i3 = FindPrefix(name, HS_Comp3, HS_N3);
            if (i3 >= 0 && strcmp(name, HS_Comp3[i3]) == 0)
                return HS_Start + i1 * (HS_N2 * HS_N3) + i2 * HS_N3 + i3;
        }
    }

    return 0;
}


#define IsCJKUnified(ch) \
    ((ch) >= 13312 && (ch) <= 40869 && !((ch) >= 19894 && (ch) <= 19967))


/* Get the name of a CJK unified ideograph. Assume ch is a code point for
   a CJK unified ideograph. */
static void GetUnicodeName(int ch, char *buf)
{
    if (IsCJKUnified(ch))
        sprintf(buf, "CJK UNIFIED IDEOGRAPH-%.4X", ch);
    else if (ch >= HS_Start && ch <= HS_End)
        GetHangulSyllableName(ch, buf);
    else
        GetGenericUnicodeName(ch, buf);
}


/* unicode::UnicodeName(ch)
   Return the Unicode name of a character. */
static AValue UnicodeName(AThread *t, AValue *frame)
{
    int ch = AGetCh(t, frame[0]);
    char buf[128];
    
    GetUnicodeName(ch, buf);
    if (*buf == '\0')
        ARaiseValueError(t, "Invalid Unicode character");
    
    return AMakeStr(t, buf);
}


#define HASH_SIZE 17299


/* Calculate the hash value of a string representing a (potential) Unicode
   name. */
static int UnicodeHash(char *buf)
{
    int hash = 12345;
    while (*buf != '\0') {
        hash = (hash * 53 + *buf) % HASH_SIZE;
        buf++;
    }
    return hash;
}


/* unicode::LookupChr(name)
   Lookup a Unicode character based on the name. */
static AValue LookupChr(AThread *t, AValue *frame)
{
    char buf[256];
    char name[128];
    int i;
    
    AGetStr(t, frame[0], buf, sizeof(buf));

    if (strncmp(buf, "CJK UNIFIED IDEOGRAPH-", 22) == 0) {
        /* CJK unified ideograph */
        char *p = buf + 22;
        if (AIsHexDigit(p[0]) && AIsHexDigit(p[1]) && AIsHexDigit(p[2]) &&
            AIsHexDigit(p[3]) && p[4] == '\0') {
            i = AUnicodeSequenceValue(p);
            if (!IsCJKUnified(i))
                goto Error;
            return AMakeCh(t, i);
        } else
            goto Error;
    } else if (strncmp(buf, "HANGUL SYLLABLE ", 16) == 0) {
        /* Hangul syllable */
        i = ParseHangulSyllable(buf + 16);
        if (i == 0)
            goto Error;
        return AMakeCh(t, i);
    }

    /* Use the generic algorithm for character lookup. */
    
    /* The hash table AUnicodeNameHashTable is used to map names to code
       points. Each item in the hash table is a code point. Hash the name
       string, and look up the name of the code point in the corresponding
       table entry. If it matches the name, we found the code point;
       otherwise continue searching in the hash chain until encountering a
       match or 0. In the latter case, there was no match found. */

    i = UnicodeHash(buf);
    while (AUnicodeNameHashTable[i] != 0) {
        GetUnicodeName(AUnicodeNameHashTable[i], name);
        if (strcmp(name, buf) == 0)
            return AMakeCh(t, AUnicodeNameHashTable[i]);
        i = (i + 13) % HASH_SIZE;
    }

    /* No match. */

  Error:
    
    return ARaiseValueError(t, "Invalid Unicode character name");
}


A_MODULE(unicode, "unicode")
    A_DEF("UnicodeName", 1, 0, UnicodeName)
    A_DEF("LookupChr", 1, 0, LookupChr)
A_END_MODULE()
