/* value.h - AValue type related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef VALUE_H_INCL
#define VALUE_H_INCL

#include "common.h"
#include <limits.h>


#define A_VALUE_SIZE (A_VALUE_BITS / 8)


struct AThread_;


typedef AValue (*ACFunction)(struct AThread_ *t, AValue *frame);


/* Identifiers of member tables defined in TypeInfo */
typedef enum {
    MT_VAR_SET_PUBLIC,
    MT_VAR_SET_PRIVATE,
    MT_VAR_GET_PUBLIC,
    MT_VAR_GET_PRIVATE,
    MT_METHOD_PUBLIC,
    MT_METHOD_PRIVATE,
    A_NUM_MEMBER_HASH_TABLES
} AMemberTableType;

#define A_TYPE_INFO_VALUE_SIZE (A_NUM_MEMBER_HASH_TABLES + 1)


/* FixArray (Array with a fixed size; used internally) */
typedef struct {
    AValue header;
    AValue elem[1];
} AFixArray;


/* Class instance */
typedef struct {
    AValue type;
    AValue member[1];
} AInstance;


/* Type object (class or interface) */
typedef struct ATypeInfo_ {
    AValue header1;
    AValue header2;
    AValue memberTable[A_NUM_MEMBER_HASH_TABLES];
    AValue interfaces;
    struct ATypeInfo_ *super;
    struct ASymbolInfo_ *sym;
    unsigned create;       /* Gvar index of the constructor */
    unsigned memberInitializer; /* Gvar index of the member initializer */
    unsigned numVars;      /* Number of values defined (not inherited) */
    unsigned totalNumVars; /* Total number of values in an instance */
    unsigned dataSize;     /* Size of extra binary data */
    unsigned dataOffset;   /* Offset of extra binary data from block start */
    unsigned instanceSize; /* Total size of an instance, rounded up */
    unsigned numInterfaces; /* Number of interfaces implemented by the type */
    unsigned interfacesSize; /* Length of the interfaces array */
    char isInterface;      /* Is the type an interface */
    char hasEqOverload;    /* Does the type have an overloaded == operator? */
    char hasHashOverload;  /* Does the type have a _hash method? */
    char hasMemberInitializer; /* Does the type have member initializers? */
    char hasFinalizer;     /* Does the type have a #f method? */
    char hasFinalizerOrData; /* Does the type have #f or binary data? */
    char isValid;          /* Has the type been initialized properly in
                              scan? Some members may be initialized only
                              later, including supertype reference, even if
                              this is TRUE. */
    char isSuperValid;     /* If FALSE, the super member has not been
                              evaluated yet; it will be evaluated next time the
                              value is needed during compilation, or after the
                              current compilation pass has been finished. */
    int extDataMember;     /* Number of member that holds external data size or
                              -1 if not present */
} ATypeInfo;


/* String */
typedef struct {
    AValue header;
    unsigned char elem[1];
} AString;


typedef struct {
    AValue header;
    ALongIntDigit digit[1];
} ALongInt;


/* Mixed object (bound method / range / pair) */
typedef struct {
    AValue header;
    AValue type;
    union {
        /* NOTE: range and pair must have the same layout! */
        struct {
            AValue start;
            AValue stop;
        } range;
        struct {
            AValue instance;
            AValue method;
        } boundMethod;
        struct {
            AValue head;
            AValue tail;
        } pair;
    } data;
} AMixedObject;


#define A_ARG_BITS 14
#define A_MAX_ARG_COUNT ((1 << A_ARG_BITS) - 1)
#define A_VAR_ARG_FLAG (1 << A_ARG_BITS)


#define A_FRAME_BITS 15
#define A_MAX_FRAME_DEPTH ((1 << A_FRAME_BITS) - 1)


/* Function object representing a global function (direct representation) or
   the body of an anonymous function or a method (as part of of a larger
   composite object) */
