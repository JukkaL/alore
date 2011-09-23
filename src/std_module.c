/* std_module.c - std module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "memberid.h"
#include "std_module.h"
#include "io_module.h"
#include "errno_module.h"
#include "mem.h"
#include "array.h"
#include "tuple.h"
#include "str.h"
#include "int.h"
#include "float.h"
#include "internal.h"
#include "gc.h"
#include "class.h"
#include "util.h"
#include "wrappers.h"

#include <math.h>
#include <stdlib.h>


static AValue ArrayRepr(AThread *t, AValue *array, AValue *str,
                       AValue *tmp, int *indices, int depth);
static AValue PairOrRangeHashValue(AThread *t, AValue *frame);
static AValue ConcatFixArrayAndArray(AThread *t, AValue fixa, AValue a);
static ASymbolInfo *TypeSymbol(int globalnum);


/* Global nums of public types */
static int StdBooleanNum;
static int StdArrayNum;
static int StdTupleNum;
static int StdMapNum;
int AStdObjectNum;
int AStdStrNum;
int AStdIntNum;
int AStdTypeNum;
int AStdFloatNum;
int AStdRangeNum;
int AStdFunctionNum;
int AStdPairNum;
int AStdConstantNum;
int AStdExceptionNum;

/* Global nums of std module exception classes. Use EX_* constants such as
   EX_TYPE_ERROR (defined in globals.h) to index this. Note that this array
   does not include std::Exception; it has an individual variable above. */
int AErrorClassNum[EX_LAST + 1];

/* Global nums of private types */
int AStdBufNum;          /* std::__Buf (used by C API function AAllocBuf) */
int ARangeIterNum;       /* Range iterator */
int AAnonFuncClassNum;   /* AnonFunc (internal representation for anonymous
                            function instances) */

/* Wrapper classes for primitive types with non-standard representations.
   Typically short-lived instances of these are constructed whenever a member
   of these types is accessed using the ordinary member-access mechanism. These
   are generally not used for optimized operations such as integer addition,
   which have special implementations. */
static int StrClassNum;      /* std::__Str */
static int IntClassNum;      /* std::__Int */
static int FloatClassNum;    /* std::__Float */
static int RangeClassNum;    /* std::__Range */
static int PairClassNum;     /* std::__Pair */
static int ConstantClassNum; /* std::__Constant */
static int FunctionClassNum; /* std::__Function */
static int TypeClassNum;     /* std::__Type */

/* Global nums of functions */
int AStdReprNum;
int AStdHashNum;

/* Global nums of Boolean constants */
static int TrueNum;
static int FalseNum;

/* Global nums of Str constants */
static int TabNum;
static int CRNum;
static int LFNum;
static int NewlineNum;


/* References to type objects of basic types. Normal pointers can be safely
   used since these are always unmovable and will never be garbage
   collected. */

/* Normal classes */
ATypeInfo *AArrayClass;
ATypeInfo *ATupleClass;
ATypeInfo *ABooleanClass;
/* Primitive wrapper classes */
ATypeInfo *AStrClass;
ATypeInfo *AIntClass;
ATypeInfo *AFloatClass;
ATypeInfo *ARangeClass;
ATypeInfo *APairClass;
ATypeInfo *AConstantClass;
ATypeInfo *AFunctionClass;
ATypeInfo *ATypeClass;
/* Internal representation class for anonymous function instances */
ATypeInfo *AAnonFuncClass;


/* Initialize references to type objects of basic types, e.g. AArrayClass. */
ABool AInitializeStdTypes(void)
{
    AArrayClass = AValueToType(AGlobalByNum(StdArrayNum));
    ATupleClass = AValueToType(AGlobalByNum(StdTupleNum));
    AStrClass = AValueToType(AGlobalByNum(StrClassNum));
    AIntClass = AValueToType(AGlobalByNum(IntClassNum));
    AFloatClass = AValueToType(AGlobalByNum(FloatClassNum));
    ABooleanClass = AValueToType(AGlobalByNum(StdBooleanNum));
    ARangeClass = AValueToType(AGlobalByNum(RangeClassNum));
    APairClass = AValueToType(AGlobalByNum(PairClassNum));
    AConstantClass = AValueToType(AGlobalByNum(ConstantClassNum));
    AFunctionClass = AValueToType(AGlobalByNum(FunctionClassNum));
    ATypeClass = AValueToType(AGlobalByNum(TypeClassNum));
    AAnonFuncClass = AValueToType(AGlobalByNum(AAnonFuncClassNum));
    return TRUE;
}    


/* Initialize pre-allocated exception instances. */
ABool AInitializeStdExceptions(AThread *t)
{
    int i;

    /* IDEA: No need to create ExitException instance and maybe some others. */

    /* IDEA: Maybe all of these can be removed, since these are inherently
             thread-unsafe. */
    
    for (i = 0; i <= EX_LAST; i++) {
        AValue obj = ACreateBasicExceptionInstance(t, i, NULL);
        if (!ASetConstGlobalValue(t, GL_FIRST_ERROR_INSTANCE + i, obj))
            return FALSE;
    }

    return TRUE;
}


/* std::Object create() */
static AValue ObjectCreate(AThread *t, AValue *frame)
{
    /* Do nothing */
    return frame[0];
}


/* std::Object _eq() */
static AValue Object_eq(AThread *t, AValue *frame)
{
    /* Objects equality is based on identity by default. */
    return frame[0] == frame[1] ? ATrue : AFalse;
}


/* std::Chr(n) */
AValue AStdChr(AThread *t, AValue *frame)
{
    ASignedValue i;
    ABool result;
    
    if (!AIsShortInt(frame[0])) {
        /* Invalid argument */
        if (AIsLongInt(frame[0]))
            return ARaiseValueErrorND(t, NULL);
        else
            return ARaiseTypeErrorND(t, "Chr() expects an Int (%T given)",
                                     frame[0]);
    }

    i = AValueToInt(frame[0]);
    if ((AValue)i < 256) {
        /* Narrow character */
        AString *s;

        AAlloc_M(t, AGetBlockSize(sizeof(AValue) + 1), s, result);
        if (!result)
            return ARaiseMemoryErrorND(t);

        AInitNonPointerBlock(&s->header, 1);
        s->elem[0] = i;

        return AStrToValue(s);
    } else if ((AValue)i < 65536) {
        /* Wide character */
        AWideString *s;

        AAlloc_M(t, AGetBlockSize(sizeof(AValue) + sizeof(AWideChar)), s,
                result);
        if (!result)
            return ARaiseMemoryErrorND(t);

        AInitNonPointerBlock(&s->header, sizeof(AWideChar));
        s->elem[0] = i;

        return AWideStrToValue(s);
    } else
        return ARaiseValueErrorND(t, NULL); /* Overflow */

    /* Not reached */
}


