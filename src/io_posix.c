/* io_posix.c - io module (low-level file operations)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* FIX: The name of this file is misleading. Move posix-specific functionality
   to another file and rename this file. */

#include "aconfig.h"

#ifdef A_HAVE_POSIX
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#endif

#if defined(A_HAVE_WINDOWS) && defined(A_HAVE_SOCKET_MODULE)
#include <winsock.h>
#endif

#if defined(A_HAVE_WINDOWS)
#include <io.h>
#endif

#include <errno.h>
#include "alore.h"
#include "runtime.h"
#include "memberid.h"
#include "io_module.h"
#include "encodings_module.h"
#include "str.h"
#include "int.h"
#include "mem.h"
#include "gc.h"
#include "thread_athread.h"
#include "internal.h"

/* NOTE: Much of this implementation is shared by the socket module. */


#define MAX_READ 8192
#define WRITE_BUF_SIZE 1024


static ABool WriteBlock(AThread *t, AValue file, char *buf, int len,
                       int status);
static ABool AddOutputStreamList(AThread *t, AValue *file);
static ABool RemoveOutputStreamList(AThread *t, AValue *file);


#ifdef A_HAVE_POSIX
static AValue CreateFileObject(AThread *t, AValue *frame, int fileNum);
#else
static AValue CreateFileObject(AThread *t, AValue *frame, FILE *file);
#define GetFILE(v, dst) \
    (ACopyMem((dst),    \
              AValueToStr(AValueToInstance(v)->member[A_FILE_ID])->elem, \
              sizeof(FILE *)))
static ABool FileExists(const char *path);
#endif


