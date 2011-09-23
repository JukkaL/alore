/* io_module.c - io module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "memberid.h"
#include "str.h"
#include "array.h"
#include "io_module.h"
#include "errmsg.h"
#include "internal.h"
#include "thread_athread.h"
#include "gc.h"


static AValue StreamReadAll(AThread *t, AValue *frame);

static AValue CreateBuffer(AThread *t, AValue inst);
static AValue StreamFillInputBuffer(AThread *t, AValue *frame);


int AUnbufferedNum;
int ALineBufferedNum;
int ABufferedNum;
int AInputNum;
int AOutputNum;
int AAppendNum;
int ANarrowNum;
int AProtectedNum;

int ARawStdInNum;
int ARawStdOutNum;
int ARawStdErrNum;

int AStdInNum;
int AStdOutNum;
int AStdErrNum;

int AFlushOutputBuffersNum;

int AFileClassNum;

int ANameMemberNum;

static int StreamIterClassNum;


AValue AStreamCreate(AThread *t, AValue *frame)
{
    AInstance *inst;
    AValue bufMode;
    AValue mode;
    int i;

    /* IDEA: Check all error conditions. */
    
    mode = 0;
    bufMode = A_BUFMODE_BUFFERED;
    
    for (i = 1; i <= 4; i++) {
        if (frame[i] == AGlobalByNum(ABufferedNum))
            bufMode = A_BUFMODE_BUFFERED;
        else if (frame[i] == AGlobalByNum(ALineBufferedNum))
            bufMode = A_BUFMODE_LINE_BUFFERED;
        else if (frame[i] == AGlobalByNum(AUnbufferedNum))
            bufMode = A_BUFMODE_UNBUFFERED;
        else if (frame[i] == AGlobalByNum(AInputNum))
            mode |= A_MODE_INPUT;
        else if (frame[i] == AGlobalByNum(AOutputNum))
            mode |= A_MODE_OUTPUT;
        else if (frame[i] == AGlobalByNum(ANarrowNum))
            mode |= A_MODE_NARROW;
        else if (frame[i] == ADefault)
            break;
        else
            return ARaiseValueErrorND(t, NULL); /* IDEA: Error message? */
    }

    if ((mode & (A_MODE_INPUT | A_MODE_OUTPUT)) == 0)
        mode = A_MODE_INPUT;

    inst = AValueToInstance(frame[0]);
    inst->member[A_STREAM_LIST_PREV] = AZero;
    inst->member[A_STREAM_LIST_NEXT] = AZero;
    inst->member[A_STREAM_MODE] = mode;
    inst->member[A_STREAM_BUF_MODE] = bufMode;
    inst->member[A_STREAM_INPUT_BUF] = AZero;
    inst->member[A_STREAM_INPUT_BUF_IND] = AZero;
    inst->member[A_STREAM_INPUT_BUF_END] = AZero;
    inst->member[A_STREAM_OUTPUT_BUF] = AZero;
    inst->member[A_STREAM_OUTPUT_BUF_IND] = AZero;
    
    return frame[0];
}


AValue AStreamInitialize(AThread *t, AValue *frame)
{
    AInstance *inst;

    /* Initialize some members to an empty value. */
    inst = AValueToInstance(frame[0]);
    inst->member[A_STREAM_MODE] = AZero;
    inst->member[A_STREAM_INPUT_BUF] = AZero;
    inst->member[A_STREAM_OUTPUT_BUF] = AZero;

    return ANil;
}


