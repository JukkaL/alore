/* re_internal.h - __re module (private definitions)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef RE_INTERNAL_H_INCL
#define RE_INTERNAL_H_INCL

#include "re.h"


/* Define some shorthands for commonly used macros. */

#define AIsInSet AReIsInSet
#define AAddToSet AReAddToSet
#define AToggleInSet AReToggleInSet

#define AAddWideToSet(set, ch) \
    do { if ((ch) < 256) AAddToSet(set, ch); } while (0)
#define AIsInWideSet(ip, ch) \
    ((ch) < 256 ? AIsInSet(ip, ch) : AIsInWideSet2(ip, ch))

#define A_SET_SIZE A_RE_SET_SIZE


/* Define some additional shorthands. */
#define A_INLINE


/* Regular expression opcodes */
enum {
    A_BOW,                /* Beginning of word */
    A_EOW,                /* End of word */
    A_BOL,                /* Beginning of line */
    A_BOL_MULTI,          /* .. newline separates lines */
    A_EOL,                /* End of line */
    A_EOL_MULTI,          /* .. newline separates lines */
    A_BACKREF,            /* Back reference */
    A_BACKREF_I,          /* .. case insensitive */
    A_BACKREF_SKIP,       /* .. break out of loop if zero-length */
    A_BACKREF_I_SKIP,     /* .. no case, break out of loop */
    A_LPAREN,             /* Beginning of subexpression */
    A_RPAREN,             /* End of subexpression */
    A_RPAREN_SKIP,        /* .. break out of loop */
    A_LITERAL,            /* Literal character */
    A_LITERAL_REPEAT,     /* .. repeated */
    A_LITERAL_REPEAT_MIN, /* .. repeated, minimize length */
    A_LITERAL_STRING,     /* .. string */
    A_LITERAL_I,          /* Literal character, case insentive */
    A_LITERAL_I_REPEAT,
    A_LITERAL_I_REPEAT_MIN,
    A_LITERAL_I_STRING,
    A_SET,                /* Character set */
    A_SET_REPEAT,
    A_SET_REPEAT_MIN,
    A_ANY,                /* Any character except newline */
    A_ANY_REPEAT,
    A_ANY_REPEAT_MIN,
    A_ANY_ALL,            /* Any character */
    A_ANY_ALL_REPEAT,
    A_ANY_ALL_REPEAT_MIN,
    A_BRANCH_BEFORE,      /* First branch, then try the following opcodes */
    A_BRANCH_AFTER,       /* First try the following opcodes, then branch */
    A_BRANCH_ALWAYS,      /* Branch unconditionally */
    A_MATCH,              /* Report a match */
    A_EMPTY               /* Dummy opcode */
};


/* Modifiers for related opcodes (for example SET_REPEAT == SET + REPEAT). */
enum {
    A_REPEAT = 1,
    A_REPEAT_MIN,
    A_STRING
};


/* Maximum number of enclosing parenthesised or alternative expressions */
#define A_MAX_NESTING_DEPTH 512

/* Maximum repeat count */
#define A_INFINITE_REPEAT (1 << 15)

#define A_INFINITE_LENGTH (1 << 24)

/* Maxmimum length of a compiled expression (in opcodes) */
#define A_MAX_EXPRESSION_LENGTH ((1 << 15) - 1)


extern const AReOpcode ACharClass[][A_SET_SIZE];


/* Wide character set flags */
#define A_WS_IGNORE_CASE 1
#define A_WS_COMPLEMENT 2
#define A_WS_WORD_CHAR 4
#define A_WS_NOT_WORD_CHAR 8


#endif