/* std::Ord(ch) */
static AValue StdOrd(AThread *t, AValue *frame)
{
    if (AIsNarrowStr(frame[0])) {
        if (AGetStrLen(AValueToStr(frame[0])) == 1)
            return AIntToValue(AValueToStr(frame[0])->elem[0]);
    } else if (AIsWideStr(frame[0])) {
        if (AGetWideStrLen(AValueToWideStr(frame[0])) == 1)
            return AIntToValue(AValueToWideStr(frame[0])->elem[0]);
    } else if (!AIsSubStr(frame[0]))
        return ARaiseTypeErrorND(t, "Ord() expects a character (%T given)",
                                 frame[0]);

    /* String with invalid length. */
    
    return ARaiseValueErrorND(t, "Ord() expects a character "
                              "(string with length %d given)",
                              (int)AStrLen(frame[0]));
}


static AValue AMakeUnmovableBoolean(AThread *t, ABool bool)
{
    AValue *obj = AAllocUnmovable(2 * sizeof(AValue));
    if (obj == NULL)
        return ARaiseMemoryError(t);
    AInitInstanceBlockGen(obj, AGlobalByNum(StdBooleanNum), 0);
    ((AInstance *)obj)->member[0] = AIntToValue(bool);
    return AInstanceToValue((AInstance *)obj);
}


/* std::Main() */
static AValue StdMain(AThread *t, AValue *frame)
{
    /* Initialize the True object. */
    frame[0] = AMakeUnmovableBoolean(t, TRUE);
    ASetGlobalByNum(GL_TRUE, frame[0]);
    ASetGlobalByNum(TrueNum, frame[0]);
    
    /* Initialize the False object. */
    frame[0] = AMakeUnmovableBoolean(t, FALSE);
    ASetGlobalByNum(GL_FALSE, frame[0]);
    ASetGlobalByNum(FalseNum, frame[0]);

    /* Initialize references to Booleans. */
    ATrue = AGlobalByNum(TrueNum);
    AFalse = AGlobalByNum(FalseNum);

    /* Initialize string constants. */
    ASetConstGlobalByNum(t, TabNum, AMakeStr(t, "\t"));
    ASetConstGlobalByNum(t, CRNum, AMakeStr(t, "\r"));
    ASetConstGlobalByNum(t, LFNum, AMakeStr(t, "\n"));
    ASetConstGlobalByNum(t, NewlineNum, AMakeStr(t, A_NEWLINE_STRING));

#ifdef HAVE_JIT_COMPILER
    {
        AValue map = ACallValue(t, AGlobalByNum(StdMapNum), 0, frame);
        if (AIsError(map))
            return AError;
        ASetGlobalByNum(AJitInfoMapNum, map);
    }
#endif

    /* Also initialize the io module. Do it explicitly to ensure that the
       built-in modules are initialized in the correct order. */
    return AIoMain(t, frame);
}


#define MAX_REPR_CALL_DEPTH 16
#define MAX_REPR_LEN 10000


/* std::Repr(object) */
AValue AStdRepr(AThread *t, AValue *frame)
{
    AValue *tmp;
    AValue retVal;
    
    if (!AAllocTempStack(t, 4))
        return AError;

    tmp = t->tempStackPtr - 4;
    tmp[0] = AZero;
    tmp[1] = AZero;
    tmp[2] = AZero;
    tmp[3] = AZero;
    
    if (AIsShortInt(frame[0]) || AIsLongInt(frame[0]))
        retVal = AStdStr(t, frame);
    else if (AIsStr(frame[0]))
        retVal = AStrRepr(t, frame);
    else if (AIsArray(frame[0])) {
        int indices[MAX_REPR_CALL_DEPTH + 1];
        *tmp = AMakeArray(t, 0);
        retVal = ArrayRepr(t, frame, tmp, tmp + 1, indices, 0);
        if (!AIsError(retVal))
            retVal = AJoin(t, *tmp, ADefault);
    } else if (AIsInstance(frame[0])) {
        if (AMemberByNum(t, frame[0], AM__REPR) == AError) {
            retVal = AStdStr(t, frame);
        } else {
            retVal = ACallMethodByNum(t, AM__REPR, 0, frame);
            if (!AIsStr(retVal) && !AIsError(retVal))
                return ARaiseTypeError(
                    t, "_repr of %T returned non-string (%T)",
                    frame[0], retVal);
        }
    } else if (AIsConstant(frame[0]) || AIsGlobalFunction(frame[0])
               || AIsNonSpecialType(frame[0]))
        retVal = AStdStr(t, frame);
    else if (AIsPair(frame[0]) || AIsRange(frame[0])) {
        retVal = AError;
        
        tmp[0] = AIsPair(frame[0])
            ? AValueToMixedObject(frame[0])->data.pair.head
            : AValueToMixedObject(frame[0])->data.range.start;
        tmp[0] = ACallValue(t, AGlobalByNum(AStdReprNum), 1, tmp);
        if (tmp[0] != AError) {
            tmp[1] = AIsPair(frame[0])
                ? AValueToMixedObject(frame[0])->data.pair.tail
                : AValueToMixedObject(frame[0])->data.range.stop;
            tmp[1] = ACallValue(t, AGlobalByNum(AStdReprNum), 1,
                                  tmp + 1);
            if (tmp[1] != AError) {
                tmp[0] = AConcatStringAndCStr(t, tmp[0], AIsPair(frame[0])
                                             ? " : " : " to ");
                if (tmp[0] != AError)
                    retVal = AConcatStrings(t, tmp[0], tmp[1]);
            }
        }
    } else if (AIsMethod(frame[0])) {
        char buf[1000];
        AFormatMessage(buf, 1000, "%F",
                      AValueToFunction(AGlobalByNum(AValueToInt(
                          AValueToMixedObject(
                              frame[0])->data.boundMethod.method))));
        retVal = ACreateStringFromCStr(t, buf);
    } else if (AIsFloat(frame[0])) {
        char buf[100];
        /* IDEA: Verify that the length of the buffer is reasonable. */
        sprintf(buf, "%.17g", AValueToFloat(frame[0]));
        /* If the float is an infinity or a NaN, the result of sprintf varies
           from system to system. Normalize any non-standard values. */
        AFixInfAndNanStrings(buf);
        retVal = ACreateStringFromCStr(t, buf);
    } else
        retVal = ARaiseTypeErrorND(t, NULL);

    t->tempStackPtr -= 4;

    return retVal;
}


