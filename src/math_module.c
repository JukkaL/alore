/* math_module.c - math module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "int.h"

#include <math.h>


/* Global numbers of constants Pi and E. */
static int PiNum;
static int ENum;


/* math::Sin(x) */
static AValue MathSin(AThread *t, AValue *frame)
{
    return AMakeFloat(t, sin(AGetFloat(t, frame[0])));
}


/* math::Cos(x) */
static AValue MathCos(AThread *t, AValue *frame)
{
    return AMakeFloat(t, cos(AGetFloat(t, frame[0])));
}


/* math::Tan(x) */
static AValue MathTan(AThread *t, AValue *frame)
{
    return AMakeFloat(t, tan(AGetFloat(t, frame[0])));
}


/* math::ArcSin(x) */
static AValue MathArcSin(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg < -1.0 || arg > 1.0)
        return ARaiseValueError(t, "Argument ouf of range");
    return AMakeFloat(t, asin(arg));
}


/* math::ArcCos(x) */
static AValue MathArcCos(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg < -1.0 || arg > 1.0)
        return ARaiseValueError(t, "Argument out of range");
    return AMakeFloat(t, acos(arg));
}


/* math::ArcTan(x[, y]) */
static AValue MathArcTan(AThread *t, AValue *frame)
{
    double arg2;
    double arg = AGetFloat(t, frame[0]);
    
    if (AIsDefault(frame[1]))
        return AMakeFloat(t, atan(arg));
    
    arg2 = AGetFloat(t, frame[1]);
    if (arg == 0.0 && arg2 == 0.0)
        return ARaiseValueError(t, "Invalid argument values");
    else
        return AMakeFloat(t, atan2(arg, arg2));
}


/* math::Ceil(x) */
static AValue MathCeil(AThread *t, AValue *frame)
{
    return AMakeFloat(t, ceil(AGetFloat(t, frame[0])));
}


/* math::Floor(x) */
static AValue MathFloor(AThread *t, AValue *frame)
{
    return AMakeFloat(t, floor(AGetFloat(t, frame[0])));
}


/* math::Round(x) */
static AValue MathRound(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg >= 0)
        arg = floor(arg + 0.5);
    else
        arg = ceil(arg - 0.5);
    return AMakeFloat(t, arg);
}


/* math::Trunc(x) */
static AValue MathTrunc(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg >= 0)
        arg = floor(arg);
    else
        arg = ceil(arg);
    return AMakeFloat(t, arg);
}


/* math::Exp(x) */
static AValue MathExp(AThread *t, AValue *frame)
{
    return AMakeFloat(t, exp(AGetFloat(t, frame[0])));
}


/* math::Log(x) */
static AValue MathLog(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg <= 0.0)
        return ARaiseValueError(t, "Argument out of range");
    return AMakeFloat(t, log(arg));
}


/* math::Sqrt(x) */
static AValue MathSqrt(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    if (arg < 0.0)
        return ARaiseValueError(t, "Argument out of range");
    return AMakeFloat(t, sqrt(arg));
}


/* math::IsInf(x) */
static AValue MathIsInf(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    return AIsInf(arg) ? ATrue : AFalse;
}


/* math::IsNaN(x) */
static AValue MathIsNaN(AThread *t, AValue *frame)
{
    double arg = AGetFloat(t, frame[0]);
    return AIsNaN(arg) ? ATrue : AFalse;
}


/* math::Main() */
static AValue MathMain(AThread *t, AValue *frame)
{
    /* Initialize constants. */
    ASetConstGlobalByNum(t, PiNum, AMakeFloat(t, M_PI));
    ASetConstGlobalByNum(t, ENum, AMakeFloat(t, M_E));
    return ANil;
}


A_MODULE(math, "math")
    A_DEF(A_PRIVATE("Main"), 0, 5, MathMain)
    A_DEF("Sin", 1, 0, MathSin)
    A_DEF("Cos", 1, 0, MathCos)
    A_DEF("Tan", 1, 0, MathTan)
    A_DEF("ArcSin", 1, 0, MathArcSin)
    A_DEF("ArcCos", 1, 0, MathArcCos)
    A_DEF_OPT("ArcTan", 1, 2, 0, MathArcTan)
    A_DEF("Ceil", 1, 0, MathCeil)
    A_DEF("Floor", 1, 0, MathFloor)
    A_DEF("Round", 1, 0, MathRound)
    A_DEF("Trunc", 1, 0, MathTrunc)
    A_DEF("Exp", 1, 0, MathExp)
    A_DEF("Log", 1, 0, MathLog)
    A_DEF("Sqrt", 1, 0, MathSqrt)
    A_DEF("IsInf", 1, 0, MathIsInf)
    A_DEF("IsNaN", 1, 0, MathIsNaN)
    A_EMPTY_CONST_P("Pi", &PiNum)
    A_EMPTY_CONST_P("E", &ENum)
A_END_MODULE()
