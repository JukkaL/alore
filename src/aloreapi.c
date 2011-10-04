/* aloreapi.c - Alore C API

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Alore C API functions: These functions along with other functions and macros
   defined in alore.h form the public Alore extension API. Functions, macros,
   types and constants not defined in alore.h are internal and should not be
   used by extensions if compatibility with future Alore versions is
   desired.

   Consult the Alore C API Reference for the details of available operations
   and their behaviour. Some of the operations defined here are not part of the
   public API. */

#include "alore.h"
#include "runtime.h"
#include "class.h"
#include "memberid.h"
#include "int.h"
#include "float.h"
#include "str.h"
#include "array.h"
#include "symtable.h"
#include "parse.h"
#include "std_module.h"
#include "gc.h"
#include "mem.h"
#include "internal.h"
#include "thread_athread.h"

#include <math.h>
#include <stdlib.h>


static AValue InstanceOp(AThread *t, AValue a, AValue b, int member);


/* Set a "const" global value identifier by the global number. Typically this
   is used for initializing global non-symbolic constants. Use ASetGlobalByNum
   for assigning to global variables. */
void ASetConstGlobalByNum(AThread *t, int num, AValue v)
{
    if (!ASetConstGlobalValue(t, num, v))
        ARaiseMemoryError(t);
}


/* Convert a value to a string object, equivalent to calling std::Str. */
AValue AToStr(AThread *t, AValue o)
{
    AValue *temp = AAllocTemp(t, o);
    AValue str = AStdStr(t, temp);
    AFreeTemp(t);
    return str;
}


/* Helper that is equivalent to AAdd, but has been optimized for (string)
   concatenation instead of Int/Float addition. */
AValue AConcat(AThread *t, AValue a, AValue b)
{
    AValue v;
    
    if (AIsStr(a)) {
        if (AIsStr(b))
            v = AConcatStrings(t, a, b);
        else
            return ARaiseTypeError(t, NULL);
    } else
        v = InstanceOp(t, a, b, AM_ADD);

    if (AIsError(v))
        ADispatchException(t);

    return v;
}


/* Allocate n thread-local temp values from the temp stack. Return a pointer to
   the first value. Raise a direct exception if the (fixed-size) stack
   overflows. */
AValue *AAllocTemps(AThread *t, int n)
{
    int i;
    if (!AAllocTempStack_M(t, n))
        ADispatchException(t);
    for (i = 1; i <= n; i++)
        *(t->tempStackPtr - i) = AZero;
    return t->tempStackPtr - n;
}


/* Return a boolean indicating whether value v is of the specified type. */
ABool AIsValue(AThread *t, AValue v, AValue type)
{
    AIsResult res = AIsOfType(v, type);
    if (res == A_IS_TRUE)
        return 1;
    else if (res == A_IS_FALSE)
        return 0;
    else {
        ARaiseTypeError(t, AMsgTypeExpected);
        return 0;
    }
}


/* Is the value a reference to a type object? */
ABool AIsType(AValue v)
{
    /* The thread parameter may by NULL since we know that the last argument
       is a type. */
    return AIsValue(NULL, v, AGlobalByNum(AStdTypeNum));
}


/* All raise operations below raise the exception as a direct exception, if
   possible. */


/* Raise an exception using the name of the exception. If msg != NULL, pass it
   as the message to the constructor. */
AValue ARaise(AThread *t, const char *type, const char *msg)
{
    int num = AGetGlobalNum(t, type);
    ARaiseByNum(t, num, msg);
    ADispatchException(t);
    return AError;
}


/* Raise an exception using the type object of the exception. If msg != NULL,
   pass it as the message to the constructor. */
AValue ARaiseByType(AThread *t, AValue type, const char *msg)
{
    if (!AIsNonSpecialType(type))
        ARaiseTypeError(t, AMsgTypeExpected);
    ARaiseByNum(t, AValueToType(type)->sym->num, msg);
    ADispatchException(t);
    return AError;
}


/* Raise an exception instance. */
AValue ARaiseValue(AThread *t, AValue v)
{
    ARaiseExceptionValue(t, v);
    ADispatchException(t);
    return AError;
}


/* Raise a pre-allocated MemoryError instance. This succeeds even if memory is
   full. */
AValue ARaiseMemoryError(AThread *t)
{
    ARaiseMemoryErrorND(t);
    ADispatchException(t);
    return AError;
}


AValue ARaiseTypeError(AThread *t, const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    ARaiseByNumFmtVa(t, AErrorClassNum[EX_TYPE_ERROR], fmt, args);
    va_end(args);
    ADispatchException(t);
    
    return AError;
}


AValue ARaiseValueError(AThread *t, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ARaiseByNumFmtVa(t, AErrorClassNum[EX_VALUE_ERROR], fmt, args);
    va_end(args);
    ADispatchException(t);
    return AError;
}


