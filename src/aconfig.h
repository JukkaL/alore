/* aconfig.h - Platform specific configuration options

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ACONFIG_H_INCL
#define ACONFIG_H_INCL

/* Include the autoconf-generated config header file. */
#include "config.h"


/* Check if compiling under Windows. */
#ifdef _WIN32
#define A_HAVE_WINDOWS
#endif


/* Activate large file support (may be needed on 32-bit platforms to support
   files larger than 2GB/4GB) */
#ifdef __linux__
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#endif


/* Generic type for a value (reference to an object). Must be unsigned, at
   least 32 bits wide and big enough to hold any pointer.
   FIX: configure should check the correct type. */
typedef unsigned long AValue;


/* Signed version of AValue. */
typedef long ASignedValue;


/* The size of AValue in bits */
#define A_VALUE_BITS (8 * SIZEOF_LONG)
/* The size of int/unsigned in bits */
/* FIX use configure to determine this */
#define A_INT_BITS 32
/* The size of long/unsigned long in bits */
#define A_LONG_BITS (8 * SIZEOF_LONG)

/* FIX: use this somewhere else.. */
#define A_VALUE_INT_SHIFT 2
#define A_VALUE_INT_BITS (A_VALUE_BITS - A_VALUE_INT_SHIFT)

#define A_POINTER_SIZE SIZEOF_VOID_P

#define A_OPCODE_BITS 32


#if A_OPCODE_BITS == 32
typedef unsigned AOpcode;
#else
typedef unsigned short AOpcode;
#endif

typedef unsigned short AWideChar;
typedef short ASignedWideChar;

/* 64-bit integer types */
typedef long long AInt64;
typedef unsigned long long AIntU64;


/* Start platform specific definitions */
#ifndef A_HAVE_WINDOWS

/* Generic definitions (for POSIX-like systems) */

#define A_NEWLINE_CHAR1 '\n'
/* #define NEWLINE_CHAR2 */
#define A_NEWLINE_STRING "\n"

#define A_DIR_SEPARATOR '/'
#define A_DIR_SEPARATOR_STRING "/"

#define A_IS_DIR_SEPARATOR(ch) ((ch) == '/')
#define A_IS_DRIVE_SEPARATOR(ch) 0

#define A_IS_ABS(path) A_IS_DIR_SEPARATOR(path[0])

#define A_IS_DRIVE_PATH(path) 0

#define A_PATH_SEPARATOR ':'
#define A_PATH_SEPARATOR_STRING ":"

#define A_HAVE_DYNAMIC_C_MODULES

#define A_MAX_PATH_LEN PATH_MAX

#define A_MODULE_SEARCH_PATH_BASE "/usr/local/lib/alore"

#define A_EXECUTABLE_EXT ""

#define A_HAVE_POSIX

#define A_APIFUNC
#define A_APIVAR

#define A_EXTERNAL
#define A_EXPORT

#else

/* Windows-specific definitions */

#define A_NEWLINE_CHAR1 '\r'
#define A_NEWLINE_CHAR2 '\n'
#define A_NEWLINE_STRING "\r\n"

#define A_DIR_SEPARATOR '\\'
#define A_DIR_SEPARATOR_STRING "\\"
#define A_ALT_DIR_SEPARATOR '/'
#define A_ALT_DIR_SEPARATOR_STRING "/"

#define A_IS_DIR_SEPARATOR(ch) ((ch) == '\\' || (ch) == '/')
#define A_IS_DRIVE_SEPARATOR(ch) ((ch) == ':')

#define A_IS_ABS(path) \
    (A_IS_DIR_SEPARATOR((path)[0]) || \
     (A_IS_DRIVE_PATH(path) && A_IS_DIR_SEPARATOR((path)[2])))

#define A_IS_DRIVE_PATH(path) \
    (((path)[0] | 32) >= 'a' && ((path)[0] | 32) <= 'z' && (path)[1] == ':')

#define A_PATH_SEPARATOR ';'
#define A_PATH_SEPARATOR_STRING ";"

#define A_HAVE_DYNAMIC_C_MODULES

#define A_MAX_PATH_LEN 1024 /* FIX */

#define A_MODULE_SEARCH_PATH_BASE "c:\\alore"

#define A_EXECUTABLE_EXT ".exe"

#ifdef A_DYNAMIC
#define A_APIFUNC __declspec(dllimport)
#define A_APIVAR __declspec(dllimport)
#else
#define A_APIFUNC __declspec(dllexport)
#define A_APIVAR __declspec(dllexport)
#endif

