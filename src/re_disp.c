/* re_disp.c - __re module (displaying compiled regexps; for debugging)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Code for displaying compiled regular expressions in a human-readable
   form. This is only included in debugging builds. */

#include "re.h"
#include "re_internal.h"
#include "internal.h"
#include "debug_params.h"

#include <stdio.h>
#include <string.h>


#ifdef A_DEBUG


/* Representations of opcodes. Some opcodes have 0 as the representation to
   signify a non-standard representation. */
static char *Name[] = {
    "\\<",
    "\\>",
    "^",
    "^ (multi)",
    "$",
    "$ (multi)",
    "\\%d",
    "\\%d (skip)",
    "\\%d (i)",
    "\\%d (i, skip)",
    "( <%d>",
    ") <%d>",
    ") <%d> (skip)",
    0,                  /* LITERAL */
    0,
    0,
    0,
    0,                  /* LITERAL_I */
    0,
    0,
    0,
    0,                  /* SET */
    0,
    0,
    ".",                /* ANY */
    ".",
    ".",
    ". (all)",          /* ANY_ALL */
    ". (all)",
    ". (all)",
    "branch before",
    "branch after",
    "branch",
    "match",
    "empty"
};


/* Descriptions of the representations of characters that are displayed using
   the \x notation. */
static unsigned char CharConv[][2] = {
    { '\a', 'a' },
    { '\b', 'b' },
    { '\033', 'e' },
    { '\f', 'f' },
    { '\n', 'n' },
    { '\r', 'r' },
    { '\t', 't' },
    { '\v', 'v' },
    { '\\', '\\' },
    { '\'', '\'' },
    { 0, 0 }
};


/* Display the representation of a single character. */
static void PrintChar(AWideChar c)
{
    int i;

    /* Check if we should use a \x form. */
    for (i = 0; CharConv[i][0] != 0; i++) {
        if (CharConv[i][0] == c) {
            /* Yes. */
            printf("\\%c", CharConv[i][1]);
            return;
        }
    }
    
    if (c >= 32 && c < 127)
        printf("%c", c); /* Ordinary 7-bit character */
    else if (c < 256)
        printf("\\x%02x", c); /* \xNN form */
    else
        printf("\\u%04x", c); /* \uNNNN form */
}


/* Display a repetition range {...}. */
void PrintRange(AReOpcode *ptr, ABool isMax)
{
    int min = ptr[0];
    int opt = ptr[1];

    printf("{%d", min);

    if (opt == A_INFINITE_REPEAT)
        printf(",");
    else if (opt != 0)
        printf(",%d", min + opt);
    
    printf("}");
    printf(isMax ? " " : "? ");
}


/* Display a string encoded as length + contents. Update *ptr to point to the
   next opcode after the string. */
void PrintString(AReOpcode **ptr)
{
    int len;
    AWideChar *str;

    len = **ptr;
    str = *ptr + 1;
    
    *ptr += len + 1;

    printf("\"");
    while (len-- > 0)
        PrintChar(*str++);
    printf("\"");
}


/* Display a character set [...]. Use the check function to check whether a
   a character is included in the set.
   NOTE: Only process character codes that are less than 256. */
static void DisplaySet(void *set, ABool (*check)(void *, int))
{
    ABool comp;
    int i;

    comp = check(set, '\0');
    i = 0;

    printf("[");
    if (comp)
        printf("^");
    
    do {
        while (check(set, i) == comp)
            if (i == 255) {
                printf("]");
                return;
            } else
                i++;

        PrintChar(i++);

        if (i < 255 && check(set, i) != comp && check(set, i + 1) != comp) {
            while (check(set, i) != comp && i < 256)
                i++;

            printf("-");

            PrintChar(i - 1);
        }
    } while (i < 256);

    printf("]");
}


static ABool IsInCharSet(void *set, int i)
{
    return AIsInSet((AReOpcode *)set, i) != 0;
}


/* Display the internal structure of a regular expression. */
void ADisplayRegExp(ARegExp *re)
{
    AReOpcode *reCode;
    AReOpcode *reCodeBeg;
    AReOpcode code;

    reCode = APtrAdd(AValueToPtr(re->code), sizeof(AValue));
    reCodeBeg = reCode;

    printf("minLen: %d\n", re->minLen);
    
    if (re->mustStringInd != 0) {
        int i;

        printf("mustString: ");
        for (i = 0; i < re->mustStringLen; i++)
            PrintChar(reCodeBeg[re->mustStringInd + i]);
        printf("\n");
        
        printf("back: %d\n", re->mustStringBack);
    }
    
    printf("startChar: ");
    DisplaySet(re->startChar, IsInCharSet);
    printf("\n");

    /* Display opcodes. */
    do {
        /* Display opcode index. */
        printf("%2d: ", (int)(reCode - reCodeBeg));

        /* Decode and display a single opcode. */
        switch (code = *reCode++) {
        case A_BOW:
        case A_EOW:
        case A_BOL:
        case A_BOL_MULTI:
        case A_EOL:
        case A_EOL_MULTI:
        case A_MATCH:
        case A_EMPTY:
            printf("%s", Name[code]);
            break;
            
        case A_LPAREN:
        case A_RPAREN:
        case A_RPAREN_SKIP:
        case A_BACKREF:
        case A_BACKREF_SKIP:
        case A_BACKREF_I:
        case A_BACKREF_I_SKIP:
            printf(Name[code], *reCode++);
            break;

        case A_LITERAL_REPEAT:
        case A_LITERAL_REPEAT_MIN:
        case A_LITERAL_I_REPEAT:
        case A_LITERAL_I_REPEAT_MIN:
            PrintRange(reCode, code == A_LITERAL_REPEAT
                       || code == A_LITERAL_I_REPEAT);
            reCode += 2;

            /* Fall through */
            
        case A_LITERAL:
        case A_LITERAL_I:
            printf("\"");
            PrintChar(*reCode++);
            printf("\"");
            if (code >= A_LITERAL_I)
                printf(" (i)");
            break;

        case A_LITERAL_STRING:
        case A_LITERAL_I_STRING:
            PrintString(&reCode);
            if (code == A_LITERAL_I_STRING)
                printf(" (i)");
            break;

        case A_SET_REPEAT:
        case A_SET_REPEAT_MIN:
            PrintRange(reCode, code == A_SET_REPEAT);
            reCode += 2;

            /* Fall through */

        case A_SET:
            DisplaySet(reCode, IsInCharSet);
            reCode += A_SET_SIZE;
            /* FIX: Display wide character sets. Now they're simply skipped. */
            reCode += 2 + reCode[0] * 2;
            break;

        case A_ANY_REPEAT:
        case A_ANY_REPEAT_MIN:
        case A_ANY_ALL_REPEAT:
        case A_ANY_ALL_REPEAT_MIN:
            PrintRange(reCode, code == A_ANY_REPEAT
                       || code == A_ANY_ALL_REPEAT);
            reCode += 2;

            /* Fall through */

        case A_ANY:
        case A_ANY_ALL:
            printf("%s", Name[code]);
            break;

        case A_BRANCH_BEFORE:
        case A_BRANCH_AFTER:
        case A_BRANCH_ALWAYS:
            printf("%s %ld", Name[code],
                   (long)((reCode -
                           reCodeBeg) + (ASignedRegExpOpcode)*reCode));
            reCode++;
            break;

        default:
            fprintf(stderr, "Invalid opcode %d!\n", code);
            return;
        }
        
        printf("\n");
    } while (code != A_MATCH);
}


#endif
