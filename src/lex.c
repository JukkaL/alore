/* lex.c - Lexical analyzer

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Lexical analyzer. The lexical analyzer converts a string representing the
   source file to an array of tokens. */

#include "lex.h"
#include "runtime.h"
#include "strtonum.h"
#include "compile.h"
#include "symtable.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"
#include "str.h"


#define TOKEN_BLOCK_SIZE 64


static unsigned char IsIdChar[256];
static unsigned char IgnoreNewLine[TT_LAST_TOKEN];


static ABool CreateTokenBlock(AToken **tokRet);
static AToken *AdvanceNewToken(AToken *tok);
static int CreateStringLiteral(const unsigned char *p,
                               const unsigned char *end, char quote,
                               int numQuotes, int numUnicodeSequences,
                               ABool isWide, AEncoding encoding);
static ABool IsInvalidUtf8Sequence(const unsigned char *s);


#define IsUnicodeSequence(p) \
    (p[0] == '\\' && p[1] == 'u' && AIsHexDigit(p[2]) && AIsHexDigit(p[3]) && \
     AIsHexDigit(p[4]) && AIsHexDigit(p[5]))


/* Convert a string representing a part of an Alore source file to an array of
   tokens. src points to the string and srcEnd to the first character past the
   string. If successful, return value is TRUE. Otherwise, returns FALSE.
   The string must end at end of line.

   If encodingPtr != NULL, use *encodingPtr as the default encoding, and store
   the final source file encoding in *encodingPtr. Otherwise, assume UTF-8
   encoding. */
