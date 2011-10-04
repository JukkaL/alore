/* re_comp.c - __re module (compiling regexps to an internal form)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "str.h"
#include "mem.h"
#include "gc.h"
#include "re_internal.h"
#include "internal.h"
#include "str_internal.h"
#include "gc_internal.h"


/* Initial code block size is at least MIN_BIG_BLOCK bytes so that the block
   will be unmovable. */
#define GetInitialOutputBufSize(strLen) \
    AMax(A_MIN_BIG_BLOCK_SIZE / sizeof(AReOpcode), (strLen) * 2)
/* IDEA: We waste memory if the expression is rather small. Devise a better
         method for allocating unmovable blocks (AllocUnmovable seems to not
         work -- it causes the heap size to skyrocket in some cases). */


/* Error messages */
static const char ErrInternalOverflow[]     = "Internal overflow";
static const char ErrUnmatchedLparen[]      = "Unmatched (";
static const char ErrUnmatchedRparen[]      = "Unmatched )";
static const char ErrUnmatchedLbracket[]    = "Unmatched [";
static const char ErrTrailingBackslash[]    = "Trailing backslash";
static const char ErrInvalidCharacterSet[]  = "Invalid character set";
static const char ErrInvalidRange[]         = "Invalid range";
static const char ErrInvalidBackReference[] = "Invalid back reference";
static const char ErrInvalidRepeat[]        = "Invalid repeat";


/* State of the parser */
typedef struct {
    ABool isError;          /* Was there an error during compilation?
                               If TRUE, thread->exception has been updated. */
    AThread *t;

    AValue strVal;
    AWideChar *str;         /* Pointer to the regexp string */
    AWideChar *strBeg;
    AWideChar *strEnd;      /* End of the regexp string */

    int mustStr;            /* Literal string that must be found for a match */
    int mustStrBack;        /* Max number of characters before literal str */

    AReOpcode *buf;          /* Pointer to output buffer */
    int bufInd;
    int bufLen;
    
    int numParen;           /* Number of subexpression in parentheses */
    int parenFlags;         /* Bitmap telling which subexprs are complete */

    unsigned minLen;        /* Minimum length of match */
    unsigned maxLen;        /* Maximum length of match */
    
    int depth;
    int flags;

    AReOpcode startChar[A_SET_SIZE];
} ParseInfo;


typedef struct {
    int length;
    int maxLength;
    AWideChar *data;
} WideCharSet;


static void AddToWideSet(ParseInfo *info, WideCharSet *set, AWideChar lo,
                         AWideChar hi);
#define InitWideSet(set) ((set)->length = 0, (set)->maxLength = 0)
#define FreeWideSet(set) \
    do { if ((set)->length > 0) AFreeStatic((set)->data); } while (0)
#define WideSetLen(set) ((set) != NULL ? (set)->length : 0)
#define WideSetLo(set, i) ((set)->data[i * 2])
#define WideSetHi(set, i) ((set)->data[i * 2 + 1])


/* Special value representing character classes */
/* IDEA: Use different constants for different character classes. Replace all
         references to CharClass[x] to use these constants. */
#define CC (A_EMPTY + 1)

#define CC_D CC
#define CC_S (CC + 1)
#define CC_W (CC + 2)

/* Flag for complemented character classes */
#define CC_COMP 128


/* Conversion table for escaped lower case characters a .. z */
/* IDEA: Make sure CC_x does not collide with the other characters.*/
static const unsigned char CharConv[] = {
    '\a', '\b', 'c',   CC_D, 'e', '\f',         /* a .. f */
     'g',  'h', 'i',  'j',   'k',  'l',         /* g .. l */
     'm', '\n', 'o',  'p',   'q', '\r',         /* m .. r */
    CC_S, '\t', 'u', '\v',  CC_W,  'x',         /* s .. x */
     'y',  'z'                                  /* y .. z */
};


#define EmitBranch(info, opcode, dest) \
    EmitBranchAt(info, (info)->bufInd, opcode, dest)

#define Unemit(info, len) \
    ((info)->bufInd -= (len))


static void ParseExpr(ParseInfo *info);
static void ParseAlternatives(ParseInfo *info);
static void ParseConcat(ParseInfo *info);
static void ParseParen(ParseInfo *info);
static void ParseRepetition(ParseInfo *info, int oldMinLen, int oldMaxLen,
                           int size, int oldLitStr, int oldLitStrBack);
