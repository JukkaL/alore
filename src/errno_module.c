/* errno_module.c - errno module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"

#include <errno.h>
#include "alore.h"
#include "symtable.h"
#include "errno_module.h"
#include "errno_info.h"
#include "util.h"


/* NOTE: The automatically-generated file errno_info.c contains a mapping
         between errno constants, errno numbers and error messages. */


/* Global nums of errno constants (indexed by AErrnoId) */
static int ErrnoNums[A_ERRNO_LAST + 1];


static AValue ErrnoConstFromId(int id);
static int LookupErrnoTable(int errnoValue);


/* errno::ErrnoToCode(errno)
   Convert an errno number to an errno symbolic constant. */
static AValue ErrnoToCode(AThread *t, AValue *frame)
{
    AValue result = AErrnoToConstant(t, frame[0]);
    if (AIsNil(result))
        return ARaiseValueError(t, "No matching code");
    else
        return result;        
}


/* errno::CodeToErrno(code)
   Convert an errno symbolic constant to an errno number. */
static AValue CodeToErrno(AThread *t, AValue *frame)
{
    AValue result = AConstantToErrno(t, frame[0]);
    if (AIsZero(result))
        return ARaiseValueError(t, "No matching number");
    else
        return result;
}


/* errno::ErrorStr(code)
   Return a string description of an errno symbolic constant. */
static AValue ErrorStr(AThread *t, AValue *frame)
{
    AValue value = AConstantToErrno(t, frame[0]);
    int errnoValue = AGetInt(t, value);
    int i = LookupErrnoTable(errnoValue);
    if (i < 0)
        return ANil;
    else
        return AMakeStr(t, AErrnoTable[i].message);
}


/* Convert an errno number to a symbolic constant. Return ANil if no match
   found. Raise a direct exception on error conditions. */
AValue AErrnoToConstant(AThread *t, AValue value)
{
    int errnoValue = AGetInt(t, value);
    int i;

    if (errnoValue == 0)
        return ARaiseValueError(t, "Errno must be non-zero");

    for (i = 0; AErrnoTable[i].errnoId != 0; i++) {
        if (AErrnoTable[i].errnoValue == errnoValue)
            return ErrnoConstFromId(AErrnoTable[i].errnoId);
    }

    return ANil;    
}


/* Convert an errno symbolic const to a an errno number. Always raise a direct
   exception. */
AValue AConstantToErrno(AThread *t, AValue value)
{    
    int i;
    int errnoId;
    
    if (!AIsConstant(value))
        return ARaiseTypeError(t, "Constant expected");

    /* Figure out the index of the const definition. */
    errnoId = 0;
    for (i = 1; i <= A_ERRNO_LAST; i++) {
        if (ErrnoNums[i] == AValueToConstant(value)->sym->num) {
            errnoId = i;
            break;
        }
    }

    if (errnoId == 0)
        return AZero;

    /* Find the corresponding errno value from the errno table. It can be
       0; this means that the value is not defined for the current operating
       system.*/
    for (i = 0; AErrnoTable[i].errnoId != 0; i++) {
        if (AErrnoTable[i].errnoId == errnoId)
            return AMakeInt(t, AErrnoTable[i].errnoValue);
    }
    
    return AZero;
}


/* Return errno constant related an errno value described by an AErrnoId
   value. */
static AValue ErrnoConstFromId(int id)
{
    return AGlobalByNum(ErrnoNums[id]);
}


/* Find the index of an AErrnoTable entry which matches the given errno
   value. */
static int LookupErrnoTable(int errnoValue)
{
    int i;

    if (errnoValue == 0)
        return -1;
    
    for (i = 0; AErrnoTable[i].errnoId != 0; i++) {
        if (AErrnoTable[i].errnoValue == errnoValue)
            return i;
    }

    return -1;
}


