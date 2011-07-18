/* io_text.c - io module (TextStream related functionality)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementations of io::TextStream and io::TextFile and related
   functionality. */

#include "alore.h"
#include "io_module.h"
#include "encodings_module.h"
#include "internal.h"

#include <ctype.h>
#include <stdlib.h>

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#include <locale.h>
#endif


int ABomNum;

int ADefaultEncodingNum;

int AUnstrictMemberNum;

int AStreamMemberNum;
int ACodecMemberNum;


static const AWideChar ReplChar[2] = { 0xfffd, '\0' };


AValue ATextStreamCreate(AThread *t, AValue *frame)
{
    int i;
    ABool isStrict = TRUE;
    AValue buffering = ADefault;
    
    if (!AIsType(frame[2])) {
        for (i = 4; i >= 3; i--)
            frame[i] = frame[i - 1];
        frame[2] = AGlobalByNum(ADefaultEncodingNum);
    }

    ASetMemberDirect(t, frame[0], AStreamMemberNum, frame[1]);

    for (i = 3; i <= 4; i++) {
        if (frame[i] == AGlobalByNum(AUnstrictNum))
            isStrict = FALSE;
        else if (frame[i] == AGlobalByNum(ALineBufferedNum) ||
                 frame[i] == AGlobalByNum(AUnbufferedNum))
            buffering = frame[i];
    }

    if (isStrict)
        frame[2] = ACallValue(t, frame[2], 0, frame + 5);
    else {
        ASetMemberDirect(t, frame[0], AUnstrictMemberNum, ATrue);
        frame[5] = AGlobalByNum(AUnstrictNum);
        frame[2] = ACallValue(t, frame[2], 1, frame + 5);
    }
    
    ASetMemberDirect(t, frame[0], ACodecMemberNum, frame[2]);

    frame[1] = AGlobal(t, "io::Input");
    frame[2] = AGlobal(t, "io::Output");
    frame[3] = buffering;
    frame[4] = ADefault;
    
    return AStreamCreate(t, frame);
}


AValue ATextStream_Write(AThread *t, AValue *frame)
{
    int i;

    for (i = 0; i < AArrayLen(frame[1]); i++) {
        frame[2] = AMemberDirect(frame[0], ACodecMemberNum);
        frame[3] = AArrayItem(frame[1], i);
        frame[2] = ACallMethod(t, "encode", 1, frame + 2);
        if (AIsError(frame[2]))
            return AError;
        ASetArrayItem(t, frame[1], i, frame[2]);
    }

    frame[2] = AMemberDirect(frame[0], AStreamMemberNum);
    frame[3] = frame[1];
    return ACallMethodVarArg(t, "write", 0, frame + 2);
}


AValue AIsUnprocessed(AThread *t, AValue *self, AValue *frame)
{
    frame[0] = AMemberDirect(*self, ACodecMemberNum);
    frame[0] = ACallMethod(t, "unprocessed", 0, frame);
    if (AIsError(frame[0]))
        return AError;
    if (!AIsStr(frame[0]))
        ARaiseTypeError(t, AMsgStrExpected);
    return AStrLen(frame[0]) != 0 ? ATrue : AFalse;
}


AValue ATextStream_Read(AThread *t, AValue *frame)
{
    /* Read the contents of the input buffer of the wrapped stream. */
    frame[2] = AMemberDirect(frame[0], AStreamMemberNum);
    frame[3] = ACallMethod(t, "peek", 0, frame + 2);
    if (AIsError(frame[3]))
        return AError;
    AExpectStr(t, frame[3]);
    
    /* If the wrapped stream has some data in the input buffer, read from
       it. */
    if (AStrLen(frame[3]) > 0) {
        /* FIX: Not 64-bit clean */
        unsigned s = AGetIntU(t, frame[1]);
        unsigned s2 = AStrLen(frame[3]);
        
        frame[2] = AMemberDirect(frame[0], AStreamMemberNum);
        frame[3] = AMakeIntU(t, AMin(s, s2));
        frame[3] = ACallMethod(t, "read", 1, frame + 2);
    } else {
        /* Call the _read method of the wrapped stream. The buffering in the
           wrapped stream is thus not used, but the above test makes sure that
           the buffer is empty. */
        frame[2] = AMemberDirect(frame[0], AStreamMemberNum);
        frame[3] = frame[1];
        frame[3] = ACallMethod(t, "_read", 1, frame + 2);
    }
    
    if (AIsError(frame[3]))
        return AError;
    
    if (AIsNil(frame[3])) {
        /* End of stream encountered. */
        
        /* If the stream is unstrict and there are unprocessed characters at
           the end of the file, return an additional replacement character to
           signify the partial character at the end of the stream. */
        
        frame[3] = AIsUnprocessed(t, frame, frame + 3);
        if (AIsError(frame[3]))
            return AError;
        
        if (AIsTrue(AMemberDirect(frame[0], AUnstrictMemberNum)) &&
            AIsTrue(frame[3])) {
            /* Mark that we have processed the partial character. */
            /* FIX: If we have an input-output stream, the caller might
               continue using the stream after having reached EOF, so modifying
               the flag here is most certainly a bad idea! */
            ASetMemberDirect(t, frame[0], AUnstrictMemberNum, AFalse);
            return AMakeStrW(t, ReplChar);
        } else
            return ANil;
    }

    /* Call the decode method of the codec to convert the string to Unicode
       and return the result. Note that this might result in return an empty
       string (if only a partial character has been read) even if not at the
       end of the file. */
    frame[2] = AMemberDirect(frame[0], ACodecMemberNum);
    return ACallMethod(t, "decode", 1, frame + 2);
}


