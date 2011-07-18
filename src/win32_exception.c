/* win32_exception.c - Windows specific error handling functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"

#ifdef A_HAVE_WINDOWS

#include <windows.h>


AValue ARaiseWin32Exception(AThread *t, int classNum)
{
    const char *msg;
    DWORD errNum;
    char buf[100];

    errNum = GetLastError();

    switch (errNum) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_OPEN_FAILED:
        msg = "No such file or directory";
        break;
    case ERROR_TOO_MANY_OPEN_FILES:
        msg = "Too many open files";
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_CURRENT_DIRECTORY:
    case ERROR_WRITE_PROTECT:
        msg = "Permission denied";
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        msg = "Not enough space";
        break;
    case ERROR_INVALID_DATA:
        msg = "Invalid data";
        break;
    case ERROR_NOT_SAME_DEVICE:
        msg = "Not same device";
        break;
    case ERROR_NOT_READY:
    case ERROR_CRC:
    case ERROR_NOT_DOS_DISK:
    case ERROR_SECTOR_NOT_FOUND:
    case ERROR_WRITE_FAULT:
    case ERROR_READ_FAULT:
        msg = "Input/output error";
        break;        
    case ERROR_BAD_LENGTH:
    case ERROR_INVALID_PARAMETER:
        msg = "Invalid argument";
        break;
    case ERROR_SEEK:
        msg = "Invalid seek";
        break;
    case ERROR_GEN_FAILURE:
        msg = "General failure";
        break;
    case ERROR_SHARING_VIOLATION:
        msg = "Sharing violation";
        break;
    case ERROR_LOCK_VIOLATION:
        msg = "Lock violation";
        break;
    case ERROR_WRONG_DISK:
        msg = "Invalid disk";
        break;
    case ERROR_SHARING_BUFFER_EXCEEDED:
        msg = "Resource overflow";
        break;        
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:
        msg = "No space left on device";
        break;        
    case ERROR_NOT_SUPPORTED:
        msg = "Not supported";
        break;        
    case ERROR_REM_NOT_LIST:
    case ERROR_BAD_NETPATH:
    case ERROR_NETWORK_BUSY:
        msg = "Network error";
        break;
    case ERROR_FILE_EXISTS:
        msg = "File exists";
        break;
    case ERROR_CANNOT_MAKE:
        msg = "Cannot create";
        break;
    case ERROR_BROKEN_PIPE:
        msg = "Broken pipe";
        break;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        msg = "Invalid path";
        break;
    case ERROR_DIR_NOT_EMPTY:
        msg = "Directory not empty";
        break;
    case ERROR_PATH_BUSY:
    case ERROR_BUSY:
        msg = "Resource busy";
        break;
    case ERROR_ALREADY_EXISTS:
        msg = "File exists";
        break;        
    default:
        sprintf(buf, "Error %d", (int)errNum);
        msg = buf;
    }

    /* FIX: See the comment in RaiseErrnoException! */

    return ARaiseByNum(t, classNum, msg);
}


typedef struct {
    int errNum;
    const char *message;
} WinsockErrorEntry;


static WinsockErrorEntry ErrorMessages[] = {
    { WSAEADDRINUSE, "Address in use" },
    { WSAEADDRNOTAVAIL, "Local address not available" },
    { WSAEBADF, "Bad file descriptor" },
    { WSAECONNABORTED, "Connection aborted" },
    { WSAECONNREFUSED, "Connection refused" },
    { WSAECONNRESET, "Connection reset by peer" },
    { WSAEFAULT, "Bad address" },
    { WSAEHOSTDOWN, "Host is down" },
    { WSAEHOSTUNREACH, "Host unreachable" },
    { WSAEINPROGRESS, "Operation in progress" },
    { WSAEINTR, "Interrupted function call" },
    { WSAEINVAL, "Invalid argument" },
    { WSAEISCONN, "Socket is already connected" },
    { WSAENETDOWN, "Network is down" },
    { WSAENETRESET, "Network dropped connection on reset" },
    { WSAENETUNREACH, "Netword unreachable" },
    { WSAENOPROTOOPT, "Protocol not available" },
    { WSAENOTCONN, "Socket is not connected" },
    { WSAENOTSOCK, "Not a socket" },
    { WSAEOPNOTSUPP, "Operation not supported" },
    { WSAEPFNOSUPPORT, "Protocol family not supported" },
    { WSAEPROTONOSUPPORT, "Protocol not supported" },
    { WSAEPROTOTYPE, "Protocol wrong type for socket" },
    { WSAESHUTDOWN, "Can't send after socket shutdown" },
    { WSAESOCKTNOSUPPORT, "Socket type not supported" },
    { WSAETIMEDOUT, "Operation timed out" },
    { WSAHOST_NOT_FOUND, "Unknown host" },
    { WSANOTINITIALISED, "Not initialized" },
    { WSANO_DATA, "Valid name without requested data" },
    { WSANO_RECOVERY, "Unrecoverable name server error" },
    { WSASYSNOTREADY, "Network subsystem is unavailable" },
    { WSATRY_AGAIN, "Temporary name sever error" },
    { WSAVERNOTSUPPORTED, "WinSock version is not supported" },
    { 0, NULL }
};


const char *AGetWinsockErrorMessage(int code)
{
    int i;

    for (i = 0; ErrorMessages[i].message != NULL; i++) {
        if (ErrorMessages[i].errNum == code)
            return ErrorMessages[i].message;
    }

    return "Unknown error";
}

#endif