ABool ATokenize(const unsigned char *src, const unsigned char *srcEnd,
                AToken **tokenList, AToken **tokPtr, AEncoding *encodingPtr)
{
    AToken *tok;
    int lineNumber;
    ABool isPreviousSymbolEncoding;
    AEncoding encoding;

    if (encodingPtr != NULL)
        encoding = *encodingPtr;
    else
        encoding = AENC_UTF8;

    if (*tokenList == NULL) {
        if (!CreateTokenBlock(tokenList))
            return FALSE;

        *tokPtr = *tokenList;

        lineNumber = 1;
    } else
        lineNumber = (*tokPtr)->lineNumber;

    tok = *tokPtr;

    /* Process the UTF-8 byte order mark character if at the start of a
       file. */
    if (lineNumber == 1 && src[0] == 0xef && src[1] == 0xbb
        && src[2] == 0xbf) {
        tok->lineNumber = 1;
        tok->type = TT_BOM;
        tok++;
        src += 3;
    }

    /* If the file starts with "#!", skip the first line of the file (except
       for the newline sequence). */
    if (lineNumber == 1 && src[0] == '#' && src[1] == '!') {
        while (*src != '\n' && *src != '\r')
            src++;
    }

    /* This variable is TRUE if the previous symbol was "encoding". */
    isPreviousSymbolEncoding = FALSE;

    for (;;) {
        tok->lineNumber = lineNumber;

        switch (*src++) {
        case '\r':
            if (src != srcEnd && *src == '\n')
                src++;

            /* Fall through */

        case '\n':
            lineNumber++;

            if (IgnoreNewLine[tok[-1].type]) {
                tok->lineNumber = lineNumber;
                if (src == srcEnd) {
                    *tokPtr = tok;
                    return TRUE;
                } else
                    continue;
            } else {
                tok->type = TT_NEWLINE;
                if (src == srcEnd) {
                    tok = AdvanceNewToken(tok);
                    if (tok == NULL)
                        return FALSE;

                    tok->lineNumber = lineNumber;
                    *tokPtr = tok;
                    return TRUE;
                }
            }

            break;

        case ';':
            if (tok[-1].type == TT_NEWLINE)
                continue;

            tok->type = TT_NEWLINE;
            break;

        case ' ':
        case '\t':
            continue;

        case '(':
            tok->type = TT_LPAREN;
            break;

        case ')':
            tok->type = TT_RPAREN;
            break;

        case '[':
            tok->type = TT_LBRACKET;
            break;

        case ']':
            tok->type = TT_RBRACKET;
            break;

        case '+':
            tok->type = TT_PLUS;
            if (*src == '=') {
                src++;
                tok->type = TT_ASSIGN_ADD;
            }
            break;

        case '-':
            if (*src == '-') {
                /* Comment */
                ABool isInvalid = FALSE;
                if (encoding == AENC_ASCII) {
                    /* Check that the comment does not include non-ascii
                       characters. */
                    do {
                        src++;
                        if (*src > 127)
                            isInvalid = TRUE;
                    } while (*src != '\n' && *src != '\r');

                    /* Report error if a character code larger than 127 was
                       encountered. */
                    if (isInvalid) {
                        tok->type = TT_ERR_NON_ASCII_COMMENT_CHAR;
                        break;
                    }
                } else if (encoding == AENC_UTF8) {
                    /* Check that the comment does not include invalid UTF-8
                       sequences. */

                    do {
                        if (*src > 127 && IsInvalidUtf8Sequence(src)) {
                            isInvalid = TRUE;
                            src++;
                        } else
                            src += A_UTF8_SKIP(*src);
                    } while (*src != '\n' && *src != '\r');

                    /* Report error if an error was found. */
                    if (isInvalid) {
                        tok->type = TT_ERR_INVALID_UTF8_SEQUENCE;
                        break;
                    }
                } else {
                    do {
                        src++;
                    } while (*src != '\n' && *src != '\r');
                }
                /* Use continue instead of break to signal that no token was
                   generated. */
                continue;
            } else if (*src == '=') {
                src++;
                tok->type = TT_ASSIGN_SUB;
            } else
                tok->type = TT_MINUS;
            break;

        case '*':
            tok->type = TT_ASTERISK;
            if (*src == '=') {
                src++;
                tok->type = TT_ASSIGN_MUL;
            } else if (*src == '*') {
                src++;
                tok->type = TT_POW;
                if (*src == '=') {
                    src++;
                    tok->type = TT_ASSIGN_POW;
                }
            }
            break;

        case '/':
            tok->type = TT_DIV;
            if (*src == '=') {
                src++;
                tok->type = TT_ASSIGN_DIV;
            }
            break;

        case ':':
            if (*src == ':') {
                src++;
                tok->type = TT_SCOPEOP;
            } else
                tok->type = TT_COLON;
            break;

        case '=':
            if (*src == '=') {
                src++;
                tok->type = TT_EQ;
            } else
                tok->type = TT_ASSIGN;
            break;

        case '<':
            tok->type = TT_LT;
            if (*src == '=') {
                tok->type = TT_LTE;
                src++;
            }
            break;

        case '!':
            if (*src == '=') {
                tok->type = TT_NEQ;
                src++;
            } else
                tok->type = TT_ERR_UNRECOGNIZED_CHAR;
            break;

        case '>':
            if (*src == '=') {
                src++;
                tok->type = TT_GTE;
            } else
                tok->type = TT_GT;
            break;

        case ',':
            tok->type = TT_COMMA;
            break;

        case '.':
            if (!AIsDigit(*src)) {
                tok->type = TT_DOT;
                break;
            }

            /* Fall through */

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
            /* Numeric literal */

            const unsigned char *numBeg = src - 1;
            AValue num;

            if (src[-1] != '.') {
                if (!IsIdChar[*src] && (*src != '.' || !AIsDigit(src[1]))) {
                    tok->type = TT_LITERAL_INT;
                    tok->info.val = AIntToValue(src[-1] - '0');
                    break;
                }

                src = AStrToInt(ACompilerThread, numBeg, srcEnd, &num);
                /* FIX: what if out of memory.. */

                if (!IsIdChar[*src] && (*src != '.' || !AIsDigit(src[1]))) {
                    if (AIsShortInt(num) && num < 32767 /* FIX: neg,
                                                          opcsize.. */) {
                        tok->type = TT_LITERAL_INT;
                        tok->info.val = num;
                    } else if (!AIsError(num)) {
                        tok->type = TT_LITERAL_FLOAT; /* FIX!! longint */
                        tok->info.num = AAddConstGlobalValue(num);
                        if (tok->info.num == -1)
                            goto Fail;
                    }
                    break;
                }
            }

            src = AStrToFloat(ACompilerThread, numBeg, srcEnd, &num);

            if (!IsIdChar[*src] && num != AError) {
                tok->type = TT_LITERAL_FLOAT;
                tok->info.num = AAddConstGlobalValue(num);
                if (tok->info.num == -1)
                    goto Fail;
            } else {
                while (IsIdChar[*src])
                    src++;
                tok->type = TT_ERR_INVALID_NUMERIC;
            }

            break;
        }

        case '"':
        case '\'': {
            /* String literal */

            unsigned numQuotes;
            unsigned numUnicodeSequences;
            ABool isWide;
            ABool isInvalid;
            const unsigned char *p;
            char quote = src[-1]; /* " or ' */

            numQuotes = 0;
            numUnicodeSequences = 0;
            isWide = FALSE;
            isInvalid = FALSE;

            /* Check the validity of the string literal and calculate the
               number of doubled quotes (""/'') and unicode sequences (\uxxxx).
               Also check if we need to build a wide string object. */
            for (p = src; *p != '\n' && *p != '\r'; p++) {
                if (p[0] == quote) {
                    if (p[1] != quote)
                        break;
                    /* Doubled quote */
                    p++;
                    numQuotes++;
                } else if (IsUnicodeSequence(p)) {
                    numUnicodeSequences++;
                    if (p[2] != '0' || p[3] != '0')
                        isWide = TRUE;
                    p += 5;
                } else if (p[0] >= 128) {
                    /* Non-7-bit character code */

                    if (encoding == AENC_ASCII) {
                        /* Invalid character (non-ascii). */
                        isInvalid = TRUE;
                    } else if (encoding == AENC_UTF8) {
                        isWide = TRUE;
                        /* Check the validity of a UTF-8 sequence and skip
                           the multi-byte character if it is valid. */
                        if (IsInvalidUtf8Sequence(p))
                            isInvalid = TRUE;
                        else
                            p += A_UTF8_SKIP(*p) - 1;
                    }
                }
            }

            if (isInvalid) {
                if (encoding == AENC_ASCII)
                    tok->type = TT_ERR_NON_ASCII_STRING_CHAR;
                else
                    tok->type = TT_ERR_INVALID_UTF8_SEQUENCE;
            } else if (*p == quote) {
                int num = CreateStringLiteral(src, p, quote, numQuotes,
                                              numUnicodeSequences, isWide,
                                              encoding);
                if (num < 0)
                    goto Fail;
                tok->type= TT_LITERAL_STRING;
                tok->info.num = num;
                src = p + 1;
            } else
                tok->type = TT_ERR_STRING_UNTERMINATED;

            break;
        }

        case 'a': case 'b': case 'c': case 'd': case 'e':
        case 'f': case 'g': case 'h': case 'i': case 'j':
        case 'k': case 'l': case 'm': case 'n': case 'o':
        case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y':
        case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E':
        case 'F': case 'G': case 'H': case 'I': case 'J':
        case 'K': case 'L': case 'M': case 'N': case 'O':
        case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y':
        case 'Z':
        case '_': {
            /* Identifier / keyword */

            const unsigned char *idBeg;
            unsigned hashValue;
            unsigned idLen;
            ASymbol *sym;

            /* Calculate symbol hash value. */
            idBeg = src - 1;
            hashValue = *idBeg;
            while (IsIdChar[*src])
                hashValue += hashValue * 32 + *src++;

            idLen = src - idBeg;

            /* Try to find the symbol in the symbol table. */
            sym = ASym[hashValue & ASymSize];
            while (sym != NULL) {
                if (sym->len == idLen) {
                    unsigned i;

                    for (i = 0; i < idLen
                             && sym->str[i] == idBeg[i]; i++);

                    if (i == idLen) {
                        /* Found a match in the symbol table. */

                        tok->type = sym->type;
                        tok->info.sym = sym;

                        /* Check if we should change the encoding? Note that
                           this does not check for errors and may sometimes
                           produce non-obvious results. This should be rare
                           and only results in misleading error messages, so
                           it is not a significant issue. */
                        if (isPreviousSymbolEncoding) {
                            if (tok->info.sym == ALatin1Symbol)
                                encoding = AENC_LATIN1;
                            else if (tok->info.sym == AUtf8Symbol)
                                encoding = AENC_UTF8;
                            else if (tok->info.sym == AAsciiSymbol)
                                encoding = AENC_ASCII;
                            if (encodingPtr != NULL)
                                *encodingPtr = encoding;
                        }

                        isPreviousSymbolEncoding = (tok->type == TT_ENCODING);
                        goto Found;
                    }
                }

                sym = sym->next;
            }

            /* Symbol not found. Create a new symbol table entry. */
            tok->type = TT_ID;
            if (!AAddSymbol(hashValue, idBeg, idLen, &tok->info.sym))
                goto Fail;

          Found:

            break;
        }

        default:
            tok->type = TT_ERR_UNRECOGNIZED_CHAR;
            break;
        }

        /* If tok points at the end of current token block, advance to a new
           block. */
        if (tok[1].type == TT_LAST_TOKEN) {
            tok = AdvanceNewToken(tok);
            if (tok == NULL)
                return FALSE;
        } else
            tok++;
    }

    /* Not reached */

  Fail:

    AFreeTokens(*tokenList);
    *tokenList = NULL;
    return FALSE;
}


