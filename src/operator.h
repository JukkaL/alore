/* operator.h - Alore operators

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef OPERATOR_H_INCL
#define OPERATOR_H_INCL

#include "common.h"


/* Operator precedence, from lowest to highest. Only right associative
   operators have the lowest precedence bit set. */
typedef enum {
    PR_VALUE_INDEX  = 0,   /* Precedence for index expressions */
    PR_VALUE  = 2,         /* Precedence for "top-level" expressions */
    PR_SINGLE = 4,         /* For parsing comma-separated expressions */
    PR_CAST   = 6,         /* "as" cast operator */
    PR_OR     = 8,         /* "or" */
    PR_AND    = 10,        /* "and" */
    PR_NOT    = 12,        /* Unary "not" */
    PR_PAIR   = 13,        /* : */
    PR_REL    = 14,        /* == != < <= > >= is in */
    PR_RNG    = 16,        /* "to" */
    PR_ADD    = 18,        /* + - */
    PR_MUL    = 20,        /* * / div mod */
    PR_UNARY  = 22,        /* Unary - */
    PR_POW    = 25,        /* ** */
    PR_OLD_CAST   = 26
} APrecedence;


/* NOTE: the order must be same as in opcodes, tokens, and array
         BinaryPrecedence[] in "parseexpr.c". */
typedef enum {
    OPER_PLUS,
    OPER_MINUS,
    OPER_EQ,      /* Must be even */
    OPER_NEQ,     /* Must be OPER_EQ + 1 */
    OPER_LT,      /* Must be even */
    OPER_GTE,     /* Must be OPER_LT + 1 */
    OPER_GT,      /* Must be even */
    OPER_LTE,     /* Must be OPER_GT + 1 */
    OPER_IN,      /* Must be even */
    OPER_NOT_IN,  /* Must be OPER_IN + 1 */
    OPER_IS,      /* Must be even */
    OPER_IS_NOT,  /* Must be OPER_IS + 1 */
    OPER_MUL,
    OPER_DIV,
    OPER_IDIV,
    OPER_MOD,
    OPER_POW,
    OPER_PAIR,
    OPER_RANGE,
    OPER_AND,
    OPER_OR,
    /* The operators below are special in some ways */
    OPER_NOT,      /* Unary not */
    OPER_INDEX,    /* Indexing ([]) */
    OPER_ITERATOR, /* Iteration (for loop; not really an operator) */
} AOperator;


#define ANegateOperator(op) \
    ((op) ^ 1)
#define ASwitchOperator(op) \
    ((op) == OPER_LT ? OPER_GT : \
     (op) == OPER_GT ? OPER_LT : \
     (op) == OPER_LTE ? OPER_GTE : \
     (op) == OPER_GTE ? OPER_LTE : (op))


/* IDEA: Implement these using look-up tables. */
#define ALtSatisfiesOp(op) \
    ((op) == OPER_LT || (op) == OPER_LTE || (op) == OPER_NEQ)
#define AEqSatisfiesOp(op) \
    ((op) == OPER_LTE || (op) == OPER_GTE || (op) == OPER_EQ)
#define AGtSatisfiesOp(op) \
    ((op) == OPER_GT || (op) == OPER_GTE || (op) == OPER_NEQ)


/* Does operator test values and branch? */
#define AIsComparisonOperator(op) \
    ((op) >= OPER_EQ && (op) <= OPER_IS_NOT)


/* Are specialized opcodes supported for operator? */
#define AIsQuickOperator(op) \
    ((op) <= OPER_LTE)


#endif
