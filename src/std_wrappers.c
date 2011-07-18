/* std_wrappers.c - std module (wrapper methods)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementations of wrapper class methods. They represent methods of built-in
   types. */

#include "alore.h"
#include "runtime.h"
#include "wrappers.h"
#include "memberid.h"
#include "std_module.h"
#include "int.h"
#include "str.h"
#include "float.h"


#define OP_WRAPPER(method, func) \
    do {                                                \
        frame[0] = AWrapObject(t, frame[0], (method));  \
        return (func)(t, A_UNWRAP(frame[0]), frame[1]); \
    } while (0)


AValue AAddWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_ADD, AAdd);
}


AValue ASubWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_SUB, ASub);
}


AValue AMulWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_MUL, AMul);
}


AValue AConcatWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_ADD, AConcat);
}


AValue ANegWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_NEGATE);
    return ANeg(t, A_UNWRAP(frame[0]));
}


AValue ADivWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_DIV, ADiv);
}


AValue AIdivWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_IDIV, AIntDiv);
}


AValue AModWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_MOD, AMod);
}


AValue APowWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_POW, APow);
}


AValue AGetItemWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_GET_ITEM);
    return AGetItem(t, A_UNWRAP(frame[0]), frame[1]);
}


AValue ASetItemWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_SET_ITEM);
    return ASetItem(t, A_UNWRAP(frame[0]), frame[1], frame[2]);
}


AValue AEqWrapper(AThread *t, AValue *frame)
{
    int ret;
    frame[0] = AWrapObject(t, frame[0], AM_EQ);
    ret = AIsEq(t, A_UNWRAP(frame[0]), frame[1]);
    if (ret < 0)
        return AError;
    else if (ret == 0)
        return AFalse;
    else
        return ATrue;
}


AValue ALtWrapper(AThread *t, AValue *frame)
{
    int ret;
    frame[0] = AWrapObject(t, frame[0], AM_LT);
    ret = AIsLt(t, A_UNWRAP(frame[0]), frame[1]);
    if (ret < 0)
        return AError;
    else if (ret == 0)
        return AFalse;
    else
        return ATrue;
}


AValue AGtWrapper(AThread *t, AValue *frame)
{
    int ret;
    frame[0] = AWrapObject(t, frame[0], AM_GT);
    ret = AIsGt(t, A_UNWRAP(frame[0]), frame[1]);
    if (ret < 0)
        return AError;
    else if (ret == 0)
        return AFalse;
    else
        return ATrue;
}


AValue AInWrapper(AThread *t, AValue *frame)
{
    int val;
    frame[0] = AWrapObject(t, frame[0], AM_IN);
    val = AIn(t, frame[1], A_UNWRAP(frame[0]));
    if (val == 0)
        return AFalse;
    else if (val == 1)
        return ATrue;
    else
        return AError;    
}


/* NOTE: Requires 1 extra temp in the stack frame. */
AValue ACallWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_CALL);
    return ACallValueVarArg(t, A_UNWRAP(frame[0]), 0, frame + 1);
}


AValue AStrWrapper(AThread *t, AValue *frame)
{
    AValue *args;
    AValue v;
    
    frame[0] = AWrapObject(t, frame[0], AM__STR);
    
    args = AAllocTemp(t, A_UNWRAP(frame[0]));
    v = AStdStr(t, args);
    AFreeTemp(t);

    return v;
}


AValue AHashWrapper(AThread *t, AValue *frame)
{
    AValue *args;
    AValue v;
    
    frame[0] = AWrapObject(t, frame[0], AM__HASH);
    
    args = AAllocTemps(t, 3);
    args[0] = A_UNWRAP(frame[0]);
    v = AStdHash(t, args);
    AFreeTemps(t, 3);

    return v;
}


AValue AReprWrapper(AThread *t, AValue *frame)
{
    AValue *args;
    AValue v;
    
    frame[0] = AWrapObject(t, frame[0], AM__REPR);
    
    args = AAllocTemp(t, A_UNWRAP(frame[0]));
    v = AStdRepr(t, args);
    AFreeTemp(t);

    return v;
}


AValue AIterWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_ITERATOR);
    return AIterator(t, A_UNWRAP(frame[0]));
}


AValue AIntWrapper(AThread *t, AValue *frame)
{
    AValue *args;
    AValue v;
    
    frame[0] = AWrapObject(t, frame[0], AM__INT);
    
    args = AAllocTemps(t, 2);
    args[0] = A_UNWRAP(frame[0]);
    args[1] = ADefault;
    v = AStdInt(t, args);
    AFreeTemps(t, 2);

    return v;
}


AValue AFloatWrapper(AThread *t, AValue *frame)
{
    AValue *args;
    AValue v;
    
    frame[0] = AWrapObject(t, frame[0], AM__FLOAT);
    
    args = AAllocTemp(t, A_UNWRAP(frame[0]));
    v = AStdFloat(t, args);
    AFreeTemp(t);

    return v;
}


/* Range methods */

AValue AStdRangeStart(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.range.start;
}


AValue AStdRangeStop(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.range.stop;
}


/* Pair methods */

AValue APairLeft(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.pair.head;
}


AValue APairRight(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.pair.tail;
}


/* Misc methods */

#if 0
AValue IntCreate(AThread *t, AValue *frame)
{
    if (!AIsInt(frame[1]))
        return ARaiseTypeError(t, NULL);

    if (AMemberDirect(frame[0], 0) != ANil)
        return ARaiseValueError(t, AMsgCreateCalled);
    
    ASetMemberDirect(t, frame[0], 0, frame[1]);
    
    return frame[0];
}


AValue FloatCreate(AThread *t, AValue *frame)
{
    if (!AIsFloat(frame[1]))
        return ARaiseTypeError(t, NULL);

    if (AMemberDirect(frame[0], 0) != ANil)
        return ARaiseValueError(t, AMsgCreateCalled);
    
    ASetMemberDirect(t, frame[0], 0, frame[1]);
    
    return frame[0];
}
#endif


AValue AHiddenCreate(AThread *t, AValue *frame)
{
    /* This should never be called, since a class with this method as the
       constructor cannot be subtyped and cannot be created like other
       classes. */
    return ARaiseValueError(t, NULL);
}
