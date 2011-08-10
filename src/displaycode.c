/* displaycode.c - Pretty-printer for bytecode

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "common.h"
#include "debug_params.h"

#ifdef A_DEBUG

#include <stdarg.h>
#include "debug_runtime.h"
#include "opcode.h"
#include "runtime.h"
#include "class.h"
#include "mem.h"
#include "util.h"
#include "class.h"


/* Information about pretty-printing a single opcode */
typedef struct {
    AOpcode opcode;
    /* The format of the pretty-printed output. Sequences of form %X (where X
       is a character) correspond to one or more arguments of the instruction.
       Other characters represent themselves.

       For example, "%l -> %l" results in decoding two local variable
       arguments (%l); the substring " -> " is displayed verbatim. So a
       bytecode sequence [opc, 4, 5] (where is opc is the correct opcode for
       the example format) would be pretty-printed as "l4 -> l5".

       The formatting sequences include these:
         %i   int value literal (interpreted as AValue)
         %u   integer literal (not valid AValue, no shifting)
         %l   local value
         %L   local value, displaced by A_NO_RET_VAL
         %g   global value
         %G   global value, include name of global definition
         %m   member access via local variable
         %v   slot access via self
         %o   branch offset
         %a   function arguments
         %r   return value location of a call

      Other formatting sequences are described in the comments of
      FormatOpcode(). */
    char *format;
} OpcodeInfo;


/* Number of opcode pretty-printing structures */
#define LEN_OPCODES (sizeof(Opcodes) / sizeof(Opcodes[0]))


/* Descriptions of how to pretty-print opcodes */
static OpcodeInfo Opcodes[] = {
    { OP_NOP,           "nop" },
    { OP_ASSIGN_IL,     "%i -> %l" },
    { OP_ASSIGN_LL,     "%l -> %l" },
    { OP_ASSIGN_GL,     "%g -> %l" },
    { OP_ASSIGN_ML,     "%m -> %l" },
    { OP_ASSIGN_VL,     "%v -> %l" },
    { OP_ASSIGN_MDL,    "%d%s -> %l" },
    { OP_ASSIGN_EL,     "exposed %l -> %l" },
    { OP_ASSIGN_FL,     "%f -> %l" },
    { OP_ASSIGN_LL_REV, "%l <- %l" },
    { OP_ASSIGN_LG,     "%g <- %l" },
    { OP_ASSIGN_LM,     "%m <- %L" },
    { OP_ASSIGN_LV,     "%v <- %l" },
    { OP_ASSIGN_LMD,    "%s%d <- %L" },
    { OP_ASSIGN_LE,     "exposed %l <- %l" },
    { OP_ASSIGN_NILL,   "nil -> %l" },
    { OP_ASSIGN_PL,     "%l <- %o" },
    { OP_ASSIGN_LGC,    "%g <- %l (mark)" },
    { OP_LEAVE_FINALLY, "leave_finally %l pop %u goto %o" },
    { OP_INC_L,         "%-l + 1 -> %l" },
    { OP_DEC_L,         "%-l - 1 -> %l" },
    { OP_ASSIGN_FALSEL, "False -> %l" },
    { OP_MINUS_LL,      "-%l -> %l" },
    { OP_HALT,          "halt" },
    { OP_AGET_LLL,      "%l[%l] -> %l" },
    { OP_AGET_GLL,      "%g[%l] -> %l" },
    { OP_ASET_LLL,      "%l[%l] <- %L" },
    { OP_ASET_GLL,      "%g[%l] <- %L" },
    { OP_JMP,           "goto %o" },
    { OP_IF_TRUE_L,     "if %l goto %o" },
    { OP_IF_FALSE_L,    "if not %l goto %o" },
    { OP_ASSIGN_TRUEL_SKIP,"True -> %n, skip next" },
    { OP_CALL_L,        "%l(%a) %r" },
    { OP_CALL_G,        "%G(%a) %r" },
    { OP_CALL_M,        "%M(%a) %r" },
    { OP_RAISE_L,       "raise %l" },
    { OP_RET_L,         "return %l" },
    { OP_RET,           "return" },
    { OP_CREATE_TUPLE,  "(%x) -> %l" },
    { OP_CREATE_ANON,   "anon %g(%a) -> %l" },
    { OP_CREATE_EXPOSED, "exposed_container(%-l) ->  %l" },
    { OP_CREATE_ARRAY,  "[%x] -> %l" },
    { OP_EXPAND,        "%l -> (%a)" },
    { OP_CHECK_TYPE,    "if not %l is %G error" },
    { OP_IS_DEFAULT,    "if %l <> default goto %o" },
    { OP_FOR_INIT,      "for %l in %l goto %o" },
    { OP_FOR_LOOP,      "for %l loop %o" },
    { OP_FOR_LOOP_RANGE,"for range %l loop %o" },
    { OP_ADD_LLL,       "%l + %l -> %l" },
    { OP_SUB_LLL,       "%l - %l -> %l" },
    { OP_EQ_LL,         "if %l == %l goto %o" },
    { OP_NEQ_LL,        "if %l != %l goto %o" },
    { OP_LT_LL,         "if %l < %l goto %o" },
    { OP_GTE_LL,        "if %l >= %l goto %o" },
    { OP_GT_LL,         "if %l > %l goto %o" },
    { OP_LTE_LL,        "if %l <= %l goto %o" },
    { OP_GET_LL,        "%b%l %p %l %e" },
    { OP_GET_LI,        "%b%l %p %i %e" },
    { OP_GET_LG,        "%b%l %p %g %e" },
    { OP_GET_IL,        "%b%i %p %l %e" },
    { OP_GET_II,        "%b%i %p %i %e" },
    { OP_GET_IG,        "%b%i %p %g %e" },
    { OP_GET_GL,        "%b%g %p %l %e" },
    { OP_GET_GI,        "%b%g %p %i %e" },
    { OP_GET_GG,        "%b%g %p %g %e" },
    { OP_TRY,           "try" },
    { OP_TRY_END,       "end try (%u)" },
    { 0, 0 }
};