AValue ARaiseKeyError(AThread *t, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ARaiseByNumFmtVa(t, AErrorClassNum[EX_KEY_ERROR], fmt, args);
    va_end(args);
    ADispatchException(t);
    return AError;
}


AValue ARaiseIoError(AThread *t, const char *msg)
{
    return ARaiseByNum(t, AErrorClassNum[EX_IO_ERROR], msg);
}


AValue ARaiseRuntimeError(AThread *t, const char *msg)
{
    return ARaiseByNum(t, AErrorClassNum[EX_RUNTIME_ERROR], msg);
}


AValue ARaiseIndexError(AThread *t, const char *msg)
{
    return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR], msg);
}


AValue ARaiseErrnoException(AThread *t, const char *type)
{
    int num = AGetGlobalNum(t, type);
    return ARaiseErrnoExceptionByNum(t, num, NULL);
}


/* Is the current exception for the thread an instance of the named type? */
ABool AIsExceptionType(AThread *t, const char *type)
{
    AValue typeVal = AGlobal(t, type);
    return AIsValue(t, AGetException(t), typeVal);
}


/* Return the internal numeric id of a member. This id can be used for fast
   member-related operations. Note that the id can vary across different runs
   of a program, but is always constant within a single execution. */
int AMemberNum(AThread *t, const char *member)
{
    int id;
    ASymbol *sym;
    ASymbolInfo *symInfo;
    
    ALockInterpreter();
    if (!AGetSymbol(member, strlen(member), &sym))
        goto Fail;
    symInfo = AGetMemberSymbol(sym);
    if (symInfo == NULL)
        goto Fail;
    id = symInfo->num;
    AUnlockInterpreter();
    
    return id;

  Fail:
    
    AUnlockInterpreter();
    return ARaiseMemoryError(t);
}


/* Read a member of an object. */
AValue AMember(AThread *t, AValue o, const char *member)
{
    int id;
    
    *t->tempStack = o;
    id = AMemberNum(t, member);
    o = *t->tempStack;
    *t->tempStack = AZero;
    
    return AMemberByNum(t, o, id);
}


/* Assign the value v to a member of an object. */
AValue ASetMember(AThread *t, AValue o, const char *member, AValue v)
{
    int id;
    AValue ret;
    
    t->tempStack[0] = o;
    t->tempStack[1] = v;
    id = AMemberNum(t, member);
    
    ret = ASetMemberHelper(t, t->tempStack, id);
    t->tempStack[0] = t->tempStack[1] = AZero;
    return ret;
}


AValue ASuperMember(AThread *t, AValue *frame, const char *member)
{
    int id = AMemberNum(t, member);
    ATypeInfo *type = AGetInstanceType(AValueToInstance(frame[0]));

    while (type->super != NULL) {
        int method, read;
        
        type = type->super;

        method = ALookupMemberTable(type, MT_METHOD_PUBLIC, id);
        if (method != -1)
            return ACreateBoundMethod(t, frame[0], method);
        else {
            read = ALookupMemberTable(type, MT_VAR_GET_PUBLIC, id);
            if (read != -1) {
                /* Direct access or call getter? */
                if (read < A_VAR_METHOD)
                    return AMemberDirect(frame[0], read);
                else
                    return ACallValue(t, AGlobalByNum(read & ~A_VAR_METHOD), 1,
                                      frame);
            }
        }
    }
    
    return ARaiseMemberErrorND(t, frame[0], id);
}


AValue ASetSuperMember(AThread *t, AValue *frame, const char *member, AValue v)
{
    int id = AMemberNum(t, member);
    ATypeInfo *type = AGetInstanceType(AValueToInstance(frame[0]));

    while (type->super != NULL) {
        int write;
        
        type = type->super;

        write = ALookupMemberTable(type, MT_VAR_SET_PUBLIC, id);
        if (write != -1) {
            /* Direct access or call setter? */
            if (write < A_VAR_METHOD) {
                ASetMemberDirect(t, frame[0], write, v);
                return ANil;
            } else {
                AValue ret;
                t->tempStack[0] = frame[0];
                t->tempStack[1] = v;
                ret = ACallValue(t, AGlobalByNum(write & ~A_VAR_METHOD), 2,
                            t->tempStack);
                t->tempStack[0] = AZero;
                t->tempStack[1] = AZero;
                if (!AIsError(ret))
                    ret = ANil;
                return ret;
            }
        }
        
    }

    return ARaiseMemberErrorND(t, frame[0], id);
}


AValue ACall(AThread *t, const char *callable, int numArgs, AValue *args)
{
    AValue callValue = AGlobal(t, callable);
    return ACallValue(t, callValue, numArgs, args);
}


