/* util.h - Miscellaneous helper functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef UTIL_H_INCL
#define UTIL_H_INCL

#include "aconfig.h"

#include <stdarg.h>
#include "thread.h"


AValue ARaiseInternalError(void);


A_APIFUNC int AFormatMessage(char *msg, int maxLen, const char *fmt, ...);
A_APIFUNC int AFormatMessageVa(char *msg, int maxLen, const char *fmt,
                               va_list args);


int AEndsWith(const char *str, const char *prefix);
int AStartsWith(const char *str, const char *prefix);

#ifndef A_HAVE_WINDOWS
#define APathEndsWith AEndsWith
#else
int APathEndsWith(const char *str, const char *prefix);
#endif


#endif
