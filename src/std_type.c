/* std_type.c - std::Type related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"
#include "wrappers.h"
#include "runtime.h"


AValue ATypeSupertype(AThread *t, AValue *frame)
{
    ATypeInfo *type;

    /* Type objects of primitive types are physically function objects. They
       all inherit from Object. */
    if (AIsGlobalFunction(frame[0]))
        return AGlobalByNum(AStdObjectNum);

    /* Create a wrapper instance for the type object. */
    frame[0] = AWrapObject(t, frame[0]);

    /* Examine the base type. */
    type = AValueToType(A_UNWRAP(frame[0]));
    if (type->super != NULL)
        return ATypeToValue(type->super);
    else
        return ANil;
}