/* Raise an errno-related exception. The exception type is specified by
   classNum; assume that the constructor is compatible with std::IoError.
   The path (if != NULL) is included in the message.

   The format of the message can be one of the following:
   
     Permission denied [EACCESS]
     dir/file.ext: Permission denied [EACCESS]
     [Errno 56]                                      (if unknown errno)
     dir/file.ext [Errno 56]                         (if unknown errno)
*/
AValue ARaiseErrnoExceptionByNum(AThread *t, int classNum, const char *path)
{
    int i;
    int errnoValue;
    const char *msgBase;
    ASymbolInfo *sym;
    char msg[1024];
    AValue *frame;
    AValue ret;

    msgBase = NULL;
    sym = NULL;
    errnoValue = errno;

    i = LookupErrnoTable(errnoValue);
    if (i >= 0) {
        /* The errno was recognized; it is included in AErrnoTable. */
        int num;
        
        msgBase = AErrnoTable[i].message;
        
        num = ErrnoNums[AErrnoTable[i].errnoId];
        sym = AValueToConstant(AGlobalByNum(num))->sym;
    }

    if (path == NULL) {
        /* No path */
        if (msgBase != NULL && sym != NULL)
            AFormatMessage(msg, sizeof(msg), "%s [%i]", msgBase, sym);
        else
            AFormatMessage(msg, sizeof(msg), "[Errno %d]", errnoValue);
    } else {
        /* Path given */
        if (msgBase != NULL && sym != NULL)
            AFormatMessage(msg, sizeof(msg), "%s: %s [%i]", path, msgBase,
                           sym);
        else
            AFormatMessage(msg, sizeof(msg), "%s [Errno %d]", path,
                           errnoValue);
    }

    frame = AAllocTemps(t, 3);
    frame[0] = AMakeStr(t, msg);
    frame[1] = AMakeInt(t, errnoValue);
    ret = ACallValue(t, AGlobalByNum(classNum), 2, frame);
    AFreeTemps(t, 3);

    if (AIsError(ret))
        return ret;
    else        
        return ARaiseValue(t, ret);
}