AValue ATextStreamClose(AThread *t, AValue *frame)
{
    ABool isError = FALSE;

    /* Flush any pending data in the TextStream output buffers. */
    if (AIsError(AStreamFlush(t, frame))) {
        /* Flush failed. Close the stream anyway below to avoid leaking
           resources. If close raises an exception, that exception is sadly
           lost, but it's probably better than the alternatives. */
        
        /* Record the exception. */
        isError = TRUE;        
        frame[2] = t->exception;
    }
    
    frame[1] = AMemberDirect(frame[0], AStreamMemberNum);
    if (ACallMethod(t, "close", 0, frame + 1) == AError) {
        /* Only propagate the exception if flush succeeded. */
        if (!isError)
            return AError;
    }
    
    /* Do we need to reraise an exception? */
    if (isError)
        return ARaiseValue(t, frame[2]);
    else
        return ANil;
}


AValue ATextStreamFlush(AThread *t, AValue *frame)
{
    /* Flush any pending data in TextStream to the wrapped stream */
    if (AIsError(AStreamFlush(t, frame)))
        return AError;

    /* Flush the wrapped stream (it might be buffered as well). */
    frame[1] = AMemberDirect(frame[0], AStreamMemberNum);
    if (ACallMethod(t, "flush", 0, frame + 1) == AError)
        return AError;
    
    return ANil;
}


AValue ATextFileCreate(AThread *t, AValue *frame)
{
    AValue tmp;
    AValue open;
    int nargs;
    int i;
    ABool isStrict = TRUE;
    AValue buffering = ADefault;

    if (!AIsType(frame[2])) {
        for (i = 5; i >= 3; i--)
            frame[i] = frame[i - 1];
        frame[2] = AGlobalByNum(ADefaultEncodingNum);
    }

    for (nargs = 3; nargs < 6 && frame[nargs] != ADefault; nargs++) {
        if (frame[nargs] == AGlobalByNum(AStrictNum) ||
            frame[nargs] == AGlobalByNum(AUnstrictNum)) {
            if (frame[nargs] == AGlobalByNum(AUnstrictNum))
                isStrict = FALSE;
            for (i = nargs; i < 6; i++)
                frame[i] = frame[i + 1];
            frame[5] = ADefault;
            nargs--;
        } else if (frame[nargs] == AGlobalByNum(AUnbufferedNum))
            buffering = AGlobalByNum(AUnbufferedNum);
        else if (frame[nargs] == AGlobalByNum(ALineBufferedNum))
            buffering = AGlobalByNum(ALineBufferedNum);
    }

    tmp = frame[1];
    frame[1] = frame[2]; /* frame[1] = encoding */
    frame[2] = tmp;      /* frame[2] = path */
    
    open = AGlobal(t, "io::File");
    /* Frame contents: [path, options...] */
    tmp = ACallValue(t, open, nargs - 2, frame + 2);
    if (AIsError(tmp))
        return AError;

    frame[2] = frame[1]; /* frame[2] = encoding */
    frame[1] = tmp;      /* frame[1] = file */
    if (isStrict) {
        frame[3] = buffering;
        frame[4] = ADefault;
    } else {
        frame[3] = AGlobalByNum(AUnstrictNum);
        frame[4] = buffering;
    }
    
    return ATextStreamCreate(t, frame);
}


#ifndef A_HAVE_WINDOWS

/* Map codeset name (e.g. "UTF-8") to a encoding type (e.g. textio::Utf8).
   Return nil if not successful. */
