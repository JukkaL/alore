/* serversocket_module.c - serversocket module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementation of the serversocket module. The module defines a simple TCP
   server class. */

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
#include "runtime.h"
#include "io_module.h"


#define MAX_ADDRESS_LEN 1024
#define LISTEN_BACKLOG 1024
#define HANDLE 0


#ifdef A_HAVE_WINDOWS
#define SOCKLEN_T int
#else
#define SOCKLEN_T socklen_t
#endif


static AValue RaiseSocketError(AThread *t);


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


static AValue ServerSocketCreate(AThread *t, AValue *frame)
{
    int port;
    int handle;
    struct sockaddr_in address;
    unsigned opt;

    port = AGetInt(t, frame[1]);

    /* Get interface address(es). */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (frame[2] == ADefault || frame[2] == ANil) {
        /* Listen to all interfaces. */
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        /* Listen to a specific interface specified by its IP address. */
        char buf[MAX_ADDRESS_LEN];
        AGetStr(t, frame[2], buf, sizeof(buf));
        if (inet_pton(AF_INET, buf, &address.sin_addr) <= 0)
            return ARaiseValueError(t, "Invalid address");
    }
    
    AAllowBlocking();

    /* Create socket. */
    handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle < 0) {
        AEndBlocking();
        return RaiseSocketError(t);
    }

    /* Set SO_REUSEADDR to avoid random errors. */
    opt = 1;
    if (setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (void *)&opt,
                   sizeof(opt)) < 0) {
        AEndBlocking();
        RaiseSocketError(t);
        close(handle);
        return AError;
    }
    
    /* Bind the socket. */
    if (bind(handle, (struct sockaddr *)&address, sizeof(address)) < 0) {
        AEndBlocking();
        RaiseSocketError(t);
        close(handle);
        return AError;
    }

    /* Start listening to the socket. The backlog size is taken from the sample
       code in Unix Network Programming (by W. Richard Stevens).

       IDEA: Stevens actually suggests that the backlog size should be
             configurable. Perhaps we should make it so? */
    if (listen(handle, LISTEN_BACKLOG) < 0) {
        AEndBlocking();
        RaiseSocketError(t);
        close(handle);
        return AError;
    }

    AEndBlocking();

    /* Store file handle. */
    frame[1] = AMakeInt(t, handle);
    ASetMemberDirect(t, frame[0], HANDLE, frame[1]);

    return frame[0];
}


static AValue ServerSocketAccept(AThread *t, AValue *frame)
{
    int handle;
    int conn;
    struct sockaddr_in address;
    SOCKLEN_T len;

    /* Determine buffering mode. */
    if (AIsDefault(frame[1]))
        frame[1] = AGlobalByNum(AUnbufferedNum);
    else if (frame[1] != AGlobalByNum(AUnbufferedNum) &&
             frame[1] != AGlobalByNum(ALineBufferedNum) &&
             frame[1] != AGlobalByNum(ABufferedNum))
        return ARaiseValueError(t, "Invalid buffering mode");

    handle = AGetInt(t, AMemberDirect(frame[0], HANDLE));
    len = sizeof(address);

    /* Wait for an incoming connection. */
    for (;;) {
        AAllowBlocking();
        conn = accept(handle, (struct sockaddr *)&address, &len);
        AEndBlocking();
#ifndef A_HAVE_WINDOWS            
        if (conn < 0) {
            if (errno == ENETDOWN || errno == EPROTO || errno == ENOPROTOOPT
                || errno == EHOSTDOWN 
#ifdef ENONET
		|| errno == ENONET
#endif
                || errno == EHOSTUNREACH || errno == EOPNOTSUPP
                || errno == ENETUNREACH) {
#ifdef linux                
                /* Linux man page for accept recommends retrying if there is
                   a protocol error. */
                continue;
#endif
            } else if (errno == EINTR) {
                if (AIsInterrupt && AHandleInterrupt(t))
                    return AError;
                else
                    continue;
            }
        }
#endif
        break;
    }
    
    if (conn < 0)
        return RaiseSocketError(t);

    /* Construct a Socket object for the new connection. */
    frame[5] = frame[1];
    frame[1] = AMakeUninitializedObject(t, AGlobal(t, "socket::Socket"));
    frame[2] = AGlobalByNum(AInputNum);
    frame[3] = AGlobalByNum(AOutputNum);
    frame[4] = AGlobalByNum(ANarrowNum);

    frame[1] = AStreamCreate(t, frame + 1);
    if (AIsError(frame[1]))
        return AError;

    frame[2] = AMakeInt(t, conn);
    ASetMemberDirect(t, frame[1], A_FILE_ID, frame[2]);
    
    return frame[1];
}


static AValue ServerSocketClose(AThread *t, AValue *frame)
{
    int handle = AGetInt(t, AMemberDirect(frame[0], HANDLE));
    /* Do nothing if already closed. */
    if (handle < 0)
        return ANil;
    if (close(handle) < 0)
        return RaiseSocketError(t);
    ASetMemberDirect(t, frame[0], HANDLE, AMakeInt(t, -1));

    return ANil;
}


static AValue ServerSocketInitialize(AThread *t, AValue *frame)
{
    AValue handle = AMakeInt(t, -1);
    ASetMemberDirect(t, frame[0], HANDLE, handle);
    return ANil;
}


static AValue RaiseSocketError(AThread *t)
{
#ifndef A_HAVE_WINDOWS
    return ARaiseErrnoIoError(t, NULL);
#else
    return ARaiseIoError(t, AGetWinsockErrorMessage(WSAGetLastError()));
#endif
}

    
A_MODULE(serversocket, "serversocket")
    A_IMPORT("io")
    A_IMPORT("socket")

    A_CLASS_PRIV("ServerSocket", 1)
        A_METHOD_OPT("create", 1, 2, 0, ServerSocketCreate)
        A_METHOD_OPT("accept", 0, 1, 4, ServerSocketAccept)
        A_METHOD("close", 0, 0, ServerSocketClose)
        A_METHOD("#i", 0, 0, ServerSocketInitialize)
    A_END_CLASS()
A_END_MODULE()

#endif
