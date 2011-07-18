/* testc_module.c - __testc module (for testing only)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Miscellaneous functions that are used to test the C API and internal Alore
   issues. You should generally not use this module for any other purpose. */

#include "alore.h"
#include "runtime.h"
#include "class.h"
#include "memberid.h"
#include "mem.h"
#include "gc.h"
#include "array.h"
#include "internal.h"
#include "str.h"
#include "error.h"
#include "util.h"
#include "debug_runtime.h"
#include "thread_athread.h"
#include "ident.h"
#include "dynaload.h"
#include "version.h"
#include "io_module.h"

#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#include <unistd.h>
#endif


static int ShortIntMaxNum;
static int ShortIntMinNum;
static int BinaryDataOffset1;
static int BinaryDataOffset2;


static AValue TestC_Main(AThread *t, AValue *frame)
{
    ASetGlobalByNum(ShortIntMaxNum, AIntToValue(A_SHORT_INT_MAX));
    ASetGlobalByNum(ShortIntMinNum, AIntToValue(A_SHORT_INT_MIN));
    return ANil;
}


static AValue TestC_AIsLongInt(AThread *t, AValue *frame)
{
    if (AIsLongInt(frame[0]))
        return ATrue;
    else
        return AFalse;
}


static AValue TestC_IsWideStr(AThread *t, AValue *frame)
{
    if (AIsWideStr(frame[0]))
        return ATrue;
    else if (AIsSubStr(frame[0])) {
        if (AIsWideStr(AValueToSubStr(frame[0])->str))
            return ATrue;
        else
            return AFalse;
    } else
        return AFalse;
}


static AValue TestC_IsSubStr(AThread *t, AValue *frame)
{
    if (AIsSubStr(frame[0]))
        return ATrue;
    else
        return AFalse;
}


static AValue TestC_PrintFloat(AThread *t, AValue *frame)
{
    unsigned *i;
    
    if (!AIsFloat(frame[0]))
        return ARaiseTypeErrorND(t, NULL);

    i = AValueToPtr(frame[0]);
    printf("ptr %p\n", i);
    printf("%x %x\n", i[0], i[1]);

    printf("%.15g\n", AValueToFloat(frame[0]));

    return ANil;
}


static AValue TestC_CallTrace(AThread *t, AValue *frame)
{
    AValue retVal;
    AValue a;

    if (AHandleException(t))
        retVal = AError;
    else
        retVal = ACallValue(t, frame[0], 1 | A_VAR_ARG_FLAG, frame + 1);

    t->contextIndex--;

    if (AIsError(retVal)) {
        ACreateTracebackArray(t); /* FIX: error handling ? */

        /* NOTE: Cannot use tempStack[0], since the array operations may
                 overwrite it. */
        t->tempStack[1] = t->exception;
        t->tempStack[0] = AFilterTracebackArray(t);
        /* FIX: error handling? */
        t->tempStack[0] =
            ASubArray(t, t->tempStack[0], 0, AArrayLen(t->tempStack[0]) - 1);
    } else {
        t->tempStack[1] = ANil; /* FIX: what about storing retVal? */
        t->tempStack[0] = retVal;
    }

    a = AMakeArray(t, 2);

    ASetArrayItemNewGen(a, 0, t->tempStack[1]);
    ASetArrayItemNewGen(a, 1, t->tempStack[0]);

    return a;
}


static AValue TestC_TypeName(AThread *t, AValue *frame)
{
    char buf[1024];

    if (!AIsInstance(frame[0]))
        return ARaiseTypeErrorND(t, NULL);
    else {
        AFormatMessage(buf, 1024, "%q",
                       AGetInstanceType(AValueToInstance(frame[0]))->sym);
        return AMakeStr(t, buf);
    }
}


static AValue TestC_CollectGarbage(AThread *t, AValue *frame)
{
    ABool status;
    
    ALockHeap();
    status = ACollectGarbage();
    AReleaseHeap();
    
    if (!status)
        return ARaiseMemoryErrorND(t);
    else
        return ANil;
}


static AValue ATestC_CollectAllGarbage(AThread *t, AValue *frame)
{
    if (!ACollectAllGarbage())
        return ARaiseMemoryErrorND(t);
    else
        return ANil;
}


static AValue TestC_VerifyMemory(AThread *t, AValue *frame)
{
#ifdef A_DEBUG
    ADebugVerifyMemory_F();
#endif
    return ANil;
}


static AValue TestC_Call(AThread *t, AValue *frame)
{
    int c1;
    int c2;
    int i;
    
    if (!AIsArray(frame[1]) || (!AIsArray(frame[2]) && frame[2] != ADefault))
        return ARaiseTypeErrorND(t, NULL);

    c1 = AArrayLen(frame[1]);
    if (c1 > 4)
        return ARaiseValueErrorND(t, NULL);
    
    /* Copy the fixed arguments. */
    for (i = 0; i < c1; i++)
        frame[3 + i] = AArrayItem(frame[1], i);

    frame[3 + i] = frame[2];

    if (AIsArray(frame[2]))
        c2 = c1 + 1 + A_VAR_ARG_FLAG;
    else
        c2 = c1;
    
    return ACallValue(t, frame[0], c2, frame + 3);
}