/* File create(path, ...) */
AValue AFileCreate(AThread *t, AValue *frame)
{
    /* IDEA: Split this into multiple functions. */
    AInstance *inst;
    ABool isAppend;
    ABool isProtected;
    ABool isOutput;
    char path[A_MAX_PATH_LEN];
    int i;

    isAppend = FALSE;
    isProtected = FALSE;
    isOutput = FALSE;
    for (i = 2; i <= 5; i++) {
        if (frame[i] == AGlobalByNum(AAppendNum) 
            || frame[i] == AGlobalByNum(AProtectedNum)) {
            int j;

            if (frame[i] == AGlobalByNum(AAppendNum))
                isAppend = TRUE;
            else
                isProtected = TRUE;

            for (j = i; j < 5; j++)
                frame[j] = frame[j + 1];
            frame[j] = ADefault;
            i--;
        } else if (frame[i] == AGlobalByNum(AOutputNum))
            isOutput = TRUE;
    }

    /* Add Output option if opening in Append mode. */
    if (isAppend) {
        for (i = 2; i <= 5; i++) {
            if (frame[i] == ADefault) {
                frame[i] = AGlobalByNum(AOutputNum);
                break;
            }
        }
    }

    if (!AIsDefault(frame[5]))
        return ARaiseValueErrorND(t, NULL); /* IDEA: Error message? */

    /* Add Narrow option. */
    for (i = 2; i <= 5; i++) {
        if (frame[i] == ADefault) {
            frame[i] = AGlobalByNum(ANarrowNum);
            break;
        }
    }

    AGetStr(t, frame[1], path, A_MAX_PATH_LEN);

    if ((isProtected && (isAppend || !isOutput))
        || (isAppend && isOutput))
        ARaiseValueError(t, NULL);

#ifdef A_HAVE_POSIX
    {
        /* Posix implementation */
        int fileNum;
        int fileFlags;
        int fileMode;

        /* Call superclass (Stream) constructor. */
        frame[1] = frame[0];
        if (AIsError(AStreamCreate(t, frame + 1)))
            return AError;
        
        inst = AValueToInstance(frame[0]);

        fileMode = 0666;
        
        if (inst->member[A_STREAM_MODE] & A_MODE_INPUT) {
            if (inst->member[A_STREAM_MODE] & A_MODE_OUTPUT)
                fileFlags = O_RDWR | O_CREAT;
            else
                fileFlags = O_RDONLY;
        } else
            fileFlags = O_WRONLY | O_CREAT | O_TRUNC;
        
        if (isAppend) {
            fileFlags |= O_APPEND;
            fileFlags &= ~O_TRUNC;
        }
        
        if (isProtected) {
            fileFlags |= O_EXCL;
            fileMode = 0600;
        }
        
        AAllowBlocking();
        fileNum = open(path, fileFlags, fileMode);
        AEndBlocking();
        
        if (fileNum == -1)
            return ARaiseErrnoIoError(t, path);
        
        inst = AValueToInstance(frame[0]);
        inst->member[A_FILE_ID] = AIntToValue(fileNum);
    }
#else
    {
        /* Generic portable implementation */
        /* NOTE: The protected flag is simply ignored. */
        char mode[4];
        FILE *file;
        AValue buf;
        
        /* Call superclass (Stream) constructor. */
        frame[1] = frame[0];
        if (AIsError(AStreamCreate(t, frame + 1)))
            return AError;
        
        inst = AValueToInstance(frame[0]);
        if (inst->member[A_STREAM_MODE] & A_MODE_INPUT) {
            if (inst->member[A_STREAM_MODE] & A_MODE_OUTPUT) {
                if (isAppend)
                    strcpy(mode, "a+b");
                else if (FileExists(path))
                    strcpy(mode, "r+b");
                else
                    strcpy(mode, "w+b");
            } else
                strcpy(mode, "rb");
        } else
            strcpy(mode, isAppend ? "ab" : "wb");

        AAllowBlocking();
        file = fopen(path, mode);
        AEndBlocking();
        
        if (file == NULL)
            return ARaiseErrnoIoError(t, path);

        setbuf(file, NULL);

        buf = ACreateString(t, (unsigned char *)&file, sizeof(FILE *));
        if (AIsError(buf))
            return AError;
        ASetMemberDirect(t, frame[0], A_FILE_ID, buf);
        
        inst = AValueToInstance(frame[0]);
        ACopyMem(AValueToStr(inst->member[A_FILE_ID])->elem, &file,
                 sizeof(file));
    }
#endif

    if ((inst->member[A_STREAM_MODE] & A_MODE_OUTPUT)
        && inst->member[A_STREAM_BUF_MODE] != A_BUFMODE_UNBUFFERED) {
        if (!AddOutputStreamList(t, frame))
            return AError;
    }

    /* TODO */
    /* frame[1] = AMakeStr(t, path);
       ASetMemberDirect(t, frame[0], NameMemberNum, frame[1]); */

    return frame[0];
}


/* File close() */
AValue AFileClose(AThread *t, AValue *frame)
{
    int isListed;
    AValue ret;

    /* Is the stream kept in a global list of open streams? */
    isListed = (AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_OUTPUT)
        && AMemberDirect(frame[0], A_STREAM_BUF_MODE) != A_BUFMODE_UNBUFFERED;
    
    ret = AFileCloseWithMethod(t, frame, A_FILE_METHOD);

    if (isListed && !RemoveOutputStreamList(t, frame))
        ret = AError;

    return ret;
}


/* Implementation of close() for File-like classes. The method can be
   A_FILE_METHOD (for File) or A_SOCKET_METHOD (for Socket). */
AValue AFileCloseWithMethod(AThread *t, AValue *frame, int method)
{
    int retVal;

    /* If already closed, do nothing. */
    if (AMemberDirect(frame[0], A_STREAM_MODE) == AZero)
        return ANil;
    
    if (AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_OUTPUT) {
        if (ACallMethodByNum(t, AM_FLUSH, 0, frame) == AError)
            return AError;
    }
    
    ASetMemberDirect(t, frame[0], A_STREAM_MODE, AZero);

#if defined(A_HAVE_WINDOWS) && defined(A_HAVE_SOCKET_MODULE)
    if (method == A_SOCKET_METHOD) {
        /* Windows Socket implementation */
        AAllowBlocking();
        retVal = closesocket(AGetInt(t, AMemberDirect(frame[0], A_FILE_ID)));
        AEndBlocking();
        
        if (retVal == SOCKET_ERROR)
            return ARaiseIoError(t,
                                 AGetWinsockErrorMessage(WSAGetLastError()));
        else
            return ANil;
    } else
#endif
    {
#ifdef A_HAVE_POSIX
        /* Posix implementation */
        AAllowBlocking();
        retVal = close(AGetInt(t, AMemberDirect(frame[0], A_FILE_ID)));
        AEndBlocking();
        
        if (retVal < 0)
            return ARaiseErrnoIoError(t, NULL);
        else
            return ANil;
#else
        /* Generic portable implementation (for files only) */
        {
            FILE *file;
            GetFILE(frame[0], &file);
            AAllowBlocking();
            retVal = fclose(file);
            AEndBlocking();
        }
        
        if (retVal != 0)
            return ARaiseErrnoIoError(t, NULL);
        else
            return ANil;
#endif
    }
}