A_MODULE(errno, "errno")
    A_DEF("ErrnoToCode", 1, 0, ErrnoToCode)
    A_DEF("CodeToErrno", 1, 0, CodeToErrno)
    A_DEF("ErrorStr", 1, 0, ErrorStr)

    /* The following definitions were generated using
       "util/errno.alo --module". DO NOT MODIFY BY HAND! */
    A_SYMBOLIC_CONST_P("E2BIG", ErrnoNums + 1)
    A_SYMBOLIC_CONST_P("EACCES", ErrnoNums + 2)
    A_SYMBOLIC_CONST_P("EADDRINUSE", ErrnoNums + 3)
    A_SYMBOLIC_CONST_P("EADDRNOTAVAIL", ErrnoNums + 4)
    A_SYMBOLIC_CONST_P("EAFNOSUPPORT", ErrnoNums + 5)
    A_SYMBOLIC_CONST_P("EAGAIN", ErrnoNums + 6)
    A_SYMBOLIC_CONST_P("EBADF", ErrnoNums + 7)
    A_SYMBOLIC_CONST_P("EBADFD", ErrnoNums + 8)
    A_SYMBOLIC_CONST_P("EBUSY", ErrnoNums + 9)
    A_SYMBOLIC_CONST_P("ECANCELED", ErrnoNums + 10)
    A_SYMBOLIC_CONST_P("ECHILD", ErrnoNums + 11)
    A_SYMBOLIC_CONST_P("ECOMM", ErrnoNums + 12)
    A_SYMBOLIC_CONST_P("ECONNABORTED", ErrnoNums + 13)
    A_SYMBOLIC_CONST_P("ECONNREFUSED", ErrnoNums + 14)
    A_SYMBOLIC_CONST_P("EDEADLK", ErrnoNums + 15)
    A_SYMBOLIC_CONST_P("EDOM", ErrnoNums + 16)
    A_SYMBOLIC_CONST_P("EEXIST", ErrnoNums + 17)
    A_SYMBOLIC_CONST_P("EFAULT", ErrnoNums + 18)
    A_SYMBOLIC_CONST_P("EFBIG", ErrnoNums + 19)
    A_SYMBOLIC_CONST_P("EHOSTDOWN", ErrnoNums + 20)
    A_SYMBOLIC_CONST_P("EHOSTUNREACH", ErrnoNums + 21)
    A_SYMBOLIC_CONST_P("EIDRM", ErrnoNums + 22)
    A_SYMBOLIC_CONST_P("EILSEQ", ErrnoNums + 23)
    A_SYMBOLIC_CONST_P("EINPROGRESS", ErrnoNums + 24)
    A_SYMBOLIC_CONST_P("EINTR", ErrnoNums + 25)
    A_SYMBOLIC_CONST_P("EINVAL", ErrnoNums + 26)
    A_SYMBOLIC_CONST_P("EIO", ErrnoNums + 27)
    A_SYMBOLIC_CONST_P("EISCONN", ErrnoNums + 28)
    A_SYMBOLIC_CONST_P("EISDIR", ErrnoNums + 29)
    A_SYMBOLIC_CONST_P("ELOOP", ErrnoNums + 30)
    A_SYMBOLIC_CONST_P("EMFILE", ErrnoNums + 31)
    A_SYMBOLIC_CONST_P("EMLINK", ErrnoNums + 32)
    A_SYMBOLIC_CONST_P("EMSGSIZE", ErrnoNums + 33)
    A_SYMBOLIC_CONST_P("ENAMETOOLONG", ErrnoNums + 34)
    A_SYMBOLIC_CONST_P("ENETDOWN", ErrnoNums + 35)
    A_SYMBOLIC_CONST_P("ENETUNREACH", ErrnoNums + 36)
    A_SYMBOLIC_CONST_P("ENFILE", ErrnoNums + 37)
    A_SYMBOLIC_CONST_P("ENODEV", ErrnoNums + 38)
    A_SYMBOLIC_CONST_P("ENOENT", ErrnoNums + 39)
    A_SYMBOLIC_CONST_P("ENOEXEC", ErrnoNums + 40)
    A_SYMBOLIC_CONST_P("ENOLCK", ErrnoNums + 41)
    A_SYMBOLIC_CONST_P("ENOLINK", ErrnoNums + 42)
    A_SYMBOLIC_CONST_P("ENOMEM", ErrnoNums + 43)
    A_SYMBOLIC_CONST_P("ENOMSG", ErrnoNums + 44)
    A_SYMBOLIC_CONST_P("ENONET", ErrnoNums + 45)
    A_SYMBOLIC_CONST_P("ENOPROTOOPT", ErrnoNums + 46)
    A_SYMBOLIC_CONST_P("ENOSPC", ErrnoNums + 47)
    A_SYMBOLIC_CONST_P("ENOSR", ErrnoNums + 48)
    A_SYMBOLIC_CONST_P("ENOSTR", ErrnoNums + 49)
    A_SYMBOLIC_CONST_P("ENOSYS", ErrnoNums + 50)
    A_SYMBOLIC_CONST_P("ENOTCONN", ErrnoNums + 51)
    A_SYMBOLIC_CONST_P("ENOTDIR", ErrnoNums + 52)
    A_SYMBOLIC_CONST_P("ENOTEMPTY", ErrnoNums + 53)
    A_SYMBOLIC_CONST_P("ENOTSOCK", ErrnoNums + 54)
    A_SYMBOLIC_CONST_P("ENOTSUP", ErrnoNums + 55)
    A_SYMBOLIC_CONST_P("ENOTTY", ErrnoNums + 56)
    A_SYMBOLIC_CONST_P("ENOTUNIQ", ErrnoNums + 57)
    A_SYMBOLIC_CONST_P("ENXIO", ErrnoNums + 58)
    A_SYMBOLIC_CONST_P("EOPNOTSUPP", ErrnoNums + 59)
    A_SYMBOLIC_CONST_P("EOVERFLOW", ErrnoNums + 60)
    A_SYMBOLIC_CONST_P("EPERM", ErrnoNums + 61)
    A_SYMBOLIC_CONST_P("EPIPE", ErrnoNums + 62)
    A_SYMBOLIC_CONST_P("EPROTO", ErrnoNums + 63)
    A_SYMBOLIC_CONST_P("EPROTONOSUPPORT", ErrnoNums + 64)
    A_SYMBOLIC_CONST_P("EPROTOTYPE", ErrnoNums + 65)
    A_SYMBOLIC_CONST_P("ERANGE", ErrnoNums + 66)
    A_SYMBOLIC_CONST_P("ERESTART", ErrnoNums + 67)
    A_SYMBOLIC_CONST_P("EROFS", ErrnoNums + 68)
    A_SYMBOLIC_CONST_P("ESPIPE", ErrnoNums + 69)
    A_SYMBOLIC_CONST_P("ESRCH", ErrnoNums + 70)
    A_SYMBOLIC_CONST_P("ESTALE", ErrnoNums + 71)
    A_SYMBOLIC_CONST_P("ETIME", ErrnoNums + 72)
    A_SYMBOLIC_CONST_P("ETIMEDOUT", ErrnoNums + 73)
    A_SYMBOLIC_CONST_P("ETXTBSY", ErrnoNums + 74)
    A_SYMBOLIC_CONST_P("EWOULDBLOCK", ErrnoNums + 75)
    A_SYMBOLIC_CONST_P("EXDEV", ErrnoNums + 76)
A_END_MODULE()
