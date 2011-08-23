/* errmsg.h - Exception error messages

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ERRMSG_H_INCL
#define ERRMSG_H_INCL

#include "aconfig.h"


extern const char AMsgReadOnly[];
extern const char AMsgWriteOnly[];
extern const char AMsgReadEOF[];

extern const char AMsgNotClassInstance[];
extern const char AMsgArrayIndexOutOfRange[];
extern const char AMsgStrIndexOutOfRange[];
extern const char AMsgTooFewValuesToAssign[];
extern const char AMsgTooManyValuesToAssign[];
extern const char AMsgDivisionByZero[];
extern const char AMsgOutOfRange[];
extern const char AMsgNegativeValueNotExpected[];
extern const char AMsgCallInterface[];
extern const char AMsgSliceIndexMustBeIntOrNil[];
extern const char AMsgTypeIsNotCallable[];

A_APIVAR extern const char AMsgIntExpected[];
A_APIVAR extern const char AMsgIntExpectedBut[];
A_APIVAR extern const char AMsgStrExpected[];
A_APIVAR extern const char AMsgStrExpectedBut[];
extern const char AMsgChrExpected[];
A_APIVAR extern const char AMsgArrayExpected[];
A_APIVAR extern const char AMsgFloatExpected[];
extern const char AMsgBooleanExpectedBut[];
extern const char AMsgTypeExpected[];
extern const char AMsgPairExpected[];
extern const char AMsgRangeExpected[];

extern const char AMsgStrIndexedWithNonInteger[];
extern const char AMsgInvalidRangeLowerBound[];
extern const char AMsgInvalidRangeUpperBound[];

extern const char AMsgKeyboardInterrupt[];

extern const char AMsgCreateCalled[];

extern const char AMsgTooManyRecursiveCalls[];


#endif