AValue AStreamWrite(AThread *t, AValue *frame)
{
    AInstance *inst;

    inst = AValueToInstance(frame[0]);

    if (inst->member[A_STREAM_OUTPUT_BUF] != AZero) {
        /* Buffered write */

        int aLen;
        int i;
        AString *buf;
        unsigned bufLen;
        unsigned bufInd;
        unsigned origBufInd;

        aLen = AArrayLen(frame[1]);

        buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
        bufLen = AGetStrLen(buf);
        bufInd = AValueToInt(inst->member[A_STREAM_OUTPUT_BUF_IND]);
        origBufInd = bufInd;
        
        for (i = 0; i < aLen; i++) {
            unsigned char *s;
            unsigned len;
            unsigned ind;
            
            frame[2] = AArrayItem(frame[1], i);

            for (;;) {
                if (AIsNarrowStr(frame[2])) {
                    s = AValueToStr(frame[2])->elem;
                    len = AGetStrLen(AValueToStr(frame[2]));
                    ind = 0;
                    
                    /* Do we have to expand narrow string to wide buffer? */
                    if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF]))
                        goto NarrowToWide;
                    
                    break;
                } else if (AIsSubStr(frame[2])) {
                    ASubString *ss;

                    ss = AValueToSubStr(frame[2]);
                    s = AValueToStr(ss->str)->elem;
                    ind = AValueToInt(ss->ind);
                    len = AValueToInt(ss->len);
                    
                    if (AIsWideStr(ss->str)) {
                        if (inst->member[A_STREAM_MODE] & A_MODE_NARROW)
                            goto WideToNarrow;
                        
                        frame[2] = ss->str;
                        s = (unsigned char *)AValueToWideStr(ss->str)->elem;
                        ind *= sizeof(AWideChar);
                        len *= sizeof(AWideChar);
                        frame[2] = ss->str;
                        goto CheckBufferExpand;
                    } else {
                        /* Do we have to expand narrow string to wide
                           buffer? */
                        frame[2] = ss->str;
                        if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF]))
                            goto NarrowToWide;
                    }
                    break;
                } else if (AIsWideStr(frame[2])) {
                    if (inst->member[A_STREAM_MODE] & A_MODE_NARROW) {

                      WideToNarrow:
                        
                        frame[2] = AWideStringToNarrow(t, frame[2], NULL);
                        if (AIsError(frame[2]))
                            return AError;
                        inst = AValueToInstance(frame[0]);
                        buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                        
                        continue;
                    }
                    
                    s = AValueToStr(frame[2])->elem;
                    len = AGetStrLen(AValueToStr(frame[2]));
                    ind = 0;

                  CheckBufferExpand:
                    
                    /* Do we have to expand the buffer to wide chars? */
                    if (!AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF])) {
                        if (bufInd > bufLen / 2) {
                            /* We need to flush the buffer because otherwise
                               it would overflow during expanding. */
                            inst->member[A_STREAM_OUTPUT_BUF_IND] =
                                AIntToValue(bufInd);
                            if (AIsError(AStreamFlush(t, frame)))
                                return AError;

                            /* Update values that could have been moved by
                               gc. */
                            inst = AValueToInstance(frame[0]);
                            buf = AValueToStr(
                                inst->member[A_STREAM_OUTPUT_BUF]);
                            s = AValueToStr(frame[2])->elem;
                            
                            bufInd = 0;
                            origBufInd = 0;

                            goto CheckBufferExpand;
                        } else {
                            /* Expand the buffer. */
                            int j;

                            /* NOTE: cast String -> WideString */
                            for (j = bufInd - 1; j >= 0; j--)
                                ((AWideString *)buf)->elem[j] =
                                    buf->elem[j];

                            bufInd *= 2;

                            /* NOTE: cast String -> WideString */
                            inst->member[A_STREAM_OUTPUT_BUF] =
                                AWideStrToValue(buf);
                        }
                    }
                    break;
                } else {
                    AValue v;

                    /* StdStr always returns a string if successful. */
                    v = AStdStr(t, frame + 2);
                    if (AIsError(v))
                        return AError;

                    /* Update values that could have been moved by gc. */
                    inst = AValueToInstance(frame[0]);
                    buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                    
                    frame[2] = v;
                }
            }
            
            while (bufInd + len > bufLen) {
                /* Fill the rest of the buffer by copying from the source
                   string. */
                ACopyMem(buf->elem + bufInd, s + ind, bufLen - bufInd);
                ind += bufLen - bufInd;
                len -= bufLen - bufInd;

                /* Write the filled buffer. */
                inst->member[A_STREAM_OUTPUT_BUF_IND] = AIntToValue(bufLen);
                if (AIsError(AStreamFlush(t, frame)))
                    return AError;

                /* Update values that could have been moved by gc. */
                inst = AValueToInstance(frame[0]);
                buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                s = AValueToStr(frame[2])->elem;
                
                bufInd = 0;
                origBufInd = 0;
            }

            /* Copy the rest of the string that doesn't fill up the buffer. */
            /* IDEA: Perhaps special case very short strings. */
            ACopyMem(buf->elem + bufInd, s + ind, len);
            bufInd += len;

            continue;

          NarrowToWide:

            {
                AWideString *wbuf;

                wbuf = (AWideString *)buf;
                
                while (bufInd + sizeof(AWideChar) * len > bufLen) {
                    /* Fill the rest of the buffer. */
                    len -= (bufLen - bufInd) / sizeof(AWideChar);
                    while (bufInd < bufLen) {
                        wbuf->elem[bufInd / sizeof(AWideChar)] = s[ind];
                        bufInd += sizeof(AWideChar);
                        ind++;
                    }

                    /* Write the filled buffer. */
                    inst->member[A_STREAM_OUTPUT_BUF_IND] =
                        AIntToValue(bufInd);
                    if (AIsError(AStreamFlush(t, frame)))
                        return AError;

                    /* Update values that could have been moved by gc. */
                    inst = AValueToInstance(frame[0]);
                    buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                    wbuf = (AWideString *)buf;
                    s = AValueToStr(frame[2])->elem;
                    
                    bufInd = 0;
                    origBufInd = 0;
                }

                /* Copy the rest of the string to the buffer. */
                while (len > 0) {
                    wbuf->elem[bufInd / sizeof(AWideChar)] = s[ind];
                    bufInd += sizeof(AWideChar);
                    ind++;
                    len--;
                }
            }
        } /* for */

        /* Is this a WriteLn call? */
        if (inst->member[A_STREAM_MODE] & A_MODE_WRITELINE) {
            /* Make sure we aren't at the end of buffer. */
            if (bufInd == bufLen) {
                inst->member[A_STREAM_OUTPUT_BUF_IND] = AIntToValue(bufLen);
                if (AIsError(AStreamFlush(t, frame)))
                    return AError;

                inst = AValueToInstance(frame[0]);
                buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                bufInd = 0;
            }

            /* Insert the first newline char. */
            if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF])) {
                ((AWideString *)buf)->elem[bufInd / sizeof(AWideChar)] =
                                                               A_NEWLINE_CHAR1;
                bufInd += sizeof(AWideChar);
            } else {
                buf->elem[bufInd] = A_NEWLINE_CHAR1;
                bufInd++;
            }
            
#ifdef A_NEWLINE_CHAR2
            /* Make sure we aren't at the end of buffer. */
            if (bufInd == bufLen) {
                inst->member[A_STREAM_OUTPUT_BUF_IND] = AIntToValue(bufLen);
                if (AIsError(AStreamFlush(t, frame)))
                    return AError;

                inst = AValueToInstance(frame[0]);
                buf = AValueToStr(inst->member[A_STREAM_OUTPUT_BUF]);
                bufInd = 0;
            }

            /* Insert the second newline char. */
            if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF])) {
                ((AWideString *)buf)->elem[bufInd / sizeof(AWideChar)] =
                                                               A_NEWLINE_CHAR2;
                bufInd += sizeof(AWideChar);
            } else {
                buf->elem[bufInd] = A_NEWLINE_CHAR2;
                bufInd++;
            }