/* File _write(*args) */
AValue AFile_Write(AThread *t, AValue *frame)
{
    return AFile_WriteWithMethod(t, frame, A_FILE_METHOD);
}


/* Implementation of _write() for File-like classes. The method can be
   A_FILE_METHOD (for File) or A_SOCKET_METHOD (for Socket). */
AValue AFile_WriteWithMethod(AThread *t, AValue *frame, int method)
{
    int aLen;
    AValue first;

    aLen = AArrayLen(frame[1]);

    first = AArrayItem(frame[1], 0);
    if (aLen == 1 && AIsNarrowStr(first)
            && !AIsInNursery(AValueToPtr(first))) {
        unsigned char *s;
        int sLen;

        s = AValueToStr(first)->elem;
        sLen = AGetStrLen(AValueToStr(first));

        if (!WriteBlock(t, frame[0], (char *)s, sLen, method))
            return AError;
    } else {
        char buf[WRITE_BUF_SIZE];
        int i;
        int bufInd;

        bufInd = 0;
        
        for (i = 0; i < aLen; i++) {
            unsigned char *s;
            int len;
            int ind;

            frame[2] = AArrayItem(frame[1], i);

          TryAgain:

            /* Handle different kinds of string. */
            if (AIsNarrowStr(frame[2])) {
                s = AValueToStr(frame[2])->elem;
                len = AGetStrLen(AValueToStr(frame[2]));
                ind = 0;
            } else if (AIsSubStr(frame[2])) {
                ASubString *ss;

                ss = AValueToSubStr(frame[2]);
                if (AIsWideStr(ss->str))
                    goto WideStr;
                
                s = AValueToStr(ss->str)->elem;
                len = AValueToInt(ss->len);
                ind = AValueToInt(ss->ind);
            } else if (AIsWideStr(frame[2])) {
                AValue v;

              WideStr:

                v = AWideStringToNarrow(t, frame[2], frame + 2);
                if (AIsError(v))
                    return AError;

                frame[2] = v;
                goto TryAgain;
            } else
                return ARaiseTypeErrorND(t, NULL);

            while (bufInd + len > WRITE_BUF_SIZE) {
                ACopyMem(buf + bufInd, s + ind, WRITE_BUF_SIZE - bufInd);
                ind += WRITE_BUF_SIZE - bufInd;
                len -= WRITE_BUF_SIZE - bufInd;

                if (!WriteBlock(t, frame[0], buf, WRITE_BUF_SIZE, method))
                    return AError;
                
                bufInd = 0;
                
                if (AIsNarrowStr(frame[2]))
                    s = AValueToStr(frame[2])->elem;
                else
                    s = AValueToStr(AValueToSubStr(frame[2])->str)->elem;
            }

            ACopyMem(buf + bufInd, s + ind, len);
            bufInd += len;
        }

        if (!WriteBlock(t, frame[0], buf, bufInd, method))
            return AError;
    }

    return ANil;
}


/* File _read(n) */
AValue AFile_Read(AThread *t, AValue *frame)
{
    /* Flush all output buffers. This is done mainly so that any data written
       to StdOut is automatically flushed before reading from StdIn.
       
       IDEA: This seems very inefficient. Only flush StdOut and perhaps only
             if reading from StdIn? */
    if (AIsError(AFlushOutputBuffers(t, frame + 2)))
        return AError;
    
    return AFile_ReadWithMethod(t, frame, A_FILE_METHOD);
}