AValue ACallMethod(AThread *t, const char *member, int nargs, AValue *args)
{
    int id = AMemberNum(t, member);
    return ACallMethodByNum(t, id, nargs, args);
}


/* Set the slot num of object o. The caller is responsible for passing a
   valid slot id; no error checking is performed. */
void ASetMemberDirect(AThread *t, AValue o, int num, AValue newVal)
{
    *t->tempStack = newVal;
    if (!ASetInstanceMember(t, AValueToInstance(o), num, t->tempStack))
        ARaiseMemoryError(t);
}


/* Assign the value v to a member specified by a numeric member id. */
AValue ASetMemberByNum(AThread *t, AValue o, int member, AValue v)
{
    AValue ret;
    
    t->tempStack[0] = o;
    t->tempStack[1] = v;
    
    ret = ASetMemberHelper(t, t->tempStack, member);
    t->tempStack[0] = t->tempStack[1] = AZero;
    return ret;
}


/* Does the object have a specific member identified by a member id? */
ABool AHasMemberByNum(AValue o, int num)
{
    ATypeInfo *type = AGetInstanceType(AValueToInstance(o));
    do {
        if (ALookupMemberTable(type, MT_METHOD_PUBLIC, num) != -1 ||
            ALookupMemberTable(type, MT_VAR_GET_PUBLIC, num) != -1)
            return TRUE;
        type = type->super;
    } while (type != NULL);

    return FALSE;
}


/* Get the number (id) of a global definition based on the fully-qualified name
   of the definition. */
int AGetGlobalNum(AThread *t, const char *name)
{
    AToken *tok;
    ASymbolInfo *sym;
    int num;
    
    ALockInterpreter();

    if (!ATokenizeStr(name, &tok)) {
        AUnlockInterpreter();
        ARaiseMemoryError(t);
        return 0; /* Unreached */
    }
    
    AParseGlobalVariable(tok, &sym, TRUE, TRUE);
    AFreeTokens(tok);

    if (sym->type == ID_ERR_PARSE || sym->num == 0) {
        AUnlockInterpreter();
        ARaiseValueError(t, "Invalid global variable \"%s\"", name);
        return -1;
    } else
        num = sym->num;
    
    AUnlockInterpreter();

    return num;
}


/* Look up the value of a global definition/variable using the fully-qualified
   name. */
AValue AGlobal(AThread *t, const char *name)
{
    return AGlobalByNum(AGetGlobalNum(t, name));
}


void AGetPair(AThread *t, AValue pair, AValue *v1, AValue *v2)
{
    if (!AIsPair(pair))
        ARaiseTypeError(t, AMsgPairExpected);
    *v1 = AValueToMixedObject(pair)->data.pair.head;
    *v2 = AValueToMixedObject(pair)->data.pair.tail;
}


/* Helper function for creating a Pair or Range object. These share the same
   runtime representation (the precise type is specified using a type tag
   attribute). */
static AValue AMakeMixed(AThread *t, int type, AValue v1, AValue v2)
{
    AMixedObject *obj;
    
    t->tempStack[0] = v1;
    t->tempStack[1] = v2;

    obj = AAlloc(t, AGetBlockSize(sizeof(AMixedObject)));
    if (obj == NULL)
        return ARaiseMemoryError(t);

    AInitValueBlock(&obj->header, 3 * sizeof(AValue));
    obj->type = type;
    obj->data.pair.head = t->tempStack[0];
    obj->data.pair.tail = t->tempStack[1];

    return AMixedObjectToValue(obj);
}


/* Make a Pair object. */
AValue AMakePair(AThread *t, AValue v1, AValue v2)
{
    return AMakeMixed(t, A_PAIR_ID, v1, v2);
}


void AGetRange(AThread *t, AValue pair, AValue *lo, AValue *hi)
{
    if (!AIsRange(pair))
        ARaiseTypeError(t, AMsgRangeExpected);
    *lo = AValueToMixedObject(pair)->data.range.start;
    *hi = AValueToMixedObject(pair)->data.range.stop;
}


/* Make a range object. */
AValue AMakeRange(AThread *t, AValue lo, AValue hi)
{
    AExpectInt(t, lo);
    AExpectInt(t, hi);
    return AMakeMixed(t, A_RANGE_ID, lo, hi);
}


/* Evaluate a == b. If the result is True, return 1. If the result is False,
   return 0. If a normal exception was raised, return -1. */
int AIsEq(AThread *t, AValue a, AValue b)
{
    AValue res;
    
    if (AIsShortInt(a) && AIsShortInt(b))
        return a == b;

    if (AIsStr(a) && AIsStr(b))
        return ACompareStrings(a, b) == 0;

    if (AIsConstant(a))
        return a == b;

    res = AIsEqual(t, a, b);
    if (AIsError(res)) {
        ADispatchException(t);
        return -1;
    } else
        return res != AZero;
}