#define MAX_MEMBERS 512

typedef struct {
    char *name;
    int classNum;
    unsigned orderNum;
} MemberStruct;


static int NumMembers(ATypeInfo *type);
static int GetMembers(MemberStruct *m, ATypeInfo *type);
static void SortMembers(MemberStruct *m, int n);


/* IDEA: This is a temporary solution and not very robust. We should implement
         a real reflection API. */
static AValue TestC_Members(AThread *t, AValue *frame)
{
    int n;
    MemberStruct members[MAX_MEMBERS];
    ATypeInfo *type;
    int i;

    if (!AIsInstance(frame[0]))
        return ARaiseTypeErrorND(t, NULL);

    type = AGetInstanceType(AValueToInstance(frame[0]));
    n = NumMembers(type);
    if (n > MAX_MEMBERS)
        return ARaiseValueErrorND(t, NULL);

    GetMembers(members, type);
    SortMembers(members, n);

    frame[1] = AMakeArray(t, n);
    if (AIsError(frame[1]))
        return AError;

    for (i = 0; i < n; i++) {
        frame[2] = AMakeStr(t, members[i].name);
        ASetArrayItem(t, frame[1], i, frame[2]);
    }
    
    return frame[1];
}


static ABool IsValidKey(int k)
{
    return k != AM_NONE && k != AM_INITIALIZER && k != AM_FINALIZER;
}


static ABool HasPublicMember(ATypeInfo *type, int key)
{
    return type != NULL &&
        (HasPublicMember(type->super, key) ||
         ALookupMemberTable(type, MT_VAR_GET_PUBLIC, key) != -1 ||
         ALookupMemberTable(type, MT_METHOD_PUBLIC, key) != -1);
}


static int NumTableMembers(AValue v, ATypeInfo *super)
{
    int i;
    int n;
    AMemberHashTable *t;

    t = (AMemberHashTable *)AValueToPtr(v);
    n = 0;
    for (i = 0; i <= t->size; i++) {
        if (IsValidKey(t->item[i].key)
            && !HasPublicMember(super, t->item[i].key))
            n++;
    }

    return n;
}


static int NumMembers(ATypeInfo *type)
{
    int n;

    if (type->super != NULL)
        n = NumMembers(type->super);
    else
        n = 0;
    
    return n +
        NumTableMembers(type->memberTable[MT_VAR_GET_PUBLIC], type->super) +
        NumTableMembers(type->memberTable[MT_METHOD_PUBLIC], type->super);
}


static int GetTableMembers(MemberStruct *m, int n, AValue v, ABool isMethod)
{
    int i;
    int j;
    AMemberHashTable *t;

    t = (AMemberHashTable *)AValueToPtr(v);
    for (i = 0; i <= t->size; i++) {
        if (IsValidKey(t->item[i].key)) {
            ASymbolInfo *sym = AGetMemberSymbolByKey(t->item[i].key);
            ALockInterpreter();
            m[n].name = (char *)AGetSymbolName(sym);
            AUnlockInterpreter();

            /* Check if the name has already been found to avoid duplicates. */
            for (j = 0; j < n; j++) {
                if (m[j].name == m[n].name)
                    break;
            }
            
            if (j == n) {
                m[n].classNum = 0;
                /* FIX This ordering is kind of ad-hoc, but maybe it doesn't
                   matter. */
                if (isMethod)
                    m[n].orderNum = t->item[i].item + 1000000;
                else
                    m[n].orderNum = t->item[i].item;
                n++;
            }
        }
    }

    return n;
}


static int GetMembers(MemberStruct *m, ATypeInfo *type)
{
    int i;

    if (type == NULL)
        return 0;

    i = GetMembers(m, type->super);
    
    i = GetTableMembers(m, i, type->memberTable[MT_VAR_GET_PUBLIC], FALSE);
    i = GetTableMembers(m, i, type->memberTable[MT_METHOD_PUBLIC], TRUE);
    
    return i;
}


static ABool IsLess(MemberStruct m1, MemberStruct m2)
{
    return m1.classNum < m2.classNum || (m1.classNum == m2.classNum &&
                                         m1.orderNum < m2.orderNum);
}


static void SortMembers(MemberStruct *m, int n)
{
    int i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && IsLess(m[j], m[j - 1]); j--) {
            MemberStruct t = m[j];
            m[j] = m[j - 1];
            m[j - 1] = t;
        }
    }
}


static AValue TestC_CheckSyntax(AThread *t, AValue *frame)
{
    return ALoaderLoadInternal(t, frame, FALSE);
}


static AValue TestC_RaiseDirectTypeError(AThread *t, AValue *frame)
{
    ARaiseTypeErrorND(t, NULL);
    ADispatchException(t);
    printf("FAILURE!\n");
    return ANil;
}


static AValue TestC_RaiseDirectMemoryError(AThread *t, AValue *frame)
{
    ARaiseMemoryErrorND(t);
    ADispatchException(t);
    printf("FAILURE!\n");
    return ANil;
}


static AValue TestC_ContextDepth(AThread *t, AValue *frame)
{
    return AIntToValue(t->contextIndex);
}


static AValue TestC_TempStackDepth(AThread *t, AValue *frame)
{
    return AIntToValue(t->tempStackPtr - t->tempStack);
}