#endif
            
            /* Flush if the stream is line buffered. */
            if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_LINE_BUFFERED) {

              Flush:
                
                inst->member[A_STREAM_OUTPUT_BUF_IND] = AIntToValue(bufInd);
                if (AIsError(AStreamFlush(t, frame)))
                    return AError;
                inst = AValueToInstance(frame[0]);
                bufInd = 0;
            }
        } else if (inst->member[A_STREAM_BUF_MODE] ==
                   A_BUFMODE_LINE_BUFFERED) {
            /* If we wrote a newline character, flush the buffer. */        
            if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF])) {
                for (i = origBufInd / sizeof(AWideChar);
                     i < bufInd / sizeof(AWideChar); i++) {
                    if (AIsNewLineChar(((AWideString *)buf)->elem[i]))
                        goto Flush;
                }
            } else {
                for (i = origBufInd; i < bufInd; i++) {
                    if (AIsNewLineChar(buf->elem[i]))
                        goto Flush;
                }
            }
        }

        inst->member[A_STREAM_OUTPUT_BUF_IND] = AIntToValue(bufInd);
    } else if ((inst->member[A_STREAM_MODE] & A_MODE_OUTPUT) == 0)
        return ARaiseByNum(t, AStdIoErrorNum, AMsgReadOnly);
    else if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_UNBUFFERED) {
        /* Unbuffered write */
        
        int aLen;
        AValue dstA;
        int i;
        ABool isNewLine;
        ABool result;

        aLen = AArrayLen(frame[1]);
        
        if (inst->member[A_STREAM_MODE] & A_MODE_WRITELINE) {
            dstA = AZero;
            
            /* Create a new array for arguments that will contain an
               additional item for newline. */
            AMakeUninitArray_M(t, aLen + 1, dstA, result);
            if (!result)
                return AError;
            
            /* Initialize the array to zero. */
            for (i = 0; i < aLen + 1; i++)
                ASetArrayItemNewGen(dstA, i, AZero);

            frame[2] = dstA;
            
            isNewLine = TRUE;
        } else {
            frame[2] = frame[1];
            isNewLine = FALSE;
        }
            
        /* Convert all arguments to strings. */
        for (i = 0; i < aLen; i++) {
            AValue item = AArrayItem(frame[1], i);
            if (!AIsStr(item)) {
                AValue *strFrame = AAllocTemp(t, item);
                item = AStdStr(t, strFrame);
                AFreeTemp(t);
                if (AIsError(item))
                    return AError;
            }

            ASetArrayItem(t, frame[2], i, item);
        }

        if (isNewLine)
            ASetArrayItem(t, frame[2], aLen, AGlobalVars[GL_NEWLINE]);

        if (AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_NARROW) {
            for (i = 0; i < aLen; i++) {
                AValue s =  AArrayItem(frame[2], i);
                if ((AIsWideStr(s) || AIsWideSubStr(s)) &&
                    !AIsNarrowStringContents(s))
                    return ARaiseValueErrorND(t, NULL); /* IDEA: Error msg! */
            }
        }

        frame[1] = frame[2];
        
        /* Perform the write. */
        return ACallMethodByNum(t, AM__WRITE, 1 | A_VAR_ARG_FLAG, frame);
    } else {
        /* Create the output buffer and try again. */
        if (CreateBuffer(t, frame[0]) == AError)
            return AError;
        return AStreamWrite(t, frame);
    }
    
    return ANil;
}


AValue AStreamWriteLn(AThread *t, AValue *frame)
{
    /* FIX: What if e.g. _write causes another call to write while the 
            writeLn is active? The flag causes it behave like a writeLn 
            call... */
    AValueToInstance(frame[0])->member[A_STREAM_MODE] |= A_MODE_WRITELINE;
    
    if (AIsError(AStreamWrite(t, frame))) {
        AValueToInstance(frame[0])->member[A_STREAM_MODE] &= ~A_MODE_WRITELINE;
        return AError;
    } else {
        AValueToInstance(frame[0])->member[A_STREAM_MODE] &= ~A_MODE_WRITELINE;
        return ANil;
    }
}