/* Evaluate a < b. If the result is True, return 1. If the result is False,
   return 0. If a normal exception was raised, return -1. */
int AIsLt(AThread *t, AValue a, AValue b)
{
    AValue res;
    
    if (AIsShortInt(a) && AIsShortInt(b))
        return (ASignedValue)a < (ASignedValue)b;
    
    res = ACompareOrder(t, a, b, OPER_LT);
    if (AIsError(res)) {
        ADispatchException(t);
        return -1;
    } else
        return res != AZero;
}


/* Evaluate a > b. If the result is True, return 1. If the result is False,
   return 0. If a normal exception was raised, return -1. */
int AIsGt(AThread *t, AValue a, AValue b)
{
    AValue res;
    
    if (AIsShortInt(a) && AIsShortInt(b))
        return (ASignedValue)a > (ASignedValue)b;
    
    res = ACompareOrder(t, a, b, OPER_GT);
    if (AIsError(res)) {
        ADispatchException(t);
        return -1;
    } else
        return res != AZero;
}


/* Return the return value of s.length() as an integer. */
Assize_t ALen(AThread *t, AValue s)
{
    if (AIsArray(s))
        return AArrayLen(s);
    else {
        AValue *arg = AAllocTemp(t, s);
        AValue ret = ACallMethodByNum(t, AM_LENGTH, 0, arg);
        AFreeTemp(t);
        if (AIsError(ret))
            return -1;
        else
            return AGetInt(t, ret);
    }
}


AValue AGetItemAt(AThread *t, AValue s, Assize_t i)
{
    if (AIsArray(s) && i >= 0)
        return AArrayItem(s, i); /* IDEA: Check range error? */
    else {
        AValue *args = AAllocTemps(t, 2);
        AValue ret;
        args[0] = s;
        args[1] = AIntToValue(i);
        ret = ACallMethodByNum(t, AM_GET_ITEM, 1, args);
        AFreeTemps(t, 2);
        return ret;
    }
}


AValue AGetItem(AThread *t, AValue s, AValue index)
{
    if (AIsInstance(s)) {
        if (AIsArray(s) && AIsShortInt(index)) {
            ASignedValue i = AValueToInt(index);
            if (i < 0 || i >= AArrayLen(s)) {
                i = ANormIndex(i, AArrayLen(s));
                if (i < 0 || i >= AArrayLen(s))
                    return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR],
                                       NULL);
            }
            return AArrayItem(s, i);
        } else {
            AValue *args = AAllocTemps(t, 2);
            AValue ret;
            args[0] = s;
            args[1] = index;
            ret = ACallMethodByNum(t, AM_GET_ITEM, 1, args);
            AFreeTemps(t, 2);
            return ret;
        }
    } else if (AIsShortInt(index)) {
        ABool result;

        /* Different cases (narrow/wide) of getting a single character are
           implemented separately for better performance. */
        
        if (AIsNarrowStr(s) || AIsNarrowSubStr(s)) {
            ASignedValue i = AValueToInt(index);
            AString *str;
        
            if (i < 0 || i >= AStrLen(s)) {
                i = ANormIndex(i, AStrLen(s));
                if (i < 0 || i >= AStrLen(s))
                    return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR],
                                       NULL);
            }
            
            /* Create a short string object of length 1. */
            AAlloc_M(t, AGetBlockSize(sizeof(AValue) + 1), str, result);
            if (!result)
                return ARaiseMemoryError(t);
            AInitNonPointerBlock(&str->header,  1);
            str->elem[0] = AStrItem(s, i);
            
            return AStrToValue(str);
        } else if (AIsWideStr(s) || AIsSubStr(s)) {
            ASignedValue i = AValueToInt(index);
            AWideString *str;
            
            if (i < 0 || i >= AStrLen(s)) {
                i = ANormIndex(i, AStrLen(s));
                if (i < 0 || i >= AStrLen(s))
                    return ARaiseByNum(t, AErrorClassNum[EX_INDEX_ERROR],
                                       NULL);
            }

            /* Create a wide string object of length 1. */
            AAlloc_M(t, AGetBlockSize(sizeof(AValue) + sizeof(AWideChar)), str,
                    result);
            if (!result)
                return ARaiseMemoryError(t);
            AInitNonPointerBlock(&str->header, sizeof(AWideChar));
            str->elem[0] = AStrItem(s, i);
            
            return AWideStrToValue(str);
        } else
            return ARaiseTypeError(t, NULL); /* IDEA: Provide error message. */
    } else if (AIsPair(index)) {
        AValue loVal, hiVal;
        Assize_t lo, hi;
        
        AGetPair(t, index, &loVal, &hiVal);

        if (AIsNil(loVal))
            loVal = AZero;
        if (AIsNil(hiVal))
            hiVal = AIntToValue(A_SLICE_END);
        
        lo = AGetInt_ssize_t(t, loVal);
        hi = AGetInt_ssize_t(t, hiVal);

        if (AIsStr(s))
            return ASubStr(t, s, lo, hi);
        else
            return ARaiseTypeError(t, NULL); /* IDEA: Provide error message. */
    } else
        return ARaiseTypeError(t, NULL); /* IDEA: Provide error message. If
                                            the index is long int, change
                                            exception! */
}