static void ParseSimpleRepetition(ParseInfo *info, AOpcode code);
static ABool ParseRepeatType(ParseInfo *info, unsigned *pMin, unsigned *pOpt);
static void ParseRange(ParseInfo *info, unsigned *pMin, unsigned *pOpt);
static int ParseChar(ParseInfo *info, AReOpcode *code);
static int GetHexDigit(ParseInfo *info);
static void Emit(ParseInfo *info, AReOpcode code);
static void EmitCharSet(ParseInfo *info, AReOpcode *set, WideCharSet *wset,
                        ABool complement, int flags);
static void AddStartSet(ParseInfo *info, AReOpcode *set, ABool complement);
static void EmitBranchAt(ParseInfo *info, int pos, AOpcode code, int dest);
static ABool Insert(ParseInfo *info, int pos, int len);
static void Copy(ParseInfo *info, int pos, int len);
static void AGenerateError(ParseInfo *info, const char *err);
static ABool GrowOutputBuffer(ParseInfo *info);
static void SetStringPointers(ParseInfo *info);


/* Compile a regular expression into the internal bytecode representation. */
AValue ACompileRegExp(AThread *t, AValue str, int flags)
{
    ParseInfo info;
    ARegExp *re;

    /* Try to convert the regular expression to a wide string. */
    if (!AIsWideStr(str)) {
        if (!AIsNarrowStr(str) && !AIsSubStr(str))
            return ARaiseTypeErrorND(t, NULL);
            
        str = ANarrowStringToWide(t, str, &str);
        if (AIsError(str))
            return AError;
    }

    /* str is a wide string or a wide substring. */
    
    info.t = t;
    info.isError = FALSE;
    
    info.strVal = str;
    info.str = info.strBeg = NULL;
    SetStringPointers(&info);

    info.buf = NULL;
    info.bufInd = 0;
    if (!GrowOutputBuffer(&info))
        return AError;

    info.mustStr = 0;
    info.numParen = 1;
    info.parenFlags = 0;
    info.minLen = 0;
    info.maxLen = 0;
    info.flags = flags;
    info.depth = 0;
    
    memset(info.startChar, 0, sizeof(info.startChar));

    ParseExpr(&info);

    if (info.str != info.strEnd)
        AGenerateError(&info, ErrUnmatchedRparen);

    if (info.bufInd >= 32768)
        AGenerateError(&info, ErrInternalOverflow);
    
    if (info.isError)
        return AError;

    /* IDEA: PtrSub(info.buf, sizeof(AValue) etc. is ugly! */
    t->tempStack[0] = ANonPointerBlockToValue(APtrSub(info.buf,
                                                         sizeof(AValue)));
    re = AAlloc(t, sizeof(ARegExp));
    if (re == NULL)
        return AError;
    
    AInitMixedBlock(&re->header1, sizeof(ARegExp), 2);

    info.buf = APtrAdd(AValueToPtr(t->tempStack[0]), sizeof(AValue));
    
    re->code = t->tempStack[0];
    re->searchTable = AZero;
    
    if (info.mustStr != 0) {
        flags |= A_ALRE_MUSTLITERAL;

        re->mustStringInd  = info.mustStr;
        re->mustStringLen  = info.buf[info.mustStr - 1];
        re->mustStringBack = info.mustStrBack;
        
        if (info.buf[info.mustStr - 1] < 255) {
            if (info.mustStr == 2
                    && info.buf[re->mustStringLen + 2] == A_MATCH)
                flags |= A_ALRE_LITERALONLY;
        } else
            re->mustStringLen = 254;
    } else
        re->mustStringInd = 0;

    if (info.minLen == 0)
        memset(re->startChar, 0xff, sizeof(info.startChar));
    else
        ACopyMem(re->startChar, info.startChar, sizeof(info.startChar));

    if (*(AReOpcode *)APtrAdd(AValueToPtr(re->code), sizeof(AValue)) == A_BOL)
        flags |= A_ALRE_ANCHORED;

    re->flags = flags;
    re->numGroups = info.numParen;
    re->minLen = info.minLen;

    return AMixedBlockToValue(re);
}