ABool AAddEofToken(AToken *tok)
{
    if (tok[-1].type != TT_NEWLINE) {
        tok->type = TT_NEWLINE;
        tok = AdvanceNewToken(tok);
        if (tok == NULL)
            return FALSE;
    }

    tok->type = TT_EOF;

    tok = AdvanceNewToken(tok);
    if (tok == NULL)
        return FALSE;

    return TRUE;
}


/* Initialize global state for the lexical analyzer. */
void AInitializeLexicalAnalyzer(void)
{
    int i;

    for (i = 'A'; i <= 'Z'; i++)
        IsIdChar[i] = IsIdChar[i + ('a' - 'A')] = TRUE;
    for (i = '0'; i <= '9'; i++)
        IsIdChar[i] = TRUE;
    IsIdChar['_'] = TRUE;

    for (i = 0; i <= TT_LAST_TOKEN; i++)
        if (AIsBinaryOperator(i) || AIsOperatorAssignment(i))
            IgnoreNewLine[i] = TRUE;
    IgnoreNewLine[TT_COMMA]    = TRUE;
    IgnoreNewLine[TT_LPAREN]   = TRUE;
    IgnoreNewLine[TT_LBRACKET] = TRUE;
    IgnoreNewLine[TT_ASSIGN]   = TRUE;
    IgnoreNewLine[TT_DOT]      = TRUE;
    IgnoreNewLine[TT_AS]       = TRUE;
    IgnoreNewLine[TT_NEWLINE]  = TRUE;

    /* Do not ignore newline after >, since it is often at the end of type
       annotations. */
    IgnoreNewLine[TT_GT]       = FALSE;
}


