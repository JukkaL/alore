/* std_wrappers.c - std module (wrapper methods)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementations of wrapper class methods. They represent methods of objects
   with special runtime representations.

   These methods get a raw object as self (frame[0]). To generate correct
   stack traces, the method should replace self with wrapped self (using
   AWrapObject) if the method can raise an exception. */

#include "alore.h"
#include "runtime.h"
#include "wrappers.h"
#include "memberid.h"
#include "std_module.h"
#include "int.h"
#include "str.h"
#include "float.h"


/* Wrap self and call func(t, self, frame[1]). Assume t and frame are defined.
   This only works for single-argument operations such as _add. The method
   argument should be the member id corresponding to the method. */
#define OP_WRAPPER(method, func) \
    do {                                                \
        frame[0] = AWrapObject(t, frame[0], (method));  \
        return (func)(t, A_UNWRAP(frame[0]), frame[1]); \
    } while (0)


/* Wrapper for the _add(x) method. */
AValue AAddWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_ADD, AAdd);
}


/* Wrapper for the _sub(x) method. */
AValue ASubWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_SUB, ASub);
}


/* Wrapper for the _mul(x) method. */
AValue AMulWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_MUL, AMul);
}


/* Wrapper for the _add(x) method. Unlike AAddWrapper, perform concatenation
   instead of addition (they only differ in efficiency). */
AValue AConcatWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_ADD, AConcat);
}


/* Wrapper for the _neg() method. */
AValue ANegWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_NEGATE);
    return ANeg(t, A_UNWRAP(frame[0]));
}


/* Wrapper for the _div(x) method. */
AValue ADivWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_DIV, ADiv);
}


/* Wrapper for the _idiv(x) method. */
AValue AIdivWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_IDIV, AIntDiv);
}


/* Wrapper for the _mod(x) method. */
AValue AModWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_MOD, AMod);
}


/* Wrapper for the _pow(x) method. */
AValue APowWrapper(AThread *t, AValue *frame)
{
    OP_WRAPPER(AM_POW, APow);
}


/* Wrapper for the _get(i) method. */
AValue AGetItemWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_GET_ITEM);
    return AGetItem(t, A_UNWRAP(frame[0]), frame[1]);
}


/* Wrapper for the _set(i, x) method. */
AValue ASetItemWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_SET_ITEM);
    return ASetItem(t, A_UNWRAP(frame[0]), frame[1], frame[2]);
}


/* Wrapper for the _eq(x) method. */
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


/* Wrapper for the _lt(x) method. */
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


/* Wrapper for the _gt(x) method. */
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


/* Wrapper for the _in(x) method. */
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


/* Wrapper for the _call method. */
/* NOTE: Requires 1 extra temp in the stack frame. */
AValue ACallWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_CALL);
    return ACallValueVarArg(t, A_UNWRAP(frame[0]), 0, frame + 1);
}


/* Wrapper for the _str() method. */
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


/* Wrapper for the _hash() method. */
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


/* Wrapper for the _repr() method. */
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


/* Wrapper for the iterator() method. */
AValue AIterWrapper(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], AM_ITERATOR);
    return AIterator(t, A_UNWRAP(frame[0]));
}


/* Wrapper for the _int() method. */
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


/* Wrapper for the _float() method. */
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


/* Wrapper for Range start getter. */
AValue AStdRangeStart(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.range.start;
}


/* Wrapper for Range stop getter. */
AValue AStdRangeStop(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.range.stop;
}


/* Pair methods */


/* Wrapper for Pair left getter. */
AValue APairLeft(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.pair.head;
}


/* Wrapper for Pair right getter. */
AValue APairRight(AThread *t, AValue *frame)
{
    frame[0] = AWrapObject(t, frame[0], -1);
    return AValueToMixedObject(A_UNWRAP(frame[0]))->data.pair.tail;
}


/* Misc methods */


/* A create method that should not be called. Simply raises an exception when
   called. */
AValue AHiddenCreate(AThread *t, AValue *frame)
{
    /* This should never be called, since a class with this method as the
       constructor cannot be subtyped and cannot be created like other
       classes. */
    return ARaiseValueError(t, NULL);
}
