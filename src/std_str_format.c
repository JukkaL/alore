/* std_str_format.c - Str format method

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"
#include "str.h"
#include "internal.h"

#include <math.h>


/* Size of the internal buffer used for narrow strings that are short enough. */
#define FORMAT_BUF_SIZE 512
/* Flag for FormatOutput index to specify a wide buffer. */
#define WIDE_BUF_FLAG (1U << 31)


/* FormatOutput is a helper struct that contains the accumulated result of
   format() + some related information. */
typedef struct {
    /* The current thread. This is stored here as a convenience to avoid
       passing it as an extra argument to various functions. */
    AThread *t;
    /* If the internal buffer cannot be used (the string has wide characters or
       it is too long), this attribute refers to a string object (narrow or
       wide) that acts as a variable-sized heap-allocated buffer. This is a
       pointer to a location that is seen by the garbage collector. */
    AValue *str;
    /* The internal buffer used for short-enough narrow strings. If the output
       contains wide characters (index has WIDE_BUF_FLAG set) or is too long
       for this buffer, the heap-allocated buffer is used instead. */
    unsigned char buf[FORMAT_BUF_SIZE];
    /* Length of output. It is ORed with WIDE_BUF_FLAG if the output contains
       wide characters (> 0xff). In this case the heap-allocated buffer is
       always used. */
    unsigned index;
    /* Length of the buffer that is currently being used. */
    unsigned len;
} FormatOutput;


/* Append a character to the output buffer. */
#define Append(out, ch) \
    ((out)->index < FORMAT_BUF_SIZE && (ch) < 0x100 \
     ? (out)->buf[(out)->index++] = (ch)            \
     : (Append_(out, ch), 0))

/* Append a non-wide character to the output buffer. */
#define AppendCh(out, ch) \
    ((out)->index < FORMAT_BUF_SIZE ?  \
     (out)->buf[(out)->index++] = (ch) \
     : (Append_(out, ch), 0))


/* Append a character to a heap-allocated buffer. Also convert the buffer to
   a wide buffer, if necessary. Also allocate the buffer, if necessary. */
static void Append_(FormatOutput *out, int ch)
{
    if ((out->index & ~WIDE_BUF_FLAG) == out->len) {
        /* Allocate larger buffer. */
        if (out->index == FORMAT_BUF_SIZE && !(out->index & WIDE_BUF_FLAG)) {
            *out->str = AMakeEmptyStr(out->t, FORMAT_BUF_SIZE * 2);
            ACopyMem(AGetStrElem(*out->str), out->buf, FORMAT_BUF_SIZE);
        } else if (out->index & WIDE_BUF_FLAG) {
            AValue s = AMakeEmptyStrW(out->t, out->len * 2);
            ACopyMem(AGetWideStrElem(s), AGetWideStrElem(*out->str), out->len *
                    sizeof(AWideChar));
            *out->str = s;
        } else {
            AValue s = AMakeEmptyStr(out->t, out->len * 2);
            ACopyMem(AGetStrElem(s), AGetStrElem(*out->str), out->len);
            *out->str = s;
        }
        out->len *= 2;
    }
    if (ch >= 0x100 && !(out->index & WIDE_BUF_FLAG)) {
        /* Widen the buffer. */
        AValue s = AMakeEmptyStrW(out->t, out->len);
        int i;
        if (out->len > FORMAT_BUF_SIZE) {
            for (i = 0; i < out->len; i++)
                ASetStrItem(s, i, AStrItem(*out->str, i));
        } else {
            for (i = 0; i < out->index; i++)
                ASetStrItem(s, i, out->buf[i]);
        }
        *out->str = s;
        out->index |= WIDE_BUF_FLAG;
    }

    ASetStrItem(*out->str, (out->index & ~WIDE_BUF_FLAG), ch);
    out->index++;
}


/* Append a narrow zero-terminated string to the output buffer. */
static void AppendStr(FormatOutput *out, const char *str)
{
    while (*str != '\0') {
        AppendCh(out, *str);
        str++;
    }
}


