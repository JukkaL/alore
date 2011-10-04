/* runtime.c - Miscellaneous internal run-time support functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "class.h"
#include "memberid.h"
#include "gc.h"
#include "int.h"
#include "float.h"
#include "str.h"
#include "array.h"
#include "std_module.h"
#include "token.h"


static AValue IsRangeOrPairEqual(AThread *t, AValue v1, AValue v2);
static ABool AIsSpecialType(AValue val);


/* If val is std::True, return the value 1; if val is False, return the value
   0; otherwise raise a non-direct type error and return AError. */
#define CheckBoolean(t, val) \
    ((val) == AFalse ? AZero : (val) == ATrue ? AIntToValue(1) : \
     (val) == AError ? (val) : \
     ARaiseTypeErrorND(t, AMsgBooleanExpectedBut, (val)))
/* FIX: error message */

/* If val is std::True, return the value 0; if val is False, return the value
   1; otherwise raise a non-direct type error and return AError. */
#define CheckBooleanNot(t, val) \
    ((val) == ATrue ? AZero : (val) == AFalse ? AIntToValue(1) : \
     (val) == AError ? (val) : \
     ARaiseTypeErrorND(t, AMsgBooleanExpectedBut, (val)))

/* Convert a C boolean to Int value 1 (if true) or 0 (otherwise). */
#define BoolToIntValue(b) \
    ((b) ? AIntToValue(1) : AZero)


/* Return obj as an instance, wrapping it if necessary. Never raise a direct
   exception. */
AValue AWrapObject(AThread *t, AValue obj)
{
    AInstance *instance;
    ATypeInfo *type;

    /* Instances do not need to be wrapped. */
    if (AIsInstance(obj))
        return obj;

    *t->tempStack = obj;
    instance = AAlloc(t, 2 * sizeof(AValue));
    if (instance == NULL)
        return AError;
    obj = *t->tempStack;

    /* Determine the corresponding wrapper class. */
    /* IDEA: Use InternalType instead. */
    if (AIsStr(obj))
        type = AStrClass;
    else if (AIsShortInt(obj) || AIsLongInt(obj))
        type = AIntClass;
    else if (AIsFloat(obj))
        type = AFloatClass;
    else if (AIsRange(obj))
        type = ARangeClass;
    else if (AIsPair(obj))
        type = APairClass;
    else if (AIsConstant(obj))
        type = AConstantClass;
    else if (AIsGlobalFunction(obj)) {
        /* obj can be a function or a primitive type object. */
        ASymbolInfo *sym = AValueToFunction(obj)->sym;
        if (AInternalTypeSymbol(sym) != sym)
            type = ATypeClass;
        else
            type = AFunctionClass;
    } else if (AIsMethod(obj))
        type = AFunctionClass;
    else if (AIsNonSpecialType(obj))
        type = ATypeClass;
    else {
        *t->tempStack = AZero;
        return ARaiseMemberErrorND(t, obj, -1);
    }

    /* Initialize the wrapper object. */
    AInitInstanceBlock(&instance->type, type);
    instance->member[0] = *t->tempStack;

    *t->tempStack = AZero;
    return AInstanceToValue(instance);
}


/* Return the internal wrapper type corresponding to an object. Assume that the
   object is not an instance. */
static ATypeInfo *InternalType(AValue obj)
{
    if (AIsStr(obj))
        return AStrClass;
    else if (AIsInt(obj))
        return AIntClass;
    else if (AIsFloat(obj))
        return AFloatClass;
    else if (AIsRange(obj))
        return ARangeClass;
    else if (AIsPair(obj))
        return APairClass;
    else if (AIsConstant(obj))
        return AConstantClass;
    else if (AIsGlobalFunction(obj)) {
        /* obj can be a function or a primitive type object. */
        ASymbolInfo *sym = AValueToFunction(obj)->sym;
        if (AInternalTypeSymbol(sym) != sym)
            return ATypeClass;
        else
            return AFunctionClass;
    } else if (AIsMethod(obj))
        return AFunctionClass;
    else if (AIsNonSpecialType(obj))
        return ATypeClass;
    else {
        AEpicInternalFailure("InternalType called with invalid object");
        return NULL;
    }
}


