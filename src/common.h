/* common.h - Miscellenous useful definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This is a collection of tool macros, type definitions and constants used by
   almost all the other source files. */

#ifndef COMMON_H_INCL
#define COMMON_H_INCL

/* Must include this before system includes so that all #defines are
   correct (large file support etc.). */
#include "aconfig.h"

/* NULL defined in <stdio.h>. */
#include <stdio.h>

/* memcpy() declared in <string.h>. */
#include <string.h>

#include <limits.h>
#include <sys/types.h>


/* Boolean type. */
typedef int ABool;

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif


/* Returns the difference of two pointers in bytes. Assumes ptr2 >= ptr1. */
#define APtrDiff(ptr1, ptr2) \
    ((unsigned long)((char *)(ptr1) - (char *)(ptr2)))

/* Adds an integral number of bytes to a pointer. */
#define APtrAdd(ptr, add) \
    ((void *)((char *)(ptr) + (add)))
/* Subtracts an integral number of bytes from a pointer. */
#define APtrSub(ptr, sub) \
    ((void *)((char *)(ptr) - (sub)))


/* Copies size bytes from source to target. Memory regions must not overlap. */
#define ACopyMem(target, source, size) memcpy((target), (source), (size))

/* Like CopyMem, but allows overlapping. */
#define AMoveMem(target, source, size) memmove((target), (source), (size))

/* Put size zero bytes at target. */
#define AClearMem(target, size) memset((target), 0, (size))


/* Alloc a new string with the same contents as the source string using
   AllocStatic. */
char *ADupStr(const char *src);


ABool AJoinPath(char *dst, const char *path, const char *add);
ABool AIsFile(const char *path);


/* FIX: Assumes sizeof(AValue) == sizeof(unsigned long) (which might not be
        true on some 64-bit platforms) */
#define A_SHORT_INT_MAX (ASignedValue)(ULONG_MAX >> 3)
#define A_SHORT_INT_MIN (ASignedValue)(-(ULONG_MAX >> 3) - 1)


typedef size_t Asize_t;
typedef ssize_t Assize_t;


/* IDEA: Use these in various places in the code. */
#define A_SIZE_T_MAX SSIZE_MAX
/* FIX: Does this work always? */
#define A_SSIZE_T_MAX (Assize_t)(A_SIZE_T_MAX >> 1)


/* Print a string of debugging output uncoditionally to stderr. All format
   specifiers of AFormatMessage() are supported. */
void ATrace(const char *format, ...);


#endif
