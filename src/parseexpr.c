/* parseexpr.c - Expression parser

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "parse.h"
#include "operator.h"
#include "class.h"
#include "error.h"
#include "output.h"
#include "memberid.h"
#include "internal.h"
#include "std_module.h"


#define OperatorIdFromTokenType(tokenType) ((tokenType) - TT_FIRST_OPERATOR)


static AToken *ParseSubExpression(AToken *tok, APrecedence prec,
                                  AExpression *result, int depth,
                                  ABool allowCast);
static AToken *ParseOperatorAssignment(AExpression *lvalue, AToken *tok);
static AOperator OperatorTypeFromOperatorAssignmentToken(AToken *tok);
static void MaintainOperandValuesForPreviousOpcode(void);
static void EmitAssignmentOpcodeForOperatorAssignment(int readOpcodeIndex,
                                                      int rvalueNum);
static ABool FindMemberVariable(ASymbolInfo *id);
static void CreateMemberAccess(ASymbolInfo *sym, AExpression *result);
static AToken *ParseExpressionList(AToken *tok, AExpression *left,
                                   AExpression *result, int depth,
                                   ABool allowCast, int oldNumLocalsActive,
                                   ABool isArray);
static ABool AIsTypeNum(int num);
static void CheckCreateMemberAccess(AToken *tok, ASymbolInfo *sym);


unsigned AAssignPos[A_MAX_MULTI_ASSIGN];


/* Indexed by OPER_x. */
static unsigned char ABinaryPrecedence[] = {
    PR_ADD,     /* +   */
    PR_ADD,     /* -   */
    PR_REL,     /* ==  */
    PR_REL,     /* !=  */
    PR_REL,     /* <   */
    PR_REL,     /* >=     */
    PR_REL,     /* >      */
    PR_REL,     /* <=     */
    PR_REL,     /* in     */
    PR_REL,     /* not in */
    PR_REL,     /* is     */
    PR_REL,     /* not is */
    PR_MUL,     /* *   */
    PR_MUL,     /* /   */
    PR_MUL,     /* div */
    PR_MUL,     /* mod */
    PR_POW,     /* **  */
    PR_PAIR,    /* :   */
    PR_RNG,     /* to  */
    PR_AND,     /* and */
    PR_OR,      /* or  */
    PR_NOT      /* not */
};


/* Parses an expression. The result of the expression is in a local variable
   and the number of that variable is returned. ANumLocalsActive is not
   modified. */
AToken *AParseExpression(AToken *tok, int *localNum, ABool allowCast)
{
    AExpression expr;
    int oldNumLocalsActive;

    oldNumLocalsActive = ANumLocalsActive;

    tok = ParseSubExpression(tok, PR_VALUE, &expr, 0, allowCast);

    ANumLocalsActive = oldNumLocalsActive;

    if (!AIsLocalExpression(expr.type))
        AConvertToLocalExpression(&expr);

    ANumLocalsActive = oldNumLocalsActive;

    *localNum = expr.num;

    return tok;
}


/* Parses an expression. Always outputs a partial opcode that targets a local
   variable but the destination variable number is omitted. It's the caller's
   responsibility to output the destination. */
AToken *AParseAssignExpression(AToken *tok, ABool allowCast)
{
    AExpression expr;
    int oldNumLocalsActive = ANumLocalsActive;

    tok = ParseSubExpression(tok, PR_VALUE, &expr, 0, allowCast);
    ACreateOpcode(&expr);

    ANumLocalsActive = oldNumLocalsActive;

    return tok;
}


/* Like AParseAssignExpression but the parse is terminated at a comma or at
   "as". */
AToken *AParseSingleAssignExpression(AToken *tok, ABool *isErr)
{
    AExpression expr;
    int oldNumLocalsActive = ANumLocalsActive;

    tok = ParseSubExpression(tok, PR_SINGLE, &expr, 0, FALSE);
    ACreateOpcode(&expr);

    ANumLocalsActive = oldNumLocalsActive;

    *isErr = expr.type == ET_ERROR;

    return tok;
}


/* Parse an rvalue expression in multiple assignment. The lvalue has num
   destinations. */
AToken *AParseMultiExpression(AToken *tok, int num, ABool allowCast)
{
    AExpression expr;

    int oldNumLocalsActive = ANumLocalsActive;

    tok = ParseSubExpression(tok, PR_VALUE, &expr, 0, allowCast);

    if (AIsArrayExpression(expr.type) ||
        AIsTupleExpression(expr.type)) {
        AUnemitArrayOrTupleCreate();
        if (expr.num != num)
            AGenerateError(tok->lineNumber, ErrIncompatibleAssignment);
        /* FIX: update NumLocalsActive?? */
    } else {
        int i;
        unsigned dst[A_MAX_MULTI_ASSIGN];

        AConvertToLocalExpression(&expr);

        ANumLocalsActive = oldNumLocalsActive;

        /* if (num > A_MAX_MULTI_ASSIGN) */
            /* FIX: do something... error */

        for (i = 0; i < num; i++)
            dst[i] = AGetLocalVariable();

        AEmitArrayExpand(expr.num, num, dst);

        ANumLocalsActive -= num;
    }

    return tok;
}


/* Parse a logical expression. If the result of the expression is equal to
   cond, execution continues at the opcode after the expression. Otherwise, a
   branch is taken according to the returned BranchList. cond must be either
   0 or 1. */
AToken *AParseLogicalExpression(AToken *tok, ABool cond, ABranchList **branch)
{
    AExpression expr;

    tok = ParseSubExpression(tok, PR_VALUE, &expr, 0, TRUE);

    if (expr.type != ET_LOGICAL) {
        AConvertToLogicalExpression(&expr);

        if (expr.type == ET_ERROR) {
            *branch = NULL;
            return AAdvanceTok(tok);
        }
    }

    expr.finalBranch->next = expr.branch[cond];

    if (cond == TRUE)
        AToggleBranches(expr.branch[0]);
    else
        AToggleBranches(expr.finalBranch);

    ASetBranches(expr.branch[1 - cond], AGetCodeIndex());

    *branch = expr.finalBranch;

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    return AAdvanceTok(tok);
}