static AValue RecursiveArrayItem(AValue array, int *indices, int depth)
{
    int i;
    
    for (i = 0; i <= depth; i++)
        array = AArrayItem(array, indices[i]);

    return array;
}


/* Return the representation of an array. */
static AValue ArrayRepr(AThread *t, AValue *array, AValue *acc,
                        AValue *tmp, int *indices, int depth)
{
    int i;
    int len;
    AValue str;

    str = AMakeStr(t, "[");
    AAppendArray(t, *acc, str);

    len = AArrayLen(RecursiveArrayItem(*array, indices, depth - 1));
    
    for (i = 0; i < len; i++) {
        indices[depth] = i;
        *tmp = RecursiveArrayItem(*array, indices, depth);
        
        if (AIsArray(*tmp)) {
            if (depth == MAX_REPR_CALL_DEPTH)
                return ARaiseValueErrorND(t, NULL);
            if (ArrayRepr(t, array, acc, tmp, indices, depth + 1) ==
                AError)
                return AError;
        } else {
            *tmp = ACallValue(t, AGlobalByNum(AStdReprNum), 1, tmp);
            if (*tmp == AError)
                return *tmp;
            
            AAppendArray(t, *acc, *tmp);
        }
        
        if (i < len - 1) {
            str = AMakeStr(t, ", ");
            AAppendArray(t, *acc, str);
        }
        
        if (AArrayLen(*acc) > MAX_REPR_LEN)
            return ARaiseValueErrorND(t, NULL);
    }

    str = AMakeStr(t, "]");
    AAppendArray(t, *acc, str);

    return *acc;
}


/* std::Exception create([message]) */
AValue AStdExceptionCreate(AThread *t, AValue *frame)
{
    if (!AIsDefault(frame[1]) && !AIsNil(frame[1])) {
        AExpectStr(t, frame[1]);
        ASetMemberDirect(t, frame[0], AError_MESSAGE, frame[1]);
    }

    return frame[0];
}


/* Function representing std::Type. It cannot be called. */
static AValue StdType(AThread *t, AValue *frame)
{
    return ARaiseValueErrorND(t, AMsgTypeIsNotCallable,
                              TypeSymbol(AStdTypeNum));
}


/* Function representing std::Range. It cannot be called. */
static AValue StdRange(AThread *t, AValue *frame)
{
    return ARaiseValueErrorND(t, AMsgTypeIsNotCallable,
                              TypeSymbol(AStdRangeNum));
}


/* Function representing std::Function. It cannot be called. */
static AValue StdFunction(AThread *t, AValue *frame)
{
    return ARaiseValueErrorND(t, AMsgTypeIsNotCallable,
                              TypeSymbol(AStdFunctionNum));
}


/* Function representing std::Pair. It cannot be called. */
static AValue StdPair(AThread *t, AValue *frame)
{
    return ARaiseValueErrorND(t, AMsgTypeIsNotCallable,
                              TypeSymbol(AStdPairNum));
}


/* Function representing std::Constant. It cannot be called. */
static AValue StdConstant(AThread *t, AValue *frame)
{
    /* IDEA: Maybe we should allow creating Constant objects dynamically. */
    return ARaiseValueErrorND(t, AMsgTypeIsNotCallable,
                              TypeSymbol(AStdConstantNum));
}


/* std::Hash(object) */
AValue AStdHash(AThread *t, AValue *frame)
{
    if (AIsNarrowStr(frame[0]) || AIsSubStr(frame[0]))
        return AStringHashValue(frame[0]);
    if (AIsShortInt(frame[0])) 
        return frame[0] ^ AIntToValue(frame[0] >> 10);
    else if (AIsInstance(frame[0])) {
        if (AGetInstanceType(AValueToInstance(frame[0]))->hasHashOverload) {
            /* Call the _hash() method to calculate hash value. */
            AValue hash = ACallMethodByNum(t, AM__HASH, 0, frame);
            /* The hash value must be an integer. */
            if (!AIsInt(hash) && !AIsError(hash))
                return ARaiseTypeError(
                    t, "_hash of %T returned non-integer (%T)", frame[0],
                    hash);
            else
                return hash;
        } else
            return AGetIdHashValue(t, frame + 0);
    } else if (AIsWideStr(frame[0]))
        return AStringHashValue(frame[0]);
    else if (AIsConstant(frame[0]))
        return AIntToValue(frame[0] >> 3);
    else if (AIsPair(frame[0]) || AIsRange(frame[0]))
        return PairOrRangeHashValue(t, frame);
    else if (AIsFloat(frame[0]))
        return AFloatHashValue(AValueToFloat(frame[0]));
    else if (AIsLongInt(frame[0]))
        return ALongIntHashValue(frame[0]);
    else if (AIsGlobalFunction(frame[0]) || AIsNonSpecialType(frame[0]))
        return AIntToValue(AValueToInt(frame[0]));
    else if (AIsMethod(frame[0]))
        return AGetIdHashValue(t, frame + 0);
    else
        return ARaiseTypeErrorND(t, NULL);
}


/* Compute the hash value of a Pair of Range object. Share the implementation,
   since they have the same representation. */
static AValue PairOrRangeHashValue(AThread *t, AValue *frame)
{
    AValue h1;
    AValue h2;
    
    frame[1] = AValueToMixedObject(frame[0])->data.range.start;
    h1 = ACallValue(t, AGlobalByNum(AStdHashNum), 1,
                      frame + 1);
    if (AIsError(h1))
        return AError;
    if (!AIsShortInt(h1)) {
        if (AIsLongInt(h1))
            h1 = ALongIntHashValue(h1);
        else
            return ARaiseTypeErrorND(t, NULL);
    }
    
    frame[1] = AValueToMixedObject(frame[0])->data.range.stop;
    h2 = ACallValue(t, AGlobalByNum(AStdHashNum), 1,
                      frame + 1);
    if (AIsError(h2))
        return AError;
    if (!AIsShortInt(h2)) {
        if (AIsLongInt(h2))
            h2 = ALongIntHashValue(h2);
        else
            return ARaiseTypeErrorND(t, NULL);
    }
    
    return h1 * 33 + h2;
}


/* Range iterator create(range)
   This type is private to std and is generated by calling Range iterator(). */