/* Information about pretty-printing operators */
static OpcodeInfo Operators[]  = {
    { OP_ADD_L,    "+" },
    { OP_SUB_L,    "-" },
    { OP_MUL_L,    "*" },
    { OP_DIV_L,    "/" },
    { OP_IDV_L,    "div" },
    { OP_MOD_L,    "mod" },
    { OP_IN_L,     "in" },
    { OP_NOT_IN_L, "not in" },
    { OP_IS_L,     "is" },
    { OP_IS_NOT_L, "is not" },
    { OP_RNG_L,    "to" },
    { OP_POW_L,    "**" },
    { OP_EQ,      "==" },
    { OP_NEQ,     "!=" },
    { OP_LT,      "<" },
    { OP_GTE,     ">=" },
    { OP_GT,      ">" },
    { OP_LTE,     "<=" },
    { OP_FOR_L,    "to(for)" },
    { OP_PAIR_L,   ":" },
    { 0, 0 }
};    


static void FormatOpcode(char *buf, char *fmt, AOpcode *code, int *ip);

/* Convert a single instruction to a string. */
int AFormatInstruction(char *buf, AOpcode *code, int ip);

static void Append(char *buf, char *fmt, ...);
static ABool AppendNameOfGlobalDef(char *buf, int num);


/* Return the format string corresponding to an opcode. */
static char *GetOpcodeStr(OpcodeInfo *info, AOpcode opcode)
{
    while (info->format != NULL) {
        if (info->opcode == opcode)
            return info->format;
        info++;
    }
    
    return NULL;
}