/* Returns the hash value of the symbol sym. */
unsigned AGetSymbolHashValue(ASymbol *sym)
{
    char *str = sym->str;
    unsigned len = sym->len;
    unsigned hashValue = str[0];
    unsigned i;

    for (i = 1; i < len; i++)
        hashValue += hashValue * 32 + str[i];

    return hashValue;
}


static AToken *AdvanceNewToken(AToken *tok)
{
    tok++;

    if (tok->type == TT_LAST_TOKEN) {
        /* Create new token block. */
        if (!CreateTokenBlock(&tok->info.nextBlock))
            return FALSE;

        /* Mark end of block. */
        tok->type = TT_EOB;

        /* Duplicate the last token of the previous block at the beginning
           of the new block to allow always a single lookahead token. */
        *tok->info.nextBlock = tok[-1];

        tok = tok->info.nextBlock + 1;
    }

    tok->lineNumber = tok[-1].lineNumber;

    return tok;
}


static ABool CreateTokenBlock(AToken **tokRet)
{
    int i;
    AToken *tok;

    tok = AAllocStatic(TOKEN_BLOCK_SIZE * sizeof(AToken));
    if (tok == NULL)
        return FALSE;

    for (i = 0; i < TOKEN_BLOCK_SIZE - 1; i++)
        tok[i].type = TT_EMPTY;
    tok[i].type = TT_LAST_TOKEN;

    *tokRet = tok;

    return TRUE;
}


void AFreeTokens(AToken *tok)
{
    AToken *next;

    if (tok == NULL)
        return;

    do {
        if (tok[TOKEN_BLOCK_SIZE - 1].type == TT_EOB)
            next = tok[TOKEN_BLOCK_SIZE - 1].info.nextBlock;
        else
            next = NULL;

        AFreeStatic(tok);

        tok = next;
    } while (tok != NULL);
}


