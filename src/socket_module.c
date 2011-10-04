/* socket_module.c - socket module (TCP socket programming interface)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"

#ifdef A_HAVE_SOCKET_MODULE

#ifndef A_HAVE_WINDOWS
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#else
#include <winsock.h>
#endif

#include "alore.h"
#include "thread_athread.h"
#include "io_module.h"
#include "athread.h"
#include "runtime.h"


#define MAX_ADDRESS_LEN 1024

/* Slot ids for Socket */
#define SOCKET_SRC_ADDRESS A_NUM_FILE_MEMBER_VARS
#define SOCKET_SRC_PORT (A_NUM_FILE_MEMBER_VARS + 1)
#define SOCKET_DST_ADDRESS (A_NUM_FILE_MEMBER_VARS + 2)
#define SOCKET_DST_PORT (A_NUM_FILE_MEMBER_VARS + 3)


#ifdef A_HAVE_WINDOWS
#define SOCKLEN_T int
#else
#define SOCKLEN_T socklen_t
#endif


static AValue SocketGetInfo(AThread *t, AValue *frame);
static AValue RaiseSocketError(AThread *t);
static int GetHErrno(void);
static AValue RaiseGetHostByNameException(AThread *t, int code);
static ABool GetHostByName(AThread *t, const char *host,
                           struct in_addr *address);


/* Global nums of definitions */
static int NameErrorNum;
static int SocketDeinitNum;


/* Mutex for guarding calls to gethostbyname etc. */
static athread_mutex_t GetHostByNameLock;

#define LockGetHostByName() athread_mutex_lock(&GetHostByNameLock)
#define UnlockGetHostByName() athread_mutex_unlock(&GetHostByNameLock)


#ifdef A_HAVE_WINDOWS
/* Win32 does not include inet_pton, so we define it here. Note that this is
   only a partial implementation! */
static int inet_pton(int af, const char *src, void *dst)
{
    unsigned long addr = inet_addr(src);
    if (addr == INADDR_NONE)
        return 0;

    ((struct in_addr *)dst)->s_addr = addr;
    return 1;
}

#define close closesocket
#endif


/* Socket create(destination, port[, buffering]) */
static AValue SocketCreate(AThread *t, AValue *frame)
{
    char addressStr[MAX_ADDRESS_LEN];
    int handle;
    struct sockaddr_in address;
    int port;

    /* Get host name. */
    AGetStr(t, frame[1], addressStr, MAX_ADDRESS_LEN);

    /* Get port number. */
    port = AGetInt(t, frame[2]);

    /* Get buffering mode. */
    if (AIsDefault(frame[3]))
        frame[1] = AGlobalByNum(AUnbufferedNum);
    else if (frame[3] == AGlobalByNum(AUnbufferedNum) ||
             frame[3] == AGlobalByNum(ALineBufferedNum) ||
             frame[3] == AGlobalByNum(ABufferedNum))
        frame[1] = frame[3];
    else
        return ARaiseValueError(t, "Invalid arguments");
    
    frame[2] = AGlobalByNum(AInputNum);
    frame[3] = AGlobalByNum(AOutputNum);
    frame[4] = AGlobalByNum(ANarrowNum);
    
    if (AIsError(AStreamCreate(t, frame)))
        return AError;

    /* Initialize address structure. */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    /* GetHostByName can handle both numeric IP addresses and host names. */
    if (!GetHostByName(t, addressStr, &address.sin_addr))
        return AError;

    /* Create socket. */
    handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle < 0)
        return RaiseSocketError(t);

    /* Create connection. */
    for (;;) {
        int status;

        AAllowBlocking();
        status = connect(handle, (struct sockaddr *)&address,
                         sizeof(address));
        AEndBlocking();
        
        if (status < 0) {
            if (errno == EINTR) {
                if (AIsInterrupt && AHandleInterrupt(t))
                    return AError;
                else
                    continue;
            } else {
                RaiseSocketError(t);
                close(handle);
                return AError;
            }
        }
        break;
    }

    frame[1] = AMakeInt(t, handle);
    ASetMemberDirect(t, frame[0], A_FILE_ID, frame[1]);

    return frame[0];
}


/* Socket _write(...) */
static AValue Socket_Write(AThread *t, AValue *frame)
{
    return AFile_WriteWithMethod(t, frame, A_SOCKET_METHOD);
}


/* Socket _read(n) */
static AValue Socket_Read(AThread *t, AValue *frame)
{
    return AFile_ReadWithMethod(t, frame, A_SOCKET_METHOD);
}


/* Socket close() */
static AValue SocketClose(AThread *t, AValue *frame)
{
    int i;
    AValue ret;

    for (i = 1; i <= 4; i++)
        ASetMemberDirect(t, frame[0], A_FILE_ID + i, ANil);
    
    ret = AFileCloseWithMethod(t, frame, A_SOCKET_METHOD);

    ASetMemberDirect(t, frame[0], A_FILE_ID, AMakeInt(t, -1));

    return ret;
}