/* Read the member of an object via a member id. */
AValue AMemberByNum(AThread *t, AValue val, int member)
{
    AInstance *inst;
    ATypeInfo *type;
    AMemberHashTable *table;
    AMemberNode *node;

    if (!AIsInstance(val)) {
        val = AWrapObject(t, val);
        if (AIsError(val))
            return AError;
    }

    inst = AValueToInstance(val);
    type = AGetInstanceType(inst);
    table = AGetMemberTable(type, MT_VAR_GET_PUBLIC);

    node = &table->item[member & table->size];

    /* Search the get hash table. */
    while (node->key != member) {
        if (node->next == NULL) {
            if (type->super == NULL)
                return AGetInstanceCodeMember(t, inst, member);
            else {
                type = type->super;
                table = AGetMemberTable(type, MT_VAR_GET_PUBLIC);
                node = &table->item[member & table->size];
            }
        } else
            node = node->next;
    }

    if (node->item < A_VAR_METHOD)
        return inst->member[node->item];
    else {
        AValue funcVal;
        AValue ret;

        /* Call getter method. */
        t->tempStack[1] = val;
        funcVal = AGlobalByNum(node->item & ~A_VAR_METHOD);
        ret = ACallValue(t, funcVal, 1, t->tempStack + 1);

        return ret;
    }
}


/* Set member of inst via slot id memberIndex. Assume that inst is an instance
   and has the specified slot. */
ABool ASetMemberDirectND(AThread *t, AValue inst, int memberIndex,
                     AValue newVal)
{
    *t->tempStack = newVal;
    return ASetInstanceMember(t, AValueToInstance(inst), memberIndex,
                             t->tempStack);
}


/* Test whether val is an instance of typeVal. NOTE: Never raise an
   exception. */
AIsResult AIsOfType(AValue val, AValue typeVal)
{
    /* Is the type object an ordinary type (a normal class or an interface? */
    if (AIsNonSpecialType(typeVal)) {
        ATypeInfo *type = AValueToType(typeVal);

        if (AIsTypeClass(typeVal)) {
            if (AIsInstance(val)) {
                AInstance *inst = AValueToInstance(val);
                ATypeInfo *instType = AGetInstanceType(inst);

                /* Class type inclusion checking. */
                do {
                    if (instType == type)
                        return A_IS_TRUE;
                    instType = instType->super;
                } while (instType != NULL);
                return A_IS_FALSE;
            } else if (val != ANil && typeVal == AGlobalByNum(AStdObjectNum))
                return A_IS_TRUE;
            else
                return A_IS_FALSE;
        } else {
            ATypeInfo *instType;

            if (AIsInstance(val)) {
                AInstance *inst = AValueToInstance(val);
                instType = AGetInstanceType(inst);
            } else if (val == ANil)
                return A_IS_FALSE;
            else
                instType = InternalType(val);

            /* Interface type inclusion checking. */
            do {
                int i;
                for (i = 0; i < instType->numInterfaces; i++) {
                    ATypeInfo *iface = ATypeInterface(instType, i);
                    do {
                        if (iface == type)
                            return A_IS_TRUE;
                        iface = iface->super;
                    } while (iface != NULL);
                }
                instType = instType->super;
            } while (instType != NULL);
            return A_IS_FALSE;
        }
    } else if (AIsInstance(val) && AIsGlobalFunction(typeVal)) {
        /* Handle wrapped instances (e.g. __Str) and anonymous function
           objects. An instance of __Str should be of type Str, etc. */
        ASymbolInfo *sym = AInternalTypeSymbol(AValueToFunction(typeVal)->sym);
        AValue newTypeVal = AGlobalByNum(sym->num);
        if (sym == AFunctionClass->sym && AIsAnonFunc(val))
            return A_IS_TRUE;
        if (AIsNonSpecialType(newTypeVal))
            return AIsOfType(val, newTypeVal);
    }

    /* The type is either a special type (represented by a function) or an
       invalid type. */

    if (typeVal == AGlobalByNum(AStdIntNum))
        return AIsShortInt(val) || AIsLongInt(val) ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdStrNum))
        return AIsNarrowStr(val) || AIsWideStr(val) || AIsSubStr(val)
            ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdFloatNum))
        return AIsFloat(val) ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdPairNum))
        return AIsPair(val) ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdRangeNum))
        return AIsRange(val) ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdConstantNum))
        return AIsConstant(val) && val != ANil ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdFunctionNum))
        return (AIsGlobalFunction(val) && !AIsSpecialType(val))
            || AIsMethod(val) ? A_IS_TRUE : A_IS_FALSE;
    else if (typeVal == AGlobalByNum(AStdTypeNum))
        return AIsNonSpecialType(val) || AIsSpecialType(val) ?
            A_IS_TRUE : A_IS_FALSE;

    return A_IS_ERROR;
}