static int CreateStringLiteral(const unsigned char *p,
                               const unsigned char *end, char quote,
                               int numQuotes, int numUnicodeSequences,
                               ABool isWide, AEncoding encoding)
{
    int i;

    if (!isWide) {
        /* Pure ascii literal or Latin-1 literal, potentially with \u sequences
           with character values less than 255. */
        AString *str;

        str = AAlloc(ACompilerThread, sizeof(AValue) + (end - p) - numQuotes -
                    5 * numUnicodeSequences);
        if (str == NULL)
            return -1;

        i = 0;
        while (p < end) {
            if (p[0] == quote)
                p++;
            if (IsUnicodeSequence(p)) {
                str->elem[i++] = AUnicodeSequenceValue(p + 2);
                p += 6;
            } else
                str->elem[i++] = *p++;
        }

        AInitNonPointerBlock(&str->header, i);

        return AAddConstGlobalValue(AStrToValue(str));
    } else if (encoding == AENC_UTF8) {
        /* Wide UTF-8 string literal (in this context this means that there
           are character values larger than 128). */
        AWideString *str;
        unsigned len;
        unsigned i;

        /* Calculate approximate length of decoded string. */
        len = 0;
        for (i = 0; p + i < end;) {
            len++;
            i += A_UTF8_SKIP(p[i]);
        }
        /* Correct the length value. */
        len -= numQuotes - 5 * numUnicodeSequences;

        /* Allocate space for string. */
        str = AAlloc(ACompilerThread, sizeof(AValue) +
                     len * sizeof(AWideChar));
        if (str == NULL)
            return -1;

        i = 0;
        /* Decode UTF-8 string literal. Errors have been flagged already, so no
           need to perform any error checking. */
        while (p < end) {
            if (p[0] == quote)
                p++;
            if (IsUnicodeSequence(p)) {
                str->elem[i++] = AUnicodeSequenceValue(p + 2);
                p += 6;
            } else {
                unsigned char ch1 = p[0];
                AWideChar result;

                if (ch1 <= 0x7f) {
                    result = ch1;
                    p++;
                } else if (ch1 < 0xc2) {
                    /* Should never be reached (invalid character sequence) */
                    result = ch1;
                    p++;
                } else if (ch1 < 0xe0) {
                    unsigned char ch2 = p[1];
                    p += 2;
                    result = ((ch1 & 0x1f) << 6) | (ch2 & 0x3f);
                } else {
                    unsigned char ch2 = p[1];
                    unsigned char ch3 = p[2];
                    p += 3;
                    result = ((ch1 & 0xf) << 12) | ((ch2 & 0x3f) << 6)
                        | (ch3 & 0x3f);
                }

                str->elem[i++] = result;
            }
        }

        AInitNonPointerBlock(&str->header, i * sizeof(AWideChar));

        return AAddConstGlobalValue(AWideStrToValue(str));
    } else {
        /* ASCII or Latin 1 wide string literal */

        AWideString *str;
        unsigned len = (end - p) - numQuotes - 5 * numUnicodeSequences;

        str = AAlloc(ACompilerThread, sizeof(AValue) +
                     len * sizeof(AWideChar));
        if (str == NULL)
            return -1;

        i = 0;
        while (p < end) {
            if (p[0] == quote)
                p++;
            if (IsUnicodeSequence(p)) {
                str->elem[i++] = AUnicodeSequenceValue(p + 2);
                p += 6;
            } else
                str->elem[i++] = *p++;
        }

        AInitNonPointerBlock(&str->header, i * sizeof(AWideChar));

        return AAddConstGlobalValue(AWideStrToValue(str));
    }
}


static ABool IsInvalidUtf8Sequence(const unsigned char *s)
{
    unsigned char ch1 = s[0];

    if (ch1 < 0xc2)
        return TRUE;
    else if (ch1 < 0xe0) {
        unsigned char ch2 = s[1];
        if (ch2 < 0x80 || ch2 > 0xbf)
            return TRUE;
    } else {
        unsigned char ch2, ch3;

        ch2 = s[1];
        if (ch2 < 0x80)
            return TRUE;
        ch3 = s[2];
        if (ch2 > 0xbf || ch3 < 0x80 || ch3 > 0xbf
            || (ch1 == 0xe0 && ch2 < 0xa0))
            return TRUE;
    }

    return FALSE;
}


/* Helper function for tokenizing a single zero-terminated string. As always,
   free with AFreeTokens. Return NULL if out of memory or if the input string
   is too long. Append a newline after the provided string before tokenizing.

   NOTE: The string must fit in a buffer of A_TOKENIZE_BUF_LENGTH
         characters. */
ABool ATokenizeStr(const char *str, AToken **tok)
{
    AToken *tokTmp;
    char buf[A_TOKENIZE_BUF_LENGTH + 1];

    *tok = NULL;

    if (strlen(str) > sizeof(buf) - 1)
        return FALSE;

    strcpy(buf, str);
    strcat(buf, "\n");

    return ATokenize((unsigned char *)buf,
                     (unsigned char *)buf + strlen(buf), tok, &tokTmp,
                     NULL);
}