/* Pretty-print all functions to stdout. */ 
void ADisplayCode(void)
{
    int i;

    for (i = 0; i < ANumMainGlobals; i++) {
        AValue val = AGlobalByNum(i);

        if (AIsNonSpecialType(val)) {
#if 0
            ATypeInfo *type = AValueToType(val);
            printf("\nclass %d (create %d, numVars %d, instanceSize %d)\n", i,
                   type->create, type->totalNumVars, type->instanceSize);
#endif
        } else if (AIsGlobalFunction(val)) {
            AFunction *func;

            func = AValueToFunction(val);

            if (!AIsCompiledFunction(func)) {
                AOpcode *code = func->code.opc;
                int j;
                int line;
                int offset;
                char funcName[1024];

                strcpy(funcName, "");
                if (AppendNameOfGlobalDef(funcName, i))
                    Append(funcName, " (%d)", i);
                
                printf("\ndef %s at %p (%d..%d%s args, frame-size %d)\n",
                       funcName,
                       func,
                       func->minArgs,
                       (func->maxArgs & ~A_VAR_ARG_FLAG) -
                       (func->maxArgs >= A_VAR_ARG_FLAG),
                       func->maxArgs & A_VAR_ARG_FLAG ? "+" : "",
                       func->stackFrameSize);

                j = 0;
                while (j < func->codeLen) {
                    char *fmt = GetOpcodeStr(Opcodes, func->code.opc[j]);
                    
                    printf("%p %2d: ", func->code.opc + j, j);
                    
                    if (fmt != NULL) {
                        char buf[4096];
                        j++;
                        FormatOpcode(buf, fmt, func->code.opc, &j);
                        printf("%s\n", buf);
                    } else {
                        printf("Invalid opcode (%d)!\n", func->code.opc[j]);
                        break;
                    }
                }

                /* Pretty-print debugging and exception information. */
                offset = 0;
                line = 0;
                while (j <= (AGetNonPointerBlockDataLength(&func->header)
                             + sizeof(AValue) - sizeof(AFunction)) >> 2) {
                    if (code[j] == A_END_TRY_BLOCK) {
                        printf("%p end_try_block\n", code + j);
                        j++;
                    } else if (!(code[j] & 1)) {
                        printf("%p begin_try_block %d\n", code + j,
                               code[j] >> 2);
                        j++;
                    } else if ((code[j] & A_EXCEPT_CODE_MASK) == A_EXCEPT) {
                        printf("%p except l%d, %d, g%d\n", code + j,
                               code[j] >> 3, code[j + 1], code[j + 2]);
                        j += 3;
                    } else if ((code[j] & A_EXCEPT_CODE_MASK) == A_FINALLY) {
                        printf("%p finally l%d, %d\n", code + j,
                               code[j] >> 3, code[j + 1]);
                        j += 2;
                    } else if ((code[j] & A_EXCEPT_CODE_MASK) ==
                               A_LINE_NUMBER) {
                        AOpcode val = code[j] >> 3;
#if A_OPCODE_BITS == 32
                        if (val & 1) {
                            line = val >> 1;
                            printf("absolute_line %d\n", line);
                        } else {
                            val >>= 1;
                            offset += val & 15;
                            line += (val >> 4) & 7;
                            printf("line_number %d at %d", line, offset);
                            for (;;) {
                                val >>= 7;
                                if (val == 0)
                                    break;
                                offset += val & 15;
                                line += (val >> 4) & 7;
                                printf(", %d at %d", line, offset);
                            }
                            printf("\n");
                        }
#else
                        FIX
#endif                  
                        j++;
                    } else {
                        printf("????\n");
                        break;
                    }
                }
            } 
        }
    }
}


/* Append the result of sprintf to a string. */
/* IDEA replace this with AppendMessage.. don't use vsprintf at all? */
static void Append(char *buf, char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsprintf(buf + strlen(buf), fmt, args);
    va_end(args);
}


/* Append the result of AFormatMessage to a string. */
static void AppendMessage(char *buf, char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    AFormatMessageVa(buf + strlen(buf), 512, fmt, args);
    va_end(args);
}


/* Append name of global definition at num to buffer. Only functions and types
   are supported properly at the moment. Return TRUE if the name of the
   function could be determined. Return FALSE if not and append a string
   of the form gNNN, where NNN is num in base 10.

   IDEA Implement names of methods and symbolic constants? */
static ABool AppendNameOfGlobalDef(char *buf, int num)
{
    AValue val = AGlobalByNum(num);
    ASymbolInfo *sym = NULL;
    ABool foundName = FALSE;
                
    if (AIsGlobalFunction(val)) {
        AFunction *f = AValueToFunction(val);
        sym = f->sym;
    }
    if (AIsNonSpecialType(val)) {
        ATypeInfo *t = AValueToType(val);
        sym = t->sym;
    }
    
    if (sym != NULL && sym->num == num) {
        AppendMessage(buf, "%q", sym);
        foundName = TRUE;
    }
                
    if (!foundName)
        Append(buf, "g%d", num);

    return foundName;
}


/* Format a bytecode instruction at code to buf using the format fmt. Display
   the relative value of the instruction pointer (at *ip) and update the
   instruction pointer to point to the next opcode. */