#define NUM_BUF_SIZE 150


static void NumberToScientific(FormatOutput *output, AValue num, int numFrac,
                               int expLen, int expChar, ABool plusExp, int
                               optFrac);


/* Convert a number to a string using a non-scientific format. The num argument
   is the number to convert, intLen is the minimum number of digits in the
   integral part of the number, fractionLen is the minimum length of the
   fraction and optFrac is the number of additional fraction digits to include
   if they are non-zero. */
static void NumberToStr(FormatOutput *output, AValue num, int intLen,
                        int fractionLen, int optFrac)
{
    int i;

    if (AIsInt(num)) {
        /* Int */
        int sign;

        output->str[1] = num;
        output->str[1] = AStdStr(output->t, output->str + 1);
        if (output->str[1] == AError)
            ADispatchException(output->t);

        if (AStrItem(output->str[1], 0) == '-') {
            Append(output, '-');
            sign = 1;
        } else
            sign = 0;

        for (i = 0; i < intLen - AStrLen(output->str[1]) + sign; i++)
            Append(output, '0');

        for (i = sign; i < AStrLen(output->str[1]); i++)
            Append(output, AStrItem(output->str[1], i));

        if (optFrac < fractionLen) {
            Append(output, '.');
            for (i = 0; i < fractionLen - optFrac; i++)
                Append(output, '0');
        }
    } else {
        /* Float */
        double f;
        char s[NUM_BUF_SIZE];
        int si;
        int sign;

        f = AGetFloat(output->t, num);
        /* Display very small and very large values in scientific format to
           avoid producing very long results. */
        if (f > 1e50 || f < -1e50) {
            NumberToScientific(output, num, fractionLen, 1, 'e', TRUE,
                               optFrac);
            return;
        }

        if (AIsNaN(f)) {
            AppendStr(output, "nan");
            return;
        }

        if (fractionLen + 50 + 5 > NUM_BUF_SIZE)
            ARaiseValueError(output->t, "Formatted Float is too long");

        si = sprintf(s, "%.*f", fractionLen, AGetFloat(output->t, num));

        if (s[0] == '-') {
            AppendCh(output, '-');
            sign = 1;
        } else
            sign = 0;

        if (intLen > 1) {
            int isFract = strchr(s, '.') != NULL;
            while (intLen + isFract + fractionLen > si - sign) {
                AppendCh(output, '0');
                intLen--;
            }
        }

        while (optFrac > 0 && si > 0 && s[si - 1] == '0') {
            si--;
            optFrac--;
            fractionLen--;
        }

        for (i = sign; i < si; i++)
            AppendCh(output, s[i]);
    }
}


/* Convert a number using a scientific format (e.g. 0.00e+00). The num argument
   is the number to convert. The argument numFrac is the number of digits in
   the fraction, expLen is the number of digits in the exponent, plusExp
   defines whether to prefix a positive exponent always with a '+', and optFrac
   speficies the number of fraction digits that are only generated if they are
   not zeroes. */
