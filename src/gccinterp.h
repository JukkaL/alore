/* gccinterp.h - GCC specific interpreter main loop implementation

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef GCCINTERP_H_INCL
#define GCCINTERP_H_INCL


#define L(opc) &&Opc_##opc

/* These empty lines are included so that the line number here match those in
   opcode.h. */




#define A_INITIALIZE_INTERPRETER \
    static void *labels[] = {  \
        L(OP_NOP),             \
        L(OP_ASSIGN_IL),       \
        L(OP_ASSIGN_LL),       \
        L(OP_ASSIGN_GL),       \
        L(OP_ASSIGN_ML),       \
        L(OP_ASSIGN_VL),       \
        L(OP_ASSIGN_MDL),      \
        L(OP_ASSIGN_EL),       \
        L(OP_ASSIGN_FL),       \
        L(OP_ASSIGN_LL_REV),   \
        L(OP_ASSIGN_LG),       \
        L(OP_ASSIGN_LM),       \
        L(OP_ASSIGN_LV),       \
        L(OP_ASSIGN_LMD),      \
        L(OP_ASSIGN_LE),       \
        L(OP_ASSIGN_NILL),     \
        L(OP_ASSIGN_PL),       \
        L(OP_LEAVE_FINALLY),   \
        L(OP_INC_L),           \
        L(OP_DEC_L),           \
        L(OP_ASSIGN_FALSEL),   \
        L(OP_MINUS_LL),        \
        L(OP_AGET_LLL),        \
        L(OP_AGET_GLL),        \
        L(OP_HALT),            \
        L(OP_JMP),             \
        L(OP_ASSIGN_TRUEL_SKIP),  \
        L(OP_ASET_LLL),        \
        L(OP_ASET_GLL),        \
        L(OP_CALL_L),          \
        L(OP_CALL_G),          \
        L(OP_CALL_M),          \
        L(OP_RAISE_L),         \
        L(OP_RET_L),           \
        L(OP_RET),             \
        L(OP_CREATE_TUPLE),    \
        L(OP_CREATE_ANON),     \
        L(OP_CREATE_EXPOSED),  \
        L(OP_CHECK_TYPE),      \
        L(OP_FOR_INIT),        \
        L(OP_FOR_LOOP),        \
        L(OP_FOR_LOOP_RANGE),  \
        L(OP_IF_TRUE_L),       \
        L(OP_IF_FALSE_L),      \
        L(OP_CREATE_ARRAY),    \
        L(OP_EXPAND),          \
        L(OP_IS_DEFAULT),      \
        L(OP_ASSIGN_LGC),      \
        L(OP_ADD_LLL),         \
        L(OP_SUB_LLL),         \
        L(OP_EQ_LL),           \
        L(OP_NEQ_LL),          \
        L(OP_LT_LL),           \
        L(OP_GTE_LL),          \
        L(OP_GT_LL),           \
        L(OP_LTE_LL),          \
        L(OP_GET_LL),          \
        L(OP_GET_LI),          \
        L(OP_GET_LG),          \
        L(OP_GET_IL),          \
        L(OP_GET_II),          \
        L(OP_GET_IG),          \
        L(OP_GET_GL),          \
        L(OP_GET_GI),          \
        L(OP_GET_GG),          \
        L(OP_TRY),             \
        L(OP_TRY_END),         \
    };


#define A_BEGIN_INTERPRETER_LOOP goto *labels[*ip];
#define A_BEGIN_INTERPRETER_LOOP_2

#define A_OPCODE(name) Opc_##name:

#define A_END_OPCODE goto *labels[*ip]

#define A_END_INTERPRETER_LOOP


#ifdef A_HAVE_GCC_X86_HACK

/* These definitions improve interpreter performance up to 15%, but might cause
   compilation problems since the optimizer has fewer registers to work with.
   They force a few local variables to be bound to registers for the duration
   of the interpreter inner loop. Without them, gcc often does a rather poor
   job of register allocation. They only work on x86. */
#define A_IP_REG asm("esi")
#define A_STACK_REG asm("edi")

#else

#define A_IP_REG
#define A_STACK_REG

#endif


#endif