/* Implementation of _read() for File-like classes. The method can be
   A_FILE_METHOD (for File) or A_SOCKET_METHOD (for Socket). */
AValue AFile_ReadWithMethod(AThread *t, AValue *frame, int method)
{
    AInstance *inst;
    char *block;
    unsigned long readLen;
    int fileNum;
    AString *str;
    size_t numRead;

    if (!AIsShortInt(frame[1]))
        return ARaiseTypeErrorND(t, NULL);
    
    if ((AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_INPUT) == 0)
        return ARaiseByNum(t, AStdIoErrorNum, AMsgWriteOnly);
    
    readLen = AValueToInt(frame[1]);
    if (readLen > MAX_READ)
        readLen = MAX_READ;

    /* IDEA: Optimize if a very small read request? The speedup might be
             insignificant, though. */
    
    block = AAllocLocked(t, sizeof(AValue) + readLen);
    if (block == NULL)
        return AError;

    str = (AString *)block;
    AInitNonPointerBlock(&str->header, readLen);
    frame[1] = AStrToValue(str);
    
    inst = AValueToInstance(frame[0]);
    fileNum = AValueToInt(inst->member[A_FILE_ID]);

  Again:

#if defined(A_HAVE_WINDOWS) && defined(A_HAVE_SOCKET_MODULE)
    if (method == A_SOCKET_METHOD) {
        /* Windows socket implementation */
        AAllowBlocking();
        numRead = recv(fileNum, block + sizeof(AValue), readLen, 0);
        AEndBlocking();
        
        if (numRead == SOCKET_ERROR)
            return ARaiseIoError(t,
                                 AGetWinsockErrorMessage(WSAGetLastError()));
    } else
#endif
    {
#ifdef A_HAVE_POSIX
        /* Posix implementation */
        AAllowBlocking();
        numRead = read(fileNum, block + sizeof(AValue), readLen);
        AEndBlocking();
        
        if (numRead == -1) {
            if (errno == EINTR) {
                if (AIsInterrupt && AHandleInterrupt(t))
                    return AError;
                else
                    goto Again;
            } else
                return ARaiseErrnoIoError(t, NULL);
        }
#else
        /* Generic portable implementation (only for files) */
        {
            FILE *file;
            GetFILE(frame[0], &file);
            
            AAllowBlocking();
            numRead = fread(block + sizeof(AValue), 1, readLen, file);
            AEndBlocking();
            
            if (numRead < readLen && ferror(file)) {
                if (errno == EINTR) {
                    if (AIsInterrupt && AHandleInterrupt(t))
                        return AError;
                    else
                        goto Again;
                } else
                    return ARaiseErrnoIoError(t, NULL);
            }

#ifdef A_HAVE_WINDOWS
            /* If ctrl+c is pressed during fread, under WinNT/Win2k the call
               returns immediately and reports EOF, and GetLastError() returns
               ERROR_OPERATION_ABORTED. Borrowed this idea from Python. */

            /* IDEA: Use win32 API file operations and WaitForMultipleObjects
                     to handle interrupted IO operations? This would not rely
                     on undocumented behaviour. */
            if (feof(file) && GetLastError() == ERROR_OPERATION_ABORTED) {
                /* The interrupt handler is called asynchronously. Wait a
                   moment to let it get executed. */

                /* IDEA: Fixed 1 msec sleep is unreliable. */
                Sleep(1);
                if (AIsInterrupt && AHandleInterrupt(t))
                    return AError;
                
                /* The sleep probably was not long enough, or something else
                   strange happened. Perhaps we should try to sleep in a loop
                   for a few iterations? */
            }
#endif
        }
#endif
    }

    if (numRead == 0)
        return ANil;

    /* IDEA: Truncate the block so that we do not waste memory if only a
             small portion of a large request can be satisfied. Alternatively,
             make MAX_READ smaller, but this might hurt performance. */
    
    return ASubStr(t, frame[1], 0, numRead);
}