/* Parse an assignment statement or an expression statement. */
AToken *AParseAssignmentOrExpression(AToken *tok)
{
    AExpression lvalue;

    int oldCodeIndex = AGetCodeIndex();

    int oldNumLocals       = ANumLocals;
    int oldNumLocalsActive = ANumLocalsActive;

    AEmitLineNumber(tok);

    /* We may need to know the number of temporary variables used when
       evaluating the expresion. */
    ANumLocals = ANumLocalsActive;

    tok = ParseSubExpression(tok, PR_VALUE, &lvalue, 0, TRUE);

    if (tok->type == TT_ASSIGN) {
        /* Assignment statement */

        ANumLocalsActive = ANumLocals;

        /* Skip '='. */
        tok = AAdvanceTok(tok);

        /* Check if we have a valid lvalue expression. */
        if (!AIsLvalueExpression(lvalue.type)) {
            if ((lvalue.type == ET_ARRAY ||
                 lvalue.type == ET_TUPLE) && lvalue.num > A_MAX_MULTI_ASSIGN)
                AGenerateError(tok->lineNumber, ErrInternalOverflow,
                              IOF_TOO_MANY_VARS_IN_MULTIPLE_ASSIGN);
            else
                AGenerateError(tok->lineNumber, ErrInvalidLvalue);
            /* Create dummy lvalue that does not cause any further errors. */
            lvalue.type = ET_LOCAL_LVALUE;
            lvalue.num = 0;
        }

        if (AIsArrayExpression(lvalue.type) ||
            AIsTupleExpression(lvalue.type)) {
            /* Multiple assignment */

            unsigned saveSize;

            AUnemitArrayOrTupleCreate();

            saveSize = AGetCodeIndex() - oldCodeIndex;
            ASaveCode(saveSize);

            if (lvalue.type == ET_ARRAY_LOCAL_LVALUE ||
                lvalue.type == ET_TUPLE_LOCAL_LVALUE)
                ANumLocalsActive = oldNumLocalsActive;

            AIsRvalue = TRUE;
            tok = AParseMultiExpression(tok, lvalue.num, TRUE);
            AIsRvalue = FALSE;

            ARestoreCode(saveSize);

            AToggleMultipleAssignment(lvalue.type, lvalue.num, oldCodeIndex,
                                      saveSize);
        } else {
            /* Single assignment */

            if (lvalue.type == ET_LOCAL_LVALUE) {
                /* Simple case - the lvalue is a local variable. */
                tok = AParseAssignExpression(tok, TRUE);
                AEmitAssignmentDestination(lvalue.num);
            } else {
                /* The lvalue is not a local variable. */
                int num;
                int saveSize;
                AOpcode assignOpcode;

                ACreateOpcode(&lvalue);
                assignOpcode = AGetPrevOpcode();
                ATogglePreviousInstruction();

                saveSize = AGetCodeIndex() - oldCodeIndex;
                ASaveCode(saveSize);

                tok = AParseExpression(tok, &num, TRUE);

                ARestoreCode(saveSize);

                if (AIsSpecialAssignmentOpcode(assignOpcode))
                    AEmitArg(num + A_NO_RET_VAL);
                else
                    AEmitArg(num);
            }
        }
    } else if (AIsOperatorAssignment(tok->type))
        tok = ParseOperatorAssignment(&lvalue, tok);
    else {
        /* Expression statement */

        /* IDEA: Is this needed? */
        ANumLocalsActive = oldNumLocalsActive;

        /* Logical expressions are incomplete. Finalize them by (arbitrarily)
           modifying them to store their result in the stack frame. */
        if (lvalue.type == ET_LOGICAL)
            AConvertToLocalExpression(&lvalue);

        if (AIsPartialExpression(lvalue.type)) {
            if (AIsCallOpcode(AGetPrevOpcode()))
                AEmitDestination(A_NO_RET_VAL);
            else
                AEmitDestination(AGetLocalVariable());
        } else if (AIsMemberExpression(lvalue.type)) {
            ACreateOpcode(&lvalue);
            AEmitDestination(AGetLocalVariable());
        }
    }

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    /* Potentially restore the value of NumLocals. */
    if (oldNumLocals > ANumLocals)
        ANumLocals = oldNumLocals;

    ANumLocalsActive = oldNumLocalsActive;

    return AAdvanceTok(tok);
}


/* Parse operator assignment statement, starting at the op= token, for example
   += (lvalue has already been parsed). */
static AToken *ParseOperatorAssignment(AExpression *lvalue, AToken *tok)
{
    AExpression rvalue;
    AOperator operator;

    /* Determine the operator based on the next token (OPER_PLUS if the token
       is +=, etc.) */
    operator = OperatorTypeFromOperatorAssignmentToken(tok);

    /* Skip op= token. */
    tok = AAdvanceTok(tok);

    /* Check if we have a valid lvalue expression. */
    if (!AIsLvalueExpression(lvalue->type)) {
        AGenerateError(tok->lineNumber, ErrInvalidLvalue);
        /* Create a dummy lvalue to avoid further errors. */
        lvalue->type = ET_LOCAL_LVALUE;
        lvalue->num = 0;
    }

    /* If the lvalue was invalid, act as if nothing has happened to avoid
       problems later. The compilation will fail anyway. */
    if (lvalue->type == ET_ERROR) {
        lvalue->type = ET_LOCAL_LVALUE;
        lvalue->num = 0;
    }

    if (!AIsArrayExpression(lvalue->type) &&
        !AIsTupleExpression(lvalue->type)) {
        /* Non-array lvalue */

        if (lvalue->type == ET_LOCAL_LVALUE) {
            /* The target is a local value. */
            AOperandType leftType, rightType;

            /* Parse the right operand expression. */
            tok = ParseSubExpression(tok, PR_VALUE, &rvalue, 0, FALSE);

            if (AIsComplexExpression(rvalue.type) || rvalue.type == ET_LOGICAL)
                AConvertToLocalExpression(&rvalue);

            /* Figure out operand types. */
            leftType = rightType = A_OT_LOCAL;
            if (AIsGlobalExpression(rvalue.type))
                rightType = A_OT_GLOBAL;
            else if (rvalue.type == ET_INT)
                rightType = A_OT_INT;

            AEmitBinaryOperation(operator, leftType, lvalue->num, rightType,
                                 rvalue.num);

            AEmitAssignmentDestination(lvalue->num);
        } else {
            /* The target is an array index, member access or exposed local
               variable expression. */
            AOperandType leftType, rightType;
            int rvalueTargetOpcodeIndex;

            /* Make sure that we won't overwrite the temporary operand values
               for the previous rvalue opcode (such as array base and index),
               so that we can reuse the operands later when creating the
               assignment opcode. Note that the opcode is incomplete at this
               point, missing the target local variable number. */
            if (lvalue->type == ET_MEMBER_LVALUE
                || lvalue->type == ET_GLOBAL_LVALUE
                || lvalue->type == ET_LOCAL_LVALUE_EXPOSED) {
                /* Member expressions require special processing. Note that
                   suboptimal code is created for global variable lvalues. */
                AConvertToLocalExpression(lvalue);
                MaintainOperandValuesForPreviousOpcode();
            } else {
                MaintainOperandValuesForPreviousOpcode();
                AConvertToLocalExpression(lvalue);
            }

            rvalueTargetOpcodeIndex = AGetPrevOpcodeIndex();

            /* Parse the right operand expression. */
            tok = ParseSubExpression(tok, PR_VALUE, &rvalue, 0, FALSE);

            if (AIsComplexExpression(rvalue.type) || rvalue.type == ET_LOGICAL)
                AConvertToLocalExpression(&rvalue);

            /* Figure out operand types. */
            leftType = rightType = A_OT_LOCAL;
            if (AIsGlobalExpression(rvalue.type))
                rightType = A_OT_GLOBAL;
            else if (rvalue.type == ET_INT)
                rightType = A_OT_INT;

            AEmitBinaryOperation(operator, leftType, lvalue->num, rightType,
                                 rvalue.num);
            AEmitAssignmentDestination(lvalue->num);

            EmitAssignmentOpcodeForOperatorAssignment(rvalueTargetOpcodeIndex,
                                                      lvalue->num);
        }
    } else {
        /* Array lvalue is an error. */
        AGenerateError(tok->lineNumber, ErrInvalidLvalue);
        while (tok->type != TT_NEWLINE)
            tok = AAdvanceTok(tok);
    }

    return tok;
}