/* Is val a special type object (e.g. std::Int, std::Str, etc.)? */
ABool AIsSpecialType(AValue val)
{
    /* IDEA: There could a special flag in Function objects specifying whether
             the function is a type constructor, making this function
             faster. */
    return val == AGlobalByNum(AStdIntNum)
        || val == AGlobalByNum(AStdStrNum)
        || val == AGlobalByNum(AStdTypeNum)
        || val == AGlobalByNum(AStdFloatNum)
        || val == AGlobalByNum(AStdRangeNum)
        || val == AGlobalByNum(AStdFunctionNum)
        || val == AGlobalByNum(AStdPairNum)
        || val == AGlobalByNum(AStdConstantNum);
}


/* Return the type object that corresponds to the class of the object
   argument. Raise ValueError if the argument is nil. */
AValue AGetTypeObject(AThread *t, AValue object)
{
    if (AIsInstance(object)) {
        /* Anonymous functions need special processing. */
        if (AIsAnonFunc(object))
            return AGlobalByNum(AStdFunctionNum);
        else {
            /* This is the generic case. It covers all user-defined types
               and most C and library types. */
            AInstance *inst = AValueToInstance(object);
            return AGlobalByNum(AGetInstanceType(inst)->sym->num);
        }
    } else if (AIsInt(object))
        return AGlobalByNum(AStdIntNum);
    else if (AIsStr(object))
        return AGlobalByNum(AStdStrNum);
    else if (AIsFloat(object))
        return AGlobalByNum(AStdFloatNum);
    else if (AIsNil(object))
        return ARaiseValueError(t, "Type of nil not defined");
    else if (AIsConstant(object))
        return AGlobalByNum(AStdConstantNum);
    else if (AIsPair(object))
        return AGlobalByNum(AStdPairNum);
    else if (AIsRange(object))
        return AGlobalByNum(AStdRangeNum);
    else if (AIsOfType(object, AGlobalByNum(AStdTypeNum)) == A_IS_TRUE)
        return AGlobalByNum(AStdTypeNum);
    else if (AIsGlobalFunction(object) || AIsMethod(object))
        return AGlobalByNum(AStdFunctionNum);
    else
        return ARaiseValueError(t, NULL);
}


/* Compare two values for equality. Return AZero if the result is false,
   AIntToValue(1) if true, and AError on error. */