AValue ASetItemAt(AThread *t, AValue s, Assize_t i, AValue o)
{
    if (AIsArray(s) && i >= 0)
        ASetArrayItem(t, s, i, o); /* IDEA: Check range error? */
    else {
        AValue *args = AAllocTemps(t, 3);
        AValue ret;
        args[0] = s;
        args[1] = AIntToValue(i);
        args[2] = o;
        ret = ACallMethodByNum(t, AM_SET_ITEM, 2, args);
        AFreeTemps(t, 3);
        if (AIsError(ret))
            return ret;
    }
    return ANil;
}


AValue ASetItem(AThread *t, AValue s, AValue index, AValue o)
{
    if (AIsInstance(s)) {
        AValue *args;
        AValue ret;

        if (AIsArray(s) && AIsShortInt(index)) {
            Assize_t len = AArrayLen(s);
            ASignedValue i = AValueToInt(index);
            i = ANormIndex(i, len);
            if (i >= 0 && i < len) {
                ASetArrayItem(t, s, i, o);
                return ANil;
            }
        }
        
        args = AAllocTemps(t, 3);
        args[0] = s;
        args[1] = index;
        args[2] = o;
        ret = ACallMethodByNum(t, AM_SET_ITEM, 2, args);
        AFreeTemps(t, 3);
        if (AIsError(ret))
            return ret;
    } else
        return ARaiseTypeError(t, NULL); /* IDEA: Provide an error message. */
    
    return ANil;
}


/* Perform call a.m(b) with member m specified by a numeric id. Return the
   result. */
static AValue InstanceOp(AThread *t, AValue a, AValue b, int member)
{
    AValue *args = AAllocTemps(t, 2);
    AValue ret;
    
    args[0] = a;
    args[1] = b;
    ret = ACallMethodByNum(t, member, 1, args);

    AFreeTemps(t, 2);

    return ret;
}


/* Evaluate a + b. */
AValue AAdd(AThread *t, AValue a, AValue b)
{
    AValue sum;

    if (AIsShortInt(a) && AIsShortInt(b)) {
        sum = a + b;
        if (!AIsAddOverflow(sum, a, b))
            return sum;
        else
            sum = ACreateLongIntFromIntND(t, AValueToInt(a) + AValueToInt(b));
    } else if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_ADD);
    else if (AIsInstance(b) && (AIsInt(a) || AIsFloat(a)))
        return InstanceOp(t, b, a, AM_ADD);
    else if (AIsStr(a))
        return AConcat(t, a, b);
    else {
        a = ACoerce(t, OPER_PLUS, a, b, (t->tempStack + 1));
        if (AIsError(a))
            sum = AError;
        else {
            b = t->tempStack[1];
            
            if (AIsLongInt(a) && AIsLongInt(b))
                sum = AAddLongInt(t, a, b);
            else if (AIsFloat(a))
                return AMakeFloat(t, AValueToFloat(a) + AValueToFloat(b));
            else
                return ARaiseTypeError(t, NULL);
        }
    }

    if (AIsError(sum))
        ADispatchException(t);

    return sum;
}