static AOperator OperatorTypeFromOperatorAssignmentToken(AToken *tok)
{
    ATokenType type = tok->type;

    switch (type) {
    case TT_ASSIGN_ADD:
        return OPER_PLUS;
    case TT_ASSIGN_SUB:
        return OPER_MINUS;
    case TT_ASSIGN_MUL:
        return OPER_MUL;
    case TT_ASSIGN_DIV:
        return OPER_DIV;
    case TT_ASSIGN_POW:
        return OPER_POW;
    default:
        /* We should not ever reach this code. */
        AGenerateError(tok->lineNumber, ErrInternalError);
        return OPER_PLUS; /* Return an arbitrary value. */
    }
}


/* Update ANumLocalsActive so that the temporary operand values of the
   previous opcode (which may be incomplete) will not be overwritten. */
static void MaintainOperandValuesForPreviousOpcode(void)
{
    int maxLvar = 0;
    int prevIndex = AGetPrevOpcodeIndex();

    switch (AGetPrevOpcode()) {
    case OP_ASSIGN_GL:
    case OP_ASSIGN_VL:
    case OP_ASSIGN_MDL:
    case OP_ASSIGN_EL:
        /* No temporary local values need to be preserved. */
        break;
    case OP_ASSIGN_ML:
        /* Preserve the value whose member is being accessed. */
        maxLvar = AGetOpcode(prevIndex + 1);
        break;
    case OP_AGET_LLL:
        /* Preserve base and index values. */
        maxLvar = AMax(AGetOpcode(prevIndex + 1),
                       AGetOpcode(prevIndex + 2));
        break;
    case OP_AGET_GLL:
        /* Preserve index value. */
        maxLvar = AGetOpcode(prevIndex + 2);
        break;
    default:
        /* Should never reach here. */
        AGenerateError(0, ErrInternalError);
        break;
    }

    ANumLocalsActive = AMax(ANumLocalsActive, maxLvar + 1);
}


/* Emit the opcode that assigns the result in an operator assignment statement
   such as += when the lvalue is a non-local lvalue. The opcode is created
   based on the opcode that reads the value of the lvalue before assignment. */
static void EmitAssignmentOpcodeForOperatorAssignment(int readOpcodeIndex,
                                                      int rvalueNum)
{
    switch (AGetOpcode(readOpcodeIndex)) {
    case OP_ASSIGN_GL:
        AEmitOpcode2Args(OP_ASSIGN_LG, AGetOpcode(readOpcodeIndex + 1),
                         rvalueNum);
        break;
    case OP_ASSIGN_ML:
        AEmitOpcode3Args(OP_ASSIGN_LM, AGetOpcode(readOpcodeIndex + 1),
                         AGetOpcode(readOpcodeIndex + 2), rvalueNum +
                         A_NO_RET_VAL);
        break;
    case OP_ASSIGN_VL:
        AEmitOpcode2Args(OP_ASSIGN_LV, AGetOpcode(readOpcodeIndex + 1),
                         rvalueNum);
        break;
    case OP_ASSIGN_MDL:
        AEmitOpcode3Args(OP_ASSIGN_LMD, AGetOpcode(readOpcodeIndex + 1),
                         AGetOpcode(readOpcodeIndex + 2), rvalueNum +
                         A_NO_RET_VAL);
        break;
    case OP_ASSIGN_EL:
        AEmitOpcode2Args(OP_ASSIGN_LE, AGetOpcode(readOpcodeIndex + 1),
                         rvalueNum);
        break;
    case OP_AGET_LLL:
        AEmitOpcode3Args(OP_ASET_LLL,
                         AGetOpcode(readOpcodeIndex + 1),
                         AGetOpcode(readOpcodeIndex + 2),
                         rvalueNum + A_NO_RET_VAL);
        break;
    case OP_AGET_GLL:
        AEmitOpcode3Args(OP_ASET_GLL,
                         AGetOpcode(readOpcodeIndex + 1),
                         AGetOpcode(readOpcodeIndex + 2),
                         rvalueNum + A_NO_RET_VAL);
        break;
    default:
        /* Should never reach here. */
        AGenerateError(0, ErrInternalError);
        break;
    }
}


AToken *AParseCaseExpression(AToken *tok, int num, int *skipIndex)
{
    AExpression expr;
    AOpcode getOpcode;
    ABranchList *trueBranches;

    int oldNumLocalsActive = ANumLocalsActive;

    trueBranches = NULL;
    oldNumLocalsActive = ANumLocalsActive;

    for (;;) {
        ABranchList *tmp;

        tok = ParseSubExpression(tok, PR_SINGLE, &expr, 0, FALSE);

        if (AIsComplexExpression(expr.type))
            AConvertToLocalExpression(&expr);

        if (AIsGlobalExpression(expr.type))
            getOpcode = OP_GET_LG;
        else if (expr.type == ET_INT)
            getOpcode = OP_GET_LI;
        else
            getOpcode = OP_GET_LL;

        AEmitOpcode2Args(getOpcode, num, expr.num); /* FIX */

        if (tok->type != TT_COMMA)
            break;

        AEmitOpcodeArg(OP_EQ, 0);
        tmp = trueBranches;
        trueBranches = ACreateBranchList();
        AMergeBranches(trueBranches, tmp);

        tok = AAdvanceTok(tok);
    }

    *skipIndex = AGetCodeIndex();
    AEmitOpcodeArg(OP_NEQ, 0);

    ASetBranches(trueBranches, AGetCodeIndex());

    ANumLocalsActive = oldNumLocalsActive;

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    return AAdvanceTok(tok);
}


/* Parse a (sub)expression. allowCast defines whether unbracketed assignments
   are accepted. */