/* Write a single block of data to a File-like object. The method argument can
   be A_FILE_METHOD (for File) or A_SOCKET_METHOD (for Socket). This performs
   the actual writing for _write(). */
static ABool WriteBlock(AThread *t, AValue file, char *buf, int len,
                        int method)
{
#if defined(A_HAVE_WINDOWS) && defined(A_HAVE_SOCKET_MODULE)    
    if (method == A_SOCKET_METHOD) {
        /* Windows socket implementation */
        int socket = AValueToInt(AValueToInstance(file)->member[A_FILE_ID]);
        
        do {
            int numWritten;
            
            AAllowBlocking();
            /* FIX: If larger than maximum request size, do multiple calls. */
            numWritten = send(socket, buf, len, 0);
            AEndBlocking();
            
            if (numWritten == SOCKET_ERROR) {
                ARaiseIoError(t, AGetWinsockErrorMessage(WSAGetLastError()));
                return FALSE;
            }
            
            buf += numWritten;
            len -= numWritten;
        } while (len > 0);
        
        return TRUE;
    } else
#endif
    {
#ifdef A_HAVE_POSIX
        /* Posix implementation */
        int fileNum = AValueToInt(AValueToInstance(file)->member[A_FILE_ID]);
        
        do {
            ssize_t numWritten;

            AAllowBlocking();
            numWritten = write(fileNum, buf, len);
            AEndBlocking();

            /* Check for keyboard interrupts. */
            if (AIsInterrupt && AHandleInterrupt(t))
                return FALSE;
            
            if (numWritten == -1) {
                if (errno == EINTR)
                    numWritten = 0;
                else {
                    ARaiseErrnoIoError(t, NULL);
                    return FALSE;
                }
            }
            
            buf += numWritten;
            len -= numWritten;
        } while (len > 0);
        
        return TRUE;
#else
        /* Generic portable implementation (for files only) */
        FILE *f;
        
        GetFILE(file, &f);
        
        do {
            size_t numWritten;
            
            AAllowBlocking();
            numWritten = fwrite(buf, 1, len, f);
            AEndBlocking();
            
            if (AIsInterrupt && AHandleInterrupt(t))
                return FALSE;
                    
            if (numWritten < len && ferror(f)) {
                if (errno == EINTR)
                    numWritten = 0;
                else {
                    ARaiseErrnoIoError(t, NULL);
                    return FALSE;
                }
            }
            
            buf += numWritten;
            len -= numWritten;
        } while (len > 0);
        
        return TRUE;
#endif
    }
}


/* File seek(n) */
AValue AFileSeek(AThread *t, AValue *frame)
{
    AInt64 offset;
    AInstance *inst;
    
    offset = AGetInt64(t, frame[1]);
    
    /* Flush if output stream. */
    if (AValueToInstance(frame[0])->member[A_STREAM_MODE] & A_MODE_OUTPUT) {
        if (AIsError(AStreamFlush(t, frame)))
            return AError;
    }
    
    inst = AValueToInstance(frame[0]);

    /* Discard input buffer if input stream. */
    if (inst->member[A_STREAM_MODE] & A_MODE_INPUT)
        inst->member[A_STREAM_INPUT_BUF] = AZero;

#ifdef A_HAVE_POSIX
    {
        int fileNum = AValueToInt(inst->member[A_FILE_ID]);
        
        if (lseek(fileNum, offset, SEEK_SET) == (off_t)-1)
            return ARaiseErrnoIoError(t, NULL);
    }
#else
    {
        FILE *file;
        GetFILE(frame[0], &file);
# ifdef A_HAVE_WINDOWS
        {
            LARGE_INTEGER li;
            li.QuadPart = offset;
            /* Use an API function instead of _fseeki64 (or similar) since
               it does not require a specific version of msvcrXX. */
            if (SetFilePointerEx((HANDLE)_get_osfhandle(fileno(file)), li, 
                                 NULL, FILE_BEGIN) == 0)
                return ARaiseErrnoIoError(t, NULL);
        }
# else
        /* Fallback solution -- might not work for files larger than 2 GB. */
        if (fseek(file, offset, SEEK_SET) < 0)
            return ARaiseErrnoIoError(t, NULL);
# endif
    }
#endif

    /* Free all buffers. */
    inst->member[A_STREAM_INPUT_BUF] = AZero;
    inst->member[A_STREAM_INPUT_BUF_IND] = AZero;
    inst->member[A_STREAM_INPUT_BUF_END] = AZero;
    inst->member[A_STREAM_OUTPUT_BUF] = AZero;
    inst->member[A_STREAM_OUTPUT_BUF_IND] = AZero;
    
    return ANil;
}