/* Initialize the state of a socket instance (#i method). */
static AValue SocketInitialize(AThread *t, AValue *frame)
{
    AValue handle = AMakeInt(t, -1);
    ASetMemberDirect(t, frame[0], A_FILE_ID, handle);
    return AStreamInitialize(t, frame);
}


/* socket::Main() */
static AValue SocketMain(AThread *t, AValue *frame)
{
#ifdef A_HAVE_WINDOWS
    /* We need to initialize sockets explicitly in Windows. */
    WSADATA wsda;
    WSAStartup(MAKEWORD(1, 1), &wsda);
#endif

    if (athread_mutex_init(&GetHostByNameLock, NULL) < 0)
        return ARaiseMemoryError(t);

    /* Call SocketDeinit when shutting down. */
    frame[0] = AGlobalByNum(SocketDeinitNum);
    return AAddExitHandler(t, frame);
}


/* Deinitialize the socket module. Free allocated resources. */
static AValue SocketDeinit(AThread *t, AValue *frame)
{
#ifdef A_HAVE_WINDOWS
    WSACleanup();
#endif
    athread_mutex_destroy(&GetHostByNameLock);
    return ANil;
}


/* Socket::localAddress() */
static AValue SocketLocalAddress(AThread *t, AValue *frame)
{
    if (AIsError(SocketGetInfo(t, frame)))
        return AError;
    return AMemberDirect(frame[0], SOCKET_SRC_ADDRESS);
}


/* Socket::localPort() */
static AValue SocketLocalPort(AThread *t, AValue *frame)
{
    if (AIsError(SocketGetInfo(t, frame)))
        return AError;
    return AMemberDirect(frame[0], SOCKET_SRC_PORT);
}


/* Socket::remoteAddress() */
static AValue SocketRemoteAddress(AThread *t, AValue *frame)
{
    if (AIsError(SocketGetInfo(t, frame)))
        return AError;
    return AMemberDirect(frame[0], SOCKET_DST_ADDRESS);
}


/* Socket::remotePort() */
static AValue SocketRemotePort(AThread *t, AValue *frame)
{
    if (AIsError(SocketGetInfo(t, frame)))
        return AError;
    return AMemberDirect(frame[0], SOCKET_DST_PORT);
}


/* Look up various bits of information related to a Socket instance and store
   them in the private member variables for future use. If this has already
   been called on a specific instance, do nothing. */
static AValue SocketGetInfo(AThread *t, AValue *frame)
{
    if (AMemberDirect(frame[0], SOCKET_DST_ADDRESS) == ANil) {
        AValue v;
        SOCKLEN_T len;
        struct sockaddr_in address;
        int handle;

        handle = AGetInt(t, AMemberDirect(frame[0], A_FILE_ID));
        
        len = sizeof(address);
        if (getsockname(handle, (struct sockaddr *)&address, &len) < 0)
            return RaiseSocketError(t);

        v = AMakeInt(t, ntohs(address.sin_port));
        ASetMemberDirect(t, frame[0], SOCKET_SRC_PORT, v);
        /* FIX: inet_ntoa is not thread-safe */
        v = AMakeStr(t, inet_ntoa(address.sin_addr));
        ASetMemberDirect(t, frame[0], SOCKET_SRC_ADDRESS, v);

        len = sizeof(address);
        if (getpeername(handle, (struct sockaddr *)&address, &len) < 0)
            return RaiseSocketError(t);

        v = AMakeInt(t, ntohs(address.sin_port));
        ASetMemberDirect(t, frame[0], SOCKET_DST_PORT, v);
        /* FIX: inet_ntoa is not thread-safe */
        v = AMakeStr(t, inet_ntoa(address.sin_addr));
        ASetMemberDirect(t, frame[0], SOCKET_DST_ADDRESS, v);
    }

    return ANil;
}


/* Return the errno-like error value produced by name resolution functions. */
static int GetHErrno(void)
{
#ifndef A_HAVE_WINDOWS
    /* h_errno is the posix way. */
    return h_errno;
#else
    /* Windows has its own method. */
    return WSAGetLastError();
#endif
}


/* Raise socket::NameError for the given error code. */
static AValue RaiseGetHostByNameException(AThread *t, int code)
{
    const char *message = NULL;
    
#ifndef A_HAVE_WINDOWS
    switch (code) {
    case HOST_NOT_FOUND:
        message = "Unknown host";
        break;
    case NO_ADDRESS:
        message = "No IP address for host";
        break;
    case NO_RECOVERY:
        message = "Unrecoverable name server error";
        break;
    case TRY_AGAIN:
        message = "Temporary name server error";
        break;
    }
#else
    message = AGetWinsockErrorMessage(code);
#endif
    
    return ARaiseByNum(t, NameErrorNum, message);
}