typedef struct {
    AValue header;
    struct ASymbolInfo_ *sym; /* Function symbol (for global functions) or
                                 class symbol (otherwise) or NULL (for
                                 module main level) or
                                 AAnonFunctionDummySymbol (for anonymous
                                 functions) */
    unsigned short minArgs;
    unsigned short maxArgs;  /* Maximum number of arguments; presence of
                                A_VAR_ARG_FLAG bit flag implies unbounded
                                number of arguments */
    unsigned stackFrameSize; /* Size of function stack frame, in bytes */
    unsigned codeLen;
    unsigned short fileNum;
#ifdef HAVE_JIT_COMPILER
    unsigned short num; /* Number of the global variable that holds this
                           function (only valid for C and compiled
                           functions) */
    char isJitFunction; /* Is this function part of the JIT Compiler */
    char filler[3];
#endif
    /* unsigned short lineNum; */ /* IDEA: Use this */
    union {
        AOpcode opc[1];
        ACFunction cfunc;
    } code;
} AFunction;


/* Constant */
typedef struct {
    AValue header;
    struct ASymbolInfo_ *sym;
} AConstant;


/* Wide string (16-bit characters) */
typedef struct {
    AValue header;
    AWideChar elem[1];
} AWideString;


/* Substring */
typedef struct {
    AValue header;
    AValue str;
    AValue ind;
    AValue len;
} ASubString;


/* Type masks */