/* File pos() */
AValue AFilePos(AThread *t, AValue *frame)
{
    int delta = 0;
    
    /* Flush if output stream. */
    if (AValueToInstance(frame[0])->member[A_STREAM_MODE] & A_MODE_OUTPUT) {
        if (AIsError(AStreamFlush(t, frame)))
            return AError;
    }

    /* If the stream has data in input buffer, the actual file pointer may
       be ahead of the logical pointer. The delta variable is used to adjust
       the physical file location to the logical one. */
    if (AMemberDirect(frame[0], A_STREAM_INPUT_BUF) != AZero)
        delta = AValueToInt(AMemberDirect(frame[0], A_STREAM_INPUT_BUF_IND) -
                            AMemberDirect(frame[0], A_STREAM_INPUT_BUF_END));
    
#ifdef A_HAVE_POSIX
    {
        int fileNum = AValueToInt(AMemberDirect(frame[0], A_FILE_ID));
        off_t pos;
        
        pos = lseek(fileNum, 0, SEEK_CUR);
        if (pos == (off_t)-1)
            return ARaiseErrnoIoError(t, NULL);
        
        return AMakeInt64(t, pos + delta);
    }
#else
    {
        FILE *file;
        AInt64 pos;
        
        GetFILE(frame[0], &file);
        pos = ftell(file);
        if (pos < 0)
            return ARaiseErrnoIoError(t, NULL);
        
        return AMakeInt64(t, pos + delta);
    }
#endif
}


/* File size() */
AValue AFileSize(AThread *t, AValue *frame)
{
    /* Flush if output stream. */
    if (AValueToInstance(frame[0])->member[A_STREAM_MODE] & A_MODE_OUTPUT) {
        if (AIsError(AStreamFlush(t, frame)))
            return AError;
    }
    
#ifdef A_HAVE_POSIX
    {
        int fileNum = AValueToInt(AMemberDirect(frame[0], A_FILE_ID));
        struct stat buf;

        if (fstat(fileNum, &buf) == -1)
            return ARaiseErrnoIoError(t, NULL);

        if (!S_ISREG(buf.st_mode))
            return ARaiseByNum(t, AStdIoErrorNum, NULL); /* IDEA: Msg */
        
        return AMakeInt64(t, buf.st_size);
    }
#else
    {
        FILE *file;
        AInt64 pos;
        AInt64 len;
        
        GetFILE(frame[0], &file);

        /* Record the current location, seek to the end of the file, get the
           file pointer there (length of the file), and seek back to the
           original location. */
        pos = ftell(file);
        if (pos == -1)
            goto Error;
        if (fseek(file, 0, SEEK_END) == -1)
            goto Error;
        len = ftell(file);
        if (len == -1)
            goto Error;
        if (fseek(file, pos, SEEK_SET) == -1)
            goto Error;

        /* IDEA: Get rid of the race condition. */

        return AMakeInt64(t, len);

      Error:

        return ARaiseErrnoIoError(t, NULL);
    }
#endif
}


/* File __handle() */
AValue AFile__Handle(AThread *t, AValue *frame)
{
    /* IDEA: Check that the file is open, as the returned value will otherwise
             be bogus. */
#ifdef A_HAVE_POSIX
    return AMemberDirect(frame[0], A_FILE_ID);
#else
    {
        FILE *file;
        GetFILE(frame[0], &file);
        return AMakeInt(t, fileno(file));
    }
#endif
}