/* socket::GetHostByName(address) */
static AValue SocketGetHostByName(AThread *t, AValue *frame)
{
    char address[MAX_ADDRESS_LEN];
    struct in_addr in_addr;
    unsigned long n;

    AGetStr(t, frame[0], address, sizeof(address));

    if (!GetHostByName(t, address, &in_addr))
        return AError;

    /* Convert the address to a string in dotted decimal notation. */
    n = ntohl(in_addr.s_addr);
    sprintf(address, "%ld.%ld.%ld.%ld", n >> 24, (n >> 16) & 0xff,
            (n >> 8) & 0xff, n & 0xff);

    return AMakeStr(t, address);
}


/* socket::GetHostByAddress(address) */
static AValue SocketGetHostByAddress(AThread *t, AValue *frame)
{
    char buf[MAX_ADDRESS_LEN];
    struct in_addr address;
    struct hostent *host;
    int addrtype;

    AGetStr(t, frame[0], buf, sizeof(buf));

    if (inet_pton(AF_INET, buf, &address) <= 0)
        return ARaiseValueError(t, "Invalid address");

    AAllowBlocking();
    LockGetHostByName();
    
    host = gethostbyaddr((void *)&address, sizeof(address), AF_INET);
    AEndBlocking();
    
    if (host == NULL) {
        int code = GetHErrno();
        UnlockGetHostByName();
        return RaiseGetHostByNameException(t, code);
    }

    if (strlen(host->h_name) >= MAX_ADDRESS_LEN) {
        UnlockGetHostByName();
        return RaiseGetHostByNameException(t, NO_ADDRESS);
    }

    addrtype = host->h_addrtype;
    strcpy(buf, host->h_name);

    UnlockGetHostByName();
    
    if (addrtype != AF_INET)
        return ARaiseIoError(t, "Invalid host type");
    
    return AMakeStr(t, buf);
}


/* socket::GetHostName() */
static AValue SocketGetHostName(AThread *t, AValue *frame)
{
    char buf[MAX_ADDRESS_LEN];

    if (gethostname(buf, sizeof(buf)) < 0)
        return RaiseSocketError(t);
    
    return AMakeStr(t, buf);
}


static AValue RaiseSocketError(AThread *t)
{
#ifndef A_HAVE_WINDOWS
    return ARaiseErrnoIoError(t, NULL);
#else
    return ARaiseIoError(t, AGetWinsockErrorMessage(WSAGetLastError()));
#endif
}


/* Helper for looking up a host name. */
static ABool GetHostByName(AThread *t, const char *hostStr,
                           struct in_addr *address)
{
    struct hostent *host;

    /* Handle numeric IP addresses quickly as a special case. */
    if (inet_pton(AF_INET, hostStr, address) > 0)
        return TRUE;
    
    AAllowBlocking();
    LockGetHostByName();
    
    host = gethostbyname(hostStr);
    
    AEndBlocking();
    
    if (host == NULL) {
        int code = GetHErrno();
        UnlockGetHostByName();
        RaiseGetHostByNameException(t, code);
        return FALSE;
    }

    if (host->h_addrtype != AF_INET) {
        UnlockGetHostByName();
        ARaiseByNum(t, NameErrorNum, "Invalid host type");
        return FALSE;
    }

    memcpy(address, host->h_addr_list[0], host->h_length);

    UnlockGetHostByName();

    return TRUE;
}


A_MODULE(socket, "socket")
    A_IMPORT("io")
    A_DEF(A_PRIVATE("Main"), 0, 1, SocketMain)
    A_DEF_P(A_PRIVATE("Deinit"), 0, 0, SocketDeinit, &SocketDeinitNum)

    A_DEF("GetHostByName", 1, 0, SocketGetHostByName)
    A_DEF("GetHostByAddress", 1, 0, SocketGetHostByAddress)
    A_DEF("GetHostName", 0, 0, SocketGetHostName)

    A_CLASS_P("NameError", &NameErrorNum)
        A_INHERIT("std::Exception")
    A_END_CLASS()

    /* The first additional private member is for FILE_ID; the others are
       for source/destination addresses. */
    A_CLASS_PRIV("Socket", 5)
        A_INHERIT("io::Stream")
        A_METHOD_OPT("create", 2, 3, 1, SocketCreate)
        A_METHOD_VARARG("_write", 0, 0, 1, Socket_Write)
        A_METHOD("_read", 1, 2, Socket_Read)
        A_METHOD("close", 0, 0, SocketClose)
        A_METHOD("localAddress", 0, 0, SocketLocalAddress)
        A_METHOD("localPort", 0, 0, SocketLocalPort)
        A_METHOD("remoteAddress", 0, 0, SocketRemoteAddress)
        A_METHOD("remotePort", 0, 0, SocketRemotePort)
        A_METHOD("#i", 0, 0, SocketInitialize)
    A_END_CLASS()
    /* FIX: add a #f method? */
A_END_MODULE()


#endif /* HAVE_SOCKET_MODULE */