/* Evaluate a - b. */
AValue ASub(AThread *t, AValue a, AValue b)
{
    AValue diff;
    
    if (AIsShortInt(a) && AIsShortInt(b)) {
        diff = a - b;
        if (!AIsSubOverflow(diff, a, b))
            return diff;
        else
            diff = ACreateLongIntFromIntND(t, AValueToInt(a) - AValueToInt(b));
    } else if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_SUB);
    else {
        a = ACoerce(t, OPER_MINUS, a, b, (t->tempStack + 1));
        if (AIsError(a))
            diff = AError;
        else {
            b = t->tempStack[1];
            
            if (AIsLongInt(a) && AIsLongInt(b))
                diff = ASubLongInt(t, a, b);
            else if (AIsFloat(a))
                return AMakeFloat(t, AValueToFloat(a) - AValueToFloat(b));
            else
                return ARaiseTypeError(t, NULL);
        }
    }

    if (AIsError(diff))
        ADispatchException(t);

    return diff;
}

    
/* Evaluate a * b. */
AValue AMul(AThread *t, AValue a, AValue b)
{
    AValue prod;
    
    if (AIsShortInt(a) && AIsShortInt(b)) {
        if (AHi(a) == 0 && AHi(b) == 0) {
            prod = AValueToInt(a) * (ASignedValue)b;
            if ((ASignedValue)prod >= 0)
                return prod;
        }

        prod = AMultiplyShortInt(t, a, b);
    } else if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_MUL);
    else if (AIsInstance(b) && (AIsInt(a) || AIsFloat(a)))
        return InstanceOp(t, b, a, AM_MUL);
    else if (AIsShortInt(a) && AIsStr(b))
        return ARepeatString(t, b, AValueToInt(a));
    else if (AIsShortInt(b) && AIsStr(a))
        return ARepeatString(t, a, AValueToInt(b));
    else {
        a = ACoerce(t, OPER_MUL, a, b, (t->tempStack + 1));
        if (AIsError(a))
            prod = AError;
        else {
            b = t->tempStack[1];
            
            if (AIsLongInt(a) && AIsLongInt(b))
                prod = AMultiplyLongInt(t, a, b);
            else if (AIsFloat(a))
                return AMakeFloat(t, AValueToFloat(a) * AValueToFloat(b));
            else
                return ARaiseTypeError(t, NULL);
        }
    }

    if (AIsError(prod))
        ADispatchException(t);

    return prod;
}


/* Raise an ArithmeticError instance as a reponse to a division or modulus by
   zero. */
static AValue DivModByZero(AThread *t)
{
    ARaiseByNum(t, AErrorClassNum[EX_ARITHMETIC_ERROR], AMsgDivisionByZero);
    ADispatchException(t);
    return AError;
}


/* Evaluate a / b. */
AValue ADiv(AThread *t, AValue a, AValue b)
{
    if (b == AZero)
        return DivModByZero(t);
    
    if (AIsShortInt(a) && AIsShortInt(b))
        return AMakeFloat(t, (double)AValueToInt(a) / AValueToInt(b));
    
    if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_DIV);

    a = ACoerce(t, OPER_DIV, a, b, t->tempStack + 1);
    if (AIsError(a)) {
        ADispatchException(t);
        return AError;
    }
    
    b = t->tempStack[1];
    
    if (AIsLongInt(a) && AIsLongInt(b))
        return AMakeFloat(t, ALongIntToFloat(a) / ALongIntToFloat(b));
    else if (AIsFloat(a)) {
        if (AValueToFloat(b) == 0.0)
            return DivModByZero(t);
        return AMakeFloat(t, AValueToFloat(a) / AValueToFloat(b));
    } else
        return ARaiseTypeError(t, NULL);
}


/* Evaluate a div b. */
AValue AIntDiv(AThread *t, AValue a, AValue b)
{
    AValue mod;
    AValue div;
    
    if (AIsShortInt(a) && AIsShortInt(b))
        div = AIntDivMod(t, a, b, &mod);
    else if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_IDIV);
    else if (b == AZero)
        return DivModByZero(t);
    else {
        a = ACoerce(t, OPER_IDIV, a, b, t->tempStack + 1);
        if (AIsError(a))
            div = AError;
        else {
            b = t->tempStack[1];
            
            if (AIsLongInt(a) && AIsLongInt(b))
                div = ADivModLongInt(t, a, b, t->tempStack + 1);
            else if (AIsFloat(a)) {
                if (AValueToFloat(b) == 0.0)
                    return DivModByZero(t);
                return AMakeFloat(t, floor(AValueToFloat(a) /
                                           AValueToFloat(b)));
            } else
                return ARaiseTypeError(t, NULL);
        }
    }

    if (AIsError(div))
        ADispatchException(t);

    return div;
}


/* Evaluate a mod b. */
AValue AMod(AThread *t, AValue a, AValue b)
{
    AValue mod;
    
    if (AIsShortInt(a) && AIsShortInt(b)) {
        if (AIntDivMod(t, a, b, &mod) == AError)
            ADispatchException(t);
        return mod;
    }
    
    if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_MOD);

    if (b == AZero)
        return DivModByZero(t);

    a = ACoerce(t, OPER_MOD, a, b, t->tempStack + 1);
    if (AIsError(a))
        ADispatchException(t);
    
    b = t->tempStack[1];
    
    if (AIsLongInt(a) && AIsLongInt(b)) {
        if (ADivModLongInt(t, a, b, &mod) == AError)
            ADispatchException(t);
        return mod;
    } else if (AIsFloat(a)) {
        double result;
        double bFloat = AValueToFloat(b);
         
        if (bFloat == 0.0)
            return DivModByZero(t);
        result = fmod(AValueToFloat(a), bFloat);
        if (result * bFloat < 0)
            result += bFloat;
        return AMakeFloat(t, result);
    } else
        return ARaiseTypeError(t, NULL);
}