#define A_TYPE_MASK(hi, lo) \
    (AValue)(((hi##UL) << (A_VALUE_BITS - 1)) | (lo))

#define A_INTEGER_MASK     A_TYPE_MASK(0x0, 0x3)
#define A_INTEGER_MAGIC    A_TYPE_MASK(0x0, 0x0)

/* FIX: the following stink badly.. */
#define A_GENERIC_MASK     A_TYPE_MASK(0x1, 0x7) /* FIX: bad name? */

/* NOTE: Do not change these! Some code may depend on this particular order. */
#define A_ARRAY_MAGIC        A_TYPE_MASK(0x0, 0x1)
#define A_TYPE_MAGIC         A_TYPE_MASK(0x0, 0x2)
/* TYPE_MASK(0x0, 0x3) is unused */
#define A_MIXED_MAGIC        A_TYPE_MASK(0x0, 0x5)
#define A_INSTANCE_MAGIC     A_TYPE_MASK(0x0, 0x6)
#define A_SUBSTRING_MAGIC    A_TYPE_MASK(0x0, 0x7)
#define A_STRING_MAGIC       A_TYPE_MASK(0x1, 0x1)
#define A_WSTRING_MAGIC      A_TYPE_MASK(0x1, 0x2)
#define A_FUNCTION_MAGIC     A_TYPE_MASK(0x1, 0x3)
#define A_CONSTANT_MAGIC     A_TYPE_MASK(0x1, 0x5)
#define A_LONGINT_MAGIC      A_TYPE_MASK(0x1, 0x6)
#define A_FLOAT_MAGIC        A_TYPE_MASK(0x1, 0x7)


#define AIsNonPtrValue(value) \
    ((value) & (1UL << (A_VALUE_BITS - 1)))
#define AIsValueBlockValue(value) \
    (((value) & ((1UL << (A_VALUE_BITS - 1)) | 1)) == 1)
#define AIsNonPtrAndNonValueBlockValueInstanceBlockValue(val) \
    (((val) & 4) != 0)


/* Type checking macros */

#define AIsShortInt(value) \
    (((value) & A_INTEGER_MASK) == A_INTEGER_MAGIC)


#define A_MASKED(value) \
    ((value) & A_GENERIC_MASK)

#define AIsFloat(value)          (A_MASKED(value) == A_FLOAT_MAGIC)
#define AIsFixArray(value)       (A_MASKED(value) == A_ARRAY_MAGIC)
#define AIsNarrowStr(value)      (A_MASKED(value) == A_STRING_MAGIC)
#define AIsWideStr(value)        (A_MASKED(value) == A_WSTRING_MAGIC)
#define AIsMixedValue(value)     (A_MASKED(value) == A_MIXED_MAGIC)
#define AIsInstance(value)       (A_MASKED(value) == A_INSTANCE_MAGIC)
#define AIsGlobalFunction(value) (A_MASKED(value) == A_FUNCTION_MAGIC)
#define AIsLongInt(value)        (A_MASKED(value) == A_LONGINT_MAGIC)
#define AIsNonSpecialType(value) (A_MASKED(value) == A_TYPE_MAGIC)
#define AIsConstant(value)       (A_MASKED(value) == A_CONSTANT_MAGIC)
#define AIsSubStr(value)         (A_MASKED(value) == A_SUBSTRING_MAGIC)

#define AIsRange(value) \
    (AIsMixedValue(value) && AValueToMixedObject(value)->type == A_RANGE_ID)
#define AIsMethod(value) \
    (AIsMixedValue(value) && \
     AValueToMixedObject(value)->type == A_BOUND_METHOD_ID)
#define AIsPair(value) \
    (AIsMixedValue(value) && \
     AValueToMixedObject(value)->type == A_PAIR_ID)
#define AIsStr(value) \
    (AIsNarrowStr(value) || AIsWideStr(value) || AIsSubStr(value))
#define AIsNarrowSubStr(value) \
    (AIsSubStr(value) && AIsNarrowStr(AValueToSubStr(value)->str))
#define AIsWideSubStr(value) \
    (AIsSubStr(value) && AIsWideStr(AValueToSubStr(value)->str))

/* NOTE: Assume that the argument is a type. */
#define AIsTypeClass(type) \
    (!AValueToType(type)->isInterface)


/* Conversion macros and functions */


/* Convert a value to a void pointer. */
#define AValueToPtr(value) \
  ((void *)(((value) & ~A_GENERIC_MASK) | A_HEAP_PTR_MASK))


#ifdef A_HAVE_ARITHMETIC_SHIFT_RIGHT
#define AValueToInt(value) \
    ((ASignedValue)(value) >> A_VALUE_INT_SHIFT)
#else
#define AValueToInt(value) \
    ((ASignedValue)(value) < 0                       \
     ? (ASignedValue)(((value) >> A_VALUE_INT_SHIFT) \
                      | (3L << (A_VALUE_BITS - 2)))  \
     : (ASignedValue)(value) >> A_VALUE_INT_SHIFT)
#endif

#define AValueToUnsignedInt(value) \
    (unsigned)((AValue)(value) >> A_VALUE_INT_SHIFT)

#define AValueToFloat(value) \
    (*(double *)AValueToPtr(value))

#define AValueToFixArray(value) \
    ((AFixArray *)AValueToPtr(value))

#define AValueToStr(value) \
    ((AString *)AValueToPtr(value))

#define AValueToWideStr(value) \
    ((AWideString *)AValueToPtr(value))

#define AValueToSubStr(value) \
    ((ASubString *)AValueToPtr(value))

#define AValueToType(value) \
    ((ATypeInfo *)AValueToPtr(value))

#define AValueToInstance(value) \
    ((AInstance *)AValueToPtr(value))

#define AValueToFunction(value) \
    ((AFunction *)AValueToPtr(value))

#define AValueToMixedObject(value) \
    ((AMixedObject *)AValueToPtr(value))

#define AValueToLongInt(value) \
    ((ALongInt *)AValueToPtr(value))

#define AValueToConstant(value) \
    ((AConstant *)AValueToPtr(value))


#define AIntToValue(intNum) \
    ((AValue)(intNum) << A_VALUE_INT_SHIFT)

#define AFloatPtrToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_FLOAT_MAGIC)

#define AStrToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_STRING_MAGIC)

#define AWideStrToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_WSTRING_MAGIC)

#define ASubStrToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_SUBSTRING_MAGIC)

#define AFunctionToValue(ptr) \
  (((AValue)(ptr) & ~A_GENERIC_MASK) | A_FUNCTION_MAGIC)

#define ATypeToValue(type) \
    (((AValue)(type) & ~A_HEAP_PTR_MASK) | A_TYPE_MAGIC)

#define AFixArrayToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_ARRAY_MAGIC)

#define AInstanceToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_INSTANCE_MAGIC)

#define AConstantToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_CONSTANT_MAGIC)

#define AMixedObjectToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_MIXED_MAGIC)

#define ALongIntToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_LONGINT_MAGIC)


#define AMixedBlockToValue(ptr) \
    ATypeToValue(ptr)

#define AValueBlockToValue(ptr) \
    AFixArrayToValue(ptr)

#define ANonPointerBlockToValue(ptr) \
    (((AValue)(ptr) & ~A_HEAP_PTR_MASK) | A_STRING_MAGIC)

#define AIsMixedBlockValue(val) \
    AIsNonSpecialType(val)


#define AZero AIntToValue(0)


#define A_SLICE_END A_SHORT_INT_MAX


#define A_COMPILED_FUNCTION_FLAG 4

#define AIsCompiledFunction(func) \
    ((func)->header & A_COMPILED_FUNCTION_FLAG)


typedef enum {
    A_IS_TRUE,
    A_IS_FALSE,
    A_IS_ERROR
} AIsResult;


AIsResult AIsOfType(AValue item, AValue typeVal);


#define AGetInstanceType(ptr) \
    ((ATypeInfo *)AValueToPtr(((AInstance *)(ptr))->type & ~A_HEAP_PTR_MASK))


#define AIsAnonFunc(v) \
    (AIsInstance(v) && AGetInstanceType(AValueToInstance(v)) == AAnonFuncClass)


#define A_BOUND_METHOD_ID AIntToValue(0)
#define A_RANGE_ID AIntToValue(1)
#define A_PAIR_ID AIntToValue(2)


#define A_SUB_STRING_SIZE AGetBlockSize(sizeof(ASubString))


#define ANormIndex(i, l) \
    (i < 0 ? (l) + (i) : (i))

#define ANormIndexV(i, l) \
    ((ASignedValue)(i) < 0 ? (l) + (i) : (i))


/* Array member slot indices (that can be used with AMemberDirect) */
enum {
    A_ARRAY_A,
    A_ARRAY_LEN,
    A_ARRAY_CAPACITY,
    /* Total number of slots. */
    A_NUM_ARRAY_SLOTS
};


/* Anonymous function (std::AnonFunc) member slot indices. Descriptions of
   slots contents included in comments. */
enum {
    /* FixArray object containing exposed variable containers. */
    A_ANON_EXPOSED_VARS,
    /* Global function object that implements the behaviour of the anonymous
       function. It takes exposed variables + actual arguments as arguments. */
    A_ANON_IMPLEMENTATION_FUNC,
    /* Total number of slots. */
    A_NUM_ANON_SLOTS
};


typedef ASignedValue AShortInt;


A_APIVAR extern AValue ANil;
A_APIVAR extern AValue ATrue;
A_APIVAR extern AValue AFalse;
A_APIVAR extern AValue AError;
A_APIVAR extern AValue ADefault;


#define AIsNil(v) ((v) == ANil)
#define AIsTrue(v) ((v) == ATrue)
#define AIsFalse(v) ((v) == AFalse)
#define AIsZero(v) ((v) == AZero)
#define AIsError(v) ((v) == AError)
#define AIsDefault(v) ((v) == ADefault)


#define ARangeStart(value) (AValueToMixedObject(value)->data.range.start)
#define ARangeStop(value) (AValueToMixedObject(value)->data.range.stop)

#define AIsShortIntRange(value) \
    (AIsRange(value) && \
     AIsShortInt(ARangeStart(value)) && AIsShortInt(ARangeStop(value)))


extern ATypeInfo *AAnonFuncClass;


#endif
