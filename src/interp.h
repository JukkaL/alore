/* interp.h - Alore interpreter

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef INTERP_H_INCL
#define INTERP_H_INCL

#include "debug_params.h"


/* Exception error message identifiers. These refer to the array
   ExceptionErrorMessages (defined in interp.c). */
typedef enum {
    EM_NO_MESSAGE,
    EM_NOT_CLASS_INSTANCE,
    EM_ARRAY_INDEX_OUT_OF_RANGE,
    EM_STR_INDEX_OUT_OF_RANGE,
    EM_TOO_FEW_VALUES_TO_ASSIGN,
    EM_TOO_MANY_VALUES_TO_ASSIGN,
    EM_DIVISION_BY_ZERO,
    EM_TOO_MANY_RECURSIVE_CALLS,
    EM_CALL_INTERFACE,
    EM_SLICE_INDEX_MUST_BE_INT_OR_NIL,
} AExceptionMessageId;


#define AErrorId(type, message) \
    ((type) | ((message) << 5))

#define AGetErrorTypeFromId(id) \
    ((id) & 0x1f)

#define AGetErrorMessageFromId(id) \
    ((id) >> 5)


#if !defined(__GNUC__) || defined(A_DEBUG) || defined(A_NO_GCC_EXTENSIONS)
#  define A_INITIALIZE_INTERPRETER
#  define A_BEGIN_INTERPRETER_LOOP for (;;) { 
#  define A_BEGIN_INTERPRETER_LOOP_2 switch (*ip) {
#  define A_OPCODE(name) case name:
#  define A_END_OPCODE break
#  define A_END_INTERPRETER_LOOP } }
#  define A_IP_REG
#  define A_STACK_REG
#else
#  include "gccinterp.h"
#endif


#endif