static AValue TestC_AGetInt(AThread *t, AValue *frame)
{
    int i = AGetInt(t, frame[0]);
    char buf[100];
    sprintf(buf, "%d", i);
    return AMakeStr(t, buf);
}


static AValue TestC_AGetIntU(AThread *t, AValue *frame)
{
    unsigned i = AGetIntU(t, frame[0]);
    char buf[100];
    sprintf(buf, "%u", i);
    return AMakeStr(t, buf);
}


/* A helper function for converting an unsigned 64-bit integer to a string. */
static void IntU64ToStr(char *buf, AIntU64 i)
{
    int n = 0;
    int j;

    /* Generate reversed string. */
    do {
        buf[n] = i % 10 + '0';
        n++;
        i /= 10;
    } while (i > 0);

    /* Reverse generated string. */
    for (j = 0; j < n / 2; j++) {
        char t = buf[j];
        buf[j] = buf[n - j - 1];
        buf[n - j - 1] = t;            
    }
    buf[n] = '\0';
}


/* A helper function for converting a 64-bit integer to a string. */
static void Int64ToStr(char *buf, AInt64 i)
{
    AIntU64 ui;
    
    if (i < 0) {
        buf[0] = '-';
        /* This is tricky because the negation of -(1<<63) is not representable
           as a signed 64 bit integer. */
        ui = (AIntU64)(-(i + 1)) + 1;
        IntU64ToStr(buf + 1, ui);
    } else
        IntU64ToStr(buf, (AIntU64)i);
}


static AValue TestC_AGetInt64(AThread *t, AValue *frame)
{
    AInt64 i = AGetInt64(t, frame[0]);
    char buf[100];

    /* sprintf(buf, "%lld", i) would be simpler but not as portable. */
    Int64ToStr(buf, i);
    
    return AMakeStr(t, buf);
}


static AValue TestC_AGetIntU64(AThread *t, AValue *frame)
{
    AIntU64 i = AGetIntU64(t, frame[0]);
    char buf[100];

    /* sprintf(buf, "%llu", i) would be simpler but not as portable. */    
    IntU64ToStr(buf, i);
    
    return AMakeStr(t, buf);
}


static AValue TestC_AMakeInt(AThread *t, AValue *frame)
{
    int i = AGetInt(t, frame[0]);
    return AMakeInt(t, i);
}


static AValue TestC_AMakeIntU(AThread *t, AValue *frame)
{
    unsigned i = AGetIntU(t, frame[0]);
    return AMakeIntU(t, i);
}


static AValue TestC_AMakeInt64(AThread *t, AValue *frame)
{
    AInt64 i = AGetInt64(t, frame[0]);
    return AMakeInt64(t, i);
}


static AValue TestC_AMakeIntU64(AThread *t, AValue *frame)
{
    AIntU64 i = AGetIntU64(t, frame[0]);
    return AMakeIntU64(t, i);
}


static AValue TestC_AGetFloat(AThread *t, AValue *frame)
{
    double f = AGetFloat(t, frame[0]);
    char buf[100];
    sprintf(buf, "%g", f);
    return AMakeStr(t, buf);
}


static AValue TestC_AGetStr(AThread *t, AValue *frame)
{
    char buf[10];
    int len = AGetStr(t, frame[0], buf, 10);
    int i;
    if (len != AStrLen(frame[0]))
        ARaiseValueError(t, "invalid length");
    if (buf[len] != '\0')
        ARaiseValueError(t, "not zero-terminated");
    for (i = 0; i < len; i++) {
        if ((unsigned char)buf[i] != AStrItem(frame[0], i))
            ARaiseValueError(t, "invalid value");
    }
    return AMakeStr(t, buf);
}


static AValue TestC_AGetStrW(AThread *t, AValue *frame)
{
    AWideChar buf[10];
    int len = AGetStrW(t, frame[0], buf, 10);
    int i;
    if (len != AStrLen(frame[0]))
        ARaiseValueError(t, "invalid length");
    if (buf[len] != '\0')
        ARaiseValueError(t, "not zero-terminated");
    for (i = 0; i < len; i++) {
        if (buf[i] != AStrItem(frame[0], i))
            ARaiseValueError(t, "invalid value");
    }
    return AMakeStrW(t, buf);
}


static AValue TestC_AGetStrUtf8(AThread *t, AValue *frame)
{
    char str[4096];
    int len = AGetStrUtf8(t, frame[0], str, 4096);
    if (len != strlen(str))
        ARaiseValueError(t, "invalid length");
    return AMakeStr(t, str);
}


static AValue TestC_AMakeEmptyStr(AThread *t, AValue *frame)
{
    int len = AGetInt(t, frame[0]);
    AValue v = AMakeEmptyStr(t, len);
    int i;
    for (i = 0; i < len; i++)
        ASetStrItem(v, i, 'a');
    return v;
}


static AValue TestC_AMakeEmptyStrW(AThread *t, AValue *frame)
{
    int len = AGetInt(t, frame[0]);
    AValue v = AMakeEmptyStrW(t, len);
    int i;
    for (i = 0; i < len; i++)
        ASetStrItem(v, i, 0x1000);
    return v;
}