/* Evaluate a ** b. */
AValue APow(AThread *t, AValue a, AValue b)
{
    if (AIsInt(a) && AIsInt(b)) {
        AValue p = APowInt(t, a, b);
        if (!AIsError(p))
            return p;
        else
            ADispatchException(t);
    }
    
    if (AIsFloat(a) && AIsInt(b)) {
        AValue p = APowFloatInt(t, a, b);
        if (!AIsError(p))
            return p;
        else
            ADispatchException(t);
    }
    
    if (AIsInstance(a))
        return InstanceOp(t, a, b, AM_POW);

    a = ACoerce(t, OPER_POW, a, b, t->tempStack + 1);
    if (AIsError(a))
        ADispatchException(t);
    
    b = t->tempStack[1];
    
    if (AIsFloat(a)) {
        if (AValueToFloat(a) < 0.0) {
            ARaiseByNum(t, AErrorClassNum[EX_ARITHMETIC_ERROR], NULL);
            ADispatchException(t);
            return AError;
        }
        return AMakeFloat(t, pow(AValueToFloat(a), AValueToFloat(b)));
    } else
        return ARaiseTypeError(t, NULL);
}


/* Evaluate -a. */
AValue ANeg(AThread *t, AValue a)
{
    AValue neg;
    
    if (AIsShortInt(a)) {
        if (a == AIntToValue(A_SHORT_INT_MIN))
            neg = ACreateLongIntFromIntND(t, -A_SHORT_INT_MIN);
        else
            return -(ASignedValue)a;
    } else if (AIsFloat(a))
        return AMakeFloat(t, -AValueToFloat(a));
    else if (AIsLongInt(a))
        neg = ANegateLongInt(t, a);
    else if (AIsInstance(a)) {
        AValue *arg = AAllocTemp(t, a);
        neg = ACallMethodByNum(t, AM_NEGATE, 0, arg);
        AFreeTemp(t);
        return neg; /* If a is an instance, do not dispatch exceptions. */
    } else
        return ARaiseTypeError(t, NULL);

    if (AIsError(neg))
        ADispatchException(t);

    return neg;
}


/* Evaluate v.iterator(). */
AValue AIterator(AThread *t, AValue v)
{
    AValue *args;
    AValue ret;

    args = AAllocTemps(t, 2);
    args[0] = v;
    
    if (AIsInstance(v))
        ret = ACallMethodByNum(t, AM_ITERATOR, 0, args);
    else if (AIsRange(v))
        ret = ACallValue(t, AGlobalByNum(ARangeIterNum), 1, args);
    else if (AIsStr(v))
        ret = ACallValue(t, AGlobalByNum(AStrIterNum), 1, args);
    else
        ret = ARaiseTypeError(t, "Invalid iterable");
    
    AFreeTemps(t, 2);
    
    return ret;
}


/* If e is an iteration with additional items, return TRUE and store the next
   item at *next. If e is a finished iteration, return FALSE. If there was an
   error, return -1 (or raise a direct exception). */
int ANext(AThread *t, AValue e, AValue *next)
{
    AValue *args;
    AValue ret;
    int status;

    args = AAllocTemps(t, 2);
    args[0] = e;
    ret = ACallMethod(t, "hasNext", 0, args);
    if (AIsError(ret))
        status = -1;
    else if (AIsTrue(ret)) {
        ret = ACallMethod(t, "next", 0, args);
        if (AIsError(ret))
            status = -1;
        else {
            *next = ret;
            status = 1;
        }
    } else if (AIsFalse(ret))
        status = 0;
    else {
        ARaiseValueError(t, "Invalid boolean value");
        status = -1;
    }
    AFreeTemps(t, 2);

    return status;
}


/* Evaluate the operation "a in b". */
int AIn(AThread *t, AValue item, AValue collection)
{
    AValue res = AIsIn(t, item, collection);
    if (AIsError(res))
        return -1;
    else if (res == AZero)
        return 0;
    else
        return 1;
}


/* Construct an instance of a type without calling the normal constructor.
   Use this only for types whose instances you know how to initialize properly
   manually! */
AValue AMakeUninitializedObject(AThread *t, AValue type)
{
    AInstance *inst;
    
    inst = AAlloc(t, AValueToType(type)->instanceSize);
    if (inst == NULL)
        return ARaiseMemoryError(t);
    
    AInitInstanceBlock(&inst->type, AValueToType(type));
    AInitInstanceData(inst, AValueToType(type));
    
    return AInstanceToValue(inst);
}


/* Return TRUE if the current program is a compiled program. This means that
   the interpreter and full libraries might not be available. */
ABool AIsStandalone(void)
{
    return AIsStandaloneFlag;
}