static AValue RangeIterCreate(AThread *t, AValue *frame)
{
    AValue lo, hi;
    
    AGetRange(t, frame[1], &lo, &hi);

    AExpectInt(t, lo);
    AExpectInt(t, hi);

    frame[1] = hi;
    ASetMemberDirect(t, frame[0], 0, lo);
    ASetMemberDirect(t, frame[0], 1, frame[1]);
    
    return frame[0];
}


/* Range iterator hasNext() */
static AValue RangeIterHasNext(AThread *t, AValue *frame)
{
    return AIsLt(t, AMemberDirect(frame[0], 0), AMemberDirect(frame[0], 1)) ?
        ATrue : AFalse;
}


/* Range iterator next() */
static AValue RangeIterNext(AThread *t, AValue *frame)
{
    AValue next;
    
    frame[1] = AMemberDirect(frame[0], 0);
    next = AAdd(t, frame[1], AMakeInt(t, 1));
    ASetMemberDirect(t, frame[0], 0, next);
    return frame[1];
}


/* std::ExitException create(code) */
static AValue ExitExceptionCreate(AThread *t, AValue *frame)
{
    /* The argument must be an integer. */
    AGetInt(t, frame[1]);
    ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS, frame[1]);
    frame[1] = ADefault;
    return AStdExceptionCreate(t, frame);
}


/* std::Exit([code]) */
static AValue StdExit(AThread *t, AValue *frame)
{
    AValue exception;
    
    if (AIsDefault(frame[0]))
        frame[0] = AIntToValue(0);

    exception = ACallValue(t,
                           AGlobalByNum(AErrorClassNum[EX_EXIT_EXCEPTION]),
                           1, frame);
    if (AIsError(exception))
        return AError;

    return ARaiseExceptionValue(t, exception);
}


/* Helper function that maps internal type symbols such as "std::__Array" to
   user-visible type symbols such as "std::Array". Other symbols are returned
   unmodified. */
ASymbolInfo *ARealTypeSymbol(ASymbolInfo *sym)
{
    if (sym == AStrClass->sym)
        return AValueToFunction(AGlobalByNum(AStdStrNum))->sym;
    else if (sym == AIntClass->sym)
        return AValueToFunction(AGlobalByNum(AStdIntNum))->sym;
    else if (sym == AFloatClass->sym)
        return AValueToFunction(AGlobalByNum(AStdFloatNum))->sym;
    else if (sym == ARangeClass->sym)
        return AValueToFunction(AGlobalByNum(AStdRangeNum))->sym;
    else if (sym == APairClass->sym)
        return AValueToFunction(AGlobalByNum(AStdPairNum))->sym;
    else if (sym == AConstantClass->sym)
        return AValueToFunction(AGlobalByNum(AStdConstantNum))->sym;
    else if (sym == AFunctionClass->sym)
        return AValueToFunction(AGlobalByNum(AStdFunctionNum))->sym;
    else if (sym == ATypeClass->sym)
        return AValueToFunction(AGlobalByNum(AStdTypeNum))->sym;
    else
        return sym;
}


/* Helper function that maps user-visible type symbols such as "std::Str" to
   internal type symbols such as "std::__Str". Other symbols are returned
   unmodified. */
ASymbolInfo *AInternalTypeSymbol(ASymbolInfo *sym)
{
    if (sym->num == AStdStrNum)
        return AStrClass->sym;
    else if (sym->num == AStdIntNum)
        return AIntClass->sym;
    else if (sym->num == AStdFloatNum)
        return AFloatClass->sym;
    else if (sym->num == AStdRangeNum)
        return ARangeClass->sym;
    else if (sym->num == AStdPairNum)
        return APairClass->sym;
    else if (sym->num == AStdConstantNum)
        return AConstantClass->sym;
    else if (sym->num == AStdFunctionNum)
        return AFunctionClass->sym;
    else if (sym->num == AStdTypeNum)
        return ATypeClass->sym;
    else
        return sym;
}


/* std::Min */
static AValue StdMin(AThread *t, AValue *frame)
{
    if (!AIsDefault(frame[1])) {
        int ret = AIsLt(t, frame[0], frame[1]);
        if (ret > 0)
            return frame[0];
        else if (ret == 0)
            return frame[1];
        else
            return AError;
    } else {
        int ret;
        
        frame[2] = AIterator(t, frame[0]);
        if (AIsError(frame[2]))
            return AError;

        while ((ret = ANext(t, frame[2], frame + 3)) > 0) {
            if (AIsDefault(frame[1]))
                frame[1] = frame[3];
            else {
                int ret = AIsLt(t, frame[3], frame[1]);
                if (ret > 0)
                    frame[1] = frame[3];
                else if (ret < 0)
                    break;
            }
        }

        if (ret < 0)
            return AError;
        else if (!AIsDefault(frame[1]))
            return frame[1];
        else
            return ARaiseValueError(t, NULL);
    }
}


/* std::Max */
static AValue StdMax(AThread *t, AValue *frame)
{
    if (!AIsDefault(frame[1])) {
        int ret = AIsGt(t, frame[0], frame[1]);
        if (ret > 0)
            return frame[0];
        else if (ret == 0)
            return frame[1];
        else
            return AError;
    } else {
        int ret;
        
        frame[2] = AIterator(t, frame[0]);
        if (AIsError(frame[2]))
            return AError;

        while ((ret = ANext(t, frame[2], frame + 3)) > 0) {
            if (AIsDefault(frame[1]))
                frame[1] = frame[3];
            else {
                int ret = AIsGt(t, frame[3], frame[1]);
                if (ret > 0)
                    frame[1] = frame[3];
                else if (ret < 0)
                    break;
            }
        }

        if (ret < 0)
            return AError;
        else if (!AIsDefault(frame[1]))
            return frame[1];
        else
            return ARaiseValueError(t, NULL);
    }
}


