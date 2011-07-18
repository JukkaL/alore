/* str_internal.h - Internal low-level Str related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef STR_INTERNAL_H_INCL
#define STR_INTERNAL_H_INCL

#include "value.h"


#define AUpper(ch) ((ch) < 256 ? AUpperCase[ch] : AUpper2(ch))
#define ALower(ch) ((ch) < 256 ? ALowerCase[ch] : ALower2(ch))


#define AIsWordChar(ch) \
    ((ch) < 256 ? AReIsInSet(AWordCharSet, ch) \
     : AReIsInSet(ACharSets + A_RE_SET_SIZE * AWordCharLookupTable[ch >> 8], \
                 ch & 255))


AWideChar AUpper2(AWideChar ch);
AWideChar ALower2(AWideChar ch);


extern const unsigned char AUpperCase[256];
extern const unsigned char ALowerCase[256];

extern const AWideChar ACharClass[][32 / sizeof(AWideChar)];


typedef struct {
    unsigned char beg;
    unsigned char end;
    short delta;
} ACaseEntry;


extern const unsigned char AUpperLookupTable[];
extern const ACaseEntry AUpperTable[];

extern const unsigned char ALowerLookupTable[];
extern const ACaseEntry ALowerTable[];


extern const unsigned char AWordCharLookupTable[256];
extern const unsigned char ALetterLookupTable[256];

extern const unsigned short ACharSets[]; /* FIX: type? */


#define AWordCharSet   ACharSets
#define ALetterCharSet (ACharSets + 31 * (32 / sizeof(AWideChar)))


#endif