/* Read up to given number of bytes from the stream. */
static AValue StreamRead(AThread *t, AValue *frame)
{
    AInstance *inst;
    ASignedValue lenWanted;

    if (!AIsShortInt(frame[1])) {
        if (AIsDefault(frame[1]))
            return StreamReadAll(t, frame);
        else
            return ARaiseTypeErrorND(t, NULL);
    } else
        lenWanted = AValueToInt(frame[1]);

    if (lenWanted <= 0) {
        if (lenWanted == 0)
            return AMakeStr(t, ""); /* FIX: What if closed or output only? */
        else
            return ARaiseValueErrorND(t, NULL); /* IDEA: Error message? */
    }

    inst = AValueToInstance(frame[0]);

    frame[2] = AZero;

    /* Read from the stream some bytes at a time (depending on buffering) and
       accumulate the result. */
    for (;;) {
        AValue v;
        int len;
        int ind;

        /* IDEA: This is terribly slow when doing long reads. Maybe we
                 should optimize. */
        
        if (inst->member[A_STREAM_INPUT_BUF] != AZero) {
            v = inst->member[A_STREAM_INPUT_BUF];
            ind = AValueToInt(inst->member[A_STREAM_INPUT_BUF_IND]);
            len = AValueToInt(inst->member[A_STREAM_INPUT_BUF_END]) - ind;
        } else {
            if ((inst->member[A_STREAM_MODE] & A_MODE_INPUT) == 0)
                return ARaiseByNum(t, AStdIoErrorNum, AMsgWriteOnly);

            if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_UNBUFFERED) {
                /* Perform unbuffered read. */

                frame[1] = AIntToValue(lenWanted);
                v = ACallMethodByNum(t, AM__READ, 1, frame);
                if (AIsError(v))
                    return AError;

                /* End of file? */
                if (AIsNil(v)) {
                    if (frame[2] == AZero)
                        return AMakeStr(t, "");
                    else
                        return frame[2];
                }

                /* Check that the return value is a string. */
                if (!AIsNarrowStr(v) && !AIsSubStr(v) && !AIsWideStr(v))
                    return ARaiseTypeErrorND(t, NULL); /* IDEA: Error msg? */

                ind = 0;
                inst = AValueToInstance(frame[0]);

                /* Calculate the length of the string. */
                if (AIsNarrowStr(v))
                    len = AGetStrLen(AValueToStr(v));
                else if (AIsSubStr(v))
                    len = AGetSubStrLen(AValueToSubStr(v));
                else
                    len = AGetWideStrLen(AValueToWideStr(v));
                len -= ind;
            } else {
                /* Perform a buffered read. */
                AValue status;

                status = StreamFillInputBuffer(t, frame);
                if (AIsError(status))
                    return AError;

                /* End of file? */
                if (AIsNil(status)) {
                    if (frame[2] == AZero)
                        return AMakeStr(t, "");
                    else
                        return frame[2];
                }

                inst = AValueToInstance(frame[0]);               
                continue;
            }
        }

        /* v is a Str value that contains the input buffer. ind is an index to
           first unread character in the buffer. len is the number of
           characters left in the buffer. */

        /* Get as much as required of the buffer as a substring. */
        v = ASubStr(t, v, ind, ind + AMin(len, lenWanted));

        /* If we have read data before, concatenate the additional data. */
        if (frame[2] != AZero) {
            v = AConcatStrings(t, frame[2], v);
            if (AIsError(v))
                return AError;
        }
        
        frame[2] = v;

        inst = AValueToInstance(frame[0]);
        
        /* Mark the input buffer empty if it's fully consumed. */
        if (lenWanted >= len)
            inst->member[A_STREAM_INPUT_BUF] = AZero;

        /* Have we read enough? */
        if (lenWanted <= len) {
            /* Update the buffer index (this is ok even if there is no
               buffer). */
            inst->member[A_STREAM_INPUT_BUF_IND] = AIntToValue(ind +
                                                               lenWanted);
            if (lenWanted == len)
                inst->member[A_STREAM_INPUT_BUF] = AZero;
            return frame[2];
        }

        /* Continue in the loop. */
        lenWanted -= len;

        if (ACheckInterrupt(t))
            return AError; /* Interrupted */
    }
    
    /* Not reached */
}


/* Read all the contents of a stream and return them as a single string.
   frame[0] is the stream, frame[1..2] are temporary locations.  */
static AValue StreamReadAll(AThread *t, AValue *frame)
{
    AInstance *inst;

    /* Collect all read blocks in an array. */
    frame[2] = AMakeArray(t, 0);

    inst = AValueToInstance(frame[0]);
    
    /* First get all data available in the input buffer. */
    if (inst->member[A_STREAM_INPUT_BUF] != AZero) {
        AValue init;
        int index;
        int endIndex;

        index = AValueToInt(inst->member[A_STREAM_INPUT_BUF_IND]);
        endIndex = AValueToInt(inst->member[A_STREAM_INPUT_BUF_END]);
        init = ASubStr(t, inst->member[A_STREAM_INPUT_BUF], index, endIndex);
        AAppendArray(t, frame[2], init);
        inst = AValueToInstance(frame[0]);
    } else if ((inst->member[A_STREAM_MODE] & A_MODE_INPUT) == 0)
        return ARaiseByNum(t, AStdIoErrorNum, AMsgWriteOnly);
    
    /* IDEA: Check if the target is a File object and then try to preallocate
       a string object with the correct size. Obviously another process might
       append to the file while we are reading it, so we would need to be
       careful. */

    /* Mark the input buffer as empty. */
    inst->member[A_STREAM_INPUT_BUF] = AZero;
    inst->member[A_STREAM_INPUT_BUF_IND] = AZero;
    inst->member[A_STREAM_INPUT_BUF_END] = AZero;

    /* Read data from the stream a block at a time. */
    for (;;) {
        AValue v;

        frame[1] = AIntToValue(A_IO_BUFFER_SIZE);
        v = ACallMethodByNum(t, AM__READ, 1, frame);
        if (AIsError(v))
            return AError;

        if (!AIsNarrowStr(v) &&  !AIsWideStr(v) && !AIsSubStr(v)) {
            if (AIsNil(v))
                break;
            else
                return ARaiseTypeErrorND(t, NULL);
        }

        AAppendArray(t, frame[2], v);
        
        if (ACheckInterrupt(t))
            return AError; /* Interrupted */
    }

    /* Join all the read blocks. */
    return AJoin(t, frame[2], ADefault);
}