AToken *ParseSubExpression(AToken *tok, APrecedence prec, AExpression *result,
                           int depth, ABool allowCast)
{
    int oldNumLocalsActive = ANumLocalsActive;

    AExpression left;

    if (depth == A_MAX_SUBEXPRESSION_DEPTH) {
        AGenerateError(tok->lineNumber, ErrInternalOverflow,
                      IOF_EXPRESSION_DEPTH_TOO_LARGE);
        while (tok->type != TT_NEWLINE)
            tok = AAdvanceTok(tok);
        return tok;
    }

    switch (tok->type) {
    case TT_ID: {
        ASymbolInfo *sym;

        sym = tok->info.sym->info;

        if (AIsLocalId(sym->type) && (tok + 1)->type != TT_SCOPEOP) {
            left.num = sym->num;
            if (sym->type == ID_LOCAL)
                left.type = ET_LOCAL_LVALUE;
            else if (sym->type == ID_LOCAL_CONST)
                left.type = ET_LOCAL;
            else if (sym->type == ID_LOCAL_EXPOSED)
                left.type = ET_LOCAL_LVALUE_EXPOSED;
            else
                left.type = ET_LOCAL_EXPOSED;
        } else if (sym->type == ID_MEMBER && ACurClass != NULL
                   && ((sym->sym == ACurClass
                        && ACurMember != AM_CREATE)
                       || FindMemberVariable(sym))
                   && (tok + 1)->type != TT_SCOPEOP) {
            CreateMemberAccess(sym, &left);
            CheckCreateMemberAccess(tok, sym);
        } else {
            tok = AParseGlobalVariable(tok, &sym, FALSE, FALSE);

            if (sym->type != ID_ERR_PARSE) {
                /* Global variable or undefined */
                left.type = (sym->type == ID_GLOBAL
                             || sym->type == ID_ERR_UNDEFINED)
                    ? ET_GLOBAL_LVALUE : ET_GLOBAL;
                left.num = sym->num;
                left.sym = sym;
                goto NoLex;
            } else {
                /* Parse error */
                result->type = ET_ERROR;
                return tok;
            }
        }

        break;
    }

    case TT_SCOPEOP: {
        ASymbolInfo *sym;

        tok = AParseQuotedGlobalVariable(tok, &sym, FALSE);

        if (sym->type != ID_ERR_PARSE) {
            left.type = (sym->type == ID_GLOBAL) ? ET_GLOBAL_LVALUE
                                                 : ET_GLOBAL;
            left.num = sym->num;
            left.sym = sym;
        } else {
            /* Parse error */
            result->type = ET_ERROR;
            return tok;
        }

        goto NoLex;
    }

    case TT_NIL:
        left.type = ET_GLOBAL;
        left.num  = GL_NIL;
        left.sym  = NULL; /* FIX: is this ok? is something going to fail? */
        break;

    case TT_SELF:
        if (ACurClass == NULL) {
            AGenerateError(tok->lineNumber, ErrSelfUsedInNonMemberFunction);
            left.type = ET_LOCAL;
            left.num = 0;
            break;
        }

        if ((tok + 1)->type == TT_DOT) {
            ASymbolInfo *sym;

            tok = AAdvanceTok(AAdvanceTok(tok));
            if (tok->type != TT_ID) {
                result->type = ET_ERROR;
                return AGenerateParseError(tok);
            }

            sym = tok->info.sym->info;
            if (sym->type == ID_LOCAL)
                sym = sym->next;
            if (sym->type == ID_MEMBER
                && ((sym->sym == ACurClass
                     && ACurMember != AM_CREATE)
                    || FindMemberVariable(sym))) {
                CreateMemberAccess(sym, &left);
                CheckCreateMemberAccess(tok, sym);
            } else {
                left.type = ET_MEMBER_LVALUE;
                left.num = 3;
                left.sym  = AGetMemberSymbol(tok->info.sym);
            }
        } else {
            left.type = ET_LOCAL;
            left.num  = 3;
        }

        break;

    case TT_SUPER: {
        ATypeInfo *type;
        unsigned member;

        /* Skip "super". */
        tok = AAdvanceTok(tok);

        /* Dot followed by an indentifier expected. */
        if (tok->type != TT_DOT || (tok + 1)->type != TT_ID) {
            if (tok->type == TT_DOT)
                tok = AAdvanceTok(tok);
            result->type = ET_ERROR;
            return AGenerateParseError(tok);
        }

        /* Skip dot. */
        tok = AAdvanceTok(tok);

        /* If we cannot find a member in the superclass, make it a dummy local
           variable instead. This won't matter, since the compilation will
           fail. */
        left.type = ET_LOCAL_LVALUE;
        left.num = 0;

        if (ACurClass == NULL) {
            AGenerateError(tok->lineNumber, ErrSuperUsedInNonMemberFunction);
            break;
        }

        member = AGetMemberSymbol(tok->info.sym)->num;
        type = AType;

#if 0
        if (member == AM_CREATE && ASuperType(type) != NULL) {
            left.type = ET_MEMBER_FUNCTION;
            left.num = type->super->create;
            break;
        }
#endif

        /* Search all the superclasses for the member. */
        while (ASuperType(type) != NULL) {
            unsigned method;

            type = type->super;

            method = ALookupMemberTable(type, MT_METHOD_PUBLIC, member);
            if (method != -1) {
                left.type = ET_MEMBER_FUNCTION;
                left.num = method;
                break;
            } else {
                unsigned read, write;

                read = ALookupMemberTable(type, MT_VAR_GET_PUBLIC, member);
                if (read != -1) {
                    type = ASuperType(AType);
                    do {
                        write = ALookupMemberTable(type, MT_VAR_SET_PUBLIC,
                                                  member);
                        if (write != -1)
                            break;
                        type = ASuperType(type);
                    } while (type != NULL);
                    if (write != -1)
                        left.type = ET_PARTIAL_LVALUE;
                    else
                        left.type = ET_PARTIAL;

                    AEmitPrivateMemberRead(read, write);
                    break;
                }
            }
        }

        if (left.type == ET_LOCAL_LVALUE)
            AGenerateError(tok->lineNumber, ErrUndefinedInSuperClass, tok);

        break;
    }

    case TT_LITERAL_INT:
        /* Integer constant */

        left.type = ET_INT;
        left.num = tok->info.num;

        break;

    case TT_LITERAL_FLOAT:
    case TT_LITERAL_STRING:
        /* Non-integer literal */

        left.type = ET_GLOBAL;
        left.num  = tok->info.num;
        left.sym  = NULL;

        break;

    case TT_COLON:
        /* : can be used as an unary operator only in an index expression. */
        if (prec == PR_VALUE_INDEX) {
            /* Add implicit nil expression as the left operand. */
            left.type = ET_GLOBAL;
            left.num  = 0;
            left.sym  = NULL; /* FIX: is this ok? */
            goto NoLex;
        } else {
            tok = AGenerateParseError(tok);
            result->type = ET_ERROR;
            return tok;
        }

    case TT_NOT:
    case TT_MINUS: {
        int operator;
        int operatorPrecedence;
        AExpression operand;

        operator = tok->type - TT_FIRST_OPERATOR;
        operatorPrecedence = (operator == OPER_NOT) ? PR_NOT : PR_UNARY;

        tok = ParseSubExpression(AAdvanceTok(tok), operatorPrecedence,
                                 &operand, depth + 1, allowCast);

        if (operand.type == ET_ERROR) {
            result->type = ET_ERROR;
            return tok;
        }

        if (operatorPrecedence == PR_NOT) {
            if (operand.type != ET_LOGICAL) {
                AConvertToLogicalExpression(&operand);
                if (operand.type == ET_ERROR) {
                    result->type = ET_ERROR;
                    return tok;
                }
            }

            left.branch[TRUE]  = operand.branch[FALSE];
            left.branch[FALSE] = operand.branch[TRUE];
            left.finalBranch   = operand.finalBranch;

            AToggleBranches(left.branch[TRUE]);
            AToggleBranches(left.branch[FALSE]);
            AToggleBranch(left.finalBranch->data);

            left.type = ET_LOGICAL;
        } else {
            if (!AIsLocalExpression(operand.type))
                AConvertToLocalExpression(&operand);

            AEmitNegateOperation(operand.num);

            left.type = ET_PARTIAL;

            ANumLocalsActive = oldNumLocalsActive;
        }

        goto NoLex;
    }

    case TT_LPAREN:
        tok = AAdvanceTok(tok);

        if (tok->type != TT_RPAREN) {
            /* Subexpression within parentheses. */
            tok = ParseSubExpression(tok, PR_VALUE, &left, depth + 1, TRUE);
            if (tok->type != TT_RPAREN) {
                if (left.type != ET_ERROR) {
                    if (tok->type == TT_NEWLINE)
                        AGenerateError(tok->lineNumber,
                                      ErrUnbalancedParentheses);
                    else
                        tok = AGenerateParseError(tok);
                }
                result->type = ET_ERROR;
                return tok;
            }
        } else {
            /* Create an empty tuple. */
            AEmitTupleCreate(0, 0);
            left.type = ET_PARTIAL;
        }

        break;

    case TT_LBRACKET:
        /* Array creation */

        tok = AAdvanceTok(tok);
        if (tok->type == TT_RBRACKET) {
            /* Create an empty array. */
            AEmitArrayCreate(0, 0);
            left.type = ET_PARTIAL;
        } else {
            tok = ParseSubExpression(tok, PR_SINGLE, &left, depth + 1, TRUE);
            if (tok->type == TT_COMMA) {
                tok = ParseExpressionList(tok, &left, result, depth, TRUE,
                                          oldNumLocalsActive, TRUE);
                if (result->type == ET_ERROR)
                    return tok;

                left = *result;
            } else {
                /* Create a single-element array. */

                AExpressionType type =
                    !AIsRvalue && AIsLvalueExpression(left.type) ?
                    ET_ARRAY_LVALUE : ET_ARRAY;
                ABool isLocal = AIsLocalExpression(left.type);

                /* We might have an extra local value active, at least if the
                   item expression is a member reference. Fix it. */
                ANumLocalsActive = oldNumLocalsActive;

                AConvertToLocalExpression(&left);

                if (type == ET_ARRAY_LVALUE)
                    ARecordAssignment(0);

                AEmitArrayCreate(oldNumLocalsActive, 1);

                if (type == ET_ARRAY_LVALUE && isLocal)
                    left.type = ET_ARRAY_LOCAL_LVALUE;
                else
                    left.type = type;
                left.num = 1;

                ANumLocalsActive = oldNumLocalsActive;
            }

            if (tok->type != TT_RBRACKET) {
                if (left.type != ET_ERROR) {
                    if (tok->type == TT_NEWLINE)
                        AGenerateError(tok->lineNumber,
                                      ErrUnbalancedParentheses);
                    else
                        tok = AGenerateParseError(tok);
                }
                result->type = ET_ERROR;
            }
        }

        break;

    case TT_SUB:
    case TT_DEF: {
        /* Anonymous function */
        tok = AParseAnonymousFunction(tok);
        left.type = ET_PARTIAL;
        goto NoLex;
    }

    default:
        tok = AGenerateParseError(tok);
        result->type = ET_ERROR;
        return tok;
    }

    tok = AAdvanceTok(tok);

    /* Use the following label to avoid implicit token advance operation.
       IDEA: This is very ugly and error-prone. */

  NoLex:

    for (;;) {
        switch (tok->type) {
        case TT_EQ:
        case TT_NEQ:
        case TT_LT:
        case TT_LTE:
        case TT_GT:
        case TT_GTE:
        case TT_PLUS:
        case TT_MINUS:
        case TT_ASTERISK:
        case TT_DIV:
        case TT_POW:
        case TT_TO:
        case TT_COLON:
        case TT_IDIV:
        case TT_MOD:
        case TT_IS:
        case TT_IN: {
            AExpression right;
            AOperator operator;
            int operatorPrecedence;
            AOperandType leftType, rightType;

            operator = OperatorIdFromTokenType(tok->type);
            operatorPrecedence = ABinaryPrecedence[operator];

            /* Return if precendence is not high enough or if the operator
               might actually introduce a type annotation. */
            if (operatorPrecedence < prec) {
                *result = left;
                return tok;
            }

            if (AIsComplexExpression(left.type) || left.type == ET_LOGICAL)
                AConvertToLocalExpression(&left);

            /* Skip newline after >, but not at EOF. */
            if (tok->type == TT_GT && AAdvanceTok(tok)->type == TT_NEWLINE
                && AAdvanceTok(AAdvanceTok(tok))->type != TT_EOF)
                tok = AAdvanceTok(tok);
            tok = AAdvanceTok(tok);
            if (operator == OPER_PAIR && tok->type == TT_RBRACKET
                  && prec == PR_VALUE_INDEX) {
                /* Add implicit nil expression. */
                right.type = ET_GLOBAL;
                right.num  = 0;
                right.sym  = NULL; /* FIX: is this ok? */
            } else
                tok = ParseSubExpression(tok, operatorPrecedence | 1, &right,
                                         depth + 1, allowCast);

            if (right.type == ET_ERROR) {
                result->type = ET_ERROR;
                return tok;
            }

            if (AIsComplexExpression(right.type) || right.type == ET_LOGICAL)
                AConvertToLocalExpression(&right);

            /* If comparing against a nil literal using == or !=, switch the
               order of the operands to avoid calling an overloaded ==
               operator. */
            if ((operator == OPER_EQ || operator == OPER_NEQ)
                && AIsGlobalExpression(right.type)
                && right.num == GL_NIL) {
                AExpression tmp = left;
                left = right;
                right = tmp;
            }

            ANumLocalsActive = oldNumLocalsActive;

            leftType = rightType = A_OT_LOCAL;

            if (AIsGlobalExpression(left.type))
                leftType = A_OT_GLOBAL;
            else if (left.type == ET_INT)
                leftType = A_OT_INT;

            if (AIsGlobalExpression(right.type))
                rightType = A_OT_GLOBAL;
            else if (right.type == ET_INT)
                rightType = A_OT_INT;

            AEmitBinaryOperation(operator,
                                 leftType, left.num, rightType, right.num);

            if (AIsComparisonOperator(operator)) {
                left.type = ET_LOGICAL;
                left.branch[TRUE]  = NULL;
                left.branch[FALSE] = NULL;
                left.finalBranch = ACreateBranchList();
                if (left.finalBranch == NULL) {
                    result->type = ET_ERROR;
                    return NULL;
                }
            } else
                left.type = ET_PARTIAL;

            break;
        }

        case TT_AS:
            /* The token sequence "as" "<" introduces a type application
               annotation. Unconditionally ignore it whenever it is
               encountered. */
            if (tok->type == TT_AS && (tok + 1)->type == TT_LT) {
                /* Type application */
                tok = AParseBracketedTypeAnnotation(tok);
                break;
            } else if (!allowCast || PR_CAST < prec) {
                *result = left;
                return tok;
            } else {
                /* Cast expression. */
                tok = AAdvanceTok(tok);
                if (tok->type == TT_DYNAMIC) {
                    /* Dynamic cast - a no-op. */
                    tok = AAdvanceTok(tok);
                    break;
                } else if (tok->type == TT_ID || tok->type == TT_SCOPEOP) {
                    /* Normal cast expression. */
                    ASymbolInfo *sym;
                    /* Parse cast target type. */
                    tok = AParseAnyGlobalVariable(tok, &sym, FALSE);
                    if (sym->type != ID_ERR_PARSE) {
                        if (!AIsTypeNum(sym->num)
                            && sym->type != ID_ERR_UNDEFINED)
                            AGenerateError(tok->lineNumber, ErrInvalidCast);
                        if (!AIsLocalExpression(left.type))
                            AConvertToLocalExpression(&left);
                        AEmitOpcode2Args(OP_CHECK_TYPE, left.num, sym->num);
                        goto NoLex;
                    } else {
                        /* Parse error */
                        result->type = ET_ERROR;
                        return tok;
                    }
                } else {
                    /* Invalid use of "as". */
                    tok = AGenerateParseError(tok);
                    result->type = ET_ERROR;
                    return tok;
                }
            }

        case TT_AND:
        case TT_OR: {
            AExpression right;

            int operatorPrecedence = ABinaryPrecedence[tok->type -
                                                     TT_FIRST_OPERATOR];
            ABool cond = (tok->type == TT_AND);

            if (operatorPrecedence < prec) {
                *result = left;
                return tok;
            }

            if (left.type != ET_LOGICAL) {
                AConvertToLogicalExpression(&left);
                if (left.type == ET_ERROR) {
                    result->type = ET_ERROR;
                    return tok;
                }
            }

            if (!cond)
                AToggleBranches(left.branch[FALSE]);
            ASetBranches(left.branch[cond], AGetCodeIndex());

            tok = ParseSubExpression(AAdvanceTok(tok), operatorPrecedence,
                                     &right, depth + 1, allowCast);

            if (right.type != ET_LOGICAL) {
                AConvertToLogicalExpression(&right);
                if (right.type == ET_ERROR) {
                    result->type = ET_ERROR;
                    return tok;
                }
            }

            left.finalBranch->next = left.branch[1 - cond];
            AMergeBranches(left.finalBranch, right.branch[1 - cond]);

            left.branch[1 - cond] = left.finalBranch;
            left.branch[cond]     = right.branch[cond];
            left.finalBranch      = right.finalBranch;

            break;
        }

        case TT_LPAREN: {
            /* Function call */

            AExpression arg;
            int quickArgs[A_MAX_QUICK_ARGS];
            int numArgs = 0;
            AOperandType type;
            ABool isVarArg = FALSE;

            /* Skip '('. */
            tok = AAdvanceTok(tok);

            if (!AIsCallableExpression(left.type))
                AConvertToLocalExpression(&left);

            while (tok->type != TT_RPAREN && tok->type != TT_NEWLINE &&
                   !isVarArg) {

                if (tok->type == TT_ASTERISK) {
                    tok = AAdvanceTok(tok);
                    isVarArg = TRUE;
                }

                tok = ParseSubExpression(tok, PR_SINGLE, &arg, depth + 1,
                                         TRUE);

                if (!AIsLocalExpression(arg.type)
                        || numArgs >= A_MAX_QUICK_ARGS)
                    AConvertToLocalExpression(&arg);

                if (numArgs < A_MAX_QUICK_ARGS)
                    quickArgs[numArgs] = arg.num;

                numArgs++;

                if (tok->type != TT_COMMA)
                    break;

                tok = AAdvanceTok(tok);
            }

            if (tok->type != TT_RPAREN) {
                if (arg.type != ET_ERROR)
                    tok = AGenerateParseError(tok);
                result->type = ET_ERROR;
                return tok;
            }

            /* Skip ')'. */
            tok = AAdvanceTok(tok);

            if (left.type != ET_MEMBER && left.type != ET_MEMBER_LVALUE) {
                if (AIsGlobalExpression(left.type)) {
                    type = A_OT_GLOBAL;
                    if (left.sym != NULL && (left.sym->type == ID_GLOBAL_DEF
                        || left.sym->type == ID_GLOBAL_CLASS)) {
                        int minArgs = AGetMinArgs(left.sym);
                        int maxArgs = AGetMaxArgs(left.sym);
                        int minActualArgs = numArgs - isVarArg;

                        if ((minActualArgs < minArgs && !isVarArg)
                            || (!(maxArgs & A_VAR_ARG_FLAG) &&
                                minActualArgs > maxArgs))
                            AGenerateError(tok->lineNumber,
                                          ErrWrongNumberOfArgs, left.sym);
                    }
                } else if (left.type == ET_MEMBER_FUNCTION) {
                    type = A_OT_MEMBER_FUNCTION;
                } else
                    type = A_OT_LOCAL;

                AEmitCall(type, left.num, numArgs, isVarArg, quickArgs);
            } else
                AEmitMemberCall(type, left.num, left.sym->num, numArgs,
                                isVarArg, quickArgs,
                                ANumLocalsActive - numArgs);

            ANumLocalsActive = oldNumLocalsActive;

            left.type = ET_PARTIAL;

            break;
        }

        case TT_LBRACKET: {
            /* Array indexing */

            AExpression index;
            int baseType;

            if (!AIsIndexableExpression(left.type))
                AConvertToLocalExpression(&left);

            tok = ParseSubExpression(AAdvanceTok(tok), PR_VALUE_INDEX, &index,
                                     depth + 1, TRUE);

            if (!AIsLocalExpression(index.type))
                AConvertToLocalExpression(&index);

            if (tok->type != TT_RBRACKET) {
                if (index.type != ET_ERROR) {
                    tok = AGenerateExpressionParseError(tok);
                    index.type = ET_ERROR;
                }
            } else
                tok = AAdvanceTok(tok);

            if (index.type == ET_ERROR) {
                result->type = ET_ERROR;
                return tok;
            }

            if (AIsLocalExpression(left.type))
                baseType = A_OT_LOCAL;
            else if (AIsGlobalExpression(left.type))
                baseType = A_OT_GLOBAL;
            else
                baseType = A_OT_MEMBER_VARIABLE;

            AEmitGetIndex(baseType, left.num, index.num);

            ANumLocalsActive = oldNumLocalsActive;

            left.type = ET_PARTIAL_LVALUE;

            break;
        }

        case TT_DOT:
            tok = AAdvanceTok(tok);

            if (tok->type != TT_ID) {
                result->type = ET_ERROR;
                return AGenerateParseError(tok);
            }

            if (!AIsLocalExpression(left.type))
                AConvertToLocalExpression(&left);

            left.type = ET_MEMBER_LVALUE;
            left.sym  = AGetMemberSymbol(tok->info.sym);
            CheckCreateMemberAccess(tok, left.sym);

            tok = AAdvanceTok(tok);

            /* Note that we cannot restore ANumLocalsActive since the base
               object needs to preserved for parsing method calls. Otherwise,
               method call arguments could overwrite the base object. */

            break;

        case TT_COMMA:
            /* Tuple creation */

            if (prec >= PR_SINGLE) {
                *result = left;
                return tok;
            }

            return ParseExpressionList(tok, &left, result, depth, allowCast,
                                       oldNumLocalsActive, FALSE);

        default:

            *result = left;
            return tok;
        }
    }
}


