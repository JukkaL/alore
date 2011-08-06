/* reflect_module.c - reflect module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "memberid.h"
#include "class.h"
#include "std_module.h"
#include "str.h"
#include "internal.h"
#include "gc.h"


#define IsLetter(c) (((c) | 32) >= 'a' && ((c) | 32) <= 'z')


/* Map a string to the corresponding member key. Raise a direct exception on
   all error conditions. */
static int GetMemberKeyByValue(AThread *t, AValue val)
{
    char buf[64];
    char *id;
    ASymbol *sym;
    ASymbolInfo *symInfo;
    int key;
    int i;

    id = AAllocCStrFromString(t, val, buf, 64);
    if (id == NULL) 
        ADispatchException(t);

    /* Verify that the id has the correct form. */
    if (!IsLetter(id[0]) && id[0] != '_') {
        AFreeCStrFromString(id);
        return ARaiseValueError(t, "Invalid member");
    }
    for (i = 1; id[i] != '\0'; i++) {
        if (!IsLetter(id[i]) && !AIsDigit(id[i]) && id[i] != '_') {
            AFreeCStrFromString(id);
            return ARaiseValueError(t, "Invalid member");
        }
    }

    ALockInterpreter();

    if (!AGetSymbol(id, strlen(id), &sym))
        goto MemoryFailure;

    if ((symInfo = AGetMemberSymbol(sym)) == NULL)
        goto MemoryFailure;

    key = symInfo->num;

    AUnlockInterpreter();
    
    AFreeCStrFromString(id);
    
    return key;

  MemoryFailure:
    
    AUnlockInterpreter();
    AFreeCStrFromString(id);
    ARaiseMemoryError(t);
    return 0;
}


/* reflect::GetMember(obj, member) */
static AValue ReflectGetMember(AThread *t, AValue *frame)
{
    int key = GetMemberKeyByValue(t, frame[1]);
    if (key == AM_CREATE)
        ARaiseValueError(t, NULL);
    return AMemberByNum(t, frame[0], key);
}


/* reflect::HasMember(obj, member) */
static AValue ReflectHasMember(AThread *t, AValue *frame)
{
    /* FIX: Only check the member hash tables - no not call getters! */
    AValue ret = ReflectGetMember(t, frame);
    if (!AIsError(ret))
        return ATrue;
    else if (AIsOfType(t->exception,
                       AGlobalByNum(AErrorClassNum[EX_MEMBER_ERROR])) ==
             A_IS_TRUE)
        return AFalse;
    else
        return AError;
}


AValue ASetMemberHelper(AThread *t, AValue *frame, int key)
{
    AInstance *inst;
    ATypeInfo *type;
    AMemberHashTable *table;
    AMemberNode *node;
    AValue funcVal;
    
    if (!AIsInstance(frame[0])) {
        frame[0] = AWrapObject(t, frame[0]);
        if (AIsError(frame[0]))
            return AError;
    }

    inst = AValueToInstance(frame[0]);
    type = AGetInstanceType(inst);
    table = AGetMemberTable(type, MT_VAR_SET_PUBLIC);
    node = &table->item[key & table->size];
    
    /* Search the setter hash table. */
    while (node->key != key) {
        if (node->next == NULL) {
            if (type->super == NULL)
                return ARaiseMemberErrorND(t, frame[0], key);
            else {
                type = type->super;
                table = AGetMemberTable(type, MT_VAR_SET_PUBLIC);
                node = &table->item[key & table->size];
            }
        } else
            node = node->next;
    }
    
    if (node->item >= A_VAR_METHOD) {
        /* Call setter method. */
        funcVal = AGlobalByNum(node->item & ~A_VAR_METHOD);
        if (AIsError(ACallValue(t, funcVal, 2, frame)))
            return AError;
    } else {
        unsigned member = node->item;
        ABool result;
        
        AModifyObject_M(t, &inst->type, inst->member + member,
                       frame + 1, result);
        if (!result)
            return ARaiseMemoryErrorND(t);
    }

    return ANil;
}


/* reflect::SetMember(obj, member, value) */
static AValue ReflectSetMember(AThread *t, AValue *frame)
{
    int key = GetMemberKeyByValue(t, frame[1]);
    frame[1] = frame[2];
    return ASetMemberHelper(t, frame, key);
}


/* reflect::TypeOf(obj) */
static AValue ReflectTypeOf(AThread *t, AValue *frame)
{
    if (AIsInstance(frame[0])) {
        /* Anonymous functions need special processing. */
        if (AIsAnonFunc(frame[0]))
            return AGlobalByNum(AStdFunctionNum);
        else {
            /* This is the generic case. It covers all user-defined types
               and most C and library types. */
            AInstance *inst = AValueToInstance(frame[0]);
            return AGlobalByNum(AGetInstanceType(inst)->sym->num);
        }
    } else if (AIsInt(frame[0]))
        return AGlobalByNum(AStdIntNum);
    else if (AIsStr(frame[0]))
        return AGlobalByNum(AStdStrNum);
    else if (AIsFloat(frame[0]))
        return AGlobalByNum(AStdFloatNum);
    else if (AIsNil(frame[0]))
        return ARaiseValueError(t, "Type of nil not defined");
    else if (AIsConstant(frame[0]))
        return AGlobalByNum(AStdConstantNum);
    else if (AIsPair(frame[0]))
        return AGlobalByNum(AStdPairNum);
    else if (AIsRange(frame[0]))
        return AGlobalByNum(AStdRangeNum);
    else if (AIsOfType(frame[0], AGlobalByNum(AStdTypeNum)) == A_IS_TRUE)
        return AGlobalByNum(AStdTypeNum);
    else if (AIsGlobalFunction(frame[0]) || AIsMethod(frame[0]))
        return AGlobalByNum(AStdFunctionNum);
    else
        return ARaiseValueErrorND(t, NULL);
}


A_MODULE(reflect, "reflect")
    A_DEF("GetMember", 2, 0, ReflectGetMember)
    A_DEF("HasMember", 2, 0, ReflectHasMember)
    A_DEF("SetMember", 3, 0, ReflectSetMember)
    A_DEF("TypeOf", 1, 0, ReflectTypeOf)
A_END_MODULE()