/* Parse a regular expression. */
static void ParseExpr(ParseInfo *info)
{
    int flags;

    flags = info->flags;

    if (flags & A_RE_BOW)
        Emit(info, A_BOW);
    else if (flags & A_RE_BOL)
        Emit(info, A_BOL);

    if (flags & A_RE_NOSPECIAL) {
        AWideChar *str;
        AWideChar *strEnd;
        int len;

        str = info->str;
        strEnd = info->strEnd;
        len = strEnd - str;
        
        info->minLen = len;

        if (len > 0) {
            if (flags & A_RE_NOCASE) {
                Emit(info, A_LITERAL_I_STRING);
                Emit(info, strEnd - str);

                AAddWideToSet(info->startChar, ALower(str[0]));
                AAddWideToSet(info->startChar, AUpper(str[0]));

                info->mustStr = info->bufInd;

                /* FIX: what if copying gc kicks in.. */
                while (str != strEnd)
                    Emit(info, ALower(*str++));
            } else {
                Emit(info, A_LITERAL_STRING);
                Emit(info, strEnd - str);

                AAddWideToSet(info->startChar, str[0]);

                info->mustStr = info->bufInd;

                /* FIX: what if copying gc kicks in.. */
                while (str != strEnd)
                    Emit(info, *str++);
            }
        }

        info->str = str;
    } else
        ParseAlternatives(info);

    if (flags & A_RE_EOW)
        Emit(info, A_EOW);
    else if (flags & A_RE_EOL)
        Emit(info, A_EOL);

    Emit(info, A_MATCH);
}


/* Parse one or more alternatives separated by '|'-characters. */
static void ParseAlternatives(ParseInfo *info)
{
    int oldBufInd;
    int oldMustStr;
    int oldMustStrBack;
    int oldMinLen, oldMaxLen;
    int altMinLen, altMaxLen;

    /* Remember the old state. */
    oldBufInd = info->bufInd;
    oldMustStr = info->mustStr;
    oldMustStrBack = info->mustStrBack;
    
    if (++info->depth >= A_MAX_NESTING_DEPTH) {
        AGenerateError(info, ErrInternalOverflow);
        return;
    }

    oldMinLen = info->minLen;
    oldMaxLen = info->maxLen;
    
    /* Parse the first alternative. */
    ParseConcat(info);
    
    /* Only single alternative? */
    if (info->str == info->strEnd || *info->str != '|') {
        info->depth--;
        return;
    }
    
    /* Multiple alternatives */

    altMinLen = info->minLen;
    altMaxLen = info->maxLen;

    info->minLen = oldMinLen;
    info->maxLen = oldMaxLen;

    /* Skip '|'. */
    info->str++;

    EmitBranchAt(info, oldBufInd, A_BRANCH_AFTER, info->bufInd + 2);

    oldBufInd = info->bufInd;

    /* Parse the rest of the alternatives. */
    ParseAlternatives(info);

    EmitBranchAt(info, oldBufInd, A_BRANCH_ALWAYS, info->bufInd);

    info->mustStr = oldMustStr;
    info->mustStrBack = oldMustStrBack;
    
    info->depth--;

    if (altMinLen < info->minLen)
        info->minLen = altMinLen;
    if (altMaxLen > info->maxLen)
        info->maxLen = altMaxLen;
}


/* Parse a run of simple and parenthised regular expressions concatenated
   together. Plain characters, dots and character classes (optionally followed
   by a repetition specifier) are considered simple. IDEA: \< and friends */