AValue AIsEqual(AThread *t, AValue left, AValue right)
{
    if (AIsInstance(left)) {
        AValue res;

        if (!AGetInstanceType(AValueToInstance(left))->hasEqOverload)
            return BoolToIntValue(left == right);

        if (!AAllocTempStack_M(t, 2))
            return AError;

        t->tempStackPtr[-2] = left;
        t->tempStackPtr[-1] = right;
        res = ACallMethodByNum(t, AM_EQ, 1, t->tempStackPtr - 2);
        t->tempStackPtr -= 2;

        return CheckBoolean(t, res);
    }

    if (AIsInstance(right) && (AIsInt(left) || AIsFloat(left)))
        return AIsEqual(t, right, left);

    if (AIsNarrowStr(left))
        goto StringComparison;

    if (AIsFloat(left)) {
        if (AIsFloat(right)) {
            if (AValueToFloat(left) == AValueToFloat(right))
                return AIntToValue(1);
            else
                return AZero;
        }

        if (AIsShortInt(right)) {
            if (AValueToFloat(left) == AValueToInt(right))
                return AIntToValue(1);
            else
                return AZero;
        }

        if (AIsLongInt(right)) {
            if (AValueToFloat(left) == ALongIntToFloat(right))
                return AIntToValue(1);
            else
                return AZero;
        }

        return AZero;
    }

    if (AIsLongInt(left)) {
        if (AIsLongInt(right) && ACompareLongInt(left, right) == 0)
            return AIntToValue(1);
        else if (AIsFloat(right))
            return AIsEqual(t, right, left);
        else
            return AZero;
    }

    if (AIsShortInt(left) && !AIsShortInt(right))
        return AIsEqual(t, right, left);

    if (AIsSubStr(left) || AIsWideStr(left))
        goto StringComparison;

    if (AIsMixedValue(left)) {
        if ((AIsPair(left) && AIsPair(right)) ||
            (AIsRange(left) && AIsRange(right)))
            return IsRangeOrPairEqual(t, left, right);
    }

    return left == right ? AIntToValue(1) : AZero;

  StringComparison:

    {
        int result = ACompareStrings(left, right);
        if (result == 0)
            return AIntToValue(1);
        else
            return AZero;
    }
}


/* Return AZero / AIntToValue(1) depending on whether two Range or Pair
   values are equal, or AError if an exception was raised. Assumes
   v1 and v2 are both either Range or Pair objects. */
AValue IsRangeOrPairEqual(AThread *t, AValue v1, AValue v2)
{
    AValue res;

    if (!AAllocTempStack(t, 2))
        return AError;

    t->tempStackPtr[-2] = v1;
    t->tempStackPtr[-1] = v2;

    res = AIsEqual(t, AValueToMixedObject(v1)->data.range.start,
                  AValueToMixedObject(v2)->data.range.start);
    if (res == AIntToValue(1))
        res = AIsEqual(
            t,
            AValueToMixedObject(t->tempStackPtr[-2])->data.range.stop,
            AValueToMixedObject(t->tempStackPtr[-1])->data.range.stop);

    t->tempStackPtr -= 2;

    return res;
}


/* Compare two objects using one of the operators ==, !=, <, <=, >, >=. Return
   Int value 1 if the result is True, 0 if the result if False, or AError if
   a (non-direct) exception was raised. */
