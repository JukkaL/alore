/* internal.h - Internal macros and stuff

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef INTERNAL_H_INCL
#define INTERNAL_H_INCL


/* NOTE: The following simple macros may evaluate some of their arguments more
         than once. */


/* Multiplies value by scale / 32. */
#define AScale(val, scale) \
    ((((val) + 31) >> 5) * (scale))


/* Returns the minimum/maximum of arguments. */
#define AMin(a, b) ((a) < (b) ? (a) : (b))
#define AMax(a, b) ((a) > (b) ? (a) : (b))

/* Returns the absolute value of the argument. */
#define AAbs(a) ((a) >= 0 ? (a) : -(a))


/* Rounds i up to the nearest multiple of pow2 (which must be a power of 2). */
#define ARoundUp(i, pow2) \
    (((i) + (pow2) - 1) & ~((pow2) - 1))


/* Converts a _letter_ to lowercase. Be careful: no non-letters! */
#define ALoCase(ch) ((ch) | 32)

/* Converts a _letter_ to uppercase. Be careful: no non-letters! */
#define AUpCase(ch) ((ch) & ~32)


/* Returns TRUE if CH is a digit [0-9]. */
#define AIsDigit(ch) \
    ((ch) >= '0' && (ch) <= '9')

/* Returns TRUE if CH is an octal digit [0-7]. */
#define AIsOctalDigit(ch) \
    ((ch) >= '0' && (ch) <= '7')

/* Returns TRUE if CH is a hexadecimal digit [0-9A-Fa-f]. */
#define AIsHexDigit(ch) \
    (AIsDigit(ch) || (ALoCase(ch) >= 'a' && ALoCase(ch) <= 'f'))

/* Returns TRUE if CH is a letter [A-Za-z].*/
#define AIsAlpha(ch) \
    (ALoCase(ch) >= 'a' && ALoCase(ch) <= 'z')

#define AHexDigitToNum(c) \
    ((c) >= 'a' ? (c) - 'a' + 10 : (c) >= 'A' ? (c) - 'A' + 10 : (c) - '0')

#define AUnicodeSequenceValue(p) \
    ((AHexDigitToNum((p)[0]) << 12) | (AHexDigitToNum((p)[1]) << 8) | \
     (AHexDigitToNum((p)[2]) << 4) | AHexDigitToNum((p)[3]))


/* Calculate the base-2 logarithm of an integer, rounded up. The result is
   calculated directly without calling a function, so it can be evaluated
   compile-time.
   NOTE: Only values up to 1<<24 are supported! */
#define ALog2(val) \
    (unsigned)((val) <= 1 << 0  ? 0  : \
     (val) <= 1 << 1  ? 1  : \
     (val) <= 1 << 2  ? 2  : \
     (val) <= 1 << 3  ? 3  : \
     (val) <= 1 << 4  ? 4  : \
     (val) <= 1 << 5  ? 5  : \
     (val) <= 1 << 6  ? 6  : \
     (val) <= 1 << 7  ? 7  : \
     (val) <= 1 << 8  ? 8  : \
     (val) <= 1 << 9  ? 9  : \
     (val) <= 1 << 10 ? 10 : \
     (val) <= 1 << 11 ? 11 : \
     (val) <= 1 << 12 ? 12 : \
     (val) <= 1 << 13 ? 13 : \
     (val) <= 1 << 14 ? 14 : \
     (val) <= 1 << 15 ? 15 : \
     (val) <= 1 << 16 ? 16 : \
     (val) <= 1 << 17 ? 17 : \
     (val) <= 1 << 18 ? 18 : \
     (val) <= 1 << 19 ? 19 : \
     (val) <= 1 << 20 ? 20 : \
     (val) <= 1 << 21 ? 21 : \
     (val) <= 1 << 22 ? 22 : \
     (val) <= 1 << 23 ? 23 : 24)


/* This is macro is used for code that isn't stricly necessary, but it may get
   rid of a compiler warning. Therefore it could be left out. */
#define AAvoidWarning_M(code) code


#endif