static AValue TestC_AMakeStrUtf8(AThread *t, AValue *frame)
{
    char str[4096];
    AGetStr(t, frame[0], str, 4096);
    return AMakeStrUtf8(t, str);
}


static AValue TestC_AStrLenUtf8(AThread *t, AValue *frame)
{
    return AMakeInt(t, AStrLenUtf8(frame[0]));
}


static AValue TestC_ASetArrayItem(AThread *t, AValue *frame)
{
    int index = AGetInt(t, frame[1]);
    ASetArrayItem(t, frame[0], index, frame[2]);
    return ANil;
}


static AValue TestC_AMakeArray(AThread *t, AValue *frame)
{
    return AMakeArray(t, AGetInt(t, frame[0]));
}


static AValue TestC_AMakeTuple(AThread *t, AValue *frame)
{
    Assize_t n = AArrayLen(frame[0]);
    Assize_t i;

    frame[1] = AMakeTuple(t, n);
    for (i = 0; i < n; i++)
        AInitTupleItem(t, frame[1], i, AArrayItem(frame[0], i));

    return frame[1];
}


static AValue TestC_ATemps(AThread *t, AValue *frame)
{
    AValue *t1;
    AValue *t2;
    AValue *tempStackPtr = t->tempStackPtr;
    
    t1 = AAllocTemp(t, AZero);
    if (*t1 != AZero)
        goto Fail;

    *t1 = AMakeStr(t, "hello");
    
    t2 = AAllocTemps(t, 3);
    if (t2[0] != AZero || t2[1] != AZero || t2[2] != AZero)
        goto Fail;
    if (t1 + 1 != t2)
        goto Fail;

    t2[0] = AMakeInt(t, 1000);
    t2[1] = AMakeInt(t, 2000);
    t2[2] = AMakeInt(t, 3000);

    AFreeTemps(t, 3);
    AFreeTemp(t);

    if (t->tempStackPtr != tempStackPtr)
        goto Fail;

    return AMakeInt(t, 1);

  Fail:

    ARaiseValueError(t, "temp error");
    return AError;
}


static AValue TestC_AIsValue(AThread *t, AValue *frame)
{
    if (AIsValue(t, frame[0], frame[1]))
        return ATrue;
    else
        return AFalse;
}


static AValue TestC_ACall(AThread *t, AValue *frame)
{
    char func[100];
    AGetStr(t, frame[0], func, sizeof(func));
    return ACall(t, func, 1, frame + 1);
}


static AValue TestC_ACallVarArg(AThread *t, AValue *frame)
{
    char func[100];
    AGetStr(t, frame[0], func, sizeof(func));
    return ACallVarArg(t, func, 1, frame + 1);
}


static AValue TestC_ACallValue(AThread *t, AValue *frame)
{
    return ACallValue(t, frame[0], 1, frame + 1);
}


static AValue TestC_ACallValueVarArg(AThread *t, AValue *frame)
{
    return ACallValueVarArg(t, frame[0], 1, frame + 1);
}


static AValue TestC_ARaise(AThread *t, AValue *frame)
{
    char type[100];
    AGetStr(t, frame[0], type, sizeof(type));
    return ARaise(t, type, NULL);
}


static AValue TestC_ARaiseByType(AThread *t, AValue *frame)
{
    char msg[100];
    AGetStr(t, frame[1], msg, 100);
    return ARaiseByType(t, frame[0], msg);
}


static AValue TestC_ARaiseValue(AThread *t, AValue *frame)
{
    return ARaiseValue(t, frame[0]);
}


static AValue TestC_ARaiseErrnoIoError(AThread *t, AValue *frame)
{
    errno = AGetInt(t, frame[0]);
    return ARaiseErrnoIoError(t, NULL);
}


static AValue TestC_ATry(AThread *t, AValue *frame)
{
    if (ATry(t))
        return AMakeInt(t, 1);

    if (AIsTrue(frame[0]))
        ARaiseTypeError(t, "problem");

    AEndTry(t);
    return AMakeInt(t, 2);    
}


static AValue TestC_AIsExceptionType(AThread *t, AValue *frame)
{
    char type[100];
    
    AGetStr(t, frame[1], type, sizeof(type));
    
    if (ATry(t)) {
        if (AIsExceptionType(t, type))
            return ATrue;
        return AError;
    }
    
    if (ACallValue(t, frame[0], 0, frame + 2) == AError) {
        AEndTry(t);
        if (AIsExceptionType(t, type))
            return ATrue;
        return AError;
    }
    
    AEndTry(t);
    return AFalse;
}


static AValue TestC_ACallMethod(AThread *t, AValue *frame)
{
    char member[100];
    int nargs;
    
    AGetStr(t, frame[0], member, 100);

    for (nargs = 3; nargs > 0 && frame[nargs + 1] == ADefault; nargs--);
    return ACallMethod(t, member, nargs, frame + 1);
}


static AValue TestC_ACallMethodVarArg(AThread *t, AValue *frame)
{
    char member[100];
    AGetStr(t, frame[0], member, 100);
    return ACallMethodVarArg(t, member, 0, frame + 1);
}


static AValue TestC_AMember(AThread *t, AValue *frame)
{
    char member[100];
    AGetStr(t, frame[1], member, 100);
    return AMember(t, frame[0], member);
}


