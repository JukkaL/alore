/* pack_module.c - __pack module (internal module used by pack)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/
   
#include "alore.h"


/* FIX: Assumes little endian host encoding. */



/* __pack::PackFloat32_LE(f) */
static AValue PackFloat32(AThread *t, AValue *frame)
{
    union {
        float f;
        unsigned char s[4];
    } floatUnion;
    int i;
        
    floatUnion.f = AGetFloat(t, frame[0]);
    frame[0] = AMakeEmptyStr(t, 4);
        
    for (i = 0; i < 4; i++)
        ASetStrItem(frame[0], i, floatUnion.s[i]);
            
    return frame[0];
}


/* __pack::PackFloat64_LE(f) */
static AValue PackFloat64(AThread *t, AValue *frame)
{
    union {
        double f;
        unsigned char s[8];
    } floatUnion;
    int i;
        
    floatUnion.f = AGetFloat(t, frame[0]);
    frame[0] = AMakeEmptyStr(t, 8);
        
    for (i = 0; i < 8; i++)
        ASetStrItem(frame[0], i, floatUnion.s[i]);
            
    return frame[0];
}


/* __pack::UnpackFloat32_LE(f) */
static AValue UnpackFloat32(AThread *t, AValue *frame)
{
    union {
        float f;
        unsigned char s[4];
    } floatUnion;
    int i;
    
    AExpectStr(t, frame[0]);
    if (AStrLen(frame[0]) != 4)
        return ARaiseValueError(t, "Str of length 4 expected");
    for (i = 0; i < 4; i++)
        floatUnion.s[i] = AStrItem(frame[0], i);
    return AMakeFloat(t, floatUnion.f);
}


/* __pack::UnpackFloat64_LE(f) */
static AValue UnpackFloat64(AThread *t, AValue *frame)
{
    union {
        double f;
        unsigned char s[8];
    } floatUnion;
    int i;
    
    AExpectStr(t, frame[0]);
    if (AStrLen(frame[0]) != 8)
        return ARaiseValueError(t, "Str of length 4 expected");
    for (i = 0; i < 8; i++)
        floatUnion.s[i] = AStrItem(frame[0], i);
    return AMakeFloat(t, floatUnion.f);
}


A_MODULE(__pack, "__pack")
  A_DEF("PackFloat32_LE", 1, 0, PackFloat32)
  A_DEF("PackFloat64_LE", 1, 0, PackFloat64)
  A_DEF("UnpackFloat32_LE", 1, 0, UnpackFloat32)
  A_DEF("UnpackFloat64_LE", 1, 0, UnpackFloat64)
A_END_MODULE()
