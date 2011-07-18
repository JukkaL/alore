/* errno_module.h - errno module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ERRNO_MODULE_H_INCL
#define ERRNO_MODULE_H_INCL

#include "alore.h"
#include "errno_info.h"


typedef struct {
    AErrnoId errnoId;
    int errnoValue;
    const char *message;
} AErrnoEntry;


extern AErrnoEntry AErrnoTable[];


AValue AErrnoToConstant(AThread *t, AValue value);
AValue AConstantToErrno(AThread *t, AValue value);


#endif
