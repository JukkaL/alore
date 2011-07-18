/* globals.h - Global variables

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef GLOBALS_H_INCL
#define GLOBALS_H_INCL

#include "thread.h"


ABool AInitializeGlobals(void);

ABool AAllocateModuleGlobals(int *firstVar, int *firstConst);

void AFreeGlobals(int index);

void ARecordNumMainGlobals(void);


int AAddGlobalValue(AValue v);
int AAddConstGlobalValue(AValue v);


#define AGlobalByNum(n) \
    (AGlobalVars[n])

#define ASetGlobalByNum(n, v) \
    (AGlobalVars[n] = v)

ABool ASetConstGlobalValue(AThread *t, int n, AValue v);


#define A_GLOBAL_LIST_INITIAL_LENGTH 128

#define A_GLOBAL_BUCKET_SIZE 8


/* Predefined global variable identifiers
   NOTE: These must match definitions below! */
enum {
    GL_NIL,                         /* nil */
    GL_TRUE,                        /* std::True */
    GL_FALSE,                       /* std::False */
    GL_DEFAULT_ARG,                 /* ADefaultArg */
    GL_ERROR,                       /* AError */
    GL_FIRST_ERROR_INSTANCE,        /* Pre-allocated exception objects */
    GL_VALUE_ERROR_INSTANCE = GL_FIRST_ERROR_INSTANCE,
    GL_RESOURCE_ERROR_INSTANCE,
    GL_TYPE_ERROR_INSTANCE,
    GL_MEMBER_ERROR_INSTANCE,
    GL_ARITHMETIC_ERROR_INSTANCE,
    GL_INDEX_ERROR_INSTANCE,
    GL_KEY_ERROR_INSTANCE,
    GL_CAST_ERROR_INSTANCE,
    GL_MEMORY_ERROR_INSTANCE,
    GL_INTERRUPT_EXCEPTION_INSTANCE,
    GL_EXIT_EXCEPTION_INSTANCE,
    GL_RUNTIME_ERROR_INSTANCE,      /* Stack overflow exception */
    GL_IO_ERROR_INSTANCE,
    GL_NEWLINE,                     /* Platform-specific line break string */
    GL_COMMAND_LINE_ARGS,           /* Array of cmd line arguments */
    GL_OUTPUT_STREAMS,              /* List of output streams; protected by a
                                       mutex in io module */
    GL_NUM_BUILTIN_GLOBALS
};


/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
   NOTE: These MUST match the definitions found above and in
         "std_module.c".
   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
typedef enum {
    EX_VALUE_ERROR,
    EX_RESOURCE_ERROR,
    EX_TYPE_ERROR,
    EX_MEMBER_ERROR,
    EX_ARITHMETIC_ERROR,
    EX_INDEX_ERROR,
    EX_KEY_ERROR,
    EX_CAST_ERROR,
    EX_MEMORY_ERROR,
    EX_INTERRUPT_EXCEPTION,
    EX_EXIT_EXCEPTION,
    EX_RUNTIME_ERROR,
    EX_IO_ERROR,
    EX_LAST = EX_IO_ERROR,
    EX_RAISED
} AExceptionType;


A_APIVAR extern AValue *AGlobalVars;
extern int AGlobalListLength;

A_APIVAR extern int ANumMainGlobals;
extern int AFirstMainGlobalVariable;

extern int AFreeGlobalBlock;

A_APIVAR extern int AFirstDynamicModule; /* IDEA: Definition in the wrong
                                            file? */


extern int ASymbolConstructorNum;

extern int AStdExceptionNum;
extern int AAnonFuncClassNum;
extern int AErrorClassNum[EX_LAST + 1];


#define AStdResourceErrorNum (AErrorClassNum[EX_RESOURCE_ERROR])
#define AStdValueErrorNum (AErrorClassNum[EX_VALUE_ERROR])
#define AStdIoErrorNum (AErrorClassNum[EX_IO_ERROR])


/* Return the index of the next global bucket in a bucket chain or 0 if no more
   are available. */
#define AGetNextGlobalBucket(num) \
    AValueToInt(AGlobalByNum(num))


#endif
