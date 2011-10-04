/* std_type.c - std::Type related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"
#include "wrappers.h"
#include "runtime.h"
#include "class.h"


/* std::Type supertype()
   Return the immediate supertype of the type. */
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


/* std::Type interfaces()
   Return an array of all the implemented interfaces of the type. */
AValue ATypeInterfaces(AThread *t, AValue *frame)
{
    ATypeInfo *type;
    int i;

    /* Convert special types represented as functions to corresponding class
       objects. */
    if (AIsGlobalFunction(frame[0])) {
        ASymbolInfo *sym = AInternalTypeSymbol(
            AValueToFunction(frame[0])->sym);
        frame[0] = AGlobalByNum(sym->num);
    }

    /* Create a wrapper instance for the type object. */
    frame[0] = AWrapObject(t, frame[0]);

    frame[1] = AMakeArray(t, 0);

    /* Go though all types in the inheritance chain and collect the
       interfaces. */
    for (type = AValueToType(A_UNWRAP(frame[0]));
         type != NULL;
         type = type->super) {
        /* Go through all implemented interfaces for the current type. */
        for (i = 0; i < type->interfacesSize; i++) {
            ATypeInfo *iface = ATypeInterface(type, i);

            /* Add interface and all its superinterfaces. */
            do {
                AValue ifaceVal = ATypeToValue(iface);

                /* Add the interface to the array, but avoid duplicates. */
                if (AIn(t, ifaceVal, frame[1]) == 0)
                    AAppendArray(t, frame[1], ifaceVal);

                iface = iface->super;
            } while (iface != NULL);
        }
    }

    return frame[1];
}