static AValue TestC_ASetMember(AThread *t, AValue *frame)
{
    char member[100];
    AGetStr(t, frame[1], member, 100);
    return ASetMember(t, frame[0], member, frame[2]);
}


static AValue TestC_ASuperMember(AThread *t, AValue *frame)
{
    char member[100];
    AGetStr(t, frame[1], member, 100);
    return ASuperMember(t, frame, member);
}


static AValue TestC_ASetSuperMember(AThread *t, AValue *frame)
{
    char member[100];
    AGetStr(t, frame[1], member, 100);
    return ASetSuperMember(t, frame, member, frame[2]);
}


static AValue TestC_AGlobal(AThread *t, AValue *frame)
{
    char name[200];
    AGetStr(t, frame[0], name, 200);
    return AGlobal(t, name);
}


static AValue TestC_AConcat(AThread *t, AValue *frame)
{
    return AConcat(t, frame[0], frame[1]);
}


static AValue TestC_ASubStr(AThread *t, AValue *frame)
{
    int i1 = AGetInt(t, frame[1]);
    int i2 = AGetInt(t, frame[2]);
    return ASubStr(t, frame[0], i1, i2);
}


static AValue TestC_AGetPair(AThread *t, AValue *frame)
{
    AValue v1, v2;
    AGetPair(t, frame[0], &v1, &v2);
    return AMakePair(t, v1, v2);
}


static AValue TestC_AGetRange(AThread *t, AValue *frame)
{
    AValue v1, v2;
    AGetRange(t, frame[0], &v1, &v2);
    return AMakeRange(t, v1, v2);
}


static AValue TestC_AMakeRange(AThread *t, AValue *frame)
{
    return AMakeRange(t, frame[0], frame[1]);
}


#ifdef HAVE_GETTIMEOFDAY
static AValue TestC_Time(AThread *t, AValue *frame)
{
    struct timeval v;
    gettimeofday(&v, NULL);
    return AMakeFloat(t, (double)v.tv_sec + v.tv_usec / 1000000.0);
}
#endif

    
static AValue TestC_AIsEq(AThread *t, AValue *frame)
{
    return AIntToValue(AIsEq(t, frame[0], frame[1]));
}

    
static AValue TestC_AIsNeq(AThread *t, AValue *frame)
{
    return AIntToValue(AIsNeq(t, frame[0], frame[1]));
}

    
static AValue TestC_AIsLt(AThread *t, AValue *frame)
{
    return AIntToValue(AIsLt(t, frame[0], frame[1]));
}

    
static AValue TestC_AIsGt(AThread *t, AValue *frame)
{
    return AIntToValue(AIsGt(t, frame[0], frame[1]));
}

    
static AValue TestC_AIsLte(AThread *t, AValue *frame)
{
    return AIntToValue(AIsLte(t, frame[0], frame[1]));
}

    
static AValue TestC_AIsGte(AThread *t, AValue *frame)
{
    return AIntToValue(AIsGte(t, frame[0], frame[1]));
}

    
static AValue TestC_ASubArray(AThread *t, AValue *frame)
{
    int i1 = AGetInt(t, frame[1]);
    int i2 = AGetInt(t, frame[2]);
    return ASubArray(t, frame[0], i1, i2);
}

    
static AValue TestC_ALen(AThread *t, AValue *frame)
{
    int l = ALen(t, frame[0]);
    if (l < 0)
        return AError;
    else
        return AIntToValue(l);
}


static AValue TestC_AGetItemAt(AThread *t, AValue *frame)
{
    Assize_t i = AGetInt(t, frame[1]);
    return AGetItemAt(t, frame[0], i);
}


static AValue TestC_ASetItemAt(AThread *t, AValue *frame)
{
    Assize_t i = AGetInt(t, frame[1]);
    return ASetItemAt(t, frame[0], i, frame[2]);
}


static AValue TestC_AGetItem(AThread *t, AValue *frame)
{
    return AGetItem(t, frame[0], frame[1]);
}


static AValue TestC_ASetItem(AThread *t, AValue *frame)
{
    return ASetItem(t, frame[0], frame[1], frame[2]);
}


static AValue TestC_AAdd(AThread *t, AValue *frame)
{
    return AAdd(t, frame[0], frame[1]);
}


static AValue TestC_ASub(AThread *t, AValue *frame)
{
    return ASub(t, frame[0], frame[1]);
}


static AValue TestC_AMul(AThread *t, AValue *frame)
{
    return AMul(t, frame[0], frame[1]);
}


static AValue TestC_ADiv(AThread *t, AValue *frame)
{
    return ADiv(t, frame[0], frame[1]);
}


static AValue TestC_AIntDiv(AThread *t, AValue *frame)
{
    return AIntDiv(t, frame[0], frame[1]);
}


static AValue TestC_AMod(AThread *t, AValue *frame)
{
    return AMod(t, frame[0], frame[1]);
}


static AValue TestC_APow(AThread *t, AValue *frame)
{
    return APow(t, frame[0], frame[1]);
}


static AValue TestC_ANeg(AThread *t, AValue *frame)
{
    return ANeg(t, frame[0]);
}


