/* opcodesize.c - Sizes of bytecode instructions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "opcode.h"
#include "internal.h"









/* Table of opcode sizes; indexed via opcode id. Variable-length opcodes have
   length 0 in the table. */
unsigned char AOpcodeSizes[] = {
    1,                  /* NOP */
    3,                  /* ASSIGN_IL */
    3,                  /* ASSIGN_LL */
    3,                  /* ASSIGN_GL */
    4,                  /* ASSIGN_ML */
    3,                  /* ASSIGN_VL */
    4,                  /* ASSIGN_MDL */
    3,                  /* ASSIGN_EL */
    3,                  /* ASSIGN_FL */
    3,                  /* ASSIGN_LL_REV */
    3,                  /* ASSIGN_LG */
    4,                  /* ASSIGN_LM */
    3,                  /* ASSIGN_LV */
    4,                  /* ASSIGN_LMD */
    3,                  /* ASSIGN_LE */
    2,                  /* ASSIGN_NILL */
    3,                  /* ASSIGN_PL */
    4,                  /* LEAVE_FINALLY */
    2,                  /* INC_L */
    2,                  /* DEC_L */
    3,                  /* PLUS_LL */
    3,                  /* MINUS_LL */
    4,                  /* AGET_LLL */
    4,                  /* AGET_GLL */
    1,                  /* HALT */
    2,                  /* JMP */
    2,                  /* ASSIGN_TRUEL_SKIP */
    4,                  /* ASET_LLL */
    4,                  /* ASET_GLL */
    0,/*FIX*/           /* CALL_L */
    0,/*FIX*/           /* CALL_G */
    0,/*FIX*/           /* CALL_M */
    0,/*FIX*/           /* CALL_S */
    2,                  /* RET_L */
    1,                  /* RET */
    4,                  /* CREATE_TUPLE */
    0,/*FIX*/           /* CREATE_ANON */
    2,                  /* CREATE_EXPOSED */
    3,                  /* CHECK_TYPE */
    4,                  /* FOR_INIT */
    3,                  /* FOR_LOOP */
    3,                  /* FOR_LOOP_RANGE */
    3,                  /* IF_TRUE_L */
    3,                  /* IF_FALSE_L */
    4,                  /* CREATE_ARRAY */
    0,/*FIX*/           /* EXPAND_ARRAY */
    3,                  /* IS_DEFAULT */
    0,/*FIX*/           /* ASSIGN_LGC */
    4,                  /* ADD_LLL */
    4,                  /* SUB_LLL */
    4,                  /* EQ_LL */
    4,                  /* NEQ_LL */
    4,                  /* LT_LL */
    4,                  /* GTE_LL */
    4,                  /* GT_LL */
    4,                  /* LTE_LL */
    3,                  /* GET_LL */
    3,                  /* GET_LI */
    3,                  /* GET_LG */
    3,                  /* GET_IL */
    3,                  /* GET_II */
    3,                  /* GET_IG */
    3,                  /* GET_GL */
    3,                  /* GET_GI */
    3,                  /* GET_GG */
    1,                  /* TRY */
    2,                  /* TRY_END */
    0,                  /* LEAVE_STACK_BLOCK FIX: remove */
    2,                  /* ADD_L */
    2,                  /* SUB_L */
    2,                  /* EQ */
    2,                  /* NEQ */
    2,                  /* LT */
    2,                  /* GTE */
    2,                  /* GT */
    2,                  /* LTE */
    2,                  /* IN_L */
    2,                  /* NOT_IN_L */
    2,                  /* IS_L */
    2,                  /* IS_NOT_L */
    2,                  /* MUL_L */
    2,                  /* DIV_L */
    2,                  /* IDV_L */
    2,                  /* MOD_L */
    2,                  /* PAIR_L */
    2,                  /* RNG_L */
    2,                  /* POW_L */
    3,                  /* FOR_L */
};