AValue AStreamReadLn(AThread *t, AValue *frame)
{
    AInstance *inst;

    inst = AValueToInstance(frame[0]);

    if (inst->member[A_STREAM_INPUT_BUF] != AZero) {
        /* There is an input buffer. */

        int bufInd;
        AValue ss;
        unsigned char endChar = '\0';

        for (;;) {
            if (AIsWideStr(inst->member[A_STREAM_INPUT_BUF])) {
                /* Wide buffer */
                AWideChar *bufBeg;
                AWideChar *buf;
                AWideChar *bufEnd;

                bufBeg = AStrPtrW(inst->member[A_STREAM_INPUT_BUF]);
                buf = bufBeg + AValueToInt(
                    inst->member[A_STREAM_INPUT_BUF_IND]);
                bufEnd = AStrPtrW(inst->member[A_STREAM_INPUT_BUF]) +
                    AValueToInt(inst->member[A_STREAM_INPUT_BUF_END]);
                
                /* Scan the buffer and try to find a newline. */
                do {
                    if (*buf == '\n' || *buf == '\r') {
                        endChar = *buf;
                        break;
                    }
                    buf++;
                } while (buf < bufEnd);

                bufInd = buf - bufBeg;
            } else {
                /* Narrow buffer */
                unsigned char *bufBeg;
                unsigned char *buf;
                unsigned char *bufEnd;

                bufBeg = AStrPtr(inst->member[A_STREAM_INPUT_BUF]);
                buf = bufBeg +
                    AValueToInt(inst->member[A_STREAM_INPUT_BUF_IND]);
                bufEnd = AStrPtr(inst->member[A_STREAM_INPUT_BUF]) +
                    AValueToInt(inst->member[A_STREAM_INPUT_BUF_END]);
                
                /* Scan the buffer and try to find a newline. */
                do {
                    if (*buf == '\n' || *buf == '\r') {
                        endChar = *buf;
                        break;
                    }
                    buf++;
                } while (buf < bufEnd);

                bufInd = buf - bufBeg;
            }

            /* Create a substring object from the buffer. */
            ss = ASubStr(t, inst->member[A_STREAM_INPUT_BUF],
                               AValueToInt(
                                   inst->member[A_STREAM_INPUT_BUF_IND]),
                               bufInd);
            
            if (frame[2] != AZero) {
                if (!AIsArray(frame[2])) {
                    frame[3] = ss;
                    frame[4] = AMakeArray(t, 0);
                    AAppendArray(t, frame[4], frame[2]);
                    AAppendArray(t, frame[4], frame[3]);
                    frame[2] = frame[4];
                } else
                    AAppendArray(t, frame[2], ss);
            } else
                frame[2] = ss;
            
            inst = AValueToInstance(frame[0]);

            if (bufInd == AValueToInt(inst->member[A_STREAM_INPUT_BUF_END])) {
                /* End of buffer reached. */
                AValue fillRes;
                
                fillRes = StreamFillInputBuffer(t, frame);
                if (AIsError(fillRes))
                    return AError;
                if (AIsNil(fillRes))
                    break;
                
                inst = AValueToInstance(frame[0]);
            } else {
                /* End of line reached within the buffer. */
                if (endChar == '\r') {
                    /* If at the end of the buffer, try to read more. */
                    if (bufInd + 1
                        == AValueToInt(inst->member[A_STREAM_INPUT_BUF_END])) {
                        AValue fillRes;
                        
                        fillRes = StreamFillInputBuffer(t, frame);
                        if (AIsError(fillRes))
                            return AError;
                        if (AIsNil(fillRes))
                            break;
                        
                        inst = AValueToInstance(frame[0]);
                        bufInd = AValueToInt(
                            inst->member[A_STREAM_INPUT_BUF_IND]) - 1;
                    }

                    /* If the next character is LF, skip it since it is a part
                       of a CR+LF sequence. */
                    if (AIsWideStr(inst->member[A_STREAM_INPUT_BUF])
                        ? AValueToWideStr(inst->member[A_STREAM_INPUT_BUF])->
                          elem[bufInd + 1] == '\n'
                        : AValueToStr(inst->member[A_STREAM_INPUT_BUF])->
                          elem[bufInd + 1] == '\n')
                        bufInd++;
                }

                if (bufInd + 1
                    == AValueToInt(inst->member[A_STREAM_INPUT_BUF_END])) {
                    inst->member[A_STREAM_INPUT_BUF] = AZero;
                    inst->member[A_STREAM_INPUT_BUF_END] = AZero;
                } else
                    inst->member[A_STREAM_INPUT_BUF_IND] =
                        AIntToValue(bufInd + 1);
                
                break;
            }
        } /* for */
    } else if ((inst->member[A_STREAM_MODE] & A_MODE_INPUT) == 0)
        return ARaiseByNum(t, AStdIoErrorNum, AMsgWriteOnly);
    else {
        /* There is no input buffer. However, we can't perform a readLn
           operation without some kind of buffering, since after a CR we have
           to know the next character before we can decide whether the line
           terminator is CR or CR+LF. So we'll allocate a small input buffer
           for this operation only. */
        
        AValue fillRes;

        fillRes = StreamFillInputBuffer(t, frame);
        if (AIsError(fillRes))
            return AError;
        else if (AIsNil(fillRes)) {
            /* IDEA: Return a specific end of file error? */
            return ARaiseByNum(t, AStdIoErrorNum, AMsgReadEOF);
        } else
            return AStreamReadLn(t, frame); /* Recursive call */
    }

    if (AIsArray(frame[2]))
        return AJoin(t, frame[2], ADefault);
    else
        return frame[2];
}