static AValue TestC_AIn(AThread *t, AValue *frame)
{
    int i = AIn(t, frame[0], frame[1]);
    if (i < 0)
        return AError;
    else
        return AMakeShortInt(i);
}


static AValue TestC_GetCModuleImports(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    AModuleDef *def;

    AGetStr(t, frame[0], path, sizeof(path));
    
    if (!ALoadCModule(path, &def))
        return ARaiseValueError(t, "Invalid path");

    frame[1] = AMakeArray(t, 0);

    for (def++; def->type == MD_IMPORT; def++) {
        AValue str = AMakeStr(t, def->str);
        AAppendArray(t, frame[1], str);
    }

    return frame[1];
}


static AValue TestC_AVersion(AThread *t, AValue *frame)
{
    return AMakeStr(t, AVersion);
}


/* Return the number of SymbolInfo structures in the symbol table, not
   including member symbols. This is used to check if symbols are removed
   correctly from the symbol table. */
static AValue TestC_SymbolInfoCount(AThread *t, AValue *frame)
{
    int count = 0;
    int i;

    for (i = 0; i <= ASymSize; i++) {
        ASymbol *base = ASym[i];

        while (base != NULL) {
            ASymbolInfo *cur;
            
            cur = base->info;
            while ((void *)cur != (void *)base) {
                /* Do not count member symbols since they cannot be cleaned up
                   currently from the symbol table. */
                if (cur->type != ID_MEMBER)
                    count++;
                cur = cur->next;
            }

            base = base->next;
        }
    }
    
    
    return AMakeInt(t, count);
}


static AValue TestC_GlobalValueCount(AThread *t, AValue *frame)
{
    int count = ANumMainGlobals;
    int num = AFirstDynamicModule;

    /* Loop through all active dynamic modules. */
    while (num != 0) {
        ADynaModule *m = AGetModuleInfo(num);
        int i;

        /* Iterate the global blocks in the module. */
        i = AValueToInt(m->globalVarIndex);
        while (i != 0) {
            count += A_GLOBAL_BUCKET_SIZE;
            i = AGetNextGlobalBucket(i);
        }
        i = AValueToInt(m->globalConstIndex);
        while (i != 0) {
            count += A_GLOBAL_BUCKET_SIZE;
            i = AGetNextGlobalBucket(i);
        }
        
        num = AGetNextModule(num);
    }    
    
    return AMakeInt(t, count);
}



static AValue TestC_TypeStats(AThread *t, AValue *frame)
{
    ATypeInfo *type = AValueToType(frame[0]);
    frame[1] = AMakeArray(t, 8);
    ASetArrayItem(t, frame[1], 0, AIntToValue(type->numVars));
    ASetArrayItem(t, frame[1], 1, AIntToValue(type->totalNumVars));
    ASetArrayItem(t, frame[1], 2, AIntToValue(type->dataSize));
    ASetArrayItem(t, frame[1], 3, AIntToValue(type->dataOffset));
    ASetArrayItem(t, frame[1], 4, AIntToValue(type->instanceSize));
    ASetArrayItem(t, frame[1], 5, AIntToValue(type->hasFinalizer));
    ASetArrayItem(t, frame[1], 6, AIntToValue(type->hasFinalizerOrData));
    ASetArrayItem(t, frame[1], 7, AIntToValue(type->extDataMember));
    return frame[1];
}


AValue TestC_AAllocContainer(AThread *t, AValue *frame)
{
    return AAllocContainer(t, frame[0]);
}


AValue TestC_AContainerPtr(AThread *t, AValue *frame)
{
    void *ptr = AContainerPtr(frame[0]);
    /* NOTE: Cast pointer to integer (AValue). */
    return AMakeIntU64(t, (AValue)ptr);
}


AValue TestC_ASetContainerValue(AThread *t, AValue *frame)
{
    AValue u = AGetIntU64(t, frame[0]);
    /* NOTE: Cast integer (AValue) to pointer. */
    void *ptr = (void *)u;
    ASetContainerValue(t, ptr, frame[1]);
    return ANil;
}


AValue TestC_AContainerValue(AThread *t, AValue *frame)
{
    AValue u = AGetIntU64(t, frame[0]);
    /* NOTE: Cast integer (AValue) to pointer. */
    void *ptr = (void *)u;
    return AContainerValue(ptr);
}


static AValue BinaryData1Set(AThread *t, AValue *frame)
{
    int i = AGetInt(t, frame[1]);
    *(int *)ADataPtr(frame[0], BinaryDataOffset1) = i;
    return ANil;
}


static AValue BinaryData1Get(AThread *t, AValue *frame)
{
    return AMakeInt(t, *(int *)ADataPtr(frame[0], BinaryDataOffset1));
}


static AValue BinaryData1Create(AThread *t, AValue *frame)
{
    *(int *)ADataPtr(frame[0], BinaryDataOffset1) = 1000;
    return frame[0];
}


static AValue BinaryData1Init(AThread *t, AValue *frame)
{
    *(int *)ADataPtr(frame[0], BinaryDataOffset1) = 1000000;
    return ANil;
}


static AValue BinaryData2Set(AThread *t, AValue *frame)
{
    int i = AGetInt(t, frame[1]);
    int j = AGetInt(t, frame[2]);
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[0] = i;
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[1] = j;
    return ANil;
}