AValue ACompareOrder(AThread *t, AValue left, AValue right,
                     AOperator operator)
{
    if (AIsInstance(left)) {
        AValue res;

        if (!AAllocTempStack_M(t, 2))
            return AError;

        t->tempStackPtr[-2] = left;
        t->tempStackPtr[-1] = right;

        if (operator == OPER_LT || operator == OPER_GTE) {
            res = ACallMethodByNum(t, AM_LT, 1, t->tempStackPtr - 2);
            res = operator == OPER_LT ?
                CheckBoolean(t, res) : CheckBooleanNot(t, res);
        } else if (operator == OPER_GT || operator == OPER_LTE) {
            res = ACallMethodByNum(t, AM_GT, 1, t->tempStackPtr - 2);
            res = operator == OPER_GT ?
                CheckBoolean(t, res) : CheckBooleanNot(t, res);
        } else {
            /* Operator is OPER_EQ or OPER_NEQ. */
            if (!AGetInstanceType(AValueToInstance(left))->hasEqOverload)
                res = BoolToIntValue(left == right);
            else {
                res = ACallMethodByNum(t, AM_EQ, 1, t->tempStackPtr - 2);
                res = operator == OPER_EQ ?
                    CheckBoolean(t, res) : CheckBooleanNot(t, res);
            }
        }

        t->tempStackPtr -= 2;
        return res;
    }

    if (AIsInstance(right) && (AIsInt(left) || AIsFloat(left)))
        return ACompareOrder(t, right, left, ASwitchOperator(operator));

    for (;;) {
        AValue temp;

        if (AIsFloat(left) && AIsFloat(right)) {
            if (AValueToFloat(left) > AValueToFloat(right))
                return BoolToIntValue(AGtSatisfiesOp(operator));
            else if (AValueToFloat(left) == AValueToFloat(right))
                return BoolToIntValue(AEqSatisfiesOp(operator));
            else
                return BoolToIntValue(ALtSatisfiesOp(operator));
        }

        if ((AIsNarrowStr(left) || AIsSubStr(left) || AIsWideStr(left))
            && (AIsNarrowStr(right) || AIsSubStr(right) || AIsWideStr(right)))
            goto StringComparison;

        if (AIsLongInt(left) && AIsLongInt(right)) {
            /* IDEA: Perhaps optimize if Digit == ushort? */
            ALongIntSignedDoubleDigit res = ACompareLongInt(left, right);
            if (res > 0)
                return BoolToIntValue(AGtSatisfiesOp(operator));
            else if (res == 0)
                return BoolToIntValue(AEqSatisfiesOp(operator));
            else
                return BoolToIntValue(ALtSatisfiesOp(operator));
        }

        if (AIsShortInt(left) && AIsShortInt(right)) {
            if ((ASignedValue)left < (ASignedValue)right)
                return BoolToIntValue(ALtSatisfiesOp(operator));
            else if (left == right)
                return BoolToIntValue(AEqSatisfiesOp(operator));
            else
                return BoolToIntValue(AGtSatisfiesOp(operator));
        }

        if ((AIsShortInt(left) || AIsLongInt(left) || AIsFloat(left))
            && (AIsShortInt(right) || AIsLongInt(right) || AIsFloat(right))) {
            left = ACoerce(t, operator, left, right, &temp);
            if (AIsError(left))
                return left;
            right = temp;
        } else if (operator == OPER_EQ)
            return AIsEqual(t, left, right);
        else if (operator == OPER_NEQ) {
            AValue res = AIsEqual(t, left, right);
            return CheckBooleanNot(t, res);
        } else
            return ARaiseBinopTypeErrorND(t, operator, left, right);
    }

  StringComparison:

    {
        int result = ACompareStrings(left, right);
        if (result < 0)
            return BoolToIntValue(ALtSatisfiesOp(operator));
        else if (result == 0)
            return BoolToIntValue(AEqSatisfiesOp(operator));
        else if (result != A_CMP_FAIL)
            return BoolToIntValue(AGtSatisfiesOp(operator));
        else
            return ARaiseTypeErrorND(t, NULL);
    }
}


/* Coerce the arguments of a binary arithmetic operation to common types.
   Return the coerced left operand and store the coerced right operand at
   *rightRes. */
