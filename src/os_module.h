/* os_module.h -

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef OS_MODULE_H_INCL
#define OS_MODULE_H_INCL

#include "thread.h"
#include "util.h"


AValue AOsRemove(AThread *t, AValue *frame);
AValue AOsRename(AThread *t, AValue *frame);
AValue AOsMakeDir(AThread *t, AValue *frame);
AValue AOsChangeDir(AThread *t, AValue *frame);
AValue AOsCurrentDir(AThread *t, AValue *frame);
AValue AOsMakeSymLink(AThread *t, AValue *frame);
AValue AOsReadSymLink(AThread *t, AValue *frame);
AValue AOsSleep(AThread *t, AValue *frame);
AValue AOsSystem(AThread *t, AValue *frame);
AValue AOsExists(AThread *t, AValue *frame);
AValue AOsIsFile(AThread *t, AValue *frame);
AValue AOsIsDir(AThread *t, AValue *frame);
AValue AOsIsLink(AThread *t, AValue *frame);
AValue AStatCreate(AThread *t, AValue *frame);
AValue AStatModificationTime(AThread *t, AValue *frame);
AValue AStatAccessTime(AThread *t, AValue *frame);
AValue AStatIsReadable(AThread *t, AValue *frame);
AValue AStatIsWritable(AThread *t, AValue *frame);
AValue AStatOwner(AThread *t, AValue *frame);
AValue AStatOwnerPerm(AThread *t, AValue *frame);
AValue AStatOtherPerm(AThread *t, AValue *frame);
AValue AStatGroupPerm(AThread *t, AValue *frame);
AValue AOsListDir(AThread *t, AValue *frame);
AValue AOsWalkDir(AThread *t, AValue *frame);
AValue AOsGetEnv(AThread *t, AValue *frame);
AValue AOsSetEnv(AThread *t, AValue *frame);
AValue AListEnv(AThread *t, AValue *frame);
AValue AOsSetPerm(AThread *t, AValue *frame);
AValue ASetModificationTime(AThread *t, AValue *frame);
AValue ASetAccessTime(AThread *t, AValue *frame);
AValue AOsUser(AThread *t, AValue *frame);


/* Platform-specific internal functions */
AValue AOsGetpwnam(AThread *t, AValue *frame);
AValue AOsGetFullPathName(AThread *t, AValue *frame);


#endif