static AValue StreamReadLines(AThread *t, AValue *frame)
{
    AValue v;
    
    frame[1] = AMakeArray(t, 0);

    for (;;) {
        v = ACallMethodByNum(t, AM_EOF, 0, frame);
        if (AIsTrue(v))
            break;
        else if (!AIsFalse(v)) {
            if (AIsError(v))
                return v;
            else
                return ARaiseValueErrorND(t, NULL);
        }

        v = ACallMethodByNum(t, AM_READLN, 0, frame);
        if (AIsError(v))
            return v;

        AAppendArray(t, frame[1], v);
    }

    v = ACallMethodByNum(t, AM_CLOSE, 0, frame);
    if (AIsError(v))
        return v;

    return frame[1];
}


AValue AStreamFlush(AThread *t, AValue *frame)
{
    AInstance *inst;
    AValue ss;
    unsigned len;

    inst = AValueToInstance(frame[0]);

    /* Is the stream closed or an input stream? */
    if ((inst->member[A_STREAM_MODE] & A_MODE_OUTPUT) == AZero)
        return ARaiseByNum(t, AStdIoErrorNum, NULL);

    if (inst->member[A_STREAM_OUTPUT_BUF] != AZero) {
        /* Write the contents of the output buffer. */
        
        len = AValueToInt(inst->member[A_STREAM_OUTPUT_BUF_IND]);
        if (AIsWideStr(inst->member[A_STREAM_OUTPUT_BUF]))
            len /= sizeof(AWideChar);
        
        if (!AAllocTempStack(t, 3))
            return AError;
        
        t->tempStackPtr[-3] = frame[0];
        t->tempStackPtr[-2] = AZero;
        t->tempStackPtr[-1] = AZero; /* IDEA: Why this 3rd one -- 2 enough? */

        ss = ASubStr(t, inst->member[A_STREAM_OUTPUT_BUF], 0, len);
        t->tempStackPtr[-2] = ss;
        
        if (ACallMethodByNum(t, AM__WRITE, 1,
                       t->tempStackPtr - 3) == AError) {
            t->tempStackPtr -= 3;
            return AError;
        }
        
        t->tempStackPtr -= 3;
        
        if (AGetInstanceType(&AValueToInstance(frame[0])->type)
            != AValueToType(AGlobalByNum(AFileClassNum)))
            return CreateBuffer(t, frame[0]);
        else {
            /* File objects may reuse the output buffer because the _write
               method will not store a reference to the buffer. */
            AValueToInstance(frame[0])->member[A_STREAM_OUTPUT_BUF_IND] =
                AZero;
            return ANil;
        }
    } else
        return ANil;
}


static AValue StreamEof(AThread *t, AValue *frame)
{
    AInstance *inst;

    inst = AValueToInstance(frame[0]);

    /* If the input buffer has data, we're not at eof. */
    if (inst->member[A_STREAM_INPUT_BUF_IND]
            < inst->member[A_STREAM_INPUT_BUF_END]) {
        return AFalse;
    } else {
        /* Input buffer has no data. */
        AValue v;

        /* If not an input stream, raise an exception (eof is only supported
           for input streams). */
        if (!(inst->member[A_STREAM_MODE] & A_MODE_INPUT))
            return ARaiseByNum(t, AStdIoErrorNum, NULL); /* IDEA: Msg */

        /* Fill the input buffer, even if the stream is unbuffered. If the
           buffer could not be filled, we must be at the end of the stream. */
        v = StreamFillInputBuffer(t, frame);
        if (AIsNil(v))
            return ATrue;
        else if (v == AZero)
            return AFalse;
        else
            return v;
    }
}


static AValue StreamClose(AThread *t, AValue *frame)
{
    /* Calling close() on a closed stream is a no-op. */
    if (AMemberDirect(frame[0], A_STREAM_MODE) == AZero)
        return ANil;
    
    if (AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_OUTPUT) {
        if (ACallMethodByNum(t, AM_FLUSH, 0, frame) == AError)
            return AError;
    }
    
    ASetMemberDirect(t, frame[0], A_STREAM_MODE, AZero);
    ASetMemberDirect(t, frame[0], A_STREAM_OUTPUT_BUF, AZero);
    ASetMemberDirect(t, frame[0], A_STREAM_INPUT_BUF, AZero);

    return ANil;
}


static AValue StreamPeek(AThread *t, AValue *frame)
{
    AValue buf;
    
    if ((AMemberDirect(frame[0], A_STREAM_MODE) & A_MODE_INPUT) == AZero)
        return ARaiseByNum(t, AStdIoErrorNum, NULL); /* IDEA: Error msg */
    
    buf = AMemberDirect(frame[0], A_STREAM_INPUT_BUF);
    if (buf != AZero) {
        int ind = AValueToInt(AMemberDirect(frame[0], A_STREAM_INPUT_BUF_IND));
        int end = AValueToInt(AMemberDirect(frame[0], A_STREAM_INPUT_BUF_END));
        return ASubStr(t, buf, ind, end);
    } else
        return AMakeStr(t, ""); /* IDEA: Should we fill input buffer in some
                                   cases? */
}


static AValue StreamIter(AThread *t, AValue *frame)
{
    frame[1] = ACallValue(t, AGlobalByNum(StreamIterClassNum), 0,
                           frame + 1);
    if (AIsError(frame[1])) {
        frame[1] = AZero;
        return AError;
    }

    ASetMemberDirect(t, frame[1], 0, frame[0]);
    
    return frame[1];
}


static AValue StreamIterHasNext(AThread *t, AValue *frame)
{
    AValue v;
    
    frame[1] = AMemberDirect(frame[0], 0);
    v = StreamEof(t, frame + 1);
    if (AIsTrue(v)) {
        return AFalse;
    } else if (AIsFalse(v))
        return ATrue;
    else if (AIsError(v))
        return v;
    else
        return ARaiseValueErrorND(t, NULL);
}