static ABool CreateTextStreamWrapper(AThread *t, int wrapperNum, int origNum,
                                     int bufMode)
{
    AValue *temp = AAllocTemps(t, 4);

    temp[0] = AGlobalByNum(origNum);
    temp[1] = AGlobalByNum(AUnstrictNum);
    temp[2] = AGlobalByNum(bufMode);
    
    temp[0] = ACall(t, "io::TextStream", 3, temp);
    if (AIsError(temp[0]))
        return FALSE;

    ASetConstGlobalByNum(t, wrapperNum, temp[0]);
    
    AFreeTemps(t, 4);
    
    return TRUE;
}


/* io::Main() */
AValue AIoMain(AThread *t, AValue *frame)
{
    int i;
    AValue v;
    static const AWideChar bom[2] = { 0xfeff, 0 };
    int stdinBuf;
    int stdoutBuf;

    for (i = 2; i <= 4; i++)
        frame[i] = ADefault;
    
    frame[1] = AGlobalByNum(AInputNum);
    frame[2] = AGlobalByNum(ANarrowNum);

    /* Determine StdIn buffering mode. */
#ifdef A_HAVE_POSIX
    if (isatty(STDIN_FILENO))
        stdinBuf = ALineBufferedNum;
    else
        stdinBuf = ABufferedNum;
#else
    /* FIX: Set StdIn buffering properly. */
    stdinBuf = AUnbufferedNum;
#endif
    frame[3] = AGlobalByNum(stdinBuf);

    /* Initialize io::RawStdIn. */
#ifdef A_HAVE_POSIX    
    v = CreateFileObject(t, frame, STDIN_FILENO);
#else
    v = CreateFileObject(t, frame, stdin);
#endif
    if (AIsError(v))
        return AError;

    ASetConstGlobalByNum(t, ARawStdInNum, v);

    frame[1] = AGlobalByNum(AOutputNum);

    /* Determine StdOut buffering mode. */
#ifdef A_HAVE_POSIX    
    if (isatty(STDOUT_FILENO))
        stdoutBuf = ALineBufferedNum;
    else
        stdoutBuf = ABufferedNum;
#else
    /* FIX: Set StdOut buffering properly. */
    stdoutBuf = ALineBufferedNum;
#endif
    frame[3] = AGlobalByNum(stdoutBuf);

    /* Initialize io::RawStdOut. */
#ifdef A_HAVE_POSIX    
    v = CreateFileObject(t, frame, STDOUT_FILENO);
#else
    setbuf(stdout, NULL);
    v = CreateFileObject(t, frame, stdout);
#endif
    if (AIsError(v))
        return AError;

    ASetConstGlobalByNum(t, ARawStdOutNum, v);

    frame[3] = AGlobalByNum(AUnbufferedNum);

    /* Initialize io::RawStdErr. */
#ifdef A_HAVE_POSIX    
    v = CreateFileObject(t, frame, STDERR_FILENO);
#else
    setbuf(stderr, NULL);
    v = CreateFileObject(t, frame, stderr);
#endif
    if (AIsError(v))
        return AError;

#ifdef A_HAVE_POSIX
    /* Ignore SIGPIPE as otherwise our process can get killed
       inconveniently if output streams are redirected or if we have child
       processes. */
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        sigprocmask(SIG_BLOCK, &set, NULL);
    }
#endif
    
    ASetConstGlobalByNum(t, ARawStdErrNum, v);

    if (AIsError(AInitDefaultEncoding(t)))
        return AError;
    
    /* Create StdIn, StdOut and StdErr objects. */
    if (!CreateTextStreamWrapper(t, AStdInNum, ARawStdInNum, stdinBuf)
        || !CreateTextStreamWrapper(t, AStdOutNum, ARawStdOutNum, stdoutBuf)
        || !CreateTextStreamWrapper(t, AStdErrNum, ARawStdErrNum,
                                    AUnbufferedNum))
        return AError;

    /* Record StdOut and RawStdOut as output streams so that they are flushed
       automatically at program exit. */
    if (!AddOutputStreamList(t, &AGlobalVars[ARawStdOutNum]))
        return AError;
    if (!AddOutputStreamList(t, &AGlobalVars[AStdOutNum]))
        return AError;
    
    ASetConstGlobalByNum(t, ABomNum, AMakeStrW(t, bom));
    
    frame[0] = AGlobalByNum(AFlushOutputBuffersNum);
    return AAddExitHandler(t, frame);
}


