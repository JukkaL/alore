/* dummy_module.c - __dummy module (for testing only)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementation of module __dummy that can be used to test dynamic loading
   of modules. It contains no otherwise useful functionality. */

#include "alore.h"


static AValue Dummy(AThread *t, AValue *frame)
{
    return AMakeStr(t, "Dummy string");
}


A_MODULE(__dummy, "__dummy")
    A_DEF("Dummy", 0, 0, Dummy)
    A_VAR("DummyVar")
    A_CLASS("DummyClass")
        A_METHOD("dummyMethod", 0, 0, Dummy)
    A_END_CLASS()
A_END_MODULE()