/* Allocate a container object for a single value whose main feature is the
   ability to get a C pointer/handle to it. This is useful for callbacks in
   C libraries, for example. Normal values can be modified by the garbage
   collector, so they cannot generally passed to third-party C functions. */
AValue AAllocContainer(AThread *t, AValue initValue)
{
    AValue *tmp;
    AFixArray *container;
    AValue ret;

    tmp = AAllocTemps(t, 2);
    
    /* NOTE: The container is always allocated in the old generation. */
    
    tmp[0] = initValue;

    /* Allocate and initialize a container object, implemented as a FixArray
       object. */
    container = AAllocUnmovable(2 * sizeof(AValue));
    AInitValueBlockOld(&container->header, sizeof(AValue));
    container->elem[0] = AZero;

    /* Store the initial value. */
    tmp[1] = AFixArrayToValue(container);
    ASetFixArrayItem(t, tmp[1], 0, tmp[0]);
    
    ret = tmp[1];
    
    AFreeTemps(t, 2);
    
    return ret;
}


/* Return an opaque handle for accessing the container. */
void *AContainerPtr(AValue value)
{
    return AValueToFixArray(value);
}


/* Return the value held by a container. */
AValue AContainerValue(void *ptr)
{
    AFixArray *container = ptr;
    return container->elem[0];
}


/* Modify the value held by a container. */
void ASetContainerValue(AThread *t, void *ptr, AValue item)
{
    AValue val = AFixArrayToValue((AFixArray *)ptr);
    ASetFixArrayItem(t, val, 0, item);
}


/* Write Repr(val) to stdout followed by a line break. */
AValue AWriteRepr(AThread *t, AValue val)
{
    AValue *temp = AAllocTemps(t, 2);

    temp[0] = val;
    temp[0] = ACall(t, "std::Repr", 1, temp);
    if (AIsError(temp[0]))
        return AError;
    return ACall(t, "std::WriteLn", 1, temp);
}


/* Display an error message and exit the program directly with status 99
   without performing ordinary cleanup. Use this only if something totally
   unexpected happens, and if recovery is difficult.

   NOTE: This function is not part of the public API. */
AValue AEpicInternalFailure(const char *message)
{
    fprintf(stderr, "\n*** UNRECOVERABLE INTERNAL ALORE ERROR ***\n"
            "Reason: %s\nPlease file a bug report!\n", message);
    exit(99);
}


/* Return the hash value of an object as an Int value. */
AValue AHash(AThread *t, AValue object)
{
    AValue hash;
    AValue *frame = AAllocTemps(t, 3);

    frame[0] = object;
    hash = AStdHash(t, frame);
    
    AFreeTemps(t, 3);

    return hash;
}


/* Return the representation of an object by calling std::Repr. If there was an
   exception, always produce a non-direct exception (AError return). The return
   value is a Str object (if successful). */
AValue ARepr(AThread *t, AValue object)
{
    /* Catch direct exceptions and propagate them as normal exceptions. */
    if (ATry(t))
        return AError;
    AValue *args = AAllocTemp(t, object);
    /* Call std::Repr(object). */
    AValue repr = ACallValue(t, AGlobalByNum(AStdReprNum), 1, args);
    AEndTry(t);
    AFreeTemp(t);
    return repr;
}


/* Store the representation of the object to buf (the size of which is bufLen
   characters) as a zero-terminated string. Produce the representation by
   calling std::Repr. If Repr fails, store empty string at buf and return
   FALSE. Otherwise, return TRUE.

   Truncate the result to fit within the buffer, potentially adding '...' at
   the end of the buffer. Return TRUE even if truncation is necessary.

   Replace character codes larger than 127 with question marks in the
   result. This does not affect the returned status. */
ABool AGetRepr(AThread *t, char *buf, Assize_t bufLen, AValue object)
{
    Assize_t len;
    Assize_t i;
    AValue repr = ARepr(t, object);
    
    /* If producing the representation fails, return an empty string and
       return error status. */
    if (AIsError(repr)) {
        *buf= '\0';
        return FALSE;
    }

    len = AStrLen(repr);

    /* Copy characters from the representation string, but avoid overflowing
       the buffer. Replace non-7-bit characters with ? characers. */
    for (i = 0; i < len && i < bufLen - 1; i++) {
        int ch = AStrItem(repr, i);
        if (ch < 128)
            buf[i] = ch;
        else
            buf[i] = '?';
    }

    /* If we had to truncate the string and the target buffer is long enough,
       add "..." at the end of the string. */
    if (len >= bufLen && bufLen > 10) {
        int j;
        for (j = 0; j < 3; j++)
          buf[bufLen - 4 + j] = '.';
    }

    buf[i] = '\0';
    return TRUE;
}