/* Returns the number of the first currently unused local variable. Increments
   the number of active local variables. */
int AGetLocalVariable(void)
{
    unsigned var = ANumLocalsActive++;

    if (var >= ANumLocals)
        ANumLocals = ANumLocalsActive;

    return var;
}


void ACreateOpcode(AExpression *expr)
{
    AExpressionType type = expr->type;

    /* FIX: these kinda don't work.. */
    if (type != ET_PARTIAL) {
        if (AIsLocalExpression(type))
            AEmitOpcodeArg(OP_ASSIGN_LL, expr->num);
        else if (type == ET_INT)
            AEmitOpcodeArg(OP_ASSIGN_IL, expr->num);
        else if (AIsGlobalExpression(type))
            AEmitOpcodeArg(OP_ASSIGN_GL, expr->num);
        else if (type == ET_LOGICAL)
            AConvertToValueExpression(expr);
        else if (type == ET_MEMBER || type == ET_MEMBER_LVALUE /* FIX */)
            AEmitOpcode2Args(OP_ASSIGN_ML, expr->num, expr->sym->num);
        else if (type == ET_MEMBER_FUNCTION)
            AEmitOpcodeArg(OP_ASSIGN_FL, expr->num); /* FIX?? */
        else if (AIsExposedLocalExpression(type))
            AEmitOpcodeArg(OP_ASSIGN_EL, expr->num);
    }
}