/* std::Reversed(iterable) */
static AValue StdReversed(AThread *t, AValue *frame)
{
    if (AIsShortIntRange(frame[0])) {
        /* Use efficient specialized implementation for short int ranges. */
        AShortInt start = AValueToInt(ARangeStart(frame[0]));
        AShortInt stop = AValueToInt(ARangeStop(frame[0]));

        frame[1] = AMakeArray(t, 0);
        
        if (stop > start) {
            AShortInt i;
            for (i = stop - 1; i >= start; i--)
                AAppendArray(t, frame[1], AIntToValue(i));
        }        
    } else if (AIsArray(frame[0])) {
        /* Use efficient specialized implementation for arrays. */
        Assize_t i;
        Assize_t len;
    
        len = AArrayLen(frame[0]);
        frame[1] = AMakeArray(t, len);
        for (i = 0; i < len; i++) {
            AValue item = AArrayItem(frame[0], i);
            ASetArrayItem(t, frame[1], len - i - 1, item);
        }
    } else {
        /* Generic implementation for arbitrary iterables. */
        int status;
        Assize_t i, n;
        
        frame[1] = AMakeArray(t, 0);
        frame[2] = AIterator(t, frame[0]);
        if (frame[2] == AError)
            return AError;

        while ((status = ANext(t, frame[2], &frame[3])) > 0)
            AAppendArray(t, frame[1], frame[3]);
        
        if (status < 0)
            return AError;

        n = AArrayLen(frame[1]);
        for (i = 0; i < n / 2; i++) {
            frame[2] = AArrayItem(frame[1], i);
            ASetArrayItem(t, frame[1], i, AArrayItem(frame[1], n - i - 1));
            ASetArrayItem(t, frame[1], n - i - 1, frame[2]);
        }
    }
    
    return frame[1];
}


/* std::Abs(n) */
static AValue StdAbs(AThread *t, AValue *frame)
{
    if (AIsShortInt(frame[0])) {
        if ((ASignedValue)frame[0] >= 0)
            return frame[0];
        else if (frame[0] != AIntToValue(A_SHORT_INT_MIN))
            return -(ASignedValue)frame[0];
        else
            return AMakeInt64(t, -A_SHORT_INT_MIN);
    } else if (AIsLongInt(frame[0])) {
        if (AGetLongIntSign(AValueToLongInt(frame[0])))
            return ANegateLongInt(t, frame[0]);
        else
            return frame[0];
    } else if (AIsFloat(frame[0]))
        return AMakeFloat(t, fabs(AValueToFloat(frame[0])));
    else
        return ARaiseTypeError(t, NULL);
}


/* std::Write(...) */
static AValue Write(AThread *t, AValue *frame)
{
    frame[1] = frame[0];
    frame[0] = AGlobalByNum(AStdOutNum);
    return AStreamWrite(t, frame);
}


/* std::WriteLn(...) */
static AValue WriteLn(AThread *t, AValue *frame)
{
    frame[1] = frame[0];
    frame[0] = AGlobalByNum(AStdOutNum);
    return AStreamWriteLn(t, frame);
}


/* std::Print(...) */
static AValue Print(AThread *t, AValue *frame)
{
    Assize_t i;
    Assize_t n = AArrayLen(frame[0]);

    frame[1] = AGlobalByNum(AStdOutNum);
    
    for (i = 0; i < n; i++) {
        frame[2] = AArrayItem(frame[0], i);
        if (ACallMethod(t, "write", 1, frame + 1) == AError)
            return AError;
        
        /* Write space separator for non-last items. */
        if (i < n - 1) {
            frame[2] = AMakeStr(t, " ");
            if (ACallMethod(t, "write", 1, frame + 1) == AError)
                return AError;
        }
    }

    /* Write a newline. */
    if (ACallMethod(t, "writeLn", 0, frame + 1) == AError)
        return AError;

    /* Finally, always flush stdout. */
    return ACallMethod(t, "flush", 0, frame + 1);
}


/* std::ReadLn() */
static AValue ReadLn(AThread *t, AValue *frame)
{
    frame[1] = frame[0];
    frame[0] = AGlobalByNum(AStdInNum);
    return AStreamReadLn(t, frame);
}


/* The _call method of anonymous function objects. */
static AValue AnonFuncCall(AThread *t, AValue *frame)
{
    int numArgs;
    int numExposed;
    int totalNumArgs;
    AValue func;
    int funcMinArgs, funcMaxArgs;
    
    /* Call the ordinary global function object that is stored in the anonymous
       function object. */

    /* Query some values. */
    numArgs = AArrayLen(frame[1]);
    numExposed = AFixArrayLen(AMemberDirect(frame[0], A_ANON_EXPOSED_VARS));
    totalNumArgs = numArgs + numExposed;
    func = AMemberDirect(frame[0], A_ANON_IMPLEMENTATION_FUNC);
    funcMinArgs = AValueToFunction(func)->minArgs;
    funcMaxArgs = AValueToFunction(func)->maxArgs;
    
    /* Check the number of arguments. The checking performed by
       ACallValueVarArg is not sufficient since it may receive exposed
       variables as additional arguments that invalidate argument counts in
       exception error messages. */
    if (totalNumArgs < funcMinArgs ||
        (!(funcMaxArgs & A_VAR_ARG_FLAG) && totalNumArgs > funcMaxArgs))
        return ARaiseArgumentErrorND(t, func, numArgs, numExposed);
    
    /* The arguments contain first any exposed variable containers needed by
       the anonymous function and then the ordinary arguments passed by the
       caller. */
    frame[1] = ConcatFixArrayAndArray(
        t, AMemberDirect(frame[0], A_ANON_EXPOSED_VARS), frame[1]);
    
    return ACallValueVarArg(t, func, 0, frame + 1);
}


/* Utility function that concatenates a FixArray and an Array object and
   returns an Array object. */
static AValue ConcatFixArrayAndArray(AThread *t, AValue fixa, AValue a)
{
    AValue *temp = AAllocTemps(t, 3);
    int lenFixa, lenA;
    AValue res;
    int i;

    lenFixa = AFixArrayLen(fixa);
    lenA = AArrayLen(a);
    
    temp[0] = fixa;
    temp[1] = a;
    temp[2] = AMakeArray(t, lenFixa + lenA);

    for (i = 0; i < lenFixa; i++)
        ASetArrayItem(t, temp[2], i, AFixArrayItem(temp[0], i));
    for (i = 0; i < lenA; i++)
        ASetArrayItem(t, temp[2], lenFixa + i, AArrayItem(temp[1], i));

    res = temp[2];

    AFreeTemps(t, 3);

    return res;
}


/* The _str method of anonymous function objects. */
static AValue AnonFuncStr(AThread *t, AValue *frame)
{
    return AMakeStr(t, "anonymous function");
}


/* std::Boolean _str() */
static AValue BooleanStr(AThread *t, AValue *frame)
{
    if (AMemberDirect(frame[0], 0) == AZero)
        return AMakeStr(t, "False");
    else
        return AMakeStr(t, "True");
}