static AValue BinaryData2Get(AThread *t, AValue *frame)
{
    int i = ((int *)ADataPtr(frame[0], BinaryDataOffset2))[0];
    int j = ((int *)ADataPtr(frame[0], BinaryDataOffset2))[1];
    return AMakeInt(t, i + j);
}
   

static AValue BinaryData2Create(AThread *t, AValue *frame)
{
    BinaryData1Create(t, frame);
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[0] = 2000;
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[1] = 3000;
    return frame[0];
}
   

static AValue BinaryData2Init(AThread *t, AValue *frame)
{
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[0] = 2000000;
    ((int *)ADataPtr(frame[0], BinaryDataOffset2))[1] = 3000000;
    return ANil;
}


static AValue TestC_InfiniteRecursionC(AThread *t, AValue *frame)
{
    AValue f = AGlobal(t, "__testc::InfiniteRecursionC");
    return ACallValue(t, f, 0, frame);
}


static AValue RecursionClassCInfiniteRecursion(AThread *t, AValue *frame)
{
    return ACallMethod(t, "infiniteRecursion", 0, frame);
}


A_MODULE(__testc, "__testc")
    A_IMPORT("io")
    A_IMPORT("loader")
    A_DEF(A_PRIVATE("Main"), 0, 5, TestC_Main)
    A_DEF("AIsLongInt", 1, 0, TestC_AIsLongInt)
    A_DEF("IsWideStr", 1, 0, TestC_IsWideStr)
    A_DEF("IsSubStr", 1, 0, TestC_IsSubStr)
    A_DEF("PrintFloat", 1, 0, TestC_PrintFloat)
    A_DEF_VARARG("CallTrace", 1, 1, 1, TestC_CallTrace)
    A_DEF("TypeName", 1, 0, TestC_TypeName)
    A_DEF("CollectGarbage", 0, 0, TestC_CollectGarbage)
    A_DEF("CollectAllGarbage", 0, 0, ATestC_CollectAllGarbage)
    A_DEF("VerifyMemory", 0, 0, TestC_VerifyMemory)
    A_DEF_OPT("Call", 2, 3, 5, TestC_Call)
    A_DEF("Members", 1, 2, TestC_Members)
    A_DEF_OPT("CheckSyntax", 1, 2, 2, TestC_CheckSyntax)
    A_DEF("RaiseDirectTypeError", 0, 0, TestC_RaiseDirectTypeError)
    A_DEF("RaiseDirectMemoryError", 0, 0, TestC_RaiseDirectMemoryError)
    A_DEF("ContextDepth", 0, 0, TestC_ContextDepth)
    A_DEF("TempStackDepth", 0, 0, TestC_TempStackDepth)
    A_DEF("AGetInt", 1, 0, TestC_AGetInt)
    A_DEF("AGetIntU", 1, 0, TestC_AGetIntU)
    A_DEF("AGetInt64", 1, 0, TestC_AGetInt64)
    A_DEF("AGetIntU64", 1, 0, TestC_AGetIntU64)
    A_DEF("AMakeInt", 1, 0, TestC_AMakeInt)
    A_DEF("AMakeIntU", 1, 0, TestC_AMakeIntU)
    A_DEF("AMakeInt64", 1, 0, TestC_AMakeInt64)
    A_DEF("AMakeIntU64", 1, 0, TestC_AMakeIntU64)
    A_DEF("AGetFloat", 1, 0, TestC_AGetFloat)
    A_DEF("AGetStr", 1, 0, TestC_AGetStr)
    A_DEF("AGetStrW", 1, 0, TestC_AGetStrW)
    A_DEF("AGetStrUtf8", 1, 0, TestC_AGetStrUtf8)
    A_DEF("AMakeEmptyStr", 1, 0, TestC_AMakeEmptyStr)
    A_DEF("AMakeEmptyStrW", 1, 0, TestC_AMakeEmptyStrW)
    A_DEF("AMakeStrUtf8", 1, 0, TestC_AMakeStrUtf8)
    A_DEF("AStrLenUtf8", 1, 0, TestC_AStrLenUtf8)
    A_DEF("ASetArrayItem", 3, 0, TestC_ASetArrayItem)
    A_DEF("AMakeArray", 1, 0, TestC_AMakeArray)
    A_DEF_VARARG("AMakeTuple", 0, 0, 1, TestC_AMakeTuple)
    A_DEF("ATemps", 0, 0, TestC_ATemps)
    A_DEF("AIsValue", 2, 0, TestC_AIsValue)
    A_DEF("ACall", 2, 1, TestC_ACall)
    A_DEF("ACallVarArg", 3, 1, TestC_ACallVarArg)
    A_DEF("ACallValue", 2, 1, TestC_ACallValue)
    A_DEF("ACallValueVarArg", 3, 1, TestC_ACallValueVarArg)
    A_DEF("ARaise", 1, 0, TestC_ARaise)
    A_DEF("ARaiseByType", 2, 0, TestC_ARaiseByType)
    A_DEF("ARaiseValue", 1, 0, TestC_ARaiseValue)
    A_DEF("ARaiseErrnoIoError", 1, 0, TestC_ARaiseErrnoIoError)
    A_DEF("ATry", 1, 0, TestC_ATry)
    A_DEF("AIsExceptionType", 2, 1, TestC_AIsExceptionType)
    A_DEF_OPT("ACallMethod", 2, 5, 0, TestC_ACallMethod)
    A_DEF("ACallMethodVarArg", 3, 0, TestC_ACallMethodVarArg)
    A_DEF("AMember", 2, 0, TestC_AMember)
    A_DEF("ASetMember", 3, 0, TestC_ASetMember)
    A_DEF("ASuperMember", 2, 0, TestC_ASuperMember)
    A_DEF("ASetSuperMember", 3, 0, TestC_ASetSuperMember)
    A_DEF("AGlobal", 1, 0, TestC_AGlobal)
    A_DEF("AConcat", 2, 0, TestC_AConcat)
    A_DEF("ASubStr", 3, 0, TestC_ASubStr)
    A_DEF("AGetPair", 1, 0, TestC_AGetPair)
    A_DEF("AGetRange", 1, 0, TestC_AGetRange)
    A_DEF("AMakeRange", 2, 0, TestC_AMakeRange)
    A_DEF("AIsEq", 2, 0, TestC_AIsEq)
    A_DEF("AIsNeq", 2, 0, TestC_AIsNeq)
    A_DEF("AIsLt", 2, 0, TestC_AIsLt)
    A_DEF("AIsGt", 2, 0, TestC_AIsGt)
    A_DEF("AIsLte", 2, 0, TestC_AIsLte)
    A_DEF("AIsGte", 2, 0, TestC_AIsGte)
    A_DEF("ASubArray", 3, 0, TestC_ASubArray)
    A_DEF("ALen", 1, 0, TestC_ALen)
    A_DEF("AGetItemAt", 2, 0, TestC_AGetItemAt)
    A_DEF("ASetItemAt", 3, 0, TestC_ASetItemAt)
    A_DEF("AGetItem", 2, 0, TestC_AGetItem)
    A_DEF("ASetItem", 3, 0, TestC_ASetItem)
    A_DEF("AAdd", 2, 0, TestC_AAdd)
    A_DEF("ASub", 2, 0, TestC_ASub)
    A_DEF("AMul", 2, 0, TestC_AMul)
    A_DEF("ADiv", 2, 0, TestC_ADiv)
    A_DEF("AIntDiv", 2, 0, TestC_AIntDiv)
    A_DEF("AMod", 2, 0, TestC_AMod)
    A_DEF("APow", 2, 0, TestC_APow)
    A_DEF("ANeg", 1, 0, TestC_ANeg)
    A_DEF("AIn", 2, 0, TestC_AIn)
    A_DEF("GetCModuleImports", 1, 1, TestC_GetCModuleImports)
    A_DEF("AVersion", 0, 0, TestC_AVersion)