static void ParseConcat(ParseInfo *info)
{
    int litLen;
    ABool isPrevLit;

    litLen = 0;
    isPrevLit = FALSE;
    
    while (info->str < info->strEnd) {
        if (!isPrevLit)
            litLen = 0;

        isPrevLit = FALSE;

        switch (*info->str) {
        case '(':
            ParseParen(info);
            break;

        case ')':
        case '|':
            return;

        case '^':
            /* Beginning of line */
            Emit(info, A_BOL_MULTI);
            info->str++;
            break;

        case '$':
            /* End of line */
            Emit(info, A_EOL_MULTI);
            info->str++;
            break;

        case '.':
            /* Any character */

            if (info->minLen == 0)
                memset(info->startChar, 0xff, sizeof(info->startChar));

            info->str++;

            /* IDEA: Perhaps ANY_ALL (i.e. match newlines)? */
            ParseSimpleRepetition(info, A_ANY);

            break;

        case '[': {
            /* Character class */

            ABool complement = FALSE;
            AReOpcode set[A_SET_SIZE];
            AWideChar ch;
            int i;
            WideCharSet wset;
            int flags = 0;

            InitWideSet(&wset);

            info->str++;

            /* Complement set? */
            if (info->str < info->strEnd && *info->str == '^') {
                info->str++;
                complement = TRUE;
            }
            
            /* End of expression? */
            if (info->str == info->strEnd)
                AGenerateError(info, ErrUnmatchedLbracket);

            memset(set, 0, sizeof(set));

            do {
                AReOpcode code;
                ch = ParseChar(info, &code);

                if (info->str == info->strEnd)
                    break;
                
                if (code >= CC) {
                    int i;
                    const AReOpcode *chClass =
                        ACharClass[(code - CC) & ~CC_COMP];
                    
                    for (i = 0; i < A_SET_SIZE; i++) {
                        if (code & CC_COMP)
                            set[i] |= ~chClass[i];
                        else
                            set[i] |= chClass[i];
                    }

                    if (code == CC_W)
                        flags |= A_WS_WORD_CHAR;
                    else if (code == (CC_W | CC_COMP))
                        flags |= A_WS_NOT_WORD_CHAR;

                    /* Underline character is part of the \w set. */
                    if (code == CC_W || code == (CC_W | CC_COMP))
                        AToggleInSet(set, '_');

                    if (*info->str == '-')
                        AGenerateError(info, ErrInvalidCharacterSet);
                } else {
                    /* Character range? */
                    if (*info->str == '-') {
                        AWideChar hiChar = ch;

                        /* Skip '-', check end of expression. */
                        if (++info->str == info->strEnd)
                            break;

                        if (*info->str == ']')
                            AAddToSet(set, '-');
                        else {
                            hiChar = ParseChar(info, &code);
                            if (code >= CC)
                                AGenerateError(info, ErrInvalidCharacterSet);
                        }

                        AddToWideSet(info, &wset, ch, hiChar);
                                     
                        for (; ch <= AMin(hiChar, 255); ch++)
                            AAddToSet(set, ch);

                    } else {
                        AAddWideToSet(set, ch);
                        AddToWideSet(info, &wset, ch, ch);
                    }
                }
            } while (info->str < info->strEnd && *info->str != ']');

            if (info->str == info->strEnd)
                AGenerateError(info, ErrUnmatchedLbracket);

            /* Skip ']'. */
            info->str++;

            if (info->flags & A_RE_NOCASE) {
                for (i = 0; i < 256; i++)
                    if (AIsInSet(set, i)) {
                        AAddToSet(set, ALower(i));
                        AAddToSet(set, AUpper(i));
                    }
            }

            AddStartSet(info, set, complement);
            ParseSimpleRepetition(info, A_SET);
            EmitCharSet(info, set, &wset, complement, flags);
            
            FreeWideSet(&wset);
            
            break;
        }

        case '*':
        case '+':
        case '?':
        case '{':
            AGenerateError(info, ErrInvalidRepeat);
            info->str = info->strEnd;
            break;

        default:
        {
            AReOpcode code;
            AWideChar ch;

            ch = ParseChar(info, &code);

            /* Special character? */
            if (code != A_EMPTY) {
                if (code < CC) {
                    Emit(info, code);

                    if (code == A_BACKREF || code == A_BACKREF_I) {
                        int num = ch & ~48;
                        int oldMaxLen = info->maxLen;
                    
                        if (!(info->parenFlags & (1 << num)))
                            AGenerateError(info, ErrInvalidBackReference);
                    
                        Emit(info, num);

                        info->maxLen *= 2;
                        
                        ParseRepetition(info, info->minLen, oldMaxLen, 2,
                                        info->mustStr, info->mustStrBack);
                    }
                } else {
                    AReOpcode set[A_SET_SIZE];
                    int flags = 0;
                    
                    memcpy(set, ACharClass[(code  - CC) & ~CC_COMP], 32);

                    /* Underline character is part of the \w set. */
                    if (code == CC_W || code == (CC_W | CC_COMP))
                        AToggleInSet(set, '_');

                    AddStartSet(info, set, code & CC_COMP);
                    ParseSimpleRepetition(info, A_SET);

                    if (code == CC_W || code == (CC_W | CC_COMP))
                        flags = A_WS_WORD_CHAR;
                    
                    EmitCharSet(info, set, NULL, code & CC_COMP, flags);
                }

                break;
            }

            if (info->flags & A_RE_NOCASE) {
                code = A_LITERAL_I;

                ch = ALower(ch);

                if (info->minLen == 0) {
                    AAddWideToSet(info->startChar, ch);
                    AAddWideToSet(info->startChar, AUpper(ch));
                }
            } else {
                code = A_LITERAL;

                if (info->minLen == 0)
                    AAddWideToSet(info->startChar, ch);
            }

            if (info->str == info->strEnd
                    || (*info->str != '*' && *info->str != '+'
                        && *info->str != '?' && *info->str != '{')) {
                info->minLen++;
                info->maxLen++;

                litLen++;
                isPrevLit = TRUE;

                if (litLen == 2) {
                    /* Convert a single character to literal string. */
                    AWideChar prevCh;

                    prevCh = info->buf[info->bufInd - 1];

                    info->buf[info->bufInd - 2] += A_STRING;
                    info->buf[info->bufInd - 1]  = 2;
                    Emit(info, prevCh);
                    Emit(info, ch);
                    
                    if (info->mustStr == 0) {
                        info->mustStr = info->bufInd - litLen;
                        info->mustStrBack = info->maxLen - 2; /* FIX? */
                    }
                } else if (litLen > 2) {
                    /* Add a character to a literal string. */

                    Emit(info, ch);
                    
                    if (info->buf[info->mustStr - 1] <= litLen) {
                        info->mustStr = info->bufInd - litLen;
                        info->mustStrBack = info->maxLen - litLen; /* FIX? */
                    }
                    
                    info->buf[info->bufInd - litLen - 1] = litLen;
                } else {
                    Emit(info, code);
                    Emit(info, ch);
                }
            } else {
                ParseSimpleRepetition(info, code);
                Emit(info, ch);
            }

            break;
        }
        }
    }
}