static AValue StreamIterNext(AThread *t, AValue *frame)
{
    frame[1] = AMemberDirect(frame[0], 0);
    return AStreamReadLn(t, frame + 1);
}


static AValue CreateBuffer(AThread *t, AValue inst)
{
    int bufSize;
    AString *buf;

    if (AValueToInstance(inst)->member[A_STREAM_BUF_MODE] ==
            A_BUFMODE_BUFFERED)
        bufSize = A_IO_BUFFER_SIZE;
    else
        bufSize = A_IO_LINE_BUFFER_SIZE;

    *t->tempStack = inst;
    buf = AAlloc(t, sizeof(AValue) + bufSize);
    if (buf == NULL)
        return AError;
    inst = *t->tempStack;

    AInitNonPointerBlock(&buf->header, bufSize);

    if (AIsWideStr(AValueToInstance(inst)->member[A_STREAM_OUTPUT_BUF])) {
        /* NOTE: Use String as WideString object. */
        *t->tempStack = AWideStrToValue(buf);
    } else
        *t->tempStack = AStrToValue(buf);
    
    if (!ASetInstanceMember(t, AValueToInstance(inst), A_STREAM_OUTPUT_BUF,
                           t->tempStack))
        return AError;

    AValueToInstance(inst)->member[A_STREAM_OUTPUT_BUF_IND] = AZero;
    
    return ANil;
}


/* Read data to the input buffer of stream frame[0]. frame[0] must be a Stream
   object. Return AZero if successful, AError if there was an error and ANil
   if could not fill the buffer due to being at the end of stream. Assume that
   the input buffer is empty and that the stream is an input stream. */
static AValue StreamFillInputBuffer(AThread *t, AValue *frame)
{
    int begInd;
    int endInd;
    AInstance *inst;
    AValue v;

    /* Call _read until it returns something other that an empty string. */
    do {
        inst = AValueToInstance(frame[0]);

        /* Figure out the preferred size of the buffer. */
#ifdef A_HAVE_POSIX    
        if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_BUFFERED)
            frame[1] = AIntToValue(A_IO_BUFFER_SIZE);
        else
            frame[1] = AIntToValue(A_IO_LINE_BUFFER_SIZE);
#else    
        if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_BUFFERED)
            frame[1] = AIntToValue(A_IO_BUFFER_SIZE);
        else if (inst->member[A_STREAM_BUF_MODE] == A_BUFMODE_LINE_BUFFERED)
            frame[1] = AIntToValue(A_IO_LINE_BUFFER_SIZE);
        else
            frame[1] = AIntToValue(1);
#endif

        /* If the stream is unbuffered, function as if the stream was line
           buffered. We need to support buffering on unbuffered streams to
           support readLn() and eof(). */

        /* Clear the previous buffer string. */
        inst->member[A_STREAM_INPUT_BUF] = AZero;
        inst->member[A_STREAM_INPUT_BUF_END] = AZero;
        
        /* Read data from the stream. */
        v = ACallMethodByNum(t, AM__READ, 1, frame);
        if (AIsError(v))
            return AError; /* _read raised an exception. */

        /* If the return value was a string, setup indexes to the beginning
           and the end of the non-substring narrow/wide Str buffer that holds
           the string contents, and make v refer to this Str object. */
        if (AIsNarrowStr(v)) {
            begInd = 0;
            endInd = AGetStrLen(AValueToStr(v));
        } else if (AIsWideStr(v)) {
            begInd = 0;
            endInd = AGetWideStrLen(AValueToWideStr(v));
        } else if (AIsSubStr(v)) {
            /* Get indexes of a substring object and make v refer to the
               Str buffer referenced by the substring. */
            ASubString *ss;
            
            ss = AValueToSubStr(v);
            begInd = AValueToInt(ss->ind);
            endInd = begInd + AValueToInt(ss->len);
            
            v = ss->str;
        } else if (AIsNil(v))
            return ANil; /* End of file */
        else
            return ARaiseTypeErrorND(t, NULL); /* Not a string or nil */

        if (ACheckInterrupt(t))
            return AError; /* Interrupted */
        
        /* If endInd == 0, an empty string was returned (begInd cannot be 0
           if the string is empty). In this case, repeat the _read call until
           something else is returned.
           
           This condition can happen, for example, when reading the first
           character of an UTF-8 sequence from an unbuffered TextStream. The
           first read does not return anything, since only a partial UTF-8
           sequence has been read. */
    } while (endInd == 0);

    /* Now v is a non-substring Str value that holds the buffered data, and
       begInd and endInd define the range of data within v that was read. */
    
    /* Set stream buffer indices directly, since they are assumed to be short
       ints. FIX: What if they are not small enough? */
    inst = AValueToInstance(frame[0]);
    inst->member[A_STREAM_INPUT_BUF_IND] = AIntToValue(begInd);
    inst->member[A_STREAM_INPUT_BUF_END] = AIntToValue(endInd);

    /* Store the Str value that holds the stream buffer. */
    ASetMemberDirect(t, frame[0], A_STREAM_INPUT_BUF, v);
    
    return AZero; /* Success */
}