#ifdef HAVE_GETTIMEOFDAY
    A_DEF("Time", 0, 0, TestC_Time)
#endif
    A_DEF("InfiniteRecursionC", 0, 50, TestC_InfiniteRecursionC)
    A_DEF("SymbolInfoCount", 0, 0, TestC_SymbolInfoCount)
    A_DEF("GlobalValueCount", 0, 0, TestC_GlobalValueCount)
    A_DEF("TypeStats", 1, 1, TestC_TypeStats)
    A_DEF("AAllocContainer", 1, 0, TestC_AAllocContainer)
    A_DEF("AContainerPtr", 1, 0, TestC_AContainerPtr)
    A_DEF("AContainerValue", 1, 0, TestC_AContainerValue)
    A_DEF("ASetContainerValue", 2, 0, TestC_ASetContainerValue)
    
    A_EMPTY_CONST_P("ShortIntMax", &ShortIntMaxNum)
    A_EMPTY_CONST_P("ShortIntMin", &ShortIntMinNum)

    A_CLASS("BinaryData")
        A_BINARY_DATA_P(sizeof(int), &BinaryDataOffset1)
        A_METHOD("create", 0, 0, BinaryData1Create)
        A_METHOD("set", 1, 0, BinaryData1Set)
        A_METHOD("get", 0, 0, BinaryData1Get)
        A_METHOD("#i", 0, 0, BinaryData1Init)
    A_END_CLASS()
    
    A_CLASS("BinaryData2")
        A_INHERIT("::BinaryData")
        A_BINARY_DATA_P(2 * sizeof(int), &BinaryDataOffset2)

        A_VAR("x")
        A_VAR("y")
        A_VAR("z")
    
        A_METHOD("create", 0, 0, BinaryData2Create)
        A_METHOD("set2", 2, 0, BinaryData2Set)
        A_METHOD("get2", 0, 0, BinaryData2Get)
        A_METHOD("#i", 0, 0, BinaryData2Init)
    A_END_CLASS()

    A_CLASS("BinaryDataNonInit")
        A_BINARY_DATA(sizeof(int))
        A_METHOD("set", 1, 0, BinaryData1Set)
        A_METHOD("get", 0, 0, BinaryData1Get)
    A_END_CLASS()
    
    A_CLASS("RecursionClassC")
        A_METHOD("infiniteRecursion", 0, 50, RecursionClassCInfiniteRecursion)
    A_END_CLASS()
A_END_MODULE()