#define A_EXPORT __declspec(dllexport)

#endif /* End platform specific definitions */


/* Maximum number of nested anonymous functions */
#define A_MAX_ANON_SUB_DEPTH 16


/* Upper bound for the 10-based exponent of a float (absolute). Need not be
   the minimum upper bound. */
#define A_MAX_FLOAT_EXPONENT 1024


#define A_HEAP_PTR_MASK 0x00000000UL


#if A_VALUE_BITS == 32

#define A_MEM_START ((void *)0x00000000UL)
#define A_MEM_END   ((void *)0x7fffffffUL)
#define A_DUMMY_OFFSET       0x7ffffff8UL

#define A_PREFERRED_OLD_GEN_OFFSET ((void *)0x39000000UL)
#define A_PREFERRED_NEW_GEN_OFFSET ((void *)0x7f000000UL)

#define A_DEFAULT_MAX_HEAP_SIZE (1024 * 1024 * 1024)

#else

#define A_MEM_START ((void *)0x0000000000000000UL)
#define A_MEM_END   ((void *)0x7fffffffffffffffUL)
#define A_DUMMY_OFFSET       0x7ffffffffffffff8UL

#define A_PREFERRED_OLD_GEN_OFFSET ((void *)0x39000000UL)
#define A_PREFERRED_NEW_GEN_OFFSET ((void *)0x7f000000UL)

#define A_DEFAULT_MAX_HEAP_SIZE 0xfffffffffffff

#endif




/* Thread-specific Alore stack size in bytes */
#define A_ALORE_STACK_SIZE (A_VALUE_SIZE * 64 * 1024)


/* Convert a block-aligned pointer value to a short Int value. */
#define APtrToIntValue(ptr) \
    ((AValue)(void *)(ptr))

/* Convert a short Int value to a block-aligned pointer. */
#define AIntValueToPtr(val) \
    ((void *)(val))


/* Atomic integer type (wrt multithreading and multiple cores or processors) */
typedef int AAtomicInt;


/* Does the >> operator on signed operands maintain the sign bit?
   FIX: Check this in the configure script */
#define A_HAVE_ARITHMETIC_SHIFT_RIGHT


#if A_VALUE_BITS == 32

/* Use 16-bit digits on a 32-bit architecture
   IDEA: Why not use 32-bit digits? It might be more efficient if the platform
         supports 32x32->64 bit multiplication. */
typedef unsigned short ALongIntDigit;
typedef unsigned int ALongIntDoubleDigit;
typedef int ALongIntSignedDoubleDigit;
#define A_LONG_INT_DIGIT_BITS 16

#else

/* Use 32-bit digits on a 64-bit architecture */
typedef unsigned int ALongIntDigit;
typedef AIntU64 ALongIntDoubleDigit;
typedef AInt64 ALongIntSignedDoubleDigit;
#define A_LONG_INT_DIGIT_BITS 32

#endif


/* Block sizes are multiplies of ALLOC_UNIT bytes. This is currently hard coded
   to 8 and any other value is unlikely to work. */
#define A_ALLOC_UNIT 8

/* Minimal size of an allocated block. */
#if A_VALUE_BITS == 32
# define A_MIN_BLOCK_SIZE 8
#elif A_VALUE_BITS == 64
# define A_MIN_BLOCK_SIZE 16
#else
error!;
#endif


/* Size of a Float object in bytes */
#define A_FLOAT_SIZE SIZEOF_DOUBLE


/* Type used in the mark bitmap */
typedef unsigned long AMarkBitmapInt;
#define A_MARK_BITMAP_INT_SIZE SIZEOF_LONG


/* FIX: Use configure script to determine the values of these macros. */
#define A_HAVE_LITTLE_ENDIAN
/* #define HAVE_BIG_ENDIAN */


//#define A_NO_KEYBOARD_INTERRUPT

#if defined(HAVE_PTHREADS) || defined(A_HAVE_WINDOWS)
#define A_HAVE_THREADS
#define A_HAVE_ATHREAD
#endif


/* FIX: Is this needed? */
//#define A_HAVE_INT_64_BITS

#define A_HAVE_OS_MODULE
#define A_HAVE_SOCKET_MODULE


/* Uncomment this to enable an experimental x86-specific gcc optimization that
   improves interpreter performance up to 15%. Note that compiling without
   optimization might be impossible if this is enabled, and this might not work
   with all gcc versions and optimization flags. */
/* #define A_HAVE_GCC_X86_HACK */


#endif
