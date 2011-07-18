/* strtonum.h - Str -> Int/Float conversion routines

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef STRTONUM_H_INCL
#define STRTONUM_H_INCL

#include "thread.h"


/* Tries to convert a string to an integer value. */
unsigned char *AStrToInt(AThread *t, const unsigned char *beg,
                        const unsigned char *end, AValue *valPtr);

/* Tries to convert a string to a floating point value. */
unsigned char *AStrToFloat(AThread *t, const unsigned char *beg,
                          const unsigned char *end, AValue *val);


#endif