static void NumberToScientific(FormatOutput *output, AValue num, int numFrac,
                                int expLen, int expChar, ABool plusExp, int
                                optFrac)
{
    /* FIX: Use sprintf for the conversion for better accuracy. */

    double f = AGetFloat(output->t, num);
    int exp = 0;
    ABool sign = f < 0.0;
    int i;
    char s[NUM_BUF_SIZE];
    int si;
    int s0;

    /* Handle infinities and NaN's as special cases. */
    if (AIsNaN(f)) {
        AppendStr(output, "nan");
        return;
    } else if (AIsInf(f)) {
        if (f > 0.0)
            AppendStr(output, "inf");
        else
            AppendStr(output, "-inf");
        return;
    }

    if (sign)
        f = -f;

    if (f != 0.0) {
        double max = 10.0 - 0.5 * pow(10.0, -numFrac);
        double min = 1.0 - 0.5 * pow(10.0, -numFrac);
        while (f >= max) {
            f /= 10.0;
            exp++;
        }
        while (f < min) {
            f *= 10.0;
            exp--;
        }
    }

    f = floor(f * pow(10.0, numFrac) + 0.5);

    if (numFrac >= NUM_BUF_SIZE - 2)
        ARaiseValueError(output->t, "Formatted Float is too long");

    si = 0;
    for (i = 0; i <= numFrac; i++) {
        int d = (int)fmod(f, 10.0);
        s[si++] = '0' + d;
        f /= 10.0;
    }

    s0 = 0;
    while (optFrac > 0 && s0 < si && s[s0] == '0') {
        s0++;
        optFrac--;
        numFrac--;
    }

    if (sign)
        AppendCh(output, '-');
    AppendCh(output, s[si - 1]);
    if (numFrac > 0)
        AppendCh(output, '.');
    for (i = si - 2; i >= s0; i--)
        AppendCh(output, s[i]);
    AppendCh(output, expChar);
    if (plusExp && exp >= 0)
        AppendCh(output, '+');
    NumberToStr(output, AMakeInt(output->t, exp), expLen, 0, 0);
}


/* Str format(...)
   Format a string based on a format string which may contain formatting
   sequences of form {...}; these correspond to the arguments of the method.
   The base string object represents the format string. */