void FormatOpcode(char *buf, char *fmt, AOpcode *code, int *ip)
{
    int i = *ip;

    buf[0] = '\0';

    /* Go through the format string and parse any formatting sequences. */
    while (*fmt != '\0') {
        /* Character other than % are passed through verbatim. */
        if (*fmt != '%')
            Append(buf, "%c", *fmt);
        else {
            /* A formatting sequence %X */
            int flags = 0;
            
            fmt++;

            /* The '-' flag results in displaying a sequence but not advancing
               the instruction pointer. Note that it's only valid for some
               instructions. */
            if (*fmt == '-') {
                flags |= 1;
                fmt++;
            }
            
            switch (*fmt) {
            case 'a': {
                /* Arguments of a function call */
                ABool isVarArg = code[i] & A_VAR_ARG_FLAG;
                int argc = code[i++] & ~A_VAR_ARG_FLAG;

                while (argc > 0) {
                    if (argc == 1 && isVarArg)
                        Append(buf, "*");
                    Append(buf, "l%d", code[i++]);
                    argc--;
                    if (argc > 0)
                        Append(buf, ", ");
                }
                
                break;
            }
            
            case 'b':
                if (AIsComparisonOpcode(code[i + 2]))
                    Append(buf, "if ");
                break;
                
            case 'd':
                /* Direct member variable access using self, possibly through
                   an accessor method */
                if (code[i] & A_VAR_METHOD) {
                    /* Call getter to read a variable. */
                    Append(buf, "self.df%d", code[i++] ^ A_VAR_METHOD);
                } else {
                    /* Read the variable directly. */
                    Append(buf, "self.dv%d", code[i++]);
                }
                break;

            case 'e':
                if (AIsComparisonOpcode(code[i]))
                    Append(buf, "goto %d", i + 1 + code[i + 1] -
                           A_DISPLACEMENT_SHIFT);
                else if (code[i] != OP_FOR_L)
                    Append(buf, "-> l%d", code[i + 1]);
                else {
                    Append(buf, "-> l%d goto %d", code[i + 1], i + 2 +
                           code[i + 2] - A_DISPLACEMENT_SHIFT);
                    i++;
                }
                i += 2;
                break;
                
            case 'f':
                /* Direct method call using self */
                Append(buf, "self.f%d", code[i++]);
                break;

            case 'g':
                /* Global variable */
                Append(buf, "g%d", code[i++]);
                break;

            case 'G': {
                /* Name of a global definition (only functions and types are
                   supported at the moment) */
                int num = code[i++];
                AppendNameOfGlobalDef(buf, num);
                break;
            }
                
            case 'i':
                /* Integer value (shifted) */
                Append(buf, "%d", (int)AValueToInt(code[i++]));
                break;
                
            case 'l':
                /* Local variable */
                Append(buf, "l%d", code[i]);
                if (!(flags & 1))
                    i++;
                break;
                
            case 'n':
                /* Local variable 1 opcode later, do not consume opcode */
                Append(buf, "l%d", code[i + 1]);
                break;

            case 'L':
                /* Local variable */
                Append(buf, "l%d", code[i] - A_NO_RET_VAL);
                if (!(flags & 1))
                    i++;
                break;
                
            case 'm':
                /* Member of a local variable */
                AppendMessage(buf, "l%d.%M", code[i], code[i + 1]);
                i += 2;
                break;          
                
            case 'M':
                /* Member of a local variable */
                AppendMessage(buf, ".%M", code[i]);
                i++;
                break;
                
            case 'o':
                /* Code offset */
                Append(buf, "%d", i + (int)code[i] - A_DISPLACEMENT_SHIFT);
                i++;
                break;
                
            case 'p':
                /* Operator id */
                Append(buf, "%s", GetOpcodeStr(Operators, code[i + 1]));
                break;

            case 'r':
                /* Return value location of a call opcode. */
                if (code[i] != A_NO_RET_VAL)
                    Append(buf, "-> l%d", code[i]);
                i += 1;
                break;
                
            case 's':
                /* Skip an opcode. */
                i++;
                break;
                
            case 'u':
                /* Integer */
                Append(buf, "%d", code[i++]);
                break;
                
            case 'v':
                /* Access a slot via self. */
                Append(buf, "self.v%d", code[i++]);
                break;

            case 'x': {
                /* A sequence of local value references. The number of values
                   is stored initially, followed by the local value numbers. */
                int j;

                for (j = 0; j < code[i]; j++) {
                    Append(buf, "l%d", code[i + 1] + j);
                    if (j < code[i] - 1)
                        Append(buf, ", ");
                }
                i += 2;

                break;
            }
                
            default:
                /* Unknown formatting sequence */
                Append(buf, "ERR");
                break;
            }
        }
        fmt++;
    }

    /* Store the ip of the next opcode. */
    *ip = i;
}


/* Format a single instruction at given ip. Return the ip of the next
   instruction. */
int AFormatInstruction(char *buf, AOpcode *code, int ip)
{
    char *fmt = GetOpcodeStr(Opcodes, code[ip]);
    if (fmt != NULL) {
        ip++;
        FormatOpcode(buf, fmt, code, &ip);
    } else {
        sprintf(buf, "Invalid opcode (%d)!", code[ip]);
        ip++;
    }
    return ip;
}

#endif /* #ifdef A_DEBUG */
