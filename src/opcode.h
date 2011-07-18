/* opcode.h - Alore bytecode opcodes

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef OPCODE_H_INCL
#define OPCODE_H_INCL


#include "common.h"


/* NOTE: If these are changed, update opcodesize.c and gccinterp.h (and
         potentially also interp.c, displaycode.c and operator.h)!
   NOTE: The implementation sometimes makes subtle assumptions about the
         relative order and numeric values of certain opcodes. Be careful
         when modifying these! */

enum {
    OP_NOP,             /* No operation */
    OP_ASSIGN_IL,       /* int -> local */
    OP_ASSIGN_LL,       /* local -> local */
    OP_ASSIGN_GL,       /* global -> local */
    OP_ASSIGN_ML,       /* local.member -> local */
    OP_ASSIGN_VL,       /* direct member of self -> local */
    OP_ASSIGN_MDL,      /* direct member or accessor of self -> local */
    OP_ASSIGN_EL,       /* exposed local ->local */
    OP_ASSIGN_FL,       /* member_function -> local */
    OP_ASSIGN_LL_REV,   /* local <- local */
    OP_ASSIGN_LG,       /* global <- local */
    OP_ASSIGN_LM,       /* local.member <- local */
    OP_ASSIGN_LV,       /* direct member of self <- local */
    OP_ASSIGN_LMD,      /* direct member or accessor of self <- local */
    OP_ASSIGN_LE,       /* exposed local <- local */
    OP_ASSIGN_NILL,     /* nil -> local */
    OP_ASSIGN_PL,       /* ip -> local */
    OP_LEAVE_FINALLY,   /* Leave finally statement */
    OP_INC_L,           /* Inc local */
    OP_DEC_L,           /* Dec local */
    OP_ASSIGN_FALSEL,   /* False -> local */
    OP_MINUS_LL,        /* -local -> local */
    OP_AGET_LLL,        /* local[local] -> local */
    OP_AGET_GLL,        /* global[local] -> local */
    OP_HALT,            /* Halt program at the last stack frame */
    OP_JMP,             /* Goto displacement */
    OP_ASSIGN_TRUEL_SKIP, /* true -> local, next op must be OP_ASSIGN_NILL */
    OP_ASET_LLL,        /* local[local] = local */
    OP_ASET_GLL,        /* global[local] = local */
    OP_CALL_L,
    OP_CALL_G,
    OP_CALL_M,
    OP_RAISE_L,
    OP_RET_L,
    OP_RET,
    OP_CREATE_TUPLE,
    OP_CREATE_ANON,     /* Create anon. function from global -> local */
    OP_CREATE_EXPOSED,  /* Created value container for an exposed variable */
    OP_CHECK_TYPE,      /* if not local is global then raise error */
    OP_FOR_INIT,
    OP_FOR_LOOP,
    OP_FOR_LOOP_RANGE,
    OP_IF_TRUE_L,       /* If local Goto disp (NOTE: must be even) */
    OP_IF_FALSE_L,      /* If not local Goto disp */
    OP_CREATE_ARRAY,
    OP_EXPAND,
    OP_IS_DEFAULT,
    OP_ASSIGN_LGC,      /* FIX: wrong place? */
    OP_ADD_LLL,
    OP_SUB_LLL,
    OP_EQ_LL,           /* NOTE: must be even */
    OP_NEQ_LL,
    OP_LT_LL,
    OP_GTE_LL,
    OP_GT_LL,
    OP_LTE_LL,
    OP_GET_LL,
    OP_GET_LI,
    OP_GET_LG,
    OP_GET_IL,
    OP_GET_II,
    OP_GET_IG,
    OP_GET_GL,
    OP_GET_GI,
    OP_GET_GG,
    OP_TRY,
    OP_TRY_END,
    OP_FILLER3,
    OP_ADD_L,
    OP_SUB_L,
    OP_EQ,              /* NOTE: must be even */
    OP_NEQ,
    OP_LT,
    OP_GTE,
    OP_GT,
    OP_LTE,
    OP_IN_L,
    OP_NOT_IN_L,
    OP_IS_L,
    OP_IS_NOT_L,
    OP_MUL_L,
    OP_DIV_L,
    OP_IDV_L,
    OP_MOD_L,
    OP_POW_L,
    OP_PAIR_L,
    OP_RNG_L,
    OP_FOR_L,
};


#define AIsQuickOperatorOpcode(opc) ((opc) >= OP_ADD_L && (opc) <= OP_LTE)
#define AIsComparisonOpcode(opc) ((opc) >= OP_EQ && (opc) <= OP_IS_NOT_L)
#define AIsRetOpcode(opc) ((opc) >= OP_RET_L && (opc) <= OP_RET)
#define AIsCallOpcode(opc) ((opc) >= OP_CALL_L && (opc) <= OP_CALL_M)
#define AIsSpecialAssignmentOpcode(opc) \
    (((opc) == OP_AGET_LLL || (opc) == OP_AGET_GLL) || (opc) == OP_ASSIGN_ML \
     || (opc) == OP_ASSIGN_MDL)


extern unsigned char AOpcodeSizes[];


#define A_DISPLACEMENT_SHIFT (1 << 28)

/* Return the target ip if the branch whose displacement is at *ip is
   taken. */
#define ABranchTarget(ip) ((ip) + ((long)*(ip) - A_DISPLACEMENT_SHIFT))


#define A_NO_RET_VAL 32768


/* Decoding exception info codes:

   code == END_TRYBLOCK      -> end try block
   (code & 1) == 0           -> begin try block at (code >> 1)
   (code & 7) == EXCEPT      -> except (localvar, code-index, type)
   (code & 7) == FINALLY     -> finally (localvar, code-index)
   (code & 7) == LINE_NUMBER -> line number info, end of exception info
*/

#define A_EXCEPT 1
#define A_FINALLY 3
#define A_LINE_NUMBER 5
#define A_LOCAL_VAR 7


#define A_EXCEPT_CODE_SHIFT 3
#define A_EXCEPT_CODE_MASK 7


#define A_END_TRY_BLOCK (AOpcode)(-1UL << 1)


#define AIsExceptCode(code) (((code) & A_EXCEPT_CODE_MASK) == A_EXCEPT)

#define AIsEndTryCode(code) ((code) == A_END_TRY_BLOCK)

#define AIsBeginTryCode(code) (!((code) & 1))
#define AIsDirectBeginTryCode(code) ((code) & 2)
#define AGetBeginTryCodeIndex(code) ((code) >> 2)

#define AIsFinallyCode(code) (((code) & A_EXCEPT_CODE_MASK) == A_FINALLY)
#define AGetFinallyLvar(code) ((code) >> 3)
#define AGetFinallyCodeIndex(ptr) ((ptr)[1])

#define AIsLineNumberCode(code) \
    (((code) & A_EXCEPT_CODE_MASK) == A_LINE_NUMBER)


#endif
