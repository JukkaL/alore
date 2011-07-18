/* re.h - __re module (matching API)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef RE_H_INCL
#define RE_H_INCL

#include "value.h"
#include "thread.h"


/* Compile-time flags */

#define A_RE_NOCASE        1  /* Case is not significant. */
#define A_RE_NOSPECIAL     2  /* No characters are special. */
#define A_RE_BOL           4  /* Match only at beginning of line. */
#define A_RE_EOL           8  /* Match only at end of line. */
#define A_RE_BOW          16  /* Match only at beginning of word. */
#define A_RE_EOW          32  /* Match only at end of word. */

/* Internal flags */

#define A_ALRE_LITERALONLY 64  /* Regular expression is a literal string. */
#define A_ALRE_MUSTLITERAL 128 /* A literal string is needed for a match. */
#define A_ALRE_ANCHORED    256 /* AExpression anchored at beginning of line. */

/* Match flags */

#define A_RE_NOBOL         1  /* Beginning of string does not begin a line. */
#define A_RE_NOEOL         2  /* End of string does not end a line. */
#define A_RE_NOSEARCH      4  /* Match only at a single location (do not search
                               whole string). */


/* Size of a character set / class. */
#define A_RE_SET_SIZE 16


/* Returns true if ch belongs to the character set. */
#define AReIsInSet(set, ch) \
    ((set)[(unsigned char)(ch) >> 4] & (1 << ((ch) & 15)))

/* Adds character ch to the set. */
#define AReAddToSet(set, ch) \
    ((set)[(unsigned char)(ch) >> 4] |= (1 << ((ch) & 15)))

/* Adds character ch to the set if it isn't there intially and vice versa. */
#define AReToggleInSet(set, ch) \
    ((set)[(unsigned char)(ch) >> 4] ^= (1 << ((ch) & 15)))


/* Type of opcodes used internally in compiled regular expessions. */
typedef AWideChar AReOpcode;
typedef ASignedWideChar ASignedRegExpOpcode;


/* Compiled regular expression */
typedef struct {
    AValue header1;
    AValue header2;
    
    AValue code;
    AValue searchTable;
    
    int flags;                    /* ALRE_x flags */
    int minLen;                   /* Minimum length of a match */
    
    int numGroups;                /* Number of parenthesised subexpessions */

    int mustStringInd;            /* 0 or index of a "must have" string */
    int mustStringLen;            /* Length of mustString */
    int mustStringBack;

    AReOpcode startChar[A_RE_SET_SIZE];
} ARegExp;


/* Span of a subexpression */
typedef struct {
    unsigned int beg;
    unsigned int end;
} AReRange;


AValue ACompileRegExp(AThread *t, AValue str, int flags);

/* Search for a regular expression in a string. Return 0 if failed to match,
   1 if matched. */
int AMatchRegExp(AValue *reValue,  AValue *strValue, int len, int pos,
                 int matchFlags, AReRange *subExp, int maxNumSubExp);

#ifdef A_DEBUG
/* Display a compiled regular expression in human-readable form. */
void ADisplayRegExp(ARegExp *re);
#endif


extern int ARegExpErrorNum;


#endif