static AValue MapCodesetToEncodingType(const char *codeset)
{
    char normCodeset[128];
    int i, j;
    int num;

    if (strlen(codeset) > 127)
        return ANil;

    /* Normalize codeset name: map '_' to '-' and convert everything to upper
       case. */
    i = j = 0;
    while (codeset[i] != '\0') {
        if (codeset[i] == '_')
            normCodeset[j] = '-';
        else
            normCodeset[j] = toupper(codeset[i]);
        j++;
        i++;
    }
    normCodeset[j] = '\0';

    /* Map normalized codeset name to a type number. Note that only some
       encodings are supported. */
    num = -1;
    if (strcmp(normCodeset, "ANSI-X3.4-1968") == 0
        || strcmp(normCodeset, "UTF-8") == 0)
        num = AUtf8Num;
    else if (strcmp(normCodeset, "US-ASCII") == 0)
        num = AAsciiNum;
    else if (strcmp(normCodeset, "ISO-8859-1") == 0)
        num = AIso8859_1Num;
    else if (strcmp(normCodeset, "ISO-8859-2") == 0)
        num = AIso8859_2Num;
    else if (strcmp(normCodeset, "ISO-8859-3") == 0)
        num = AIso8859_3Num;
    else if (strcmp(normCodeset, "ISO-8859-4") == 0)
        num = AIso8859_4Num;
    else if (strcmp(normCodeset, "ISO-8859-5") == 0)
        num = AIso8859_5Num;
    else if (strcmp(normCodeset, "ISO-8859-6") == 0)
        num = AIso8859_6Num;
    else if (strcmp(normCodeset, "ISO-8859-7") == 0)
        num = AIso8859_7Num;
    else if (strcmp(normCodeset, "ISO-8859-8") == 0)
        num = AIso8859_8Num;
    else if (strcmp(normCodeset, "ISO-8859-9") == 0)
        num = AIso8859_9Num;
    else if (strcmp(normCodeset, "ISO-8859-10") == 0)
        num = AIso8859_10Num;
    else if (strcmp(normCodeset, "ISO-8859-11") == 0)
        num = AIso8859_11Num;
    else if (strcmp(normCodeset, "ISO-8859-13") == 0)
        num = AIso8859_13Num;
    else if (strcmp(normCodeset, "ISO-8859-14") == 0)
        num = AIso8859_14Num;
    else if (strcmp(normCodeset, "ISO-8859-15") == 0)
        num = AIso8859_15Num;
    else if (strcmp(normCodeset, "ISO-8859-16") == 0)
        num = AIso8859_16Num;
    else if (strcmp(normCodeset, "KOI8-R") == 0)
        num = AKoi8RNum;
    else if (strcmp(normCodeset, "KOI8-U") == 0)
        num = AKoi8UNum;
    
    if (num != -1)
        return AGlobalByNum(num);
    else
        return ANil;
}

#endif


AValue AInitDefaultEncoding(AThread *t)
{
#ifdef HAVE_LANGINFO_H
    const char *codeset;
    char *savedLocale;
#endif

    /* Use ISO Latin 1 as the fallback encoding in case we cannot determine the
       encoding using locale information. */
    ASetConstGlobalByNum(t, ADefaultEncodingNum, AGlobalByNum(AIso8859_1Num));

#ifdef A_HAVE_WINDOWS
    /* Use UTF-8 as the default encoding in Windows. There are a few problems
       with this:

        - Some Windows applications only recognize UTF-8 text files if they
          have the BOM mark at the beginning (e.g. Notepad).
        - We cannot use UTF-8 in the command prompt???
    */
    ASetConstGlobalByNum(t, ADefaultEncodingNum, AGlobalByNum(AUtf8Num));
#else
    
#ifdef HAVE_LANGINFO_H
    /* Determine DefaultEncoding based on the current locale setting (LC_CTYPE
       environment variable). */

    /* Save current locale setting. */
    savedLocale = setlocale(LC_CTYPE, NULL);
    if (savedLocale == NULL)
        return ARaiseMemoryError(t);
    savedLocale = strdup(savedLocale);
    if (savedLocale == NULL)
        return ARaiseMemoryError(t);
    
    /* Initialize locale based on the environment. */
    setlocale(LC_CTYPE, "");

    /* Determine encoding. */
    codeset = nl_langinfo(CODESET);
    if (codeset != NULL) {
        AValue encoding = MapCodesetToEncodingType(codeset);
        if (!AIsNil(encoding))
            ASetConstGlobalByNum(t, ADefaultEncodingNum, encoding);
    }
    
    /* Restore locale setting. */
    setlocale(LC_CTYPE, savedLocale);
    free(savedLocale);
#endif

#endif
    
    return ANil;
}