/* IDEA: Not nice... Generates an opcode that targets a local variable. Well,
   this probably needs some restructuring. */
void AConvertToLocalExpression(AExpression *expr)
{
    ACreateOpcode(expr);

    if (expr->type != ET_ERROR) {
        expr->num = AGetLocalVariable();
        expr->type = ET_LOCAL;
    }

    AEmitDestination(expr->num);
}


/* expr is assumed to be a logical expression. */
void AConvertToValueExpression(AExpression *expr)
{
    expr->finalBranch->next = expr->branch[FALSE];
    expr->branch[FALSE] = expr->finalBranch;

    ASetBranches(expr->branch[TRUE], AGetCodeIndex());
    AEmitOpcode(OP_ASSIGN_TRUEL_SKIP); /* FIX */

    AToggleBranches(expr->branch[FALSE]);
    ASetBranches(expr->branch[FALSE], AGetCodeIndex());
    AEmitOpcode(OP_ASSIGN_FALSEL); /* FIX */

    expr->type = ET_PARTIAL;
}


/* Assumes that the expression isn't already a logical one. */
void AConvertToLogicalExpression(AExpression *expr)
{
    if (!AIsLocalExpression(expr->type)) {
        if (expr->type == ET_ERROR)
            return;
        AConvertToLocalExpression(expr);
        ANumLocalsActive--;
    }

    AEmitIfBoolean(expr->num, 0);

    expr->type = ET_LOGICAL;
    expr->branch[FALSE] = NULL;
    expr->branch[TRUE]  = NULL;
    expr->finalBranch   = ACreateBranchList();
    if (expr->finalBranch == NULL)
        expr->type = ET_ERROR;
}