/* Parse a parenthised expression, optionally followed by a repetition
   specifier. */
static void ParseParen(ParseInfo *info)
{
    int oldBufInd;
    int oldMustStr;
    int oldMustStrBack;
    int groupNum;
    int oldMinLen;
    int oldMaxLen;

    oldBufInd = info->bufInd;
    oldMustStr = info->mustStr;
    oldMustStrBack = info->mustStrBack;

    oldMinLen = info->minLen;
    oldMaxLen = info->maxLen;

    /* Initialize only to satisfy stupid C compilers. */
    groupNum = 0;
    
    /* Skip '('. */
    info->str++;
    
    /* Check end of expression. */
    if (info->str == info->strEnd) {
        AGenerateError(info, ErrUnmatchedLparen);
        return;
    }

    groupNum = info->numParen++;

    Emit(info, A_LPAREN);
    Emit(info, groupNum);

    /* Parse the expression inside parentheses. */
    ParseAlternatives(info);

    Emit(info, A_RPAREN);
    Emit(info, groupNum);

    if (groupNum < 10)
        info->parenFlags |= 1 << groupNum;
    
    /* Unexpected end of expression? */
    if (info->str == info->strEnd || *info->str != ')')
        AGenerateError(info, ErrUnmatchedLparen);
    else {
        /* Skip ')'. */
        info->str++;
    }

    ParseRepetition(info, oldMinLen, oldMaxLen, info->bufInd - oldBufInd,
                    oldMustStr, oldMustStrBack);
}


static void ParseRepetition(ParseInfo *info, int oldMinLen, int oldMaxLen,
                           int size, int oldMustStr, int oldMustStrBack)
{
    unsigned min, opt;
    ABool minimize;

    minimize = ParseRepeatType(info, &min, &opt);

    if (min == 0) {
        info->mustStr = oldMustStr;
        info->mustStrBack = oldMustStrBack;
    }
    
    info->minLen = oldMinLen + min * (info->minLen - oldMinLen);
    if (opt < A_INFINITE_REPEAT)
        info->maxLen = oldMaxLen + (min + opt) * (info->maxLen - oldMinLen);
    else
        info->maxLen = A_INFINITE_LENGTH;

    while (min > 0) {
        Copy(info, info->bufInd - size, size);
        min--;
    }

    if (opt == A_INFINITE_REPEAT) {
        /* We know that the previous opcode is at info->code[2] and that it is
           either RPAREN or BACKREF(_I). Convert it to the
           corresponding SKIP opcode. */
        info->buf[info->bufInd - 2]++;
        
        EmitBranchAt(info, info->bufInd - size, A_BRANCH_AFTER - minimize,
                     info->bufInd + 2);
        EmitBranch(info, A_BRANCH_BEFORE + minimize, info->bufInd - size);
    } else if (opt > 0) {
        EmitBranchAt(info, info->bufInd - size, A_BRANCH_AFTER - minimize,
                     info->bufInd);
        while (--opt > 0) {
            EmitBranch(info, A_BRANCH_AFTER - minimize, info->bufInd + size);
            Copy(info, info->bufInd - size - 2, size);
        }
    } else
        Unemit(info, size);
}