/* NOTE: Does not handle int+int, float+float and longint+longint. */
AValue ACoerce(AThread *t, AOperator op, AValue left, AValue right,
               AValue *rightRes)
{
    /* IDEA: Could this code be made smaller? This is ugly. */

    if (AIsFloat(left)) {
        if (AIsShortInt(right)) {
            *t->tempStack = left;
            right = ACreateFloat(t, AValueToInt(right));
            left = *t->tempStack;
            *t->tempStack = AZero;
        } else  if (AIsLongInt(right)) {
            *t->tempStack = left;
            right = ACreateFloatFromLongInt(t, right);
            left = *t->tempStack;
            *t->tempStack = AZero;
        } else if (!AIsFloat(right))
            goto Fail;
    } else if (AIsLongInt(left)) {
        if (AIsShortInt(right)) {
            *t->tempStack = left;
            right = ACreateLongIntFromIntND(t, AValueToInt(right));
            left = *t->tempStack;
            *t->tempStack = AZero;
        } else if (AIsFloat(right)) {
            *t->tempStack = right;
            left = ACreateFloatFromLongInt(t, left);
            right = *t->tempStack;
            *t->tempStack = AZero;
        } else if (!AIsLongInt(right))
            goto Fail;
    } else if (AIsShortInt(left)) {
        if (AIsFloat(right)) {
            *t->tempStack = right;
            left = ACreateFloat(t, AValueToInt(left));
            right = *t->tempStack;
            *t->tempStack = AZero;
        } else if (AIsLongInt(right)) {
            *t->tempStack = right;
            left = ACreateLongIntFromIntND(t, AValueToInt(left));
            right = *t->tempStack;
            *t->tempStack = AZero;
        } else if (!AIsShortInt(right))
            goto Fail;
    } else
        goto Fail;

    if (AIsError(right))
        return right;

    *rightRes = right;
    return left;

  Fail:

    return ARaiseBinopTypeErrorND(t, op, left, right);
}


/* Perform operation "left in right". Return Int value 1 if True, 0 if False
   and AError on error. */
AValue AIsIn(AThread *t, AValue left, AValue right)
{
    AValue res;

    if (AIsInstance(right)) {
        if (!AAllocTempStack_M(t, 2))
            return AError;

        t->tempStackPtr[-2] = right;
        t->tempStackPtr[-1] = left;

        res = ACallMethodByNum(t, AM_IN, 1, t->tempStackPtr - 2);

        t->tempStackPtr -= 2;

        return CheckBoolean(t, res);
    } else if (AIsRange(right)) {
        if (!AIsInt(left))
            return AZero;

        if (!AAllocTempStack_M(t, 3))
            return AError;

        t->tempStackPtr[-3] = left;
        t->tempStackPtr[-2] = AValueToMixedObject(right)->data.range.start;
        t->tempStackPtr[-1] = AValueToMixedObject(right)->data.range.stop;

        res = ACompareOrder(t, left, t->tempStackPtr[-2], OPER_GTE);
        if (res == AIntToValue(1))
            res = ACompareOrder(t, t->tempStackPtr[-3],
                                t->tempStackPtr[-1], OPER_LT);

        t->tempStackPtr -= 3;

        return res;
    } else if (AIsStr(right))
        return AIsInString(t, right, left);
    else
        return ARaiseBinopTypeErrorND(t, OPER_IN, left, right);
}


/* Look up a member of an instance from the specified member table via a member
   id. */
int AGetInstanceMember(AInstance *inst, AMemberTableType memberType,
                       unsigned member)
{
    ATypeInfo *type;
    AMemberHashTable *table;
    AMemberNode *node;

    type = AGetInstanceType(inst);
    table = AGetMemberTable(type, memberType);
    node = &table->item[member & table->size];

    while (node->key != member) {
        if (node->next == NULL) {
            if (type->super == NULL)
                return -1;
            else {
                type = type->super;
                table = AGetMemberTable(type, memberType);
                node = &table->item[member & table->size];
            }
        } else
            node = node->next;
    }

    return node->item;
}


/* Get the method with given member id or raise an exception. Return a bound
   method if successful. */
AValue AGetInstanceCodeMember(AThread *t, AInstance *inst, unsigned member)
{
    int methodNum;

    methodNum = AGetInstanceMember(inst, MT_METHOD_PUBLIC, member);
    if (methodNum == -1)
        return ARaiseMemberErrorND(t, AInstanceToValue(inst), member);

    return ACreateBoundMethod(t, AInstanceToValue(inst), methodNum);
}