AValue AFormat(AThread *t, AValue *frame)
{
    int fi;      /* Format string index */
    int ai;      /* Argument index */
    int fmtLen;  /* Format string length */
    FormatOutput output;

    AExpectStr(t, frame[0]);

    output.t = t;
    output.index = 0;
    output.len = FORMAT_BUF_SIZE;
    output.str = &frame[2];

    fmtLen = AStrLen(frame[0]);
    ai = 0;

    /* IDEA: Refactor this into multiple functions. */

    /* Iterate over the format string. Handle { sequences and copy other
       characters directly to the output buffer (} may be duplicated). */
    for (fi = 0; fi < fmtLen; fi++) {
        int ch = AStrItem(frame[0], fi);
        if (ch == '{') {
            if (fi == fmtLen - 1 || AStrItem(frame[0], fi + 1) == '{') {
                /* Literal '{'. */
                fi++;
                Append(&output, ch);
            } else {
                /* A format sequence {...}. */
                int negAlign = FALSE;
                int align = 0;
                int oldInd;
                int oldOutInd;
                AValue arg;

                fi++;
                oldInd = fi;

                /* Parse alignment */
                if (fi < fmtLen && AStrItem(frame[0], fi) == '-'
                    && AIsDigit(AStrItem(frame[0], fi + 1))) {
                    negAlign = TRUE;
                    fi++;
                }
                if (fi < fmtLen && AIsDigit(AStrItem(frame[0], fi))) {
                    do {
                        align = align * 10 + AStrItem(frame[0], fi) - '0';
                        fi++;
                    } while (fi < fmtLen && AIsDigit(AStrItem(frame[0], fi)));
                    if (AStrItem(frame[0], fi) != ':') {
                        fi = oldInd;
                        align = 0;
                    } else
                        fi++;
                }

                oldOutInd = output.index & ~WIDE_BUF_FLAG;

                if (ai >= AArrayLen(frame[1]))
                    ARaiseValueError(t, "Too few arguments");

                arg = AArrayItem(frame[1], ai);
                if (AIsInstance(arg)
                    && AStrItem(frame[0], fi) != '}'
                    && AMember(t, arg, "_format") != AError) {
                    int i;
                    oldInd = fi;
                    while (fi <= fmtLen && AStrItem(frame[0], fi) != '}')
                        fi++;
                    if (fi >= fmtLen)
                        ARaiseValueError(t, "Unterminated format");
                    frame[3] = AArrayItem(frame[1], ai);
                    frame[4] = ASubStr(t, frame[0], oldInd, fi);
                    frame[3] = ACallMethod(t, "_format", 1, frame + 3);
                    if (AIsError(frame[3]))
                        return AError;
                    AExpectStr(t, frame[3]);
                    for (i = 0; i < AStrLen(frame[3]); i++)
                        Append(&output, AStrItem(frame[3], i));
                } else {
                    /* Either format an Int/Float or use the default conversion
                       by calling std::Str. */
                    int minNumLen = 0;
                    int fractionLen = 0;
                    int optFractionLen = 0;
                    int expLen = 0;
                    int fraction = FALSE;
                    int scientific = FALSE;
                    int plusExp = FALSE;
                    int expChar = ' ';

                    while (fi < fmtLen &&
                           (ch = AStrItem(frame[0], fi)) != '}') {
                        if (ch == '0') {
                            if (scientific)
                                expLen++;
                            else if (fraction)
                                fractionLen++;
                            else
                                minNumLen++;
                        } else if (ch == '.')
                            fraction = TRUE;
                        else if (ch == 'e' || ch == 'E') {
                            scientific = TRUE;
                            expChar = ch;
                        } else if (ch == '+' && scientific)
                            plusExp = TRUE;
                        else if (ch == '#' && fraction && !scientific) {
                            fractionLen++;
                            optFractionLen++;
                        } else
                            ARaiseValueError(t, "Invalid character in format");
                        fi++;
                    }

                    if (scientific)
                        NumberToScientific(&output, arg, fractionLen, expLen,
                                           expChar, plusExp, optFractionLen);
                    else if (minNumLen > 0 || fraction)
                        NumberToStr(&output, arg, minNumLen, fractionLen,
                                    optFractionLen);
                    else {
                        /* Not a number formatting sequence. Use std::Str to
                           convert the argument. */
                        int i;

                        frame[3] = arg;
                        frame[3] = AStdStr(t, &frame[3]);
                        if (AIsError(frame[3]))
                            return AError;

                        for (i = 0; i < AStrLen(frame[3]); i++)
                            Append(&output, AStrItem(frame[3], i));
                    }
                }

                if (fi >= fmtLen)
                    ARaiseValueError(t, "Unterminated format");

                align -= (output.index & ~WIDE_BUF_FLAG) - oldOutInd;
                if (align > 0) {
                    int i;

                    for (i = 0; i < align; i++)
                        Append(&output, ' ');

                    if (!negAlign) {
                        if (output.index < FORMAT_BUF_SIZE) {
                            AMoveMem(output.buf + oldOutInd + align,
                                    output.buf + oldOutInd,
                                    output.index - oldOutInd - align);
                            for (i = 0; i < align; i++)
                                output.buf[oldOutInd + i] = ' ';
                        } else {
                            for (i = (output.index & ~WIDE_BUF_FLAG) - 1;
                                 i >= oldOutInd + align; i--)
                                ASetStrItem(frame[2], i,
                                            AStrItem(frame[2], i - align));
                            for (i = 0; i < align; i++)
                                ASetStrItem(frame[2], oldOutInd + i, ' ');
                        }
                    }
                }
                ai++;
            }
        } else if (ch == '}') {
            /* Literal '}' can be duplicated (but does not need to be). */
            if (fi < fmtLen - 1 && AStrItem(frame[0], fi + 1) == '}')
                fi++;
            Append(&output, ch);
        } else
            Append(&output, ch);
    }

    if (ai < AArrayLen(frame[1]))
        ARaiseValueError(t, "Too many arguments");

    if (output.index <= FORMAT_BUF_SIZE) {
        /* The output contains only narrow characters (since the index does not
           have the WIDE_BUF_FLAG flag) and is short enough to not require a
           heap-allocated buffer. Create a string based on the internal
           buffer. */
        AValue res = AMakeEmptyStr(t, output.index);
        ACopyMem(AGetStrElem(res), output.buf, output.index);
        return res;
    } else {
        /* The output is stored in a heap-allocated buffer (represented as a
           Str object). Return the initialized part of the buffer. */
        return ASubStr(t, frame[2], 0, output.index & ~WIDE_BUF_FLAG);
    }
}