/* Output a variant of code depending on whether a repetition count follows.
   code must be either LITERAL(_I), SET or ANY. */
static void ParseSimpleRepetition(ParseInfo *info, AOpcode code)
{
    unsigned min, opt;
    ABool minimize;

    minimize = ParseRepeatType(info, &min, &opt);

    if (min != 1 || opt != 0) {
        Emit(info, code + A_REPEAT + minimize);
        Emit(info, min);
        Emit(info, opt);
    } else
        Emit(info, code);

    info->minLen += min;
    if (opt < A_INFINITE_REPEAT)
        info->maxLen += min + opt;
    else
        info->maxLen = A_INFINITE_LENGTH;
    
    return;
}


/* Parse an optional repetition specifier (?, +, * or { ... }), potentially
   followed by ?. */
static ABool ParseRepeatType(ParseInfo *info, unsigned *pMin, unsigned *pOpt)
{
    unsigned min, opt;
    AWideChar ch;

    ch = info->str < info->strEnd ? *info->str : '\0';
    
    info->str++;
    
    switch (ch) {
    case '?':
        min = 0;
        opt = 1;
        break;

    case '+':
        min = 1;
        opt = A_INFINITE_REPEAT;
        break;

    case '*':
        min = 0;
        opt = A_INFINITE_REPEAT;
        break;

    case '{':
        ParseRange(info, &min, &opt);
        break;

    default:
        min = 1;
        opt = 0;
        info->str--;
        break;
    }

    *pMin = min;
    *pOpt = opt;
    
    if (info->str < info->strEnd && *info->str == '?') {
        info->str++;
        return TRUE;
    } else
        return FALSE;
}


/* Parse a range {...}. */
static void ParseRange(ParseInfo *info, unsigned *pMin, unsigned *pOpt)
{
    unsigned min, opt;

    *pMin = 0;
    *pOpt = 0;

    if (info->str >= info->strEnd - 1 || !AIsDigit(*info->str)) {
        AGenerateError(info, ErrInvalidRange);
        return;
    }

    min = 0;
    do {
        min = min * 10 + (*info->str - '0');
        info->str++;
    } while (info->str < info->strEnd - 1 && AIsDigit(*info->str));

    if (*info->str == ',') {
        info->str++;
        if (info->str < info->strEnd && AIsDigit(*info->str)) {
            opt = 0;
            do {
                opt = opt * 10 + (*info->str - '0');
                info->str++;
            } while (info->str < info->strEnd && AIsDigit(*info->str));
            opt -= min;
        } else
            opt = A_INFINITE_REPEAT;
    } else
        opt = 0;

    if (info->str == info->strEnd || *info->str != '}' || opt < 0
            || opt > A_INFINITE_REPEAT)
        AGenerateError(info, ErrInvalidRange);

    info->str++;

    *pMin = min;
    *pOpt = opt;
}


#define ReLoCase(c) ((c) | 32)

#define ReIsDigit(c) ((c) >= '0' && (c) <= '9')
#define IsHex(c) (ReLoCase(c) >= 'a' && ReLoCase(c) <= 'f')
#define IsOctal(c) ((c) >= '0' && (c) <= '7')


/* Parse character (possibly an escape sequence). Return the unicode value of
   the character. *code will be assigned the opcode corresponding to the
   character or EMPTY if the character has no special meaning. */