/* std::IoError create([message[, error]]) */
static AValue IoErrorCreate(AThread *t, AValue *frame)
{
    if (frame[2] != ADefault) {
        if (AIsInt(frame[2])) {
            ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS,
                             frame[2]);
            if (!AIsZero(frame[2]))
                frame[3] = AErrnoToConstant(t, frame[2]);
            else
                frame[3] = ANil;
            ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS + 1,
                             frame[3]);
        } else if (AIsConstant(frame[2])) {
            frame[3] = AConstantToErrno(t, frame[2]);
            ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS,
                             frame[3]);
            ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS + 1,
                             frame[2]);
        } else
            return ARaiseTypeError(t, "Int or Constant expected");
    } else {
        ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS, AZero);
        ASetMemberDirect(t, frame[0], A_NUM_EXCEPTION_MEMBER_VARS + 1, ANil);
    }
    return AStdExceptionCreate(t, frame);
}


/* Return the symbol representing the type with the given global num. This
   only works for primitive type objects represented as functions. */
static ASymbolInfo *TypeSymbol(int globalnum)
{
    return AValueToFunction(AGlobalByNum(globalnum))->sym;
}


A_MODULE(std, "std")
    A_DEF("Main", 0, 5, StdMain)

    /* Internal symbolic constants used by std::Map and std::Set
       implementations */
    A_SYMBOLIC_CONST_P(A_PRIVATE("Empty"), &AEmptyMarkerNum)
    A_SYMBOLIC_CONST_P(A_PRIVATE("Removed"), &ARemovedMarkerNum)

    A_CLASS_P("Object", &AStdObjectNum)
        A_METHOD("create", 0, 0, ObjectCreate)
        A_METHOD("_eq", 1, 0, Object_eq)
    A_END_CLASS()

    A_INTERFACE("Iterable")
        A_METHOD("iterator", 0, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Iterator")
        A_METHOD("hasNext", 0, 0, NULL)
        A_METHOD("next", 0, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Sequence")
        A_METHOD("_get", 1, 0, NULL)
        A_METHOD("length", 0, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Comparable")
        A_METHOD("_lt", 1, 0, NULL)
        A_METHOD("_gt", 1, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Addable")
        A_METHOD("_add", 1, 0, NULL)
    A_END_INTERFACE()

    A_INTERFACE("Multipliable")
        A_METHOD("_mul", 1, 0, NULL)
    A_END_INTERFACE()

    A_CLASS_PRIV_P("Boolean", 1, &StdBooleanNum)
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("_str", 0, 0, BooleanStr)
    A_END_CLASS()
    
    A_CLASS_PRIV_P("Array", A_NUM_ARRAY_SLOTS, &StdArrayNum)
        A_IMPLEMENT("std::Iterable")
        A_IMPLEMENT("std::Sequence")
        A_IMPLEMENT("std::Comparable")
        A_IMPLEMENT("std::Multipliable")
        A_METHOD_OPT("create", 0, 1, 2, AArrayCreate)
        A_METHOD("#i", 0, 0, AArrayInitialize)
        A_METHOD("length", 0, 0, AArrayLengthMethod)
        A_METHOD("find", 1, 1, AArrayFind)
        A_METHOD("index", 1, 1, AArrayIndex) /* NOTE: Identical to find */
        A_METHOD("count", 1, 1, AArrayCount)
        A_METHOD("append", 1, 0, AArrayAppend)
        A_METHOD("extend", 1, 1, AArrayExtend)
        A_METHOD("insertAt", 2, 1, AArrayInsertAt)
        A_METHOD("remove", 1, 2, AArrayRemove)
        A_METHOD("removeAt", 1, 2, AArrayRemoveAt)
        A_METHOD("iterator", 0, 3, AArrayIter)
        A_METHOD("copy", 0, 0, AArrayCopy)
        A_METHOD("_get", 1, 0, AArray_get)
        A_METHOD("_set", 2, 0, AArray_set)
        A_METHOD("_add", 1, 1, AArray_add)
        A_METHOD("_mul", 1, 1, AArray_mul)
        A_METHOD("_eq", 1, 2, AArray_eq)
        A_METHOD("_lt", 1, 2, AArray_lt)
        A_METHOD("_gt", 1, 2, AArray_gt)
        A_METHOD("_in", 1, 1, AArray_in)
        A_METHOD("_str", 0, 5, AArray_str)
        A_METHOD("_hash", 0, 2, AArray_hash)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("ArrayIterator"), 3, &AArrayIterNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("create", 2, 0, AArrayIterCreate)
        A_METHOD("hasNext", 0, 0, AArrayIterHasNext)
        A_METHOD("next", 0, 0, AArrayIterNext)
    A_END_CLASS()
    
    A_CLASS_PRIV_P("Tuple", A_NUM_ARRAY_SLOTS, &StdTupleNum)
        A_IMPLEMENT("std::Iterable")
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("length", 0, 0, ATupleLengthMethod)
        A_METHOD("iterator", 0, 3, ATupleIter)
        A_METHOD("_get", 1, 0, ATuple_get)
        A_METHOD("_in", 1, 1, ATuple_in)
        A_METHOD("_eq", 1, 2, ATuple_eq)
        A_METHOD("_lt", 1, 2, ATuple_lt)
        A_METHOD("_gt", 1, 2, ATuple_gt)
        A_METHOD("_str", 0, 5, ATuple_str)
        A_METHOD("_hash", 0, 2, ATuple_hash)
    A_END_CLASS()

    A_CLASS_PRIV_P(A_PRIVATE("TupleIterator"), 3, &ATupleIterNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("create", 2, 0, ATupleIterCreate)
        A_METHOD("hasNext", 0, 0, ATupleIterHasNext)
        A_METHOD("next", 0, 0, ATupleIterNext)
    A_END_CLASS()

    A_CLASS_PRIV_P("Map", 4, &StdMapNum)
        A_IMPLEMENT("std::Iterable")
        A_METHOD_VARARG("create", 0, 0, 6, AMapCreate)
        A_METHOD("#i", 0, 0, AMapInitialize)
        A_METHOD("_get", 1, 3, AMap_get)
        A_METHOD("_set", 2, 3, AMap_set)
        A_METHOD("get", 2, 2, AMapGet)
        A_METHOD("hasKey", 1, 3, AMapHasKey)
        A_METHOD("length", 0, 0, AMapLength)
        A_METHOD("remove", 1, 3, AMapRemove)
        A_METHOD("items", 0, 4, AMapItems)
        A_METHOD("keys", 0, 0, AMapKeys)
        A_METHOD("values", 0, 0, AMapValues)
        A_METHOD("_str", 0, 5, AMap_str)
        A_METHOD("iterator", 0, 1, AMapIter)
    A_END_CLASS()
    
    A_CLASS_PRIV_P(A_PRIVATE("MapIterator"), 3, &AMapIterNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("create", 1, 0, AMapIterCreate)
        A_METHOD("hasNext", 0, 0, AMapIterHasNext)
        A_METHOD("next", 0, 0, AMapIterNext)
    A_END_CLASS()

    /* Wrapper class for Str objects; this is used when accessing Str
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Str"), 1, &StrClassNum)
        A_IMPLEMENT("std::Iterable")
        A_IMPLEMENT("std::Sequence")
        A_IMPLEMENT("std::Comparable")
        A_IMPLEMENT("std::Multipliable")
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("length", 0, 0, AStrLengthMethod)
        A_METHOD("iterator", 0, 2, AStrIter)
        A_METHOD("count", 1, 1, AStrCount)
        A_METHOD("upper", 0, 1, AStrUpper)
        A_METHOD("lower", 0, 1, AStrLower)
        A_METHOD("strip", 0, 0, AStrStrip)
        A_METHOD_OPT("find", 1, 2, 2, AStrFind)
        A_METHOD("index", 1, 2, AStrIndex)
        A_METHOD_VARARG("format", 0, 0, 4, AStrFormat)
        A_METHOD("startsWith", 1, 0, AStrStartsWith)
        A_METHOD("endsWith", 1, 0, AStrEndsWith)
        A_METHOD_OPT("replace", 2, 3, 1, AStrReplace)
        A_METHOD_OPT("split", 0, 2, 2, AStrSplit)
        A_METHOD("join", 1, 2, AStrJoin) /* NOTE: Remember AJoin! */
        A_METHOD_OPT("encode", 1, 2, 1, AStrEncode)
        A_METHOD_OPT("decode", 1, 2, 2, AStrDecode)
        A_METHOD("_add", 1, 0, AConcatWrapper)
        A_METHOD("_mul", 1, 0, AMulWrapper)
        A_METHOD("_get", 1, 0, AGetItemWrapper)
        A_METHOD("_eq", 1, 0, AEqWrapper)
        A_METHOD("_lt", 1, 0, ALtWrapper)
        A_METHOD("_gt", 1, 0, AGtWrapper)
        A_METHOD("_in", 1, 0, AInWrapper)
        A_METHOD("_hash", 0, 0, AHashWrapper)
        A_METHOD("_repr", 0, 0, AReprWrapper)
        A_METHOD("_int", 0, 0, AIntWrapper)
        A_METHOD("_float", 0, 0, AFloatWrapper)
    A_END_CLASS()

    /* Str iterator class */
    A_CLASS_PRIV_P(A_PRIVATE("StrIterator"), 3, &AStrIterNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("create", 1, 0, AStrIterCreate)
        A_METHOD("hasNext", 0, 0, AStrIterHasNext)
        A_METHOD("next", 0, 0, AStrIterNext)
    A_END_CLASS()

    /* Wrapper class for Int objects; this is used when accessing Int
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Int"), 1, &IntClassNum)
        A_IMPLEMENT("std::Comparable")
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("_add", 1, 0, AAddWrapper)
        A_METHOD("_sub", 1, 0, ASubWrapper)
        A_METHOD("_neg", 0, 0, ANegWrapper)
        A_METHOD("_mul", 1, 0, AMulWrapper)
        A_METHOD("_div", 1, 0, ADivWrapper)
        A_METHOD("_idiv", 1, 0, AIdivWrapper)
        A_METHOD("_mod", 1, 0, AModWrapper)
        A_METHOD("_pow", 1, 0, APowWrapper)
        A_METHOD("_eq", 1, 0, AEqWrapper)
        A_METHOD("_lt", 1, 0, ALtWrapper)
        A_METHOD("_gt", 1, 0, AGtWrapper)
        A_METHOD("_hash", 0, 0, AHashWrapper)
        A_METHOD("_str", 0, 0, AStrWrapper)
        A_METHOD("_float", 0, 0, AFloatWrapper)
    A_END_CLASS()

    /* Wrapper class for Float objects; this is used when accessing Float
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Float"), 1, &FloatClassNum)
        A_IMPLEMENT("std::Comparable")
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("_add", 1, 0, AAddWrapper)
        A_METHOD("_sub", 1, 0, ASubWrapper)
        A_METHOD("_neg", 0, 0, ANegWrapper)
        A_METHOD("_mul", 1, 0, AMulWrapper)
        A_METHOD("_div", 1, 0, ADivWrapper)
        A_METHOD("_idiv", 1, 0, AIdivWrapper)
        A_METHOD("_mod", 1, 0, AModWrapper)
        A_METHOD("_pow", 1, 0, APowWrapper)
        A_METHOD("_eq", 1, 0, AEqWrapper)
        A_METHOD("_lt", 1, 0, ALtWrapper)
        A_METHOD("_gt", 1, 0, AGtWrapper)
        A_METHOD("_hash", 0, 0, AHashWrapper)
        A_METHOD("_str", 0, 0, AStrWrapper)
        A_METHOD("_repr", 0, 0, AReprWrapper)
        A_METHOD("_int", 0, 0, AIntWrapper)
    A_END_CLASS()
    
    /* Wrapper class for Range objects; this is used when accessing Range
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Range"), 1, &RangeClassNum)
        A_IMPLEMENT("std::Iterable")
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_GETTER("start", 0, AStdRangeStart)
        A_GETTER("stop", 0, AStdRangeStop)
        A_METHOD("_eq", 1, 0, AEqWrapper)
        A_METHOD("_in", 1, 0, AInWrapper)
        A_METHOD("iterator", 0, 0, AIterWrapper)
        A_METHOD("_hash", 0, 0, AHashWrapper)
        A_METHOD("_str", 0, 0, AStrWrapper)
    A_END_CLASS()
    
    /* Wrapper class for Pair objects; this is used when accessing Pair
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Pair"), 1, &PairClassNum)
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_GETTER("left", 0, APairLeft)
        A_GETTER("right", 0, APairRight)
        A_METHOD("_eq", 1, 0, AEqWrapper)
        A_METHOD("_hash", 0, 0, AHashWrapper)
        A_METHOD("_str", 0, 0, AStrWrapper)
    A_END_CLASS()
    
    /* Wrapper class for Constant objects; this is used when accessing Constant
       members */
    A_CLASS_PRIV_P(A_PRIVATE("__Constant"), 1, &ConstantClassNum)
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("_str", 0, 0, AStrWrapper)
    A_END_CLASS()
    
    /* Wrapper class for Function objects; this is used when accessing
       Function members */
    A_CLASS_PRIV_P(A_PRIVATE("__Function"), 1, &FunctionClassNum)
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("_str", 0, 0, AStrWrapper)
    A_END_CLASS()
    
    /* Wrapper class for Type objects; this is used when accessing
       Type members */
    A_CLASS_PRIV_P(A_PRIVATE("__Type"), 1, &TypeClassNum)
        A_METHOD("create", 0, 0, AHiddenCreate)
        A_METHOD("supertype", 0, 0, ATypeSupertype)
        A_METHOD("interfaces", 0, 1, ATypeInterfaces)
        A_METHOD("_str", 0, 0, AStrWrapper)
    A_END_CLASS()

    /* Internal anonymous function class (user-visible type is Function) */
    A_CLASS_PRIV_P(A_PRIVATE("AnonFunc"), A_NUM_ANON_SLOTS, &AAnonFuncClassNum)
        /* The first slot is a FixArray object of exposed variable objects */
        /* The second slot is a function object that implements the function */
        A_METHOD_VARARG("_call", 0, 0, 0, AnonFuncCall)
        A_METHOD("_str", 0, 0, AnonFuncStr)
    A_END_CLASS()

    /* Primitive type objects with special runtime representations are
       internally represented as function objects. */
    A_DEF_P("Str", 1, 0, AStdStr, &AStdStrNum) /* NOTE: don't change args */
    A_DEF_P("Repr", 1, 0, AStdRepr, &AStdReprNum) /* NOTE: don't change args */
    A_DEF_OPT_P("Int", 1, 2, 0, AStdInt, &AStdIntNum)
    A_DEF_P("Type", 0, 0, StdType, &AStdTypeNum)
    A_DEF_P("Float", 1, 0, AStdFloat, &AStdFloatNum)
    A_DEF_P("Range", 0, 0, StdRange, &AStdRangeNum)
    A_DEF_P("Function", 0, 0, StdFunction, &AStdFunctionNum)
    A_DEF_P("Pair", 0, 0, StdPair, &AStdPairNum)
    A_DEF_P("Constant", 0, 0, StdConstant, &AStdConstantNum)
    
    A_DEF("Abs", 1, 0, StdAbs)
    
    A_DEF("Chr", 1, 0, AStdChr)
    A_DEF("Ord", 1, 0, StdOrd)
    
    A_DEF_P("Hash", 1, 2, AStdHash, &AStdHashNum) /* NOTE: don't change */
    A_DEF_OPT("Min", 1, 2, 2, StdMin)
    A_DEF_OPT("Max", 1, 2, 2, StdMax)
    A_DEF_OPT("Sort", 1, 2, 5, AStdSort)
    A_DEF("Reversed", 1, 3, StdReversed)
    
    A_DEF_OPT("Exit", 0, 1, 2, StdExit)

    A_DEF_VARARG("Print", 0, 0, 3, Print)
    A_DEF_VARARG("WriteLn", 0, 0, 2, WriteLn) /* NOTE: See Print and Stream */
    A_DEF_VARARG("Write", 0, 0, 2, Write)     /* NOTE: See Print and Stream */
    A_DEF("ReadLn", 0, 5, ReadLn)
    
    A_EMPTY_CONST_P("False", &FalseNum)
    A_EMPTY_CONST_P("True", &TrueNum)

    A_EMPTY_CONST_P("Tab", &TabNum)
    A_EMPTY_CONST_P("CR", &CRNum)
    A_EMPTY_CONST_P("LF", &LFNum)
    A_EMPTY_CONST_P("Newline", &NewlineNum)

    /* Range iterator type */
    A_CLASS_PRIV_P(A_PRIVATE("RangeIterator"), 2, &ARangeIterNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("create", 1, 0, RangeIterCreate)
        A_METHOD("hasNext", 0, 0, RangeIterHasNext)
        A_METHOD("next", 0, 1, RangeIterNext)
    A_END_CLASS()
    
    A_CLASS_PRIV_P("Exception", 1, &AStdExceptionNum)
        A_EMPTY_CONST("message")
        A_METHOD_OPT("create", 0, 1, 0, AStdExceptionCreate)
    A_END_CLASS()
    A_CLASS_P("ValueError", AErrorClassNum + EX_VALUE_ERROR)
        A_INHERIT("Exception")
    A_END_CLASS()
    A_CLASS_P("ResourceError", AErrorClassNum + EX_RESOURCE_ERROR)
        A_INHERIT("Exception")
    A_END_CLASS()
    A_CLASS_P("TypeError", AErrorClassNum + EX_TYPE_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("MemberError", AErrorClassNum + EX_MEMBER_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("ArithmeticError", AErrorClassNum + EX_ARITHMETIC_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("IndexError", AErrorClassNum + EX_INDEX_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("KeyError", AErrorClassNum + EX_KEY_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("CastError", AErrorClassNum + EX_CAST_ERROR)
        A_INHERIT("ValueError")
    A_END_CLASS()
    A_CLASS_P("MemoryError", AErrorClassNum + EX_MEMORY_ERROR)
        A_INHERIT("ResourceError")
    A_END_CLASS()
    A_CLASS_P("InterruptException", AErrorClassNum + EX_INTERRUPT_EXCEPTION)
        A_INHERIT("Exception")
    A_END_CLASS()
    A_CLASS_PRIV_P("ExitException", 1, AErrorClassNum + EX_EXIT_EXCEPTION)
        A_INHERIT("Exception")
        A_METHOD("create", 1, 0, ExitExceptionCreate)
    A_END_CLASS()
    A_CLASS_P("RuntimeError", AErrorClassNum + EX_RUNTIME_ERROR)
        A_INHERIT("Exception")
    A_END_CLASS()
    A_CLASS_P("IoError", AErrorClassNum + EX_IO_ERROR)
        A_INHERIT("Exception")
        A_METHOD_OPT("create", 0, 2, 1, IoErrorCreate)
        A_EMPTY_CONST("errno")
        A_EMPTY_CONST("code")
    A_END_CLASS()

    /* Type for representing buffers returned by C API function AAllocBuf(). */
    A_CLASS_PRIV_P(A_PRIVATE("__Buf"), 2, &AStdBufNum)
    A_END_CLASS()

#ifdef HAVE_JIT_COMPILER
    A_VAR_P(A_PRIVATE("__JitInfoMap"), &AJitInfoMapNum)
    A_CLASS_P(A_PRIVATE("__JitInfo"), &AJitInfoNum)
        A_VAR("originalFunction")
    A_END_CLASS()
#endif
A_END_MODULE()