/* Create a bound method object for given member id. Assume that inst is an
   instance with the given method. */
AValue ACreateBoundMethod(AThread *t, AValue inst, int methodNum)
{
    AMixedObject *method;

    *t->tempStack = inst;

    method = AAlloc(t, sizeof(AMixedObject));
    if (method == NULL)
        return AError;

    AInitValueBlock(&method->header, 3 * sizeof(AValue));

    method->type = A_BOUND_METHOD_ID;
    method->data.boundMethod.instance = *t->tempStack;
    method->data.boundMethod.method = AIntToValue(methodNum);

    *t->tempStack = AZero;

    return AMixedObjectToValue(method);
}


/* Read the data member of an instance. Raise MemberError if failed. */
AValue AGetInstanceDataMember(AThread *t, AInstance *inst, unsigned member)
{
    int memberNum;

    memberNum = AGetInstanceMember(inst, MT_VAR_GET_PUBLIC, member);
    if (memberNum == -1)
        return ARaiseMemberErrorND(t, AInstanceToValue(inst), member);

    if (memberNum >= A_VAR_METHOD) {
        AValue retVal;

        *t->tempStack = AInstanceToValue(inst);
        retVal = ACallValue(t, AGlobalByNum(memberNum & ~A_VAR_METHOD),
                              1, t->tempStack);
        *t->tempStack = AZero;
        return retVal;
    } else
        return inst->member[memberNum];
}


ABool AUpdateContext(AThread *t)
{
    if (t->contextIndex >= t->contextSize) {
        AExceptionContext *newContext =
            AGrowStatic(t->context,
                       2 * t->contextSize * sizeof(AExceptionContext));
        if (newContext == NULL)
            goto Fail;
        t->context = newContext;
        t->contextSize *= 2;
    } else if (t->contextIndex < 0) {
      Fail:
        ARaiseMemoryErrorND(t); /* FIX: ResourceError; add error message */
        t->contextIndex++;
        return FALSE;
    }

    t->context[t->contextIndex].stackPtr = t->stackPtr;
    t->context[t->contextIndex].tempStackPtr = t->tempStackPtr;
    t->contextIndex++;

    return TRUE;
}


/* If a direct exception has been raised at the current thread, dispatch it
   (i.e. do not return normally but jump directly to the next exception
   handler). Otherwise, do nothing. */
void ADispatchException(AThread *t)
{
    if (AIsOfType(t->exception,
                 AGlobalByNum(AErrorClassNum[EX_MEMORY_ERROR])) == A_IS_TRUE ||
        AIsOfType(t->exception,
                 AGlobalByNum(AErrorClassNum[EX_VALUE_ERROR])) == A_IS_TRUE)
        longjmp(t->context[t->contextIndex - 1].env, 1);
}


/* Initialize the GL_COMMAND_LINE_ARGS global value to contain an array of
   command line arguments. */
ABool ASetupCommandLineArgs(AThread *t, int argc, char **argv)
{
    AValue array;
    int i;

    array = AMakeArrayND(t, argc);
    if (AIsError(array))
        return FALSE;

    if (!ASetConstGlobalValue(t, GL_COMMAND_LINE_ARGS, array))
        return FALSE;

    for (i = 0; i < argc; i++) {
        AValue str = ACreateStringFromCStr(t, argv[i]);
        if (AIsError(str))
            return FALSE;

        if (!ASetArrayItemND(t, AGlobalByNum(GL_COMMAND_LINE_ARGS),
                             i, str))
            return FALSE;
    }

    return TRUE;
}


/* Does sym refer to a valid superclass (i.e. a type that is not a primitive
   one)? */
ABool AIsValidSuperClass(ASymbolInfo *sym)
{
    if (sym->type == ID_GLOBAL_CLASS) {
        return sym != ATupleClass->sym && sym != ABooleanClass->sym;
    } else
        return FALSE;
}