static int ParseChar(ParseInfo *info, AReOpcode *code)
{
    unsigned char c;
    
    *code = A_EMPTY;

    /* Assumption: info->bufInd < info->bufLen. */

    /* Escape sequence? */
    if (*info->str != '\\')
        /* No, plain character. */
        return *info->str++;

    /* Skip '\\'. */
    info->str++;

    /* Check end of line. */
    if (info->str == info->strEnd) {
        AGenerateError(info, ErrTrailingBackslash);
        return 0;
    }

    /* Translate an escape sequence. */

    c = *info->str++;

    if (c == 'x') {
        /* Hexadecimal ascii value? */
        
        int digit1 = GetHexDigit(info);
        int digit2;
        
        if (digit1 < 0)
            return 'x';
        
        digit2 = GetHexDigit(info);
        
        if (digit2 < 0)
            return digit1;
        else
            return digit1 * 16 + digit2;
    }
    
    if (c >= 'a' && c <= 'z') {
        int conv = CharConv[c - 'a'];
        if (conv >= CC)
            *code = conv;
        return conv;
    }
    
    if (c == '<') {
        *code = A_BOW;
        return '<';
    }
    
    if (c == '>') {
        *code = A_EOW;
        return '>';
    }
    
    if (c == 'D') {
        *code = CC + CC_COMP;
        return 0;
    }
    
    if (c == 'S') {
        *code = CC + 1 + CC_COMP;
        return 0;
    }
    
    if (c == 'W') {
        *code = CC + 2 + CC_COMP;
        return 0;
    }
    
    if (ReIsDigit(c)) {
        /* Octal ascii value / back reference */
        
        if (c != '0' && (c > '7' || info->str == info->strEnd
                || !IsOctal(*info->str))) {
            /* Back reference */
            
            *code = info->flags & A_RE_NOCASE ? A_BACKREF_I : A_BACKREF;
            
            return c > '7' ? c : c - '0';
        } else {
            /* Octal value */
            
            int number = c - '0';
            
            if (info->str < info->strEnd && IsOctal(*info->str)) {
                number = number * 8 + (*info->str++ - '0');
                if (info->str < info->strEnd && IsOctal(*info->str))
                    number = number * 8 + (*info->str++ - '0');
            }
            
            return number;
        }
    }
    
    /* No special interpretion */
    return c;
}


/* Return the hex value of the character at *info->str. Return a value
   between 0..15 if it is a valid hex digit, -1 if invalid. Increment
   info->str. */
static int GetHexDigit(ParseInfo *info)
{
    if (info->str == info->strEnd)
        return -1;
    
    if (ReIsDigit(*info->str))
        return *info->str++ - '0';

    if (IsHex(*info->str))
        return ReLoCase(*info->str++) - 'a' + 10;

    return -1;
}


/* Output a regular expression opcode. */
static void Emit(ParseInfo *info, AReOpcode code)
{
    if (info->bufInd == info->bufLen) {
        if (!GrowOutputBuffer(info))
            return;
    }
    
    info->buf[info->bufInd++] = code;
}


static void EmitCharSet(ParseInfo *info, AReOpcode *set, WideCharSet *wset,
                        ABool complement, int flags)
{
    int i;
    int len;

    len = A_SET_SIZE + 2 + WideSetLen(wset) * 2;
    
    while (info->bufInd + len > info->bufLen) {
        if (!GrowOutputBuffer(info))
            return;
    }

    if (complement) {
        for (i = 0; i < A_SET_SIZE; i++)
            info->buf[info->bufInd++] = set[i] ^ 0xffff;
    } else {
        for (i = 0; i < A_SET_SIZE; i++)
            info->buf[info->bufInd++] = set[i];
    }

    Emit(info, WideSetLen(wset));

    Emit(info, flags | ((info->flags & A_RE_NOCASE) ? A_WS_IGNORE_CASE : 0) |
         (complement ? A_WS_COMPLEMENT : 0));
    
    for (i = 0; i < WideSetLen(wset); i++) {
        Emit(info, WideSetLo(wset, i));
        Emit(info, WideSetHi(wset, i));
    }
}


static void AddStartSet(ParseInfo *info, AReOpcode *set, ABool complement)
{
    int i;
    
    if (info->minLen == 0)
        for (i = 0; i < A_SET_SIZE; i++)
            info->startChar[i] |= set[i] ^ (complement ? 0xffff : 0);
}


/* Emit a branch at pos. Opcode is code and the target of branch is dest. */
static void EmitBranchAt(ParseInfo *info, int pos, AOpcode code, int dest)
{
    int disp;

    disp = dest - pos - 1;
    
    if (dest >= pos)
        disp += 2;
    
    /* Get enough space for opcode. */
    if (!Insert(info, pos, 2))
        return;
    
    info->buf[pos]     = code;
    info->buf[pos + 1] = disp;
}