/* Flush output buffers of all open output streams. */
AValue AFlushOutputBuffers(AThread *t, AValue *arg)
{
    ALockStreams();

    *arg = AGlobalByNum(GL_OUTPUT_STREAMS);
    while (*arg != AZero) {
        if (AHandleException(t)) {
            t->contextIndex--;
            AUnlockStreams();
            return AError;
        }
        if (ACallMethodByNum(t, AM_FLUSH, 0, arg) == AError) {
            t->contextIndex--;
            AUnlockStreams();
            return AError;
        }
        t->contextIndex--;
        *arg = AMemberDirect(*arg, A_STREAM_LIST_NEXT);
    }

    AUnlockStreams();

    return ANil;
}


AValue ARaiseErrnoIoError(AThread *t, const char *path)
{
    return ARaiseErrnoExceptionByNum(t, AStdIoErrorNum, path);
}


A_MODULE(io, "io")
    A_DEF("Main", 0, 5, AIoMain)
    
    A_DEF_P("__FlushOutputBuffers", 0, 1, AFlushOutputBuffers,
            &AFlushOutputBuffersNum)

    A_SYMBOLIC_CONST_P("Unbuffered", &AUnbufferedNum)
    A_SYMBOLIC_CONST_P("LineBuffered", &ALineBufferedNum)
    A_SYMBOLIC_CONST_P("Buffered", &ABufferedNum)
    A_SYMBOLIC_CONST_P("Input", &AInputNum)
    A_SYMBOLIC_CONST_P("Output", &AOutputNum)
    A_SYMBOLIC_CONST_P("Append", &AAppendNum)
    A_SYMBOLIC_CONST_P("Narrow", &ANarrowNum)
    A_SYMBOLIC_CONST_P("Protected", &AProtectedNum)
    
    A_EMPTY_CONST_P("RawStdIn", &ARawStdInNum)
    A_EMPTY_CONST_P("RawStdOut", &ARawStdOutNum)
    A_EMPTY_CONST_P("RawStdErr", &ARawStdErrNum)
    
    A_EMPTY_CONST_P("StdIn", &AStdInNum)
    A_EMPTY_CONST_P("StdOut", &AStdOutNum)
    A_EMPTY_CONST_P("StdErr", &AStdErrNum)

    /* NOTE: The stack frames of write, writeLn and flush methods must
             have the same size. Other functions may depend on some of these
             methods having these specific sizes. */
    A_CLASS_PRIV("Stream", A_NUM_STREAM_MEMBER_VARS)
        A_IMPLEMENT("std::Iterable")
        A_METHOD_OPT("create", 0, 4, 0, AStreamCreate)
        A_METHOD_VARARG("write", 0, 0, 1, AStreamWrite)
        A_METHOD_VARARG("writeLn", 0, 0, 1, AStreamWriteLn)
        A_METHOD_OPT("read", 0, 1, 1, StreamRead)
        /* Frame size compatibility requirements: std::ReadLn (+1),
             __StreamIter next (+1) */
        A_METHOD("readLn", 0, 4, AStreamReadLn)
        A_METHOD("readLines", 0, 1, StreamReadLines)
        A_METHOD("flush", 0, 3, AStreamFlush)
        A_METHOD("eof", 0, 1, StreamEof)
        A_METHOD("close", 0, 0, StreamClose)
        A_METHOD("peek", 0, 0, StreamPeek)
        A_METHOD("iterator", 0, 1, StreamIter)
        A_METHOD("#i", 0, 0, AStreamInitialize)
    A_END_CLASS()
    
    A_CLASS_PRIV_P("__StreamIter", 1, &StreamIterClassNum)
        A_IMPLEMENT("std::Iterator")
        A_METHOD("hasNext", 0, 2, StreamIterHasNext)
        A_METHOD("next", 0, 5, StreamIterNext)
    A_END_CLASS()
    
    A_CLASS_PRIV_P("File", 1, &AFileClassNum)
        A_INHERIT("::Stream")
        A_METHOD_OPT("create", 1, 5, 0, AFileCreate)
        A_METHOD_VARARG("_write", 0, 0, 1, AFile_Write)
        A_METHOD("_read", 1, 2, AFile_Read)
        A_METHOD("seek", 1, 1, AFileSeek)
        A_METHOD("pos", 0, 0, AFilePos)
        A_METHOD("size", 0, 0, AFileSize)
        A_METHOD("close", 0, 0, AFileClose)
        A_METHOD("__handle", 0, 0, AFile__Handle)
        A_METHOD("#i", 0, 0, AStreamInitialize)
        /* A_EMPTY_CONST_P("name", &NameMemberNum) */
    A_END_CLASS()

    A_EMPTY_CONST_P("DefaultEncoding", &ADefaultEncodingNum)
    
    A_EMPTY_CONST_P("Bom", &ABomNum)
    
    A_CLASS("TextStream")
      A_INHERIT("::Stream")
      A_METHOD_OPT("create", 1, 4, 2, ATextStreamCreate)
      A_METHOD_VARARG("_write", 0, 0, 2, ATextStream_Write)
      A_METHOD("_read", 1, 2, ATextStream_Read)
      A_METHOD("close", 0, 3, ATextStreamClose) /* Compat: Stream flush */
      A_METHOD("flush", 0, 3, ATextStreamFlush) /* Compat: Stream flush */

      A_VAR_P(A_PRIVATE("unstrict"), &AUnstrictMemberNum)
      A_VAR_P(A_PRIVATE("stream"), &AStreamMemberNum)
      A_VAR_P(A_PRIVATE("decoder"), &ATextStreamDecoderNum)
      A_VAR_P(A_PRIVATE("encoder"), &ATextStreamEncoderNum)
    A_END_CLASS()

    A_CLASS("TextFile")
      A_INHERIT("::TextStream")
      A_METHOD_OPT("create", 1, 6, 2, ATextFileCreate)
    A_END_CLASS()
A_END_MODULE()