ABranchList *ACreateBranchList(void)
{
    ABranchList *branch = ACAlloc(AListSize); /* FIX? */

    if (branch == NULL) {
        AGenerateOutOfMemoryError();
        return NULL;
    }

    branch->next = NULL;
    branch->data = AGetPrevOpcodeIndex();

    return branch;
}


void ASetBranches(ABranchList *branch, int codeIndex)
{
    ABranchList *prev;

    while (branch != NULL) {
        ASetBranchDest(branch->data, codeIndex);

        prev = branch;
        branch = branch->next;
        ACFree(prev, sizeof(ABranchList)); /* FIX? */
    }
}


void AAdjustBranchPos(ABranchList *branch, int indexDif)
{
    while (branch != NULL) {
        branch->data += indexDif;
        branch = branch->next;
    }
}


/* Negates the branch opcodes in the given branch list. */
void AToggleBranches(ABranchList *branch)
{
    while (branch) {
        AToggleBranch(branch->data);
        branch = branch->next;
    }
}


/* Appends append to the end of branch. Branch must not be empty. */
void AMergeBranches(ABranchList *branch, ABranchList *append)
{
    while (branch->next)
        branch = branch->next;
    branch->next = append;
}


/* Appends an unconditional branch based at CodeIndex to the branch list
   and returns a pointer to the new branch list. Emits the opcode as well.
   Note: this breaks the list in a way - it can't be toggled any longer. */
/* This is actually another type of a list. */
/* FIX: the above explanation doesn't hold any longer. */
ABranchList *AAddBranch(ABranchList *branch)
{
    ABranchList *newBranch = AAddIntList(branch, AGetCodeIndex());

    AEmitJump(0);

    return newBranch;
}


/* Finds out whether id is an active member variable or method in current
   scope. Returns TRUE if so and sets SymbolInfo fields accordingly
   (info.memberValue and sym). */