/* Inserts len words at src, which points inside the compiled re. */
static ABool Insert(ParseInfo *info, int pos, int len)
{
    int dest;
    int copyInd;

    if (info->mustStr != 0 && info->mustStr > pos)
        info->mustStr += len;
    
    info->bufInd += len;
    if (info->bufInd >= info->bufLen) {
        if (!GrowOutputBuffer(info))
            return FALSE;
    }
    
    dest = pos + len;
    copyInd = info->bufInd - dest;

    do {
        copyInd--;
        info->buf[dest + copyInd] = info->buf[pos + copyInd];
    } while (copyInd > 0);

    return TRUE;
}


/* Copy len opcodes at src to the end of output buffer. */
static void Copy(ParseInfo *info, int pos, int len)
{
    if (info->bufInd + len >= info->bufLen) {
        if (!GrowOutputBuffer(info))
            return;
    }
    
    memmove(info->buf + info->bufInd, info->buf + pos,
            len * sizeof(AReOpcode));
    info->bufInd += len;
}


/* Generate regular rexpression error. */
static void AGenerateError(ParseInfo *info, const char *err)
{
    if (!info->isError) {
        info->isError = TRUE;
        ARaiseByNum(info->t, ARegExpErrorNum, err);
    }
}


/* Grow the bytecode output buffer. */
static ABool GrowOutputBuffer(ParseInfo *info)
{
    AValue *buf;
    int len;

    if (info->buf == NULL)
        len = GetInitialOutputBufSize(info->strEnd - info->strBeg);
    else
        len = info->bufLen * 2;

    info->t->tempStack[0] = info->strVal;
    if (info->buf != NULL)
        info->t->tempStack[1] =
            ANonPointerBlockToValue(APtrSub(info->buf, sizeof(AValue)));
    
    buf = AAlloc(info->t, sizeof(AValue) + len * sizeof(AReOpcode));
    
    info->strVal = info->t->tempStack[0];
    if (info->buf != NULL)
        info->buf = APtrAdd(AValueToPtr(info->t->tempStack[1]),
                           sizeof(AValue));
    
    if (buf == NULL) {
        AGenerateError(info, NULL);
        return FALSE;
    }

    AInitNonPointerBlock(buf, len * sizeof(AReOpcode));

    buf++;

    /* Copy stuff from previous buffer if it exists. */
    if (info->buf != NULL)
        memcpy(buf, info->buf, info->bufInd * sizeof(AReOpcode));

    info->buf = (AReOpcode *)buf;
    
    SetStringPointers(info);

    info->bufLen = len;
    
    return TRUE;
}


static void SetStringPointers(ParseInfo *info)
{
    int ind;

    ind = info->str - info->strBeg;
    
    if (AIsWideStr(info->strVal)) {
        info->strBeg = AGetWideStrElem(info->strVal);
        info->strEnd = info->strBeg + AStrLen(info->strVal);
    } else {
        info->strBeg = AGetWideSubStrElem(info->strVal);
        info->strEnd = info->strBeg +
                       AGetSubStrLen(AValueToSubStr(info->strVal));
    }

    info->str = info->strBeg + ind;
}


static void AddToWideSet(ParseInfo *info, WideCharSet *set, AWideChar lo,
                         AWideChar hi)
{
    if (lo > hi || hi < 256)
        return;
    
    if (set->length == set->maxLength) {
        /* Keep track of pointers that might by moved. */
        info->t->tempStack[0] = info->strVal;
        info->t->tempStack[1] =
            ANonPointerBlockToValue(APtrSub(info->buf, sizeof(AValue)));
        
        if (set->length == 0) {
            set->data = AAllocStatic(16 * 2 * sizeof(AWideChar));
            set->maxLength = 16;
        } else {
            set->data = AGrowStatic(set->data, set->maxLength * 2 * 2 *
                                   sizeof(AWideChar));
            set->maxLength *= 2;
        }

        /* Update the pointers that might have been moved. */
        info->strVal = info->t->tempStack[0];
        info->buf = APtrAdd(AValueToPtr(info->t->tempStack[1]),
                           sizeof(AValue));
        SetStringPointers(info);
    }

    set->data[set->length * 2] = lo;
    set->data[set->length * 2 + 1] = hi;
    set->length++;
}
