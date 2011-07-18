/* io_module.h - io module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef IO_MODULE_H_INCL
#define IO_MODULE_H_INCL

#include "thread.h"


A_APIVAR extern int AUnbufferedNum;
A_APIVAR extern int ALineBufferedNum;
A_APIVAR extern int ABufferedNum;
A_APIVAR extern int AInputNum;
A_APIVAR extern int AOutputNum;
A_APIVAR extern int AAppendNum;
A_APIVAR extern int ANarrowNum;
A_APIVAR extern int AProtectedNum;

extern int ARawStdInNum;
extern int ARawStdOutNum;
extern int ARawStdErrNum;

extern int AStdInNum;
extern int AStdOutNum;
extern int AStdErrNum;

extern int ADefaultEncodingNum;

extern int ABomNum;

extern int AFlushOutputBuffersNum;

extern int AFileClassNum;

extern int AUnstrictMemberNum;

extern int AStreamMemberNum;
extern int ACodecMemberNum;


#define A_FILE_METHOD 0
#define A_SOCKET_METHOD 1


AValue AIoMain(AThread *t, AValue *frame);
AValue AStreamWrite(AThread *t, AValue *frame);
AValue AStreamWriteLn(AThread *t, AValue *frame);
AValue AStreamReadLn(AThread *t, AValue *frame);
AValue AFileCreate(AThread *t, AValue *frame);
AValue AFile_Write(AThread *t, AValue *frame);
A_APIFUNC AValue AFile_WriteWithMethod(AThread *t, AValue *frame, int method);
AValue AFile_Read(AThread *t, AValue *frame);
A_APIFUNC AValue AFile_ReadWithMethod(AThread *t, AValue *frame, int method);
AValue AFileSeek(AThread *t, AValue *frame);
AValue AFilePos(AThread *t, AValue *frame);
AValue AFileSize(AThread *t, AValue *frame);
AValue AFileFinalize(AThread *t, AValue *frame);
AValue AFileClose(AThread *t, AValue *frame);
A_APIFUNC AValue AFileCloseWithMethod(AThread *t, AValue *frame, int method);
A_APIFUNC AValue AStreamCreate(AThread *t, AValue *frame);
AValue AFile__Handle(AThread *t, AValue *frame);
AValue AStreamFlush(AThread *t, AValue *frame);
A_APIFUNC AValue AStreamInitialize(AThread *t, AValue *frame);
AValue AFlushOutputBuffers(AThread *t, AValue *arg);

AValue ATextStreamCreate(AThread *t, AValue *frame);
AValue ATextStream_Write(AThread *t, AValue *frame);
AValue AIsUnprocessed(AThread *t, AValue *self, AValue *frame);
AValue ATextStream_Read(AThread *t, AValue *frame);
AValue ATextStreamClose(AThread *t, AValue *frame);
AValue ATextStreamFlush(AThread *t, AValue *frame);
AValue ATextFileCreate(AThread *t, AValue *frame);
AValue AInitDefaultEncoding(AThread *t);

/* Raise an IoError exception. Use errno to specify the error condition.
   The path argument may be NULL.*/
A_APIFUNC AValue ARaiseErrnoIoError(AThread *t, const char *path);


#define A_MODE_INPUT AIntToValue(1)
#define A_MODE_OUTPUT AIntToValue(2)
#define A_MODE_WRITELINE AIntToValue(4)
#define A_MODE_NARROW AIntToValue(8)


#define A_BUFMODE_UNBUFFERED    AIntToValue(0)
#define A_BUFMODE_LINE_BUFFERED AIntToValue(1)
#define A_BUFMODE_BUFFERED      AIntToValue(2)


enum {
    A_STREAM_LIST_PREV,
    A_STREAM_LIST_NEXT,
    A_STREAM_MODE,
    A_STREAM_BUF_MODE,
    A_STREAM_INPUT_BUF,
    A_STREAM_INPUT_BUF_IND,
    A_STREAM_INPUT_BUF_END,
    A_STREAM_OUTPUT_BUF,
    A_STREAM_OUTPUT_BUF_IND,
    A_FILE_ID
};


#define A_NUM_FILE_MEMBER_VARS (A_FILE_ID + 1)
#define A_NUM_STREAM_MEMBER_VARS (A_STREAM_OUTPUT_BUF_IND + 1)


#define A_IO_BUFFER_SIZE 8192 /* IDEA: Find out the optimal value */
#define A_IO_LINE_BUFFER_SIZE 256 /* IDEA: Find out the optimal value */
#define A_MAX_READ_SIZE (8 * 1024 * 1024)


#endif