static ABool FindMemberVariable(ASymbolInfo *id)
{
    int key;
    int item;
    ABool searchPrivate;
    ATypeInfo *type;

    key = id->num;

    /* Do we bypass getters/setters? */
    if (ACurMember == AM_CREATE) {
        /* FIX: should we fail if bypassing and not found? */
        item = AGetMemberVar(FALSE, key);
        if (item == -1)
            item = AGetMemberVar(TRUE, key);

        if (item != -1) {
            id->sym = NULL;
            id->info.memberValue = item |
                                   A_MEMBER_PRIVATE | A_MEMBER_DIRECT_VARIABLE;
            return TRUE;
        }
    }

    type = AType;
    searchPrivate = TRUE;

    do {
        /* Search public and private methods. */
        item = ALookupMemberTable(type, MT_METHOD_PUBLIC, key);
        if (item == -1) {
            if (searchPrivate) {
                item = ALookupMemberTable(type, MT_METHOD_PRIVATE, key);
                if (item != -1) {
                    id->info.memberValue = item | A_MEMBER_PRIVATE |
                                           A_MEMBER_DIRECT_METHOD;
                    id->sym = ACurClass;
                    return TRUE;
                }
            }
        } else {
            id->info.memberValue = key;
            id->sym = ACurClass;
            return TRUE;
        }

        /* Search public and private writable variables. */
        item = ALookupMemberTable(type, MT_VAR_SET_PUBLIC, key);
        if (item == -1) {
            if (searchPrivate) {
                item = ALookupMemberTable(type, MT_VAR_SET_PRIVATE, key);
                if (item != -1) {
                    if ((item & A_VAR_METHOD)
                        || (ALookupMemberTable(type, MT_VAR_GET_PRIVATE, key) &
                            A_VAR_METHOD))
                        id->info.memberValue = key | A_MEMBER_PRIVATE;
                    else
                        id->info.memberValue = item | A_MEMBER_PRIVATE |
                                               A_MEMBER_DIRECT_VARIABLE;

                    id->sym = ACurClass;
                    return TRUE;
                }
            }
        } else {
            id->info.memberValue = key;
            id->sym = ACurClass;
            return TRUE;
        }

        /* Search public and private readable variables. */
        item = ALookupMemberTable(type, MT_VAR_GET_PUBLIC, key);
        if (item == -1) {
            if (searchPrivate) {
                item = ALookupMemberTable(type, MT_VAR_GET_PRIVATE, key);
                if (item != -1) {
                    if (item & A_VAR_METHOD)
                        id->info.memberValue = key | A_MEMBER_PRIVATE |
                                               A_MEMBER_CONSTANT;
                    else
                        id->info.memberValue = item | A_MEMBER_PRIVATE |
                            A_MEMBER_CONSTANT | A_MEMBER_DIRECT_VARIABLE;
                    id->sym = ACurClass;
                    return TRUE;
                }
            }
        } else {
            id->info.memberValue = key | A_MEMBER_CONSTANT;
            id->sym = ACurClass;
            return TRUE;
        }

        searchPrivate = FALSE;

        /* Search the superclass. */
        type = ASuperType(type);
    } while (type != NULL);

    /* Couldn't find a match. */
    return FALSE;
}


static void CreateMemberAccess(ASymbolInfo *sym, AExpression *result)
{
    unsigned member = sym->info.memberValue;

    if (member & A_MEMBER_PRIVATE) {
        if (member & A_MEMBER_DIRECT_METHOD) {
            result->type = ET_MEMBER_FUNCTION;
            result->num  = AGetMemberValue(member);
        } else {
            unsigned memberValue;

            if (member & A_MEMBER_CONSTANT)
                result->type = ET_PARTIAL;
            else
                result->type = ET_PARTIAL_LVALUE;

            memberValue = AGetMemberValue(member);
            if (member & A_MEMBER_DIRECT_VARIABLE)
                AEmitDirectMemberRead(memberValue);
            else
                AEmitPrivateMemberRead(
                    ALookupMemberTable(AType, MT_VAR_GET_PRIVATE, memberValue),
                    ALookupMemberTable(AType, MT_VAR_SET_PRIVATE,
                                       memberValue));
        }
    } else {
        if (member & A_MEMBER_CONSTANT)
            result->type = ET_MEMBER;
        else
            result->type = ET_MEMBER_LVALUE;
        result->num = 3;
        result->sym = sym;
    }
}


static AToken *ParseExpressionList(AToken *tok, AExpression *left,
                                   AExpression *result, int depth,
                                   ABool allowCast, int oldNumLocalsActive,
                                   ABool isArray)
{
    int numElem = 1;
    ABool isLvalue = !AIsRvalue;
    ABool isLocal = TRUE;

    for (;;) {
        if (!AIsLvalueExpression(left->type))
            isLvalue = FALSE;

        if (!AIsLocalExpression(left->type))
            isLocal = FALSE;

        ANumLocalsActive = oldNumLocalsActive + numElem - 1;
        AConvertToLocalExpression(left);

        if (isLvalue) {
            if (numElem <= A_MAX_MULTI_ASSIGN)
                ARecordAssignment(numElem - 1);
            else
                isLvalue = FALSE;
        }

        if (tok->type != TT_COMMA)
            break;

        tok = AAdvanceTok(tok);

        if (tok->type == TT_RPAREN || tok->type == TT_ASSIGN
            || tok->type == TT_RBRACKET || tok->type == TT_EOF)
            break;

        numElem++;

        tok = ParseSubExpression(tok, PR_SINGLE, left, depth + 1, allowCast);
    }

    if (isArray)
        AEmitArrayCreate(oldNumLocalsActive, numElem);
    else
        AEmitTupleCreate(oldNumLocalsActive, numElem);

    ANumLocalsActive = oldNumLocalsActive;

    if (isLvalue && isLocal)
        result->type = isArray ? ET_ARRAY_LOCAL_LVALUE : ET_TUPLE_LOCAL_LVALUE;
    else if (isLvalue)
        result->type = isArray ? ET_ARRAY_LVALUE : ET_TUPLE_LVALUE;
    else
        result->type = isArray ? ET_ARRAY : ET_TUPLE;
    result->num  = numElem;

    return tok;
}


/* Return TRUE if num represents the global value id of a type (either a
   normal class, an interface or a special type). */
static ABool AIsTypeNum(int num)
{
    AValue v = AGlobalByNum(num);
    if (AIsNonSpecialType(v))
        return AValueToType(v)->sym->num == num;
    else if (AIsOfType(v, AGlobalByNum(AStdTypeNum)) == A_IS_TRUE)
        return AValueToFunction(v)->sym->num == num;
    else
        return FALSE;
}


static void CheckCreateMemberAccess(AToken *tok, ASymbolInfo *sym)
{
    if (sym->num == AM_CREATE)
        AGenerateError(tok->lineNumber, ErrCannotAccessCreate);
}