#ifdef A_HAVE_POSIX
static AValue CreateFileObject(AThread *t, AValue *frame, int fileNum)
{
    frame[0] = AMakeUninitializedObject(t, AGlobalByNum(AFileClassNum));
    ASetMemberDirect(t, frame[0], A_FILE_ID, AIntToValue(fileNum));
    
    if (AIsError(AStreamCreate(t, frame)))
        return AError;

    return frame[0];
}
#else
static AValue CreateFileObject(AThread *t, AValue *frame, FILE *file)
{
    /* IDEA: Use AMakeUninitializedObject! */
    AInstance *inst;
    
    /* Allocate the block that holds the FILE * value. The block is implemented
       as a string (i.e. non-pointer) block. */
    frame[0] = ACreateString(t, (unsigned char *)&file, sizeof(FILE *));
    if (AIsError(frame[0])) {
        frame[0] = AZero;
        return AError;
    }
    
    inst = AAlloc(t, (A_NUM_FILE_MEMBER_VARS + 1) * sizeof(AValue));
    if (inst == NULL)
        return AError;

    AInitInstanceBlock(&inst->type,
                      AValueToType(AGlobalByNum(AFileClassNum)));
    inst->member[A_FILE_ID] = frame[0];

    frame[0] = AInstanceToValue(inst);
    
    if (AIsError(AStreamCreate(t, frame)))
        return AError;

    return frame[0];
}
#endif


/* Add a stream to the list of output streams. All streams in the list are
   flushed at program exit. */
static ABool AddOutputStreamList(AThread *t, AValue *file)
{
    ALockStreams();
    
    if (!ASetMemberDirectND(t, *file, A_STREAM_LIST_NEXT,
                         AGlobalByNum(GL_OUTPUT_STREAMS)))
        goto Fail;
    if (AGlobalByNum(GL_OUTPUT_STREAMS) != AZero) {
        if (!ASetMemberDirectND(t, AGlobalByNum(GL_OUTPUT_STREAMS),
                             A_STREAM_LIST_PREV, *file))
            goto Fail;
    }    
    if (!ASetConstGlobalValue(t, GL_OUTPUT_STREAMS, *file))
        goto Fail;
    
    AUnlockStreams();
    return TRUE;

  Fail:

    AUnlockStreams();
    return FALSE;
}


/* Remove a stream from the output stream list. */
static ABool RemoveOutputStreamList(AThread *t, AValue *file)
{
    AValue f;

    ALockStreams();
    
    if ((f = AMemberDirect(*file, A_STREAM_LIST_NEXT)) != AZero) {
        if (!ASetMemberDirectND(t, f, A_STREAM_LIST_PREV,
                             AMemberDirect(*file, A_STREAM_LIST_PREV)))
            goto Fail;
    }
    if ((f = AMemberDirect(*file, A_STREAM_LIST_PREV)) != AZero) {
        if (!ASetMemberDirectND(t, f, A_STREAM_LIST_NEXT,
                             AMemberDirect(*file, A_STREAM_LIST_NEXT)))
            goto Fail;
    } else {
        if (!ASetConstGlobalValue(t, GL_OUTPUT_STREAMS,
                                 AMemberDirect(*file, A_STREAM_LIST_NEXT)))
            goto Fail;
    }

    AUnlockStreams();
    return TRUE;

  Fail:

    AUnlockStreams();
    return FALSE;
}


#ifndef A_HAVE_POSIX
static ABool FileExists(const char *path)
{
    FILE *f;
    
    AAllowBlocking();
    f = fopen(path, "r");
    AEndBlocking();
    
    if (f != NULL) {
        fclose(f);
        return TRUE;
    } else
        return FALSE;
}
#endif
