/* debug_params.h - User-definable parameters for Alore run-time debugging

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef DEBUG_PARAMS_H_INCL
#define DEBUG_PARAMS_H_INCL


/* Uncomment the following line to enable debugging */
//#define A_DEBUG
#ifdef A_DEBUG


/* Check memory structure before and after each garbage collection increment */
#define A_DEBUG_VERIFY_MEMORY_AROUND_GC
/* Display status messages (if disabled, only errors are reported) */
#define A_DEBUG_STATUS_MESSAGES
/* Update instruction counter and display messages in the compiler */
//#define A_DEBUG_COMPILER
/* Show instruction counter and opcodes */
#define A_DEBUG_INSTRUCTION_TRACE
/* Show each garbage collection increment (FIX implement) */
//#define A_DEBUG_SHOW_GC_INCREMENTS
/* Clear free blocks with dummy value */
#define A_DEBUG_FILL_FREE_BLOCKS
/* Check memory structure */
#define A_DEBUG_VERIFY_MEMORY
/* Display stack trace */
//#define A_DEBUG_DISPLAY_STACK_TRACE
/* Dump global variables */
//#define A_DEBUG_DUMP_GLOBALS
/* When dumping globals, dump class contents */
//#define A_DEBUG_DUMP_CLASSES
/* Call Debug_PeriodicCheck() when debug checks are active */
#define A_DEBUG_CALL_PERIODIC_CHECK
/* Display information on allocated blocks */
//#define A_DEBUG_ALLOC_TRACE
/* Perform garbage collection every X allocations */
//#define A_DEBUG_GC_EVERY_NTH 1
/* Check memory structure before each allocation */
//#define A_DEBUG_VERIFY_MEMORY_AT_ALLOC

/* Show return values FIX: implement this better */
//#define A_DEBUG_SHOW_RETURN_VALUES

//#define A_DEBUG_SHOW_HEAP_INFO

/* Show pointers to allocated heap blocks as the heap is grown */
//#define A_DEBUG_SHOW_HEAP_POINTERS


#endif /* DEBUG */


#ifdef A_DEBUG_GC_EVERY_NTH
#define A_NO_INLINE_ALLOC 1
#else
#define A_NO_INLINE_ALLOC 0
#endif


#endif
