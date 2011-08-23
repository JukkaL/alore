/* errmsg.c - Exception error messages

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "errmsg.h"
#include "internal.h"


/* These exception error messages are declared in errmsg.h. Exception error
   messages that are used in multiple places should be defined here. Very
   specific ones can be given directly as literals. */


const char AMsgReadOnly[]  = "Attempted write with a read-only stream";
const char AMsgWriteOnly[] = "Attempted read with a write-only stream";
const char AMsgReadEOF[]   = "Read at end of file";

const char AMsgNotClassInstance[]          = "Not a class instance";
const char AMsgArrayIndexOutOfRange[]      = "Array index out of range";
const char AMsgStrIndexOutOfRange[]        = "String index out of range";
const char AMsgTooFewValuesToAssign[]      = "Too few values to assign";
const char AMsgTooManyValuesToAssign[]     = "Too many values to assign";
const char AMsgDivisionByZero[]            = "Division or modulus by zero";
const char AMsgOutOfRange[]                = "Out of range";
const char AMsgNegativeValueNotExpected[]  = "Negative value not expected";
const char AMsgCallInterface[]             = "Attempted to call an interface";
const char AMsgSliceIndexMustBeIntOrNil[]  =
    "Slice index neither Int nor nil";
const char AMsgTypeIsNotCallable[] = "Type \"%q\" is not callable";

const char AMsgIntExpected[]    = "Int expected";
const char AMsgIntExpectedBut[] = "Int expected (but %T found)";
const char AMsgStrExpected[]    = "Str expected";
const char AMsgStrExpectedBut[] = "Str expected (but %T found)";
const char AMsgChrExpected[]   = "Str of length 1 expected";
const char AMsgArrayExpected[] = "Array expected";
const char AMsgFloatExpected[] = "Float expected";
const char AMsgBooleanExpectedBut[] = "Boolean expected (but %T found)";
const char AMsgTypeExpected[]  = "Type expected";
const char AMsgPairExpected[]  = "Pair expected";
const char AMsgRangeExpected[] = "Range expected";

const char AMsgStrIndexedWithNonInteger[] =
    "Str indexed with non-integer (%T)";
const char AMsgInvalidRangeLowerBound[] = "Non-integer Range lower bound (%T)";
const char AMsgInvalidRangeUpperBound[] = "Non-integer Range upper bound (%T)";

const char AMsgKeyboardInterrupt[] = "Keyboard interrupt";

const char AMsgCreateCalled[] = "create called after construction";

const char AMsgTooManyRecursiveCalls[] = "Too many recursive calls";
