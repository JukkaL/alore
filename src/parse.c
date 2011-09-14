/* parse.c - Alore parser and compiler

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "parse.h"
#include "class.h"
#include "memberid.h"
#include "error.h"
#include "ident.h"
#include "output.h"
#include "globals.h"
#include "gc_internal.h"
#include "dynaload.h"
#include "internal.h"
#include "debug_runtime.h"
#include "std_module.h"
#include "gc.h" /* IDEA: Use cmem instead? */


#define MIN_EXPOSED_VARIABLES_SIZE 32


AList *AImportedModules;

ASymbolInfo *ACurModule;
ASymbolInfo *ACurClass;
ASymbolInfo *ACurFunction;
int ACurMember;

ATypeInfo *AType;

ASymbolInfo *AAccessorMember;

int ANumLocals;
int ANumLocalsActive;
int AFunDepth;

int ABlockDepth;

static int LoopDepth;

static ASymbolInfo *Locals;

/* List of active break statements. */
static ABreakList *LoopExits;

static AReturnList *ReturnBranches;

static int TryLocalVar;
static AIntList *FinallyExits;

AIntList *AInitFunctions;


int ANumMemberIds; /* FIX: does not belong here? */


ABool AIsMainDefined;
int AMainFunctionNum;

ABool AIsRvalue;


/* A growable array of exposed variables accessed by the current anonymous
   function */
int ANumAccessedExposedVariables;
int AAccessedExposedVariablesSize;
AExposedInfo *AAccessedExposedVariables;


static ABreakList ExitSentinel = { NULL, NULL, 0, 1, -1 };


static AToken *ParseModuleHeader(AToken *tok, AModuleId *module);
static AToken *ParseEncoding(AToken *tok, ABool isUtf8Bom);
static AToken *ParseImports(AToken *tok);

static AToken *ParseGlobalDefinitions(AToken *tok);
static AToken *ParseGlobalDefinition(AToken *tok);

static AToken *ParseVariableDefinition(AToken *tok, ABool isPrivate);
static AToken *ParseFunctionDefinition(AToken *tok, ABool isPrivate,
                                       ABool isInterface);
static AToken *ParseSpecialMethodHeader(AToken *tok, ABool isPrivate,
                                        int *minArgs, int *maxArgs,
                                        AMemberTableType *type);
static AToken *ParseGetterOrSetterMethodHeader(AToken *tok, ABool isPrivate,
                                               int *minArgs, int *maxArgs,
                                               AMemberTableType *type);
static AToken *ParseFunctionArguments(AToken *tok, int *minArgs, int *maxArgs,
                                      ABool allowAnnotation,
                                      ABool isInterface);
static AToken *ParseAdditionalFunctionSignatures(AToken *tok, int minArgs,
                                                 int maxArgs);
static AToken *ParseTypeDefinition(AToken *tok);
static AToken *ParseBindDeclarations(AToken *tok, ATypeInfo *type);
static void BuildConstructor(void);
static AToken *ParseBlock(AToken *tok);
static AToken *ParseBlockWithTerminator(AToken *tok, ATokenType terminator,
                                        AToken **terminatorTok);

static AToken *ParseIfStatement(AToken *tok);
static AToken *ParseWhileStatement(AToken *tok);
static AToken *ParseRepeatStatement(AToken *tok);
static AToken *ParseBreakStatement(AToken *tok);
static AToken *ParseReturnStatement(AToken *tok);
static AToken *ParseForStatement(AToken *tok);
static AToken *ParseTryStatement(AToken *tok);
static AToken *ParseRaiseStatement(AToken *tok);
static AToken *ParseSwitchStatement(AToken *tok);

static ASymbolInfo *AddLocalVariable(AToken *tok, AIdType type);
static ASymbolInfo *AddLocalVariableAt(AToken *tok, AIdType type,
                                       unsigned num);
static AIdType PrepareLocalVariable(AToken *tok);
static void LeaveLoop(void);
    
static ASymbolInfo *FindGlobalVariable(AToken *tok, ABool isSilent);

static ASymbolInfo *CheckGlobal(AToken *tok, AIdType type);

static void CheckSuperInitializer(ATypeInfo *type);

static void GenerateModuleId(ASymbolInfo *id, AModuleId *module);
static AToken *ImportCompiledModule(AToken *tok, AList **moduleList);
static void DeactivateModules(AList *modules);
static void ActivateModules(AList *modules);

static ABool IsDirectTryStatement(AToken *tok);
static void FreeFinallyExits(void);

static void SaveParseState(AParseState *state);
static void RestoreParseState(AParseState *state);

static void CalculateExposedVariableMappingInAnonFunc(AToken *tok);

static void SetSuperType(ATypeInfo *type, ATypeInfo *super, int lineNumber);

static AToken *ClearUntilSeparator(AToken *tok);
static AToken *ClearAnnotationToken(AToken *tok);


/* Parse a source file and compile it to opcodes. The source file must have
   been tokenized and scanned before parsing. Any imported modules must have
   been scanned. */
ABool AParse(AModuleId *module, char *path, AToken *tok)
{
    int initFunctionNum;
    ABool isUtf8Bom;

    /* Enter a new code section for the main level definitions. */
    AEnterSection();

    AEmitAbsoluteLineNumber(tok);

    AInitFunctionParseState();
    
    if (ANumActiveFiles == A_MAX_COMPILE_DEPTH) {
        AGenerateError(-1, ErrInternalOverflow,
                      IOF_COMPILE_DEPTH_TOO_LARGE);
        return FALSE;
    }

    AActiveFiles[ANumActiveFiles] = path;
    ANumActiveFiles++;

    AAccessorMember = NULL; /* FIX: not the right place? */

    /* Check if the file starts with a UTF-8 byte order mark character. If it
       does, the file is encoded in UTF-8 and there must be a matching encoding
       declaration (encoding utf8). */
    isUtf8Bom = FALSE;
    if (tok->type == TT_BOM) {
        isUtf8Bom = TRUE;
        tok = AAdvanceTok(tok);
    }            
    
    if (tok->type == TT_NEWLINE)
        tok = AAdvanceTok(tok);

    tok = ParseEncoding(tok, isUtf8Bom);
    tok = ParseModuleHeader(tok, module);
    tok = ParseImports(tok);

    if (tok == NULL)
        return FALSE;

    if (module->numParts > 0)
        ACurModule = module->id[module->numParts - 1];
    else
        ACurModule = ACurMainModule;
    
    tok = ParseGlobalDefinitions(tok);

    DeactivateModules(AImportedModules);
    ADisposeList(AImportedModules);
    AImportedModules = NULL;

    ANumActiveFiles--;

    /* If we have output some opcodes, create a function and store it in the
       list of initialization functions. */
    if (ABufInd != 1) {
        AEmitOpcode(OP_RET);
        
        initFunctionNum = AAddConstGlobalValue(ALeaveSection(NULL, 0, 0, 0));
        if (initFunctionNum != -1)
            AInitFunctions = AAddIntListEnd(AInitFunctions, initFunctionNum);
        else
            AGenerateOutOfMemoryError();
    } else
        ALeaveSection(NULL, 0, 0, 0);

    return TRUE;
}


static AToken *ParseModuleHeader(AToken *tok, AModuleId *module)
{
    /* Check module header if not compiling the main source file. */
    if (module->numParts > 0) {
        if (tok->type == TT_MODULE) {
            int i;

            for (i = 0; i < module->numParts; i++) {
                if ((tok + 1)->type != TT_ID
                    || (tok + 1)->info.sym != AGetSymbolFromSymbolInfo(
                        module->id[i]))
                    break;
                tok = AAdvanceTok(tok);
                
                if (i != module->numParts - 1 && (tok + 1)->type != TT_SCOPEOP)
                    break;
                tok = AAdvanceTok(tok);
            }
                    
            if (tok->type != TT_NEWLINE) {
                AGenerateError(tok->lineNumber, ErrInvalidModuleHeader);
                do {
                    tok = AAdvanceTok(tok);
                } while (tok->type != TT_NEWLINE);
            }
            
            tok = AAdvanceTok(tok);
        } else
            AGenerateError(tok->lineNumber, ErrMissingModuleHeader);
    }

    return tok;
}


static AToken *ParseEncoding(AToken *tok, ABool isUtf8Bom)
{
    /* Parse encoding declaration, if present. Only check the syntax: semantic
       checking has already been performed in ATokenize. */
    if (tok->type == TT_ENCODING) {
        tok = AAdvanceTok(tok);

        if (tok->type == TT_ID) {
            if (tok->info.sym == ALatin1Symbol
                    || tok->info.sym == AUtf8Symbol
                    || tok->info.sym == AAsciiSymbol) {
                /* If a UTF-8 byte order mark character is present, the file
                   encoding must be specified as UTF-8. */
                if (isUtf8Bom && tok->info.sym != AUtf8Symbol)
                    AGenerateError(tok->lineNumber,
                                   ErrUtf8EncodingDeclarationExpected);
                
                tok = AAdvanceTok(tok);
                if (tok->type != TT_NEWLINE)
                    tok = AGenerateParseError(tok);
            } else
                tok = AGenerateParseError(tok);
        } else
            tok = AGenerateParseError(tok);

        /* Skip newline. */
        tok = AAdvanceTok(tok);
    }
    
    return tok;
}


static AToken *ParseImports(AToken *tok)
{
    AList *newImportedModules;

    newImportedModules = NULL;

    /* Skip encoding declaration if is present and report and error. It might
       confuse parsing of the rest of the file due to unprocessed imports if
       we do not given it special processing here. */
    if (tok->type == TT_ENCODING) {
        tok = AGenerateParseError(tok);
        if (tok->type == TT_NEWLINE)
            tok = AAdvanceTok(tok);
    }
    
    /* Parse import statements. */
    while (tok->type == TT_IMPORT) {
        do {
            if ((tok + 1)->type == TT_ID) {
                tok = ImportCompiledModule(AAdvanceTok(tok),
                                           &newImportedModules);
            } else
                tok = AGenerateParseError(AAdvanceTok(tok));
        } while (tok->type == TT_COMMA);

        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);

        tok = AAdvanceTok(tok);
    }
    
    AImportedModules = newImportedModules;

    return tok;
}


static AToken *ParseGlobalDefinitions(AToken *tok)
{
    while (tok->type != TT_EOF)
        tok = ParseGlobalDefinition(tok);
    return tok;
}


static AToken *ParseGlobalDefinition(AToken *tok)
{
    switch (tok->type) {
    case TT_PRIVATE:
        tok = AAdvanceTok(tok);
        if (tok->type != TT_CONST && tok->type != TT_VAR
            && !AIsDefToken(tok->type) && tok->type != TT_CLASS
            && tok->type != TT_INTERFACE)
            tok = AGenerateParseErrorSkipNewline(tok);
        break;

    case TT_CONST:
    case TT_VAR:
        /* The isPrivate argument is not used in this case. */
        tok = ParseVariableDefinition(tok, 0);
        break;

    case TT_SUB:
    case TT_DEF:
        if (AAdvanceTok(tok)->type != TT_LPAREN)
            tok = ParseFunctionDefinition(tok, 0, FALSE);
        else
            tok = AParseAssignmentOrExpression(tok);
        break;

    case TT_CLASS:
    case TT_INTERFACE:
        tok = ParseTypeDefinition(tok);
        break;

    case TT_NEWLINE:
        /* We are not supposed to ever get here, but be defensive... */
        tok = AAdvanceTok(tok);
        break;

    case TT_EOF:
        break;

    case TT_IF:
        ABlockDepth++;
        tok = ParseIfStatement(tok);
        ABlockDepth--;
        break;

    case TT_WHILE:
        ABlockDepth++;
        tok = ParseWhileStatement(tok);
        ABlockDepth--;
        break;

    case TT_REPEAT:
        ABlockDepth++;
        tok = ParseRepeatStatement(tok);
        ABlockDepth--;
        break;

    case TT_FOR:
        ABlockDepth++;
        tok = ParseForStatement(tok);
        ABlockDepth--;
        break;

    case TT_SWITCH:
        ABlockDepth++;
        tok = ParseSwitchStatement(tok);
        ABlockDepth--;
        break;

    case TT_RAISE:
        ABlockDepth++;
        tok = ParseRaiseStatement(tok);
        ABlockDepth--;
        break;

    case TT_TRY:
        ABlockDepth++;
        tok = ParseTryStatement(tok);
        ABlockDepth--;
        break;

    case TT_ID:
    case TT_LITERAL_INT:
    case TT_LITERAL_FLOAT:
    case TT_LITERAL_STRING:
    case TT_NIL:
    case TT_NOT:
    case TT_MINUS:
    case TT_LPAREN:
    case TT_LBRACKET:
    case TT_SCOPEOP:
    case TT_SELF:
    case TT_SUPER:
    case TT_ERR_STRING_UNTERMINATED:
    case TT_ERR_INVALID_NUMERIC:
    case TT_ERR_UNRECOGNIZED_CHAR:
    case TT_ERR_NON_ASCII_STRING_CHAR:
    case TT_ERR_NON_ASCII_COMMENT_CHAR:
    case TT_ERR_INVALID_UTF8_SEQUENCE:
        ABlockDepth++;
        tok = AParseAssignmentOrExpression(tok);
        ABlockDepth--;
        break;

    case TT_BREAK:
        tok = AGenerateErrorSkipNewline(tok, ErrBreakOutsideLoop);
        break;

    case TT_RETURN:
        tok = AGenerateErrorSkipNewline(tok, ErrReturnOutsideFunction);
        break;

    default:
        tok = AGenerateParseErrorSkipNewline(tok);
        break;
    }
    
    return tok;
}


static AToken *ParseVariableDefinition(AToken *tok, ABool isPrivate)
{
    AToken *vars[A_MAX_MULTI_ASSIGN];
    int numVars = 0;

    ABool isMulti = FALSE;
    ABool isInit  = FALSE;
    ABool isConst = tok->type == TT_CONST;

    int i;

    ADebugCompilerMsg(("Parse vardef"));
    
    AEmitLineNumber(tok);

    /* Skip "var" or "const". */
    tok = AAdvanceTok(tok);
    
    if (!AIsIdTokenType(tok->type))
        return AGenerateParseErrorSkipNewline(tok);

    /* Parse the name list in this definition and store references to the
       name tokens. */
    do {
        if (numVars == A_MAX_MULTI_ASSIGN) {
            AGenerateError(tok->lineNumber, ErrInternalOverflow,
                          IOF_TOO_MANY_VARS_IN_MULTIPLE_ASSIGN);
            return tok;
        }
        
        vars[numVars++] = tok;
        tok = AAdvanceTok(tok);

        if (tok->type != TT_COMMA)
            break;

        tok = AAdvanceTok(tok);

        if (tok->type == TT_NEWLINE)
            AGenerateParseError(tok); /* FIX: this seems strange?? */

        isMulti = TRUE;
    } while (AIsIdTokenType(tok->type));

    if (tok->type == TT_ASSIGN) {
        /* Initialization */

        tok = AAdvanceTok(tok);

        isInit = TRUE;
        
        if (isMulti)
            tok = AParseMultiExpression(tok, numVars, FALSE);
        else {
            if (ACurFunction != NULL || ACurMember != AM_NONE) {
                tok = AParseAssignExpression(tok, FALSE);
                AEmitArg(ANumLocalsActive);
            } else {
                int local;
                tok = AParseExpression(tok, &local, FALSE);
            }
        }
    }

    /* What kind of variable(s) are we defining? */
    if (ACurFunction != NULL || ACurMember != AM_NONE || ABlockDepth > 0) {
        /* Define local variables. */   
        for (i = 0; i < numVars; i++) {
            AIdType type;
            
            if (!isInit) {
                AEmitOpcode(OP_ASSIGN_NILL);
                AEmitArg(ANumLocalsActive);
            }

            type = PrepareLocalVariable(vars[i]);
            AddLocalVariable(vars[i], type);
        }
    } else if (ACurClass == NULL) {
        /* Define global variables. */  
        for (i = 0; i < numVars; i++) {
            ASymbolInfo *var = CheckGlobal(vars[i], isConst ? ID_GLOBAL_CONST
                                                            : ID_GLOBAL);
            if (isInit)
                AEmitOpcode2Args(OP_ASSIGN_LGC, var->num,
                                 ANumLocalsActive + i);
            else if (isConst) {
                /* FIX: what if CreateConstant or SetConst.. fails? */
                ASetConstGlobalValue(ACompilerThread, var->num,
                                     ACreateConstant(var));
            }
        }
    } else {
        /* Define class member variables. */
        for (i = 0; i < numVars; i++) {
            int memberId;
            int num;

            memberId = AGetMemberSymbol(vars[i]->info.sym)->num;

            num = AType->totalNumVars++;

            ACheckMemberVariableDefinition(AType, memberId, isPrivate,
                                           vars[i]);

            if (isInit)
                AEmitOpcode2Args(OP_ASSIGN_LV, num, ANumLocalsActive + i);
        }
    }

    tok = AParseTypeAnnotation(tok);

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    ADebugCompilerMsg(("End parse vardef"));
    
    return AAdvanceTok(tok);
}


/* Initialize state variables that are used during function/method
   compilation. */
void AInitFunctionParseState(void)
{
    LoopDepth = 0;
    LoopExits = &ExitSentinel;
    FinallyExits = NULL;
    ReturnBranches = NULL;    
    Locals = NULL;
    
    ANumLocals = ANumLocalsActive = 3; /* FIX: constant? */

    ABlockDepth = 0;
    TryLocalVar = -1;
}


static AToken *ParseFunctionDefinition(AToken *tok, ABool isPrivate,
                                       ABool isInterface)
{
    int oldNumLocals = ANumLocals;
    int oldNumLocalsActive = ANumLocalsActive;
    int oldBlockDepth = ABlockDepth;
    int minArgs, maxArgs;
    AMemberTableType type;
    AValue funcValue;
    AToken *endTok;
    int num;

    ADebugCompilerMsg(("Parse fundef"));

    AInitFunctionParseState();
    AEnterSection();
    AEmitAbsoluteLineNumber(tok);

    type = MT_METHOD_PUBLIC;

    /* Skip "def". */
    tok = AAdvanceTok(tok);

    minArgs = maxArgs = 0;
            
    /* An identifier followed by '(' (or '<') introduces an ordinary
       function (or method). */
    if (tok->type == TT_ID && ((tok + 1)->type == TT_LPAREN
                               || (tok + 1)->type == TT_LT)) {   
        if (ACurClass == NULL) {
            /* Function definition */

            ACurFunction = CheckGlobal(tok, ID_GLOBAL_DEF);

            /* Is this a definition of main function? */
            if (tok->info.sym == AMainSymbol) {
                int minArgs = ACurFunction->info.global.minArgs;
                int maxArgs = ACurFunction->info.global.maxArgs;
            
                if (!(minArgs == maxArgs
                      && (minArgs == 0 ||
                          (minArgs == 1 && ACurModule == ACurMainModule))))
                    AGenerateError(tok->lineNumber,
                                   ErrWrongNumberOfArgsForMain);
                
                AIsMainDefined = TRUE;
                AMainFunctionNum = ACurFunction->num;
            }
        } else {
            /* Method definition */
            minArgs = maxArgs = 1;
            ACurMember = AGetMemberSymbol(tok->info.sym)->num;
            ANumLocals = ANumLocalsActive = 4;
            ACheckMethodDefinition(AType, ACurMember, isPrivate, tok);
            if (ACurMember == AM_CREATE && !AType->hasMemberInitializer) {
                /* Check if any of the superclasses define initializer
                   functions and call them if found. */
                CheckSuperInitializer(AType);
            }
        }

        /* Skip identifier. */
        tok = AAdvanceTok(tok);
        tok = ParseFunctionArguments(tok, &minArgs, &maxArgs,
                                     TRUE, isInterface);
    } else if (ACurClass != NULL) {
        /* Probably a getter or a setter */
        tok = ParseSpecialMethodHeader(tok, isPrivate, &minArgs, &maxArgs,
                                       &type);
    } else {
        /* Invalid header */
        if (tok->type == TT_ID)
            tok = AAdvanceTok(tok);
        tok = AGenerateParseError(tok);
    }

    /* Skip newline after function header. */
    tok = AAdvanceTok(tok);

    /* Do not parse function body if this is an interface method. */
    if (!isInterface) {
        endTok = NULL;
        tok = ParseBlockWithTerminator(tok, TT_END, &endTok);
        
        /* FIX: is it possible that something else has to be freed? */
        
        /* Simply ignore the active finally exits, since they already have the
           correct offset that specifies that the opcode is in the outermost
           finally statement. */
        FreeFinallyExits();

        if (!AIsRetOpcode(AGetPrevOpcode())) {
            if (endTok != NULL)
                AEmitLineNumber(endTok);
            AEmitOpcode(OP_RET);
        }
    }

    num = 0;
    if (ACurFunction != NULL)
        num = ACurFunction->num;
    else if (ACurMember != AM_NONE)
        num = ALookupMemberTable(AType, type + isPrivate, ACurMember) &
            ~A_VAR_METHOD;
    
    if (ACurClass == NULL)
        funcValue = ALeaveSection(ACurFunction, minArgs, maxArgs, num);
    else
        funcValue = ALeaveSection(ACurClass, minArgs, maxArgs, num);

    if (ACurFunction != NULL) {
        if (!ASetConstGlobalValue(ACompilerThread, ACurFunction->num,
                                  funcValue))
            AGenerateOutOfMemoryError();
    } else if (ACurMember != AM_NONE) {
        AValue globalNum = ALookupMemberTable(AType, type + isPrivate,
                                              ACurMember);
        /* If the there are compile errors, globalNum might be -1. */
        if (globalNum != -1UL) {
            if (!ASetConstGlobalValue(ACompilerThread,
                                      globalNum & ~A_VAR_METHOD, funcValue))
                AGenerateOutOfMemoryError();
        }
    }
    
    ALeaveBlock();

    ANumLocals = oldNumLocals;
    ANumLocalsActive = oldNumLocalsActive;
    ABlockDepth = oldBlockDepth;

    ACurFunction = NULL;
    ACurMember = AM_NONE;
    AAccessorMember = NULL;

    ADebugCompilerMsg(("End parse fundef"));
    
    return tok;
}


/* Parse an anonymous function expression and create a partial opcode that
   constructs an anonymous function object. */
AToken *AParseAnonymousFunction(AToken *tok)
{
    AToken *startTok;
    AParseState oldState;
    AValue funcValue;
    AToken *endTok;
    int minArgs, maxArgs;
    int i;
    int num;

    if (AFunDepth == A_MAX_ANON_SUB_DEPTH) {
        AGenerateError(tok->lineNumber, ErrInternalOverflow,
                       IOF_TOO_MANY_NESTED_SUBS);
        return AGenerateParseError(tok);
    }

    /* Remember start token so that we go back in case we reach the end of
       file. The caller expects that we don't consume the final newline token
       before the end of file. */
    startTok = tok;

    /* Preserve state variables related to the enclosing function. */
    SaveParseState(&oldState);
    AFunDepth++;

    /* Calculate mapping between exposed variables in an outer function and
       local variables in the current function. */
    CalculateExposedVariableMappingInAnonFunc(tok);
    
    AInitFunctionParseState();
    
    /* Reserve space for self in the stack frame within a class definition. */
    if (ACurClass != NULL) {
        ANumLocals++;
        ANumLocalsActive++;
    }
    
    AEnterSection();    
    AEmitAbsoluteLineNumber(tok);

    /* Reserve space for exposed variables referenced in this anonymous
       function but defined in an outer function. The containers of these
       variables will be copied to the frame of this anonymous function
       before the actual function arguments. */
    for (i = 0; i < ANumAccessedExposedVariables; i++) {
        AIdType type;
        int n;

        if (AAccessedExposedVariables[i].isConst)
            type = ID_LOCAL_CONST_EXPOSED;
        else
            type = ID_LOCAL_EXPOSED;
        
        n = AGetLocalVariable();
        AAddLocalVariableSymbol(AAccessedExposedVariables[i].sym, type, n);
    }
    
    /* Skip "def". */
    tok = AAdvanceTok(tok);

    /* Parse argument list. */
    minArgs = maxArgs = 0;
    if (tok->type != TT_LPAREN)
        tok = AGenerateParseError(tok);
    else 
        tok = ParseFunctionArguments(tok, &minArgs, &maxArgs, TRUE, FALSE);

    /* The exposed variables and self will be passed as additional hidden
       arguments to this function. */
    minArgs += ANumAccessedExposedVariables;
    maxArgs += ANumAccessedExposedVariables;
    if (ACurClass != NULL) {
        /* Account for self. */
        minArgs++;
        maxArgs++;
    }

    /* Skip newline. */
    tok = AAdvanceTok(tok);

    /* Parse function body. */
    tok = ParseBlock(tok);

    if (tok->type == TT_EOF) {
        /* We've gone too far: the next token is the end of the file. Move
           back to the token before the eof token. If we don't do this, the
           caller(s) may advance past eof, causing serious problems. */
        tok = startTok;
        while (AAdvanceTok(tok)->type != TT_EOF)
            tok = AAdvanceTok(tok);
    }

    endTok = NULL;
    if (tok->type != TT_END)
        tok = AGenerateParseError(tok);
    else {
        endTok = tok;
        /* Skip "end". */
        tok = AAdvanceTok(tok);
    }
    
    FreeFinallyExits();

    /* Emit the implicit return opcode unless an explicit return statement
       can be detected. */
    if (!AIsRetOpcode(AGetPrevOpcode())) {
        if (endTok != NULL)
            AEmitLineNumber(endTok);
        AEmitOpcode(OP_RET);
    }
    
    num = AAddConstGlobalValue(ANil);
    funcValue = ALeaveSection(AAnonFunctionDummySymbol, minArgs, maxArgs, num);
    
    if (!ASetConstGlobalValue(ACompilerThread, num, funcValue))
        AGenerateOutOfMemoryError();
    
    ALeaveBlock();
    
    /* Emit opcode for creating the anonymous function. */
    AEmitOpcodeArg(OP_CREATE_ANON, num);
    /* Emit exposed variables accessed by the function. */
    if (ACurClass == NULL) {
        AEmitArg(ANumAccessedExposedVariables);
    } else {
        /* Emit reference to self + exposed ordinary variables. */
        AEmitArg(ANumAccessedExposedVariables + 1);
        AEmitArg(3);
    }
    for (i = 0; i < ANumAccessedExposedVariables; i++)
        AEmitArg(AAccessedExposedVariables[i].sym->info->num);

    AFunDepth--;
    RestoreParseState(&oldState);
    
    return tok;
}


/* Parse a method header that is not of the form "name(args)", i.e.
   a getter/setter method. tok points to the next token
   after the "def" keyword. */
static AToken *ParseSpecialMethodHeader(AToken *tok, ABool isPrivate,
                                        int *minArgs, int *maxArgs,
                                        AMemberTableType *type)
{
    ATokenType t1, t2;

    t1 = tok->type;
    t2 = (tok + 1)->type;
    
    if (t1 == TT_ID && (t2 == TT_NEWLINE || t2 == TT_ASSIGN || t2 == TT_AS))
        tok = ParseGetterOrSetterMethodHeader(
            tok, isPrivate, minArgs, maxArgs, type);
    else
        tok = AGenerateParseError(tok);

    tok = AParseTypeAnnotation(tok);
    
    if (tok->type != TT_NEWLINE)
        tok = AGenerateParseError(tok);
    
    return tok;
}


static AToken *ParseGetterOrSetterMethodHeader(AToken *tok, ABool isPrivate,
                                               int *minArgs, int *maxArgs,
                                               AMemberTableType *type)
{
    AAccessorMember = AGetMemberSymbol(tok->info.sym); /* FIX: ok? */
    ACurMember = AAccessorMember->num;
    
    ANumLocals = ANumLocalsActive = 4;
    *minArgs = *maxArgs = 1;
    
    /* Skip identifier. */
    tok = AAdvanceTok(tok);

    if (ACurMember == AM_CREATE)
        AGenerateError(tok->lineNumber, ErrCreateMustBeMethod);
    
    if (tok->type == TT_NEWLINE || tok->type == TT_AS) {
        /* Getter method */
        
        *type = MT_VAR_GET_PUBLIC;
        
        ACheckGetterDefinition(AType, ACurMember, isPrivate, tok - 1);
    } else if (tok->type == TT_ASSIGN) {
        /* Setter method */
        
        *minArgs = *maxArgs = 2;
        
        *type = MT_VAR_SET_PUBLIC;
        
        ACheckSetterDefinition(AType, ACurMember, isPrivate, tok - 1);
        
        /* Skip '='. */
        tok = AAdvanceTok(tok);
        
        if (AIsIdTokenType(tok->type)) {
            AIdType type = PrepareLocalVariable(tok);
            AddLocalVariable(tok, type);
            tok = AAdvanceTok(tok);
        } else
            tok = AGenerateParseError(tok);
    }

    tok = AParseTypeAnnotation(tok);

    return tok;
}


/* Parse function argument list. Store the range of accepted arguments in
   *minArgs and *maxArgs (A_VAR_ARG_FLAG might be "or"ed to maxArgs). The
   return value will point to the TT_NEWLINE token at the end of the argument
   list. Also compile code for any argument default value expressions. */
static AToken *ParseFunctionArguments(AToken *tok, int *minArgs, int *maxArgs,
                                      ABool allowAnnotation, ABool isInterface)
{
    /* Skip any <...> after function name. */
    tok = AParseGenericAnnotation(tok);

    if (tok->type != TT_LPAREN)
        return AGenerateParseError(tok);
    
    /* Parse function arguments. */
    do {
        /* Skip '(' or ','. */
        tok = AAdvanceTok(tok);
        
        if (AIsIdTokenType(tok->type)) {
            AToken *arg = tok;
            AIdType type;
            
            tok = AAdvanceTok(tok);
            
            (*maxArgs)++;

            /* Is there a default argument value? */
            if (tok->type == TT_ASSIGN) {
                tok = AAdvanceTok(tok);
                if (!isInterface) {
                    int branchIndex;
                    ABool isErr;
                
                    branchIndex = AGetCodeIndex();
                    AEmitOpcode2Args(OP_IS_DEFAULT, ANumLocalsActive, 0);

                    AEmitLineNumber(tok);
                
                    tok = AParseSingleAssignExpression(tok, &isErr);
                    AEmitArg(ANumLocalsActive);
                
                    ASetBranchDest(branchIndex, AGetCodeIndex());
                    
                    if (isErr)
                        goto ArgsDone;
                }
            } else {
                (*minArgs)++;
                
                if (*minArgs != *maxArgs) {
                    /* Argument without default value after optional
                       argument. */
                    AGenerateError(tok->lineNumber,
                                   ErrRequiredArgumentAfterOptionalArgument);
                }
            }

            type = PrepareLocalVariable(arg);
            AddLocalVariable(arg, type);
        } else if (tok->type == TT_ASTERISK) {
            tok = AAdvanceTok(tok);
            
            *maxArgs = (*maxArgs + 1) | A_VAR_ARG_FLAG;
            
            if (AIsIdTokenType(tok->type)) {
                AIdType type = PrepareLocalVariable(tok);
                AddLocalVariable(tok, type);
                tok = AAdvanceTok(tok);
                tok = AParseTypeAnnotationUntilSeparator(tok);
                break;
            } else {
                tok = AGenerateParseError(tok);
                goto ArgsDone;
            }
        } else
            break;

        tok = AParseTypeAnnotationUntilSeparator(tok);
    } while (tok->type == TT_COMMA);
    
    if (tok->type != TT_RPAREN)
        tok = AGenerateParseError(tok);
    else {
        tok = AAdvanceTok(tok);
        if (allowAnnotation)
            tok = AParseTypeAnnotationUntilSeparator(tok);
        /* Parse the rest of the signatures if the function has more than one
           signature. */
        if (tok->type == TT_OR)
            tok = ParseAdditionalFunctionSignatures(tok, *minArgs, *maxArgs);
        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);
    }

  ArgsDone:

    return tok;
}


/* Parse function signatures after the first one. The tok argument should point
   to the "or" token after the first signature. Return the token after the
   signature (but do not consume the newline after the signature). */
static AToken *ParseAdditionalFunctionSignatures(AToken *tok, int minArgs,
                                                 int maxArgs)
{
    if (tok->type != TT_OR)
        return AGenerateParseError(tok);
    
    do {
        ABool hasAsterisk;
        ABool hasDefault;
        
        /* Skip "or. */
        tok = AAdvanceTok(tok);
        
        /* Skip any generic type variables (<...>). */
        tok = AParseGenericAnnotation(tok);
    
        if (tok->type != TT_LPAREN)
            return AGenerateParseError(tok);

        /* Skip '('. */
        tok = AAdvanceTok(tok);

        hasAsterisk = FALSE;
        hasDefault = FALSE;
        while (tok->type != TT_NEWLINE && tok->type != TT_LPAREN) {
            if (tok->type == TT_ASTERISK) {
                tok = AAdvanceTok(tok);
                hasAsterisk = TRUE;
            }
            if (tok->type != TT_ID)
                return AGenerateParseError(tok);
            tok = AAdvanceTok(tok);
            if (tok->type == TT_ASSIGN && !hasAsterisk) {
                tok = AAdvanceTok(tok);
                hasDefault = TRUE;
            } else if (hasDefault && !hasAsterisk)
                return AGenerateParseError(tok);
            if (tok->type != TT_AS)
                return AGenerateParseError(tok);
            tok = AParseTypeAnnotationUntilSeparator(tok);
            if (hasAsterisk || tok->type != TT_COMMA)
                break;
            tok = AAdvanceTok(tok);
        }
        
        if (tok->type != TT_RPAREN)
            return AGenerateParseError(tok);

        /* Skip ')'. */
        tok = AAdvanceTok(tok);

        tok = AParseTypeAnnotationUntilSeparator(tok);
    } while (tok->type == TT_OR);
    
    return tok;
}


static AToken *ParseTypeDefinition(AToken *tok)
{
    ABool isInterface;
    int oldNumLocals;
    int oldNumLocalsActive;
    int oldBlockDepth;
    ABool isPrivate;
    ASymbolInfo *class_;
    ATypeInfo *type;
    int lineNumber;

    oldNumLocals = ANumLocals;
    oldNumLocalsActive = ANumLocalsActive;
    oldBlockDepth = ABlockDepth;
    
    AEnterSection();

    AEmitAbsoluteLineNumber(tok);
    lineNumber = tok->lineNumber;

    isInterface = tok->type == TT_INTERFACE;

    /* Skip "class". */
    tok = AAdvanceTok(tok);

    class_ = NULL;
    /* FIX: ID_GLOBAL_CLASS is invalid for interfaces below! */
    if (tok->type == TT_ID)
        ACurClass = class_ = CheckGlobal(tok, ID_GLOBAL_CLASS);
    else
        tok = AGenerateParseError(tok);
    
    if (class_ == NULL || !AIsGlobalId(class_->type)) { /* FIX invalid check */
        /* We can't really continue compilation without a symbol for a class.
           The error has been already flagged, simly skip until end of file. */
        while (tok->type != TT_EOF)
            tok = AAdvanceTok(tok);
        return tok;
    }

    ADebugCompilerMsg(("Parse class \"%q\"", ACurClass));
    
    AType = type = AValueToType(AGlobalByNum(class_->num));

    /* FIX What if !type->isValid? Can we reach here? */

    /* Skip class name. */
    tok = AAdvanceTok(tok);

    /* Skip any <...> after class name. */
    tok = AParseGenericAnnotation(tok);
    
    /* Parse superclass, if present. */
    if (tok->type == TT_IS) {
        tok = AAdvanceTok(tok);

        if (tok->type == TT_ID || tok->type == TT_SCOPEOP) {
            ASymbolInfo *super;

            tok = AParseAnyGlobalVariable(tok, &super, FALSE);
            tok = AParseGenericAnnotation(tok);

            if (super->type != ID_ERR_PARSE
                  && super->type != ID_ERR_UNDEFINED) {
                if (!isInterface && AIsValidSuperClass(super)) {
                    SetSuperType(type, AValueToType(AGlobalByNum(super->num)),
                                 lineNumber);
                    /* Check if any of the superclasses define initializer
                       functions and call them if found. */
                    CheckSuperInitializer(AType);
                } else if (isInterface
                           && super->type == ID_GLOBAL_INTERFACE)
                    SetSuperType(type, AValueToType(AGlobalByNum(super->num)),
                                 lineNumber);
                else if (isInterface)
                    AGenerateError(tok->lineNumber, ErrInvalidInterface,
                                   super);
                else
                    AGenerateError(tok->lineNumber, ErrInvalidSuperClass);
            }
        } else
            tok = AGenerateParseError(tok);
    }

    /* Parse implemented interfaces, if present. */
    if (tok->type == TT_IMPLEMENTS && !isInterface) {
        tok = AAdvanceTok(tok);
        do {
            ASymbolInfo *super;

            tok = AParseAnyGlobalVariable(tok, &super, FALSE);
            tok = AParseGenericAnnotation(tok);

            if (super->type != ID_ERR_PARSE) {
                if (super->type == ID_GLOBAL_INTERFACE) {
                    AAddInterface(type, AValueToType(AGlobalByNum(super->num)),
                                  FALSE);
                } else if (super->type != ID_ERR_UNDEFINED)
                    AGenerateError(tok->lineNumber, ErrInvalidInterface,
                                   super);
            }

            if (tok->type != TT_COMMA)
                break;
            tok = AAdvanceTok(tok);                
        } while (tok->type == TT_ID || tok->type == TT_SCOPEOP);
    }

    ANumLocals = 3 + 1 + (AGetMaxArgs(class_) & ~A_VAR_ARG_FLAG);
    ANumLocalsActive = ANumLocals;

    AUpdateInheritedMiscTypeData(type);

    if (tok->type != TT_NEWLINE)
        tok = AGenerateParseError(tok);

    if (class_->info.global.minArgs == A_UNKNOWN_MIN_ARGS) {
        /* If the class has a constructor, the previous call to GetMaxArgs
           would have updated the value of minArgs. Therefore, the class
           has no constructor and therefore does not take any arguments. */
        class_->info.global.minArgs = 0;
        class_->info.global.maxArgs = 0;
    }

    /* Calculate the number of inherited member variables. */
    AUpdateTypeTotalNumVars(type);

    /* Skip newline. */
    tok = AAdvanceTok(tok);

    /* Verify that all members defined in implemented interfaces have
       implementations. */
    if (!isInterface)
        AVerifyInterfaces(type, lineNumber);
    
    isPrivate = FALSE;

    /* Parse bind declarations. */
    if (isInterface)
        tok = ParseBindDeclarations(tok, type);

    while (tok->type != TT_END && tok->type != TT_EOF) {
        switch (tok->type) {
        case TT_PRIVATE:
            if (isInterface)
                AGenerateError(tok->lineNumber,
                               ErrInterfaceCannotHavePrivateMembers);

            isPrivate = TRUE;
            tok = AAdvanceTok(tok);
            if (tok->type != TT_VAR && tok->type != TT_CONST
                && !AIsDefToken(tok->type)) {
                tok = AGenerateParseErrorSkipNewline(tok);
                break;
            }
            continue;
            
        case TT_VAR:
        case TT_CONST:
            if (isInterface)
                AGenerateError(tok->lineNumber,
                               ErrInterfaceCannotHaveMemberVariables);
            
            tok = ParseVariableDefinition(tok, isPrivate);
            break;
            
        case TT_SUB:
        case TT_DEF:
            tok = ParseFunctionDefinition(tok, isPrivate, isInterface);
            break;

        default:
            tok = AGenerateParseErrorSkipNewline(tok);
            break;
        }
        
        isPrivate = FALSE;
    }

    /* tok->type is either TT_END or TT_EOF. */

    if (tok->type == TT_END) {
        tok = AAdvanceTok(tok);
        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);
        tok = AAdvanceTok(tok);
    } else
        tok = AGenerateParseError(tok);  

    ADebugCompilerMsg(("..."));
    
    BuildConstructor();
    
    /* Calculate the block size of an instance. */
    ACalculateInstanceSize(type);
    
    ANumLocals = oldNumLocals;
    ANumLocalsActive = oldNumLocalsActive;
    ABlockDepth = oldBlockDepth;

    ADebugCompilerMsg(("End parse class \"%q\"", ACurClass));
    
    ACurClass = NULL;
    
    return tok;
}


/* Parse bind declarations. Assume tok points to the next token after
   interface header. */
static AToken *ParseBindDeclarations(AToken *tok, ATypeInfo *type)
{
    while (tok->type == TT_BIND) {
        ASymbolInfo *target;
        int line = tok->lineNumber;

        if (ASuperType(type) != NULL)
            AGenerateError(tok->lineNumber, ErrBindWithSuperinterface);
        
        tok = AAdvanceTok(tok);

        tok = AParseAnyGlobalVariable(tok, &target, FALSE);

        /* Map symbols such as std::Str (which refer to functions in the
           implementation level, not types) to the corresponding internal type
           symbols such as std::__Str. */
        target = AInternalTypeSymbol(target);
        
        if (target->type != ID_ERR_PARSE) {
            if (target->type == ID_GLOBAL_CLASS) {
                ATypeInfo *targetType = AValueToType(
                    AGlobalByNum(target->num));
                if (!AAddInterface(targetType, type, TRUE))
                    AGenerateOutOfMemoryError();
                AVerifyInterface(targetType, type, line, FALSE);
            } else if (target->type != ID_ERR_UNDEFINED) {
                AGenerateError(tok->lineNumber, ErrInvalidBindTarget,
                               target);
            }
        }
        
        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);
        tok = AAdvanceTok(tok);
    }

    return tok;
}


static void CallMemberInitializers(ATypeInfo *t);


static void BuildConstructor(void)
{
    unsigned create;
    ATypeInfo *t;
    AValue constructor;
    AValue memberInitializer;
    int i;
    int numArgs;

    create = -1;
    t = AType;
    do {
        create = ALookupMemberTable(t, MT_METHOD_PUBLIC, AM_CREATE);
        t = ASuperType(t);
    } while (create == -1 && t != NULL);

    /* Create member initializer if it's non-empty. */
    if (!AType->hasMemberInitializer) {
        AQuitSection();
        memberInitializer = AZero;
    } else {
        AEmitOpcode(OP_RET);
        memberInitializer = ALeaveSection(ACurClass, 1, 1,
                                         AType->memberInitializer);
        if (!ASetConstGlobalValue(ACompilerThread, AType->memberInitializer,
                                 memberInitializer))
            AGenerateOutOfMemoryError();
    }
    
    if (0 /* no create function at all and no superclass */) {
        /* Create default create function that initializes all the member
           variables that don't have default values. */
    }

    /* Create the constuctor. */
    AEnterSection();
    numArgs = AGetMaxArgs(ACurClass) + 1;
    ANumLocals = ANumLocalsActive = 3 + (numArgs & ~A_VAR_ARG_FLAG);
    CallMemberInitializers(AType);
    
    /* Call the create method. */
    if (create != -1) {
        AEmitOpcode2Args(OP_CALL_G, create, numArgs);
        for (i = 0; i < (numArgs & ~A_VAR_ARG_FLAG); i++)
            AEmitArg(i + 3);
        AEmitArg(A_NO_RET_VAL);
    }
    
    AEmitOpcodeArg(OP_RET_L, 3);
    constructor = ALeaveSection(ACurClass, AGetMinArgs(ACurClass) + 1,
                                AGetMaxArgs(ACurClass) + 1, AType->create);

    if (ACurClass != NULL) {
        if (!ASetConstGlobalValue(ACompilerThread, AType->create, constructor))
            AGenerateOutOfMemoryError();
    }
    //else
    //    AQuitSection();
}


static void CallMemberInitializers(ATypeInfo *t)
{
    if (ASuperType(t) != NULL)
        CallMemberInitializers(t->super);
    
    /* If this class has a member initializer, call it. */
    if (t->hasMemberInitializer) {
        AEmitOpcode3Args(OP_CALL_G, t->memberInitializer, 1, 3);
        AEmitArg(A_NO_RET_VAL);
    }
}


static AToken *ParseBlock(AToken *tok)
{
    if (ABlockDepth == A_MAX_BLOCK_DEPTH) {
        AGenerateError(tok->lineNumber, ErrInternalOverflow,
                       IOF_BLOCK_DEPTH_TOO_LARGE);
        return tok;
    }
    
    ABlockDepth++;
    
    for (;;) {
        switch (tok->type) {
        case TT_VAR:
            tok = ParseVariableDefinition(tok, FALSE);
            break;
            
        case TT_IF:
            tok = ParseIfStatement(tok);
            break;
            
        case TT_WHILE:
            tok = ParseWhileStatement(tok);
            break;
            
        case TT_REPEAT:
            tok = ParseRepeatStatement(tok);
            break;
            
        case TT_BREAK:
            tok = ParseBreakStatement(tok);
            break;
            
        case TT_RETURN:
            tok = ParseReturnStatement(tok);
            break;
            
        case TT_FOR:
            tok = ParseForStatement(tok);
            break;
            
        case TT_SWITCH:
            tok = ParseSwitchStatement(tok);
            break;
            
        case TT_RAISE:
            tok = ParseRaiseStatement(tok);
            break;
            
        case TT_TRY:
            tok = ParseTryStatement(tok);
            break;
            
        case TT_ID:
        case TT_LITERAL_INT:
        case TT_LITERAL_FLOAT:
        case TT_LITERAL_STRING:
        case TT_NIL:
        case TT_NOT:
        case TT_MINUS:
        case TT_LPAREN:
        case TT_LBRACKET:
        case TT_SCOPEOP:
        case TT_SELF:
        case TT_SUPER:
        case TT_ERR_STRING_UNTERMINATED:
        case TT_ERR_INVALID_NUMERIC:
        case TT_ERR_UNRECOGNIZED_CHAR:
        case TT_ERR_NON_ASCII_STRING_CHAR:
        case TT_ERR_NON_ASCII_COMMENT_CHAR:
        case TT_ERR_INVALID_UTF8_SEQUENCE:
        case TT_SUB:
        case TT_DEF:
            tok = AParseAssignmentOrExpression(tok);
            break;
            
        case TT_EOF:
            tok = AGenerateParseError(tok);

            /* Fall through */
            
        case TT_END:
        case TT_ELSE:
        case TT_CASE:
        case TT_ELIF:
        case TT_UNTIL:
        case TT_EXCEPT:
        case TT_FINALLY:
            ALeaveBlock();
            return tok;
            
        default:
            tok = AGenerateParseErrorSkipNewline(tok);
            break;
        }       
    }
}


static AToken *ParseBlockWithTerminator(AToken *tok, ATokenType terminator,
                                        AToken **terminatorTok)
{
    if (terminatorTok != NULL)
        *terminatorTok = NULL;
    
    for (;;) {
        tok = ParseBlock(tok);

        if (tok->type == terminator) {
            if (terminatorTok != NULL)
                *terminatorTok = tok;
            tok = AAdvanceTok(tok);
            if (terminator == TT_END) {
                if (tok->type != TT_NEWLINE)
                    tok = AGenerateParseError(tok);
                tok = AAdvanceTok(tok);
            }
            return tok;
        }

        if (tok->type == TT_EOF)
            return tok;
        
        tok = AGenerateParseErrorSkipNewline(tok);
    }
}


static AToken *ParseIfStatement(AToken *tok)
{
    ABranchList *ifBranch;
    ABranchList *outBranches = NULL;

    tok = AAdvanceTok(tok);
    
    for (;;) {
        AEmitLineNumber(tok);
        tok = AParseLogicalExpression(tok, FALSE, &ifBranch);

        for (;;) {
            tok = ParseBlock(tok);

            if (tok->type == TT_END || tok->type == TT_ELSE
                || tok->type == TT_ELIF || tok->type == TT_EOF)
                break;

            tok = AGenerateParseErrorSkipNewline(tok);
        }

        /* tok->type is TT_EOF, TT_END, TT_ELSE or TT_ELIF. */

        if (tok->type != TT_ELIF)
            break;

        tok = AAdvanceTok(tok);
        
        outBranches = AAddBranch(outBranches);
        ASetBranches(ifBranch, AGetCodeIndex());
    }

    /* tok->type is TT_EOF, TT_END or TT_ELSE. */

    if (tok->type == TT_ELSE) { 
        tok = AAdvanceTok(tok);

        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);

        tok = AAdvanceTok(tok);

        outBranches = AAddBranch(outBranches);
        ASetBranches(ifBranch, AGetCodeIndex());

        tok = ParseBlockWithTerminator(tok, TT_END, NULL);
    } else {
        /* tok->type is TT_EOF or TT_END. */
        
        if (tok->type == TT_END) {
            tok = AAdvanceTok(tok);
            if (tok->type != TT_NEWLINE)
                tok = AGenerateParseError(tok);
            tok = AAdvanceTok(tok);
        }
        
        ASetBranches(ifBranch, AGetCodeIndex());
    }

    ASetBranches(outBranches, AGetCodeIndex());
    AClearPrevOpcode();

    return tok;
}


static AToken *ParseWhileStatement(AToken *tok)
{
    ABranchList *whileBranch;

    unsigned loopTop = AGetCodeIndex();
    unsigned condSize;
    AToken *topTok;

    int oldLoopDepth = LoopDepth;
    LoopDepth = ABlockDepth;

    topTok = tok;
    tok = AParseLogicalExpression(AAdvanceTok(tok), TRUE, &whileBranch);

    condSize = AGetCodeIndex() - loopTop;
    ASaveCode(condSize);

    AEmitOpcodeArg(OP_JMP, 0);

    tok = ParseBlockWithTerminator(tok, TT_END, NULL);
    ASetBranchDest(loopTop, AGetCodeIndex());

    AAdjustBranchPos(whileBranch, AGetCodeIndex() - loopTop);

    AEmitAbsoluteLineNumber(topTok);
    AEmitLineNumber(topTok);
    ARestoreCode(condSize);
    AEmitAbsoluteLineNumber(tok);

    ASetBranches(whileBranch, loopTop + 2);

    LeaveLoop();

    LoopDepth = oldLoopDepth;

    return tok;  
}


static AToken *ParseRepeatStatement(AToken *tok)
{
    ABranchList *untilBranch;
    int loopTop = AGetCodeIndex();

    int oldLoopDepth = LoopDepth;
    LoopDepth = ABlockDepth;

    /* Skip "repeat". */
    tok = AAdvanceTok(tok);
    
    if (tok->type != TT_NEWLINE)
        tok = AGenerateParseError(tok);

    tok = ParseBlockWithTerminator(AAdvanceTok(tok), TT_UNTIL, NULL);

    if (tok->type != TT_EOF) {
        AEmitLineNumber(tok);
        tok = AParseLogicalExpression(tok, FALSE, &untilBranch);

        ASetBranches(untilBranch, loopTop);
    }

    LeaveLoop();
    
    LoopDepth = oldLoopDepth;

    return tok;
}


static AToken *ParseForStatement(AToken *tok)
{
    int loopTop;
    int initOpcodeIndex = 0;
    int oldNumLocalsActive = ANumLocalsActive;
    AOpcode forOpcode = OP_FOR_LOOP;
    int numVars = 1;
    int oldLoopDepth = LoopDepth;
    AToken *vars;
    ABool isExposed = FALSE; /* Is one of the for loop variables exposed? */
    int createExposedOpcodeIndex = -1;
    int firstExposedVar = 0;
    
    LoopDepth = ABlockDepth;    
    ABlockDepth++;

    AEmitLineNumber(tok);
    
    /* Skip "for". */
    tok = AAdvanceTok(tok);
    
    if (tok->type == TT_ID || tok->type == TT_ID_EXPOSED) {
        int i;

        isExposed = tok->type == TT_ID_EXPOSED;

        /* Store a pointer to beginning of the for loop variable list. */
        vars = tok;
        
        AGetLocalVariable();
        tok = AAdvanceTok(tok);
        tok = AParseTypeAnnotationUntilSeparator(tok);
        
        /* Handle any additional variables separated by commas. */
        while (tok->type == TT_COMMA) {
            tok = AAdvanceTok(tok);
            if (!AIsIdTokenType(tok->type)) {
                tok = AGenerateParseError(tok);
                break;
            }
            
            if (tok->type == TT_ID_EXPOSED)
                isExposed = TRUE;
            
            AGetLocalVariable();
            numVars++;
            
            tok = AAdvanceTok(tok);
            tok = AParseTypeAnnotationUntilSeparator(tok);
        }
        
        if (tok->type == TT_IN) {
            int num;
            
            tok = AAdvanceTok(tok);

            tok = AParseExpression(tok, &num, TRUE);
            
            if (tok->type != TT_NEWLINE)
                tok = AGenerateExpressionParseError(tok);

            tok = AAdvanceTok(tok);

            if (!isExposed && AGetPrevOpcode() == OP_RNG_L) {
                initOpcodeIndex = AGetPrevOpcodeIndex();

                ASetOpcode(initOpcodeIndex, OP_FOR_L);
                ASetOpcode(initOpcodeIndex + 1, oldNumLocalsActive +
                           numVars - 1);
                AEmitArg(0);

                forOpcode = OP_FOR_LOOP_RANGE;

                AGetLocalVariable();
            } else {
                /* Emit partial opcodes for creating exposed variable
                   containers for for loop variables if needed. They will
                   patched later. */
                if (isExposed) {
                    createExposedOpcodeIndex = AGetCodeIndex();
                    for (i = 0; i < numVars; i++)
                        AEmitOpcodeArg(OP_CREATE_EXPOSED, 0);
                }
                
                initOpcodeIndex = AGetCodeIndex();
                
                AEmitOpcode3Args(OP_FOR_INIT, oldNumLocalsActive + numVars - 1,
                                 num, 0);

                AGetLocalVariable();
                AGetLocalVariable();
            }
        } else
            tok = AGenerateParseErrorSkipNewline(tok);

        /* Create the for loop variables. */
        if (!isExposed) {
            /* Create ordinary loop variables. */
            for (i = 0; i < numVars; i++) {
                AddLocalVariableAt(vars, ID_LOCAL_CONST,
                                   oldNumLocalsActive + i);
                vars = AAdvanceTok(vars);
                vars = ASkipTypeAnnotationUntilSeparator(vars);
                vars = AAdvanceTok(vars);
            }
        } else if (createExposedOpcodeIndex >= 0) {
            /* Store the index of the first exposed variable. */
            firstExposedVar = ANumLocalsActive;

            /* Create exposed loop variables there is a single exposed
               loop variable + hidden ordinary loop variable for each
               actual loop variable. */
            for (i = 0; i < numVars; i++) {
                int exposed = AGetLocalVariable();
                AddLocalVariableAt(vars, ID_LOCAL_CONST_EXPOSED, exposed);
                vars = AAdvanceTok(vars);
                vars = ASkipTypeAnnotationUntilSeparator(vars);
                vars = AAdvanceTok(vars);
                /* Patch the opcode that creates the exposed variable
                   container. */
                ASetOpcode(createExposedOpcodeIndex + 1 + i * 2, exposed);
            }
        }
    } else
        tok = AGenerateParseErrorSkipNewline(tok);

    loopTop = AGetCodeIndex();

    /* If there are more than a single for loop variable, expand the current
       value in iteration to these variables. */
    if (numVars > 1) {
        if (numVars > 128)
            AGenerateError(-1, ErrInternalOverflow,
                          IOF_TOO_MANY_VARIABLES_IN_FOR);
        else {
            int dst[128];
            int i;

            for (i = 0; i < numVars; i++)
                dst[i] = oldNumLocalsActive + i;

            AEmitArrayExpand(oldNumLocalsActive + numVars - 1, numVars, dst);
        }
    }

    /* Copy internal loop variables to the exposed variable containers if
       needed. */
    if (isExposed) {
        int i;
        for (i = 0; i < numVars; i++)
            AEmitOpcode2Args(OP_ASSIGN_LE, firstExposedVar + i,
                             oldNumLocalsActive + i);
    }
    
    tok = ParseBlockWithTerminator(tok, TT_END, NULL);

    if (initOpcodeIndex != 0)
        ASetBranchDest(initOpcodeIndex, AGetCodeIndex());
    
    AEmitOpcode2Args(forOpcode, oldNumLocalsActive + numVars - 1, 0);
    ASetBranchDest(AGetPrevOpcodeIndex(), loopTop);

    LeaveLoop();
    ALeaveBlock();

    ANumLocalsActive = oldNumLocalsActive;
    
    LoopDepth = oldLoopDepth;

    return tok;
}


static AToken *ParseBreakStatement(AToken *tok)
{
    ABranchList *exits;
    ABreakList *list;
    int index;

    AEmitLineNumber(tok);
    
    /* Skip "break". */
    tok = AAdvanceTok(tok);
    
    if (LoopDepth == 0)
        return AGenerateErrorSkipNewline(tok, ErrBreakOutsideLoop);

    index = AGetCodeIndex();
    if (TryLocalVar != -1) {
        /* The below instructions are for properly exiting try statements.
           The opcode arguments will be updated later. The TryLocalVar
           argument to ASSIGN_PL is a dummy default value. */
        AEmitOpcode2Args(OP_ASSIGN_PL, TryLocalVar, 0);
        AEmitOpcodeArg(OP_TRY_END, 0);
        AEmitOpcode2Args(OP_ASSIGN_IL, AZero, TryLocalVar + 2);
    }

    exits = AAddBranch(NULL);
    if (tok->type != TT_NEWLINE)
        tok = AGenerateParseError(tok);
    tok = AAdvanceTok(tok);
    
    list = ACAlloc(sizeof(ABreakList));
    if (list != NULL) {
        list->next  = LoopExits;
        list->exits = exits;
        list->depth = LoopDepth;
        list->isSet = FALSE;
        list->opcodeIndex = index;
        LoopExits = list;
    } else
        AGenerateOutOfMemoryError();
        
    return tok;
}


static AToken *ParseReturnStatement(AToken *tok)
{
    AEmitLineNumber(tok);
    
    /* Skip "return". */
    tok = AAdvanceTok(tok);

    if (TryLocalVar == -1) {    
        if (tok->type != TT_NEWLINE) {
            int retVal;
        
            tok = AParseExpression(tok, &retVal, TRUE);
            
            AEmitOpcodeArg(OP_RET_L, retVal);
        } else
            AEmitOpcode(OP_RET);
    } else {
        AReturnList *list;
        
        if (tok->type != TT_NEWLINE) {
            tok = AParseAssignExpression(tok, TRUE);
            AEmitArg(TryLocalVar + 1);
        } else
            AEmitOpcodeArg(OP_ASSIGN_NILL, TryLocalVar + 1);
        
        AEmitOpcode2Args(OP_ASSIGN_IL, AZero, TryLocalVar + 2);
        AEmitOpcodeArg(OP_TRY_END, 0);
        AEmitOpcodeArg(OP_ASSIGN_NILL, TryLocalVar);

        /* Add the return statement to a list of return statements. */
        list = ACAlloc(sizeof(AReturnList));
        if (list != NULL) {
            list->next = ReturnBranches;
            list->index = AGetCodeIndex();
            list->isSet = FALSE;
            ReturnBranches = list;
        } else
            AGenerateOutOfMemoryError();

        /* Emit the branch. The target index is currently unknown. */
        AEmitJump(0);
    }
    
    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    return AAdvanceTok(tok);
}


static AToken *ParseTryStatement(AToken *tok)
{
    int beginIndex;
    int oldTryLocalVar;
    ABranchList *outBranches;
    ABool isDirect;

    isDirect = IsDirectTryStatement(tok);

    /* Skip "try". */
    tok = AAdvanceTok(tok);

    if (tok->type != TT_NEWLINE)
        tok = AGenerateParseError(tok);
    tok = AAdvanceTok(tok);

    oldTryLocalVar = TryLocalVar;
    
    /* Allocate three local variables for the try statement. */
    TryLocalVar = AGetLocalVariable();
    AGetLocalVariable();
    AGetLocalVariable();
    
    beginIndex = AGetCodeIndex();

    AEmitTryBlockBegin(isDirect);

    if (isDirect)
        AEmitOpcode(OP_TRY);
    
    for (;;) {
        tok = ParseBlock(tok);

        if (tok->type == TT_EXCEPT || tok->type == TT_FINALLY
            || tok->type == TT_EOF)
            break;

        tok = AGenerateParseErrorSkipNewline(tok);
    }

    if (isDirect) {
        ABreakList *exit;
        AReturnList *ret;
        ABranchList *fin;
        
        AEmitOpcodeArg(OP_TRY_END, 1);
        /* Update the context stack pop count for all loop exit statements
           and return statements within the try statement. */
        for (exit = LoopExits; exit != NULL; exit = exit->next) {
            int index = exit->opcodeIndex;
            if (index >= beginIndex) {
                if (!exit->isSet)
                    ASetOpcode(index + 4, AGetOpcode(index + 4) + 1);
                ASetOpcode(index + 6, AGetOpcode(index + 6) + AIntToValue(1));
            }
        }
        for (ret = ReturnBranches; ret != NULL; ret = ret->next) {
            if (ret->index >= beginIndex) {
                if (!ret->isSet)
                    ASetOpcode(ret->index - 3,
                               AGetOpcode(ret->index - 3) + 1);
                ASetOpcode(ret->index - 6, AGetOpcode(ret->index - 6) +
                           AIntToValue(1));
            }
        }
        /* Update the context stack pop count for the finally statements
           within the try statement. */
        for (fin = FinallyExits; fin != NULL && fin->data > beginIndex;
             fin = fin->next)
            ASetOpcode(fin->data + 2, AGetOpcode(fin->data + 2) + 1);
    }        

    /* FIX: better error message if no except/finally? */

    outBranches = NULL;
    if (tok->type == TT_EXCEPT) {
        while (tok->type == TT_EXCEPT) {
            int isExposedException = FALSE;
            
            /* Skip "except". */
            tok = AAdvanceTok(tok);

            outBranches = AAddBranch(outBranches);

            ABlockDepth++;

            /* Is the exception object bound to a local variable (either
               ordinary or an exposed variable)? */
            if ((tok->type == TT_ID || tok->type == TT_ID_EXPOSED)
                  && (tok + 1)->type == TT_IS) {
                isExposedException = tok->type == TT_ID_EXPOSED;
                AddLocalVariableAt(tok, isExposedException
                                   ? ID_LOCAL_CONST_EXPOSED : ID_LOCAL_CONST,
                                   TryLocalVar);
                /* Skip id and "is". */
                tok = AAdvanceTok(AAdvanceTok(tok));
            }
            
            if (tok->type == TT_ID_EXPOSED && (tok + 1)->type == TT_IS) {
                AddLocalVariableAt(tok, ID_LOCAL_CONST, TryLocalVar);
                tok = AAdvanceTok(AAdvanceTok(tok));
            }

            if (tok->type == TT_ID || tok->type == TT_SCOPEOP) {
                ASymbolInfo *class_;
                
                tok = AParseAnyGlobalVariable(tok, &class_, FALSE);
                
                if (class_->type != ID_ERR_PARSE) {
                    if (class_->type == ID_GLOBAL_CLASS
                        /* && descendant of certain class... FIX */)
                        AEmitExcept(TryLocalVar, class_->num);
                    else
                        AGenerateError(tok->lineNumber,
                                       ErrInvalidExceptionType);
                }
                
                if (tok->type != TT_NEWLINE)
                    tok = AGenerateParseError(tok);
                else
                    tok = AAdvanceTok(tok);
            } else
                tok = AGenerateParseErrorSkipNewline(tok);

            /* Prepare exposed exception variable if needed. */
            if (isExposedException)
                AEmitOpcodeArg(OP_CREATE_EXPOSED, TryLocalVar);
            
            for (;;) {
                tok = ParseBlock(tok);

                if (tok->type == TT_EXCEPT || tok->type == TT_END ||
                    tok->type == TT_EOF)
                    break;

                tok = AGenerateParseErrorSkipNewline(tok);
            }

            ALeaveBlock();
        }

        ASetBranches(outBranches, AGetCodeIndex());

        /* tok->type is either TT_END or TT_EOF. */
        if (tok->type == TT_END) {
            tok = AAdvanceTok(tok);
            if (tok->type != TT_NEWLINE)
                tok = AGenerateParseError(tok);
            tok = AAdvanceTok(tok);
        }
    } else if (tok->type == TT_FINALLY) {
        ABreakList *exit;
        AReturnList *ret;

        /* Skip "finally". */
        tok = AAdvanceTok(tok);

        /* Expect newline. */
        if (tok->type != TT_NEWLINE)
            tok = AGenerateParseError(tok);
        tok = AAdvanceTok(tok);

        AEmitOpcode2Args(OP_ASSIGN_IL, 0, TryLocalVar);
        
        AEmitFinally(TryLocalVar);

        /* Make break statements contained within the try statement point at
           the finally code block. */
        for (exit = LoopExits; exit->opcodeIndex >= beginIndex;
             exit = exit->next) {
            if (!exit->isSet) {
                ASetBranches(exit->exits, AGetCodeIndex());
                /* Update reference to TryLocalVar in the exit statement. */
                ASetOpcode(exit->opcodeIndex + 1, TryLocalVar);
                ASetOpcode(exit->opcodeIndex + 7, TryLocalVar + 2);
                exit->isSet = TRUE;
                exit->exits = NULL;
            }
            /* Clear the after-last-finally pop count. */
            ASetOpcode(exit->opcodeIndex + 6, AZero);
        }
        
        /* Make return statements within the try statement point at the finally
           code block. */
        for (ret = ReturnBranches; ret != NULL && ret->index > beginIndex;
             ret = ret->next) {
            if (!ret->isSet) {
                ASetBranchDest(ret->index, AGetCodeIndex());
                /* The return statement stores values in the TryLocalVar-based
                   values in the stack. Store the correct indices that refer to
                   these variables. */
                ASetOpcode(ret->index - 8, TryLocalVar + 1);
                ASetOpcode(ret->index - 5, TryLocalVar + 2);
                ASetOpcode(ret->index - 1, TryLocalVar);
                ret->isSet = TRUE;
            }
            /* Clear the after-last-finally pop count. */
            ASetOpcode(ret->index - 6, AZero);
        }
        
        /* Make finally statements contained within the try statement point at
           the finally code block. */
        while (FinallyExits != NULL && FinallyExits->data > beginIndex) {
            AIntList *prev = FinallyExits;
            ASetBranchDest(prev->data, AGetCodeIndex());
            FinallyExits = prev->next;
            ACFree(prev, sizeof(AIntList));
        }
        
        tok = ParseBlockWithTerminator(tok, TT_END, NULL);

        FinallyExits = AAddIntList(FinallyExits, AGetCodeIndex());
        AEmitOpcode3Args(OP_LEAVE_FINALLY, TryLocalVar, 0,
                         1U << (A_OPCODE_BITS - 1));
    }

    AEmitTryBlockEnd();
    
    TryLocalVar = oldTryLocalVar;

    if (TryLocalVar == -1) {
        /* Some return statements may be incomplete (it was assumed that they
           will jump to a finally statement). Complete them. */
        while (ReturnBranches != NULL) {
            AReturnList *prev = ReturnBranches;

            if (!prev->isSet) {
                int index = prev->index;
                
                ASetOpcode(index - 2, OP_RET_L);
                ASetOpcode(index - 1, AGetOpcode(index - 8));
            }
            
            ReturnBranches = prev->next;
            ACFree(prev, sizeof(AReturnList));
        }
    }

    return tok;
}


static AToken *ParseRaiseStatement(AToken *tok)
{
    int val;
    
    AEmitLineNumber(tok);
    
   /* Skip "return". */
    tok = AAdvanceTok(tok);

    tok = AParseExpression(tok, &val, TRUE);

    AEmitOpcodeArg(OP_RAISE_L, val);

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    return AAdvanceTok(tok);
}

    
static AToken *ParseSwitchStatement(AToken *tok)
{
    ABranchList *outBranches = NULL;

    int num;

    AEmitLineNumber(tok);
    
    /* Skip "switch". */
    tok = AAdvanceTok(tok);
    
    tok = AParseAssignExpression(tok, TRUE);

    num = AGetLocalVariable();
    AEmitArg(num);

    if (tok->type != TT_NEWLINE)
        tok = AGenerateExpressionParseError(tok);

    tok = AAdvanceTok(tok);

    for (;;) {
        if (tok->type == TT_CASE) {
            int skipIndex;
            
            AEmitLineNumber(tok);
            tok = AParseCaseExpression(AAdvanceTok(tok), num, &skipIndex);

            for (;;) {
                tok = ParseBlock(tok);

                if (tok->type == TT_CASE || tok->type == TT_END
                    || tok->type == TT_ELSE || tok->type == TT_EOF)
                    break;

                tok = AGenerateParseErrorSkipNewline(tok);
            }

            outBranches = AAddBranch(outBranches);

            ASetBranchDest(skipIndex, AGetCodeIndex());
        } else if (tok->type == TT_ELSE) {
            tok = AAdvanceTok(tok);
            if (tok->type != TT_NEWLINE)
                tok = AGenerateParseError(tok);
            tok = ParseBlockWithTerminator(AAdvanceTok(tok), TT_END, NULL);
            break;
        } else if (tok->type == TT_END) {
            tok = AAdvanceTok(tok);
            if (tok->type != TT_NEWLINE)
                tok = AGenerateParseError(tok);
            tok = AAdvanceTok(tok);
            break;
        } else {
            if (tok->type == TT_EOF)
                break;
            tok = AGenerateParseErrorSkipNewline(tok);
        }
    }

    ASetBranches(outBranches, AGetCodeIndex());
    AClearPrevOpcode();

    ANumLocalsActive--;
    
    return tok;
}


static ASymbolInfo *AddLocalVariable(AToken *tok, AIdType type)
{
    return AddLocalVariableAt(tok, type, AGetLocalVariable());
}


static ASymbolInfo *AddLocalVariableAt(AToken *tok, AIdType type, unsigned num)
{
    ASymbolInfo *var;
    ASymbolInfo *next;

    var = AAddLocalVariableSymbol(tok->info.sym, type, num);
    if (var == NULL)
        return NULL;

    next = var->next;
    if (AIsLocalId(next->type)) {
        if (next->info.blockDepth == 0 && ABlockDepth == 1) {
            AGenerateError(tok->lineNumber, ErrLocalVariableShadowsArgument,
                          tok);
            return NULL;
        } else {
            AGenerateError(tok->lineNumber, ErrAlreadyDefined, tok);
            return NULL;
        }
    }

    return var;
}


ASymbolInfo *AAddLocalVariableSymbol(ASymbol *sym, AIdType type, unsigned num)
{
    ASymbolInfo *var;
    
    var = AAddIdentifier(sym, ID_LOCAL);
    if (var == NULL)
        return NULL;

    var->type = type;
    var->num = num;
    var->info.blockDepth = ABlockDepth;
    
    /* Update the list of local variables. */
    var->sym = Locals;
    Locals = var;

    return var;
}


/* Update compiler state related to leaving a block. Undefine local variables
   defined in the current block and decrement ANumLocalsActive for each local
   variable. Decrease ABlockDepth. */
void ALeaveBlock(void)
{
    ASymbolInfo *locals = Locals;
    int blockDepth = ABlockDepth;
    int numRemoved = 0;

    while (locals != NULL &&
           locals->info.blockDepth == blockDepth) {
        ASymbolInfo *next = locals->sym;
        ARemoveIdentifier(locals);
        locals = next;
        numRemoved++;
    }

    Locals = locals;
    ANumLocalsActive -= numRemoved;

    ABlockDepth--;
}


static ASymbolInfo InvalidVariable = {
    NULL, &InvalidVariable, 0, ID_ERR_PARSE, { 0 }
};
static ASymbolInfo UndefinedVariable = {
    NULL, &UndefinedVariable, 0, ID_ERR_UNDEFINED, { 0 }
};


AToken *AParseAnyGlobalVariable(AToken *tok, ASymbolInfo **var, ABool isSilent)
{
    if (tok->type == TT_SCOPEOP)
        return AParseQuotedGlobalVariable(tok, var, isSilent);
    else
        return AParseGlobalVariable(tok, var, FALSE, isSilent);
}


/* Parse a global variable reference. Return a pointer to the variable in *var
   or a pointer to one of the error structures (InvalidVariable or
   UndefinedVariable). If override is true, allow access to any module and do
   not generate parse errors. If isSilent if true, do not generate parse
   errors. */
AToken *AParseGlobalVariable(AToken *tok, ASymbolInfo **var,
                             ABool override, ABool isSilent)
{
    ASymbolInfo *id;

    if (tok->type != TT_ID)
        goto ParseError;
    
    id = tok->info.sym->info;
    
    if (!AIsId(id->type))
        goto UndefinedError;

    /* Is id a module prefix? */
    if ((tok + 1)->type == TT_SCOPEOP) {
        /* Try to find the first interpretion as a module. */
        while ((!AIsModuleId(id->type) || (!id->info.module.isActive &&
                                          !override))
               && AIsId(id->type))
            id = id->next;

        /* Did we find a module? */
        if (AIsModuleId(id->type)) {
            ASymbolInfo *next;

            /* Report error if there are at least two possible interpretions
               for id. */
            /* FIX: loop?? what if more than two submodules with same name */
            if (AIsSubModuleId(id->type)
                && AIsSubModuleId(id->next->type)
                && id->next->info.module.isActive
                && !override && !isSilent)
                AGenerateError(tok->lineNumber, ErrAmbiguous, tok);
            
            do {
                tok = AAdvanceTok(AAdvanceTok(tok));

                /* An identifier is expected after a quote. */
                if (tok->type != TT_ID)
                    goto ParseError;

                /* Continue only if there is an additional prefix, separated by
                   quotes. */
                if ((tok + 1)->type != TT_SCOPEOP)
                    break;

                next = tok->info.sym->info;

                while (AIsId(next->type)) {
                    if (AIsSubModuleId(next->type)
                        && (next->info.module.isActive || override)
                        && next->sym == id) {
                        id = next;
                        break;
                    }
                    next = next->next;
                }

                if (next != id) {
                    do {
                        tok = AAdvanceTok(AAdvanceTok(tok));
                    } while (tok->type == TT_ID
                             && (tok + 1)->type == TT_SCOPEOP);

                    if (tok->type != TT_ID)
                        goto ParseError;
                    else
                        goto UndefinedError;
                }
            } while (id == next);

            /* id is the module. */

            if (!id->info.module.isImported && !override) {
                if (!isSilent) {
                    AModuleId module;

                    GenerateModuleId(id, &module);
                    AGenerateError(tok->lineNumber, ErrModuleNotImported,
                                   &module);
                }
                
                *var = &UndefinedVariable;
                
                return AAdvanceTok(tok);
            }

            /* IDEA: next is a bad name! */
            next = tok->info.sym->info;
            
            while (AIsId(next->type)) {
                if (AIsGlobalId(next->type)
                    && next->sym == id
                    && (!next->info.global.isPrivate || override)) {
                    *var = next;
                    return AAdvanceTok(tok);
                }
                next = next->next;
            }

            goto UndefinedError;
        }
    }

    /* tok is a global variable without module prefix. */
    /* IDEA: This is somewhat slow. Maybe it could be optimized? */
    *var = FindGlobalVariable(tok, override || isSilent);
    
    return AAdvanceTok(tok);

  ParseError:

    if (!override && !isSilent)
        tok = AGenerateParseError(tok);
    *var = &InvalidVariable;
    return tok;

  UndefinedError:

    if (!override && !isSilent)
        AGenerateError(tok->lineNumber, ErrUndefined, tok);
    *var = &UndefinedVariable;
    return AAdvanceTok(tok);
}


/* If tok represents a global variable, returns a pointer to the
   corresponding SymbolInfo structure. Otherwise, returns &UndefinedVariable.
   tok is assumed to point to an identifier. If isSilent is defined, do
   not generate compile errors. */
ASymbolInfo *FindGlobalVariable(AToken *tok, ABool isSilent)
{
    ASymbolInfo *id;
    ASymbolInfo *global;
    ABool isAmbiguous;

    id = tok->info.sym->info;
    isAmbiguous = FALSE;
    
    while (AIsId(id->type) && !AIsGlobalId(id->type))
        id = id->next;

    global = NULL;
    while (AIsId(id->type)) {
        if (id->sym == ACurModule) {
            /* id is defined in the current module. */
            global = id;
            isAmbiguous = FALSE;
            break;
        } else if (id->sym != ACurMainModule
                   && id->sym->info.module.isImported
                   && !id->info.global.isPrivate) {
            /* id is defined in an imported module. */
            if (global == NULL)
                global = id;
            else
                isAmbiguous = TRUE;
        }
        
        id = id->next;
    }

    if (isAmbiguous && !isSilent)
        AGenerateError(tok->lineNumber, ErrAmbiguous, tok);

    if (global == NULL) {
        if (!isSilent)
            AGenerateError(tok->lineNumber, ErrUndefined, tok);
        global = &UndefinedVariable;
    }

    return global;
}


AToken *AParseQuotedGlobalVariable(AToken *tok, ASymbolInfo **var,
                                   ABool isSilent)
{
    ASymbolInfo *id;
    
    if ((tok + 1)->type != TT_ID) {
        tok = AGenerateParseError(tok);
        *var = &InvalidVariable;
        return tok;
    }

    tok = AAdvanceTok(tok);
    
    id = tok->info.sym->info;
    while (AIsId(id->type)) {
        if (AIsGlobalId(id->type) && id->sym == ACurModule) {
            *var = id;
            return AAdvanceTok(tok);
        }
        id = id->next;
    }

    if (!isSilent)
        AGenerateError(tok->lineNumber, ErrUndefined, tok);
    
    *var = &UndefinedVariable;
    return AAdvanceTok(tok);
}


/* Find the global variable described by tok and check if it is defined
   multiple times in the current module. Assumption: tok defined in the
   current module with the given type. */
static ASymbolInfo *CheckGlobal(AToken *tok, AIdType type)
{
    ASymbolInfo *sym = FindGlobalVariable(tok, FALSE);

    /* Is this variable defined multiple times in the current module? Multiple
       definitions are known to be adjacent. */
    if (AIsGlobalId(sym->next->type)
        && sym->next->sym == sym->sym) {
        if (sym->next != sym)
            AGenerateError(tok->lineNumber, ErrRedefined, tok);
        else
            AGenerateError(tok->lineNumber, ErrUndefined, tok);
        /* Find a definition with the correct type (circular list). */
        ASymbolInfo *orig = sym;
        while (sym->type != type) {
            sym = sym->next;
            if (sym == orig)
                return NULL; /* Could not find the definition. */
        }
    }

    return sym;
}


static void LeaveLoop(void)
{
    while (LoopExits->depth >= LoopDepth) {
        ABreakList *exit = LoopExits;
        ABreakList *next = exit->next;

        if (exit->exits != NULL)
            ASetBranches(exit->exits, AGetCodeIndex());
        else
            ASetBranchDest(exit->opcodeIndex, AGetCodeIndex());

        ACFree(exit, sizeof(ABreakList)); /* FIX? */
        LoopExits = next;
    }
}


/* Create a ModuleId for module id. Find all the parts of the module name
   and stores them into location pointed by module. */
static void GenerateModuleId(ASymbolInfo *id, AModuleId *module)
{
    module->numParts = 0;
    do {
        int i;

        for (i = module->numParts; i > 0; i--)
            module->id[i + 1] = module->id[i];

        module->id[0] = id;
        id = id->sym;

        module->numParts++;
    } while (id != NULL);
}


static void CheckSuperInitializer(ATypeInfo *type)
{
    while (ASuperType(type) != NULL) {
        int init;
        
        type = type->super;
        init = ALookupMemberTable(type, MT_METHOD_PUBLIC, AM_INITIALIZER);
        if (init != -1) {
            AEmitOpcode3Args(OP_CALL_G, init, 1, 3);
            AEmitArg(A_NO_RET_VAL);
        }
    }
}


/* Return the type of the local variable (ID_LOCAL or ID_LOCAL_EXPOSED). The
   argument token is the name of variable. If the local variable is exposed,
   also emit opcode for creating the exposed value container. */
static AIdType PrepareLocalVariable(AToken *tok)
{
    /* Determine local variable type (ordinary or exposed). Exposed local
       variables are accessed by anonymous functions. */
    if (tok->type == TT_ID_EXPOSED) {
        /* Create exposed variable containers for exposed local
           variables. */
        AEmitOpcodeArg(OP_CREATE_EXPOSED, ANumLocalsActive);
        return ID_LOCAL_EXPOSED;
    } else
        return ID_LOCAL;
}


/* If the token starts a block statement, increment *depth. If the token ends a
   block statement, decrement *depth. */
static void UpdateDepth(AToken *tok, int *depth)
{
    switch (tok->type) {
    case TT_IF:
    case TT_WHILE:
    case TT_REPEAT:
    case TT_FOR:
    case TT_TRY:
    case TT_SWITCH:
        (*depth)++;
        break;
    case TT_END:
    case TT_UNTIL:
        (*depth)--;
        break;
    }
}


/* Return TRUE if the try statement starting at tok may catch ValueError or
   ResourceError. tok points to 'try'. */
static ABool IsDirectTryStatement(AToken *tok)
{
    int depth;
    
    tok = AAdvanceTok(tok);
    depth = 0;
    while (((tok->type != TT_EXCEPT && tok->type != TT_FINALLY) || depth > 0)
           && tok->type != TT_EOF) {
        UpdateDepth(tok, &depth);
        tok = AAdvanceTok(tok);
    }

    if (tok->type == TT_FINALLY)
        return TRUE;

    while (tok->type == TT_EXCEPT) {
        /* Skip "except". */
        tok = AAdvanceTok(tok);

        if (AIsIdTokenType(tok->type) || tok->type == TT_SCOPEOP) {
            /* Skip "is". */
            if (AIsIdTokenType(tok->type) && (tok + 1)->type == TT_IS)
                tok = AAdvanceTok(AAdvanceTok(tok));

            if (tok->type == TT_ID || tok->type == TT_SCOPEOP) {
                ASymbolInfo *type;
                
                tok = AParseAnyGlobalVariable(tok, &type, TRUE);

                if (type->type == ID_GLOBAL_CLASS) {
                    AValue val = AGlobalByNum(type->num);
                    if (val == AGlobalByNum(AStdExceptionNum)
                        || AIsSubType(val, AGlobalByNum(AStdResourceErrorNum))
                        || AIsSubType(val, AGlobalByNum(AStdValueErrorNum)))
                        return TRUE;
                }
            }
        }

        /* Skip to the next except clause or to the end of the try
           statement. */
        depth = 0;
        while (((tok->type != TT_END && tok->type != TT_EXCEPT) || depth > 0)
               && tok->type != TT_EOF) {
            UpdateDepth(tok, &depth);
            tok = AAdvanceTok(tok);
        }
    }
    
    return FALSE;
}


void FreeFinallyExits(void)
{
    AFreeIntList(FinallyExits);
    FinallyExits = NULL;
}


static void SetSuperType(ATypeInfo *type, ATypeInfo *super, int lineNumber)
{
    ATypeInfo *t = super;

    /* Check if we are going to introduce a supertype cycle (in this case
       we will find the original type in the supertype chain). Note that this
       will always terminate as long there wasn't a cycle before, which we
       can assume.

       Note that we do not use ASuperType() since this might cause an
       infinite recursion. */
    while (t != NULL && t != type)
        t = t->super;

    if (t == NULL)
        type->super = super;
    else if (lineNumber > 0)
        AGenerateError(lineNumber, ErrCycleInSupertypeHierarchy);
}


ATypeInfo *AGetResolveSupertype(ATypeInfo *type)
{
    AUnresolvedSupertype *unresolv;
    AUnresolvedSupertype *prev = NULL;

    unresolv = AUnresolvedSupertypes;
    while (unresolv != NULL) {
        if (unresolv->type == type) {
            AUnresolvedSupertype *next = unresolv->next;
            AFixSupertype(unresolv);
            
            /* Remove from linked list. */
            if (prev == NULL)
                AUnresolvedSupertypes = next;
            else
                prev->next = next;
            
            return type->super;
        } else {
            prev = unresolv;
            unresolv = unresolv->next;
        }
    }

    /* IDEA: We should never get here. Generate an internal error. */

    return NULL;
}


void AResolveTypeInterfaces(ATypeInfo *type)
{
    if (!type->isSuperValid)
        AGetResolveSupertype(type);
}


/* Resolve all supertypes. Only call this at the end of a compilation, as some
   modules may otherwise not have been compiled yet. */
void AFixSupertypes(void)
{
    AUnresolvedSupertype *unresolv;
    AUnresolvedSupertype *prev;

    unresolv = AUnresolvedSupertypes;
    while (unresolv != NULL) {
        prev = unresolv;
        unresolv = unresolv->next;
        AFixSupertype(prev);
    }
    
    AUnresolvedSupertypes = NULL;
}


void AFixSupertype(AUnresolvedSupertype *unresolv)
{
    AUnresolvedNameList *mod, *interfaces;
    ASymbolInfo *super;
    AList *modules;
    AToken nameTok[A_MODULE_NAME_MAX_PARTS * 2 + 4];
    ASymbolInfo *oldModule;
 
    oldModule = ACurModule;
    ACurModule = unresolv->type->sym->sym;

    /* Unimport all imported modules. */
    DeactivateModules(AImportedModules);

    /* Import relevant modules to provide the right context for expanding
       supertype references, which might not be fully qualified. */
    modules = NULL;
    for (mod = unresolv->modules; mod != NULL; mod = mod->next) {
        AExpandUnresolvedName(mod, nameTok);
        ImportCompiledModule(nameTok, &modules);
    }

    /* Bind the superclass, if present. */
    if (unresolv->super != NULL) {
        AExpandUnresolvedName(unresolv->super, nameTok);
        
        AParseAnyGlobalVariable(nameTok, &super, TRUE);

        if (super != &UndefinedVariable
            && super->type == unresolv->type->sym->type)
            SetSuperType(unresolv->type,
                         AValueToType(AGlobalByNum(super->num)),
                         -1);
        else {
            /* Superclass was undefined, probably due to programmer error. */
            ADebugCompilerMsg(("Unresolved superclass for %i\n",
                               unresolv->type->sym));
        }
    } else if (!unresolv->type->isInterface) {
        /* Default to Object as the superclass (unless type is interface). */
        AValue objectType = AGlobalByNum(AStdObjectNum);
        unresolv->type->super = AValueToType(objectType);
    }

    /* Bind the interfaces, if present. */
    interfaces = unresolv->interfaces;
    while (interfaces != NULL) {
        AExpandUnresolvedName(interfaces, nameTok);
        AParseAnyGlobalVariable(nameTok, &super, TRUE);

        if (super != &UndefinedVariable && super->type == ID_GLOBAL_INTERFACE)
            AAddInterface(unresolv->type,
                          AValueToType(AGlobalByNum(super->num)), FALSE);
        interfaces = interfaces->next;
    }

    unresolv->type->isSuperValid = TRUE;
    
    DeactivateModules(modules);
    ADisposeList(modules);
    
    AFreeUnresolvedNameList(unresolv->modules);
    AFreeUnresolvedNameList(unresolv->super);
    AFreeUnresolvedNameList(unresolv->interfaces);
    
    AFreeStatic(unresolv);

    /* Restore the previous compiler state. */
    ACurModule = oldModule;
    ActivateModules(AImportedModules);
}


AToken *ImportCompiledModule(AToken *tok, AList **moduleList)
{
    AModuleId impMod;
    ABool isCompiled;
    ASymbolInfo *sym;
    
    tok = AFindModule(tok, &impMod, &isCompiled);
    
    sym = impMod.id[impMod.numParts - 1];
    
    /* FIX: is the error checking ok? */
    if (AIsDynamicCompile && !AAddDynamicImportedModule(sym))
        AGenerateOutOfMemoryError();
    
    /* FIX: what if failure? */
    *moduleList = AAddList(*moduleList, sym);
    
    if (sym->info.module.cModule != A_CM_AUTO_IMPORT) {
        sym->info.module.isImported = TRUE;
        do {
            sym->info.module.isActive = TRUE;
            sym = sym->sym;
        } while (sym != NULL);
    }

    return tok;
}


void DeactivateModules(AList *modules)
{
    for (; modules != NULL; modules = modules->next) {
        ASymbolInfo *mod = modules->data;

        mod->info.module.isImported = FALSE;
        do {
            mod->info.module.isActive = FALSE;
            mod = mod->sym;
        } while (mod != NULL);
    }
}


void ActivateModules(AList *modules)
{
    for (; modules != NULL; modules = modules->next) {
        ASymbolInfo *mod = modules->data;

        if (mod->info.module.cModule != A_CM_AUTO_IMPORT) {
            mod->info.module.isImported = TRUE;
            do {
                mod->info.module.isActive = TRUE;
                mod = mod->sym;
            } while (mod != NULL);
        }
    }
}


static void SaveParseState(AParseState *state)
{
    state->numLocals = ANumLocals;
    state->numLocalsActive = ANumLocalsActive;
    state->blockDepth = ABlockDepth;
    state->loopDepth = LoopDepth;
    state->locals = Locals; /* FIX this might not work properly */
    state->loopExits = LoopExits;
    state->returnBranches = ReturnBranches;
    state->tryLocalVar = TryLocalVar;
    state->finallyExits = FinallyExits;
    state->isRvalue = AIsRvalue;
    
    state->numAccessedExposedVariables = ANumAccessedExposedVariables;
    state->accessedExposedVariables =
        AAllocStatic(sizeof(AExposedInfo) * ANumAccessedExposedVariables);
    if (state->accessedExposedVariables == NULL) {
        state->numAccessedExposedVariables = 0;
        AGenerateOutOfMemoryError();
    } else
        memcpy(state->accessedExposedVariables, AAccessedExposedVariables,
               sizeof(AExposedInfo) * ANumAccessedExposedVariables);
    
    memcpy(state->assignPos, AAssignPos, sizeof(AAssignPos)); /* FIX
                                                                 optimize? */
}


static void RestoreParseState(AParseState *state)
{
    ANumLocals = state->numLocals;
    ANumLocalsActive = state->numLocalsActive;
    ABlockDepth = state->blockDepth;
    LoopDepth = state->loopDepth;
    Locals = state->locals; /* FIX this might not work properly */
    LoopExits = state->loopExits;
    ReturnBranches = state->returnBranches;
    TryLocalVar = state->tryLocalVar;
    FinallyExits = state->finallyExits;
    AIsRvalue = state->isRvalue;
    
    ANumAccessedExposedVariables = state->numAccessedExposedVariables;
    if (state->accessedExposedVariables != NULL) {
        memcpy(AAccessedExposedVariables, state->accessedExposedVariables,
               sizeof(AExposedInfo) * ANumAccessedExposedVariables);
        AFreeStatic(state->accessedExposedVariables);
    }
    
    memcpy(AAssignPos, state->assignPos, sizeof(AAssignPos)); /* FIX
                                                                 optimize? */
}


/* Add a symbol of an exposed variable to the exposed variable array unless
   it has already been added. */
static void AddExposedVariable(ASymbol *sym)
{
    int i;

    /* Return if sym is included in the list. */
    for (i = 0; i < ANumAccessedExposedVariables; i++) {
        if (AAccessedExposedVariables[i].sym == sym)
            return;
    }

    /* Grow exposed variable array if needed. Return directly without doing
       anything if out of memory. */
    if (ANumAccessedExposedVariables == AAccessedExposedVariablesSize) {
        int newSize = AMax(MIN_EXPOSED_VARIABLES_SIZE,
                           2 * AAccessedExposedVariablesSize);
        AExposedInfo *exp;

        exp = AGrowStatic(AAccessedExposedVariables,
                          newSize * sizeof(AExposedInfo));
        if (exp == NULL) {
            /* Generate error and return. */
            AGenerateOutOfMemoryError();
            return;
        } else {
            AAccessedExposedVariablesSize = newSize;
            AAccessedExposedVariables = exp;
        }
    }
    
    AAccessedExposedVariables[ANumAccessedExposedVariables].sym = sym;
    AAccessedExposedVariables[ANumAccessedExposedVariables].isConst =
        sym->info->type == ID_LOCAL_CONST_EXPOSED;
    ANumAccessedExposedVariables++;
}


/* Exposed variables in the surrounding function will be mapped to local
   variables in an anonymous function. Calculate this mapping (needed before
   starting the compilation of a function), and store information about the
   mapping in the AAccessedExposedVariables array. tok should point to the
   "def" token starting the anonymous function expression.
   
   If there is an error in the expression, the mapping might be incomplete
   or contain errors, but this generally does not matter since the compilation
   will fail later on and the mapping will not be actually used anywhere where
   it might cause problems. */
static void CalculateExposedVariableMappingInAnonFunc(AToken *tok)
{
    int depth;
    ATokenType prev;

    ANumAccessedExposedVariables = 0;

    depth = 1;
    tok = AAdvanceTok(tok);

    /* Initialize previous token type. */
    prev = TT_DEF;    
    
    /* Loop over tokens and record any id tokens that refer to an exposed
       local variable unless the id tokens are part of a :: expression or
       member access expression. Quit the loop after having reached the end
       token that is part of the the anonymous function. Do not really parse
       the function to simplify things a bit. */
    while (tok->type != TT_EOF && depth > 0) {
        switch (tok->type) {
        case TT_IF:
        case TT_FOR:
        case TT_WHILE:
        case TT_REPEAT:
        case TT_SWITCH:
        case TT_TRY:
        case TT_SUB:
        case TT_DEF:
            /* Block start token (must be matched by a block end token) */
            depth = depth + 1;
            break;

        case TT_END:
        case TT_UNTIL:
            /* Block end token */
            depth = depth - 1;
            break;

        case TT_ID: {
            /* Identifier (potentially a reference to an exposed local
               variable) */
            ASymbolInfo *sym;
            
            sym = tok->info.sym->info;

            /* Process all references to exposed local variables defined in an
               outer scope (i.e. not within the target function). */
            if (AIsExposedLocalId(sym->type)) {
                /* An identifier after :: or . and before :: does not refer to
                   a local variable. */
                if (prev != TT_SCOPEOP && prev != TT_DOT
                        && AAdvanceTok(tok)->type != TT_SCOPEOP)
                    AddExposedVariable(tok->info.sym);
            }
            
            break;
        }
        }

        prev = tok->type;
        tok = AAdvanceTok(tok);
    }
}


/* If tok points to a "as" token, skip type annotation that extends until the
   end of the line, and return the next newline token. Otherwise, return tok.
   Report any invalid tokens as errors, but perform no real parsing. */
AToken *AParseTypeAnnotation(AToken *tok)
{
    if (tok->type == TT_AS) {
        /* The scanner has cleared all annotations, so this is trivial. */
        tok = AAdvanceTok(tok);
        while (tok->type != TT_NEWLINE) {
            if (tok->type != TT_ANNOTATION) {
                tok = AGenerateParseError(tok);
                break;
            }
            tok = AAdvanceTok(tok);
        }
    }
    return tok;
}


/* If tok points to "as", skip type annotation until reaching a separator such
   as a comma or ')', and return the next token. Otherwise, return tok. Unlike
   AParseTypeAnnotationUntilSeparator, do not report any invalid tokens as
   errors; otherwise that function is identical to this. */
AToken *ASkipTypeAnnotationUntilSeparator(AToken *tok)
{
    if (tok->type == TT_AS) {
        tok = AAdvanceTok(tok);
        while (tok->type == TT_ANNOTATION || AIsErrorToken(tok->type))
            tok = AAdvanceTok(tok);
    }
    return tok;
}


/* If tok points to a "as" token, skip type annotation until reaching a
   separator such as a comma or ')', and return the next token. Otherwise,
   return tok. Report any invalid tokens as errors, but perform no real
   parsing. */
AToken *AParseTypeAnnotationUntilSeparator(AToken *tok)
{
    if (tok->type == TT_AS) {
        /* The scanner has cleared all annotations, so this is trivial. */
        tok = AAdvanceTok(tok);
        /* The annotation must be non-empty. */
        if (tok->type != TT_ANNOTATION && !AIsErrorToken(tok->type))
            AGenerateParseError(tok);
        while (tok->type == TT_ANNOTATION || AIsErrorToken(tok->type)) {
            if (tok->type != TT_ANNOTATION)
                AGenerateParseError(tok);
            tok = AAdvanceTok(tok);
        }
    }
    return tok;
}


/* If tok points to a '<' token, skip type annotation of form '< ... >', or
   otherwise return tok. */
AToken *AParseGenericAnnotation(AToken *tok)
{
    if (tok->type == TT_LT) {
        tok = AAdvanceTok(tok);
        
        while (tok->type != TT_NEWLINE && tok->type != TT_GT) {
            if (tok->type != TT_ANNOTATION)
                AGenerateParseError(tok);
            
            tok = AAdvanceTok(tok);
        }

        if (tok->type == TT_GT)
            tok = AAdvanceTok(tok);
        else
            tok = AGenerateParseError(tok);
    }

    return tok;
}


/* Skip inline type annotation within 'as' '<' ... '>', if tok points to an
   'as' token followed by '<', and return the token after '>'. Otherwise,
   return tok. The annotation must have a matching number of '<'s and '>'s. */
AToken *AParseBracketedTypeAnnotation(AToken *tok)
{
    if (tok->type == TT_AS && (tok + 1)->type == TT_LT) {
        tok = AAdvanceTok(tok);
        tok = AParseGenericAnnotation(tok);
    }

    return tok;
}


/* If tok points to "as", clear the type annotation by toggling all non-error
   tokens in the annotation to TT_ANNOTATION tokens, and return the next token
   after the cleared run. Otherwise, simply return tok.

   This function clears the annotation until newline; the functions below can
   be used for other annotation kinds.

   Note that none of the clear functions clear the lookahead token at the end
   of token blocks. This should be fine, since lookahead is never used 
   when skipping annotations, and there is also a "sentinel" token before a
   type annotation.
   
   By clearing annotations during scanning, annotation contents will not
   confuse parsing or scanning. Additionally, we can detect if an annotation
   was not properly processed by the scanner (annotation contains
   non-annotation, non-error tokens). This indicates that there is a bug in
   the scanner. */
AToken *AClearTypeAnnotation(AToken *tok)
{
    /* IDEA: Handle lookahead at the end of a token block properly. */
    if (tok->type == TT_AS) {
        tok = AAdvanceTok(tok);
        while (tok->type != TT_NEWLINE)
            tok = ClearAnnotationToken(tok);
    }
    return tok;
}


/* If tok points to "as", clear tokens until encountering statement break,
   comma, right parenthesis, ">", "or" or "in". Skip sections that are within
   ( ), [ ] or < > without looking for breaks in them. Assume that brackets
   are properly nested. Return the first token after the cleared ones.

   If tok does not point to "as", return tok as such.

   See also the comment for AClearTypeAnnotation above. */
AToken *AClearTypeAnnotationUntilSeparator(AToken *tok)
{
    if (tok->type == TT_AS)
        tok = ClearUntilSeparator(AAdvanceTok(tok));
    return tok;
}


/* If tok points to '<', clear annotation of form "< ... >". Otherwise,
   simply return tok.

   See also the comment for AClearTypeAnnotation above. */
AToken *AClearGenericAnnotation(AToken *tok)
{
    if (tok->type == TT_LT) {
        tok = AAdvanceTok(tok);
        for (;;) {
            tok = ClearUntilSeparator(tok);
            if (tok->type == TT_GT || tok->type == TT_NEWLINE)
                break;
            tok = ClearAnnotationToken(tok);
        }
        if (tok->type == TT_GT)
            tok = AAdvanceTok(tok);
    }

    return tok;
}


/* If tok points to 'as' followed '<', clear annotation of form "as < ... >".
   Otherwise, simply return tok.

   See also the comment for AClearTypeAnnotation above. */
AToken *AClearBracketedTypeAnnotation(AToken *tok)
{
    if (tok->type == TT_AS && (tok + 1)->type == TT_LT) {
        tok = AAdvanceTok(tok);
        tok = AClearGenericAnnotation(tok);
    }
    return tok;
}


/* Clear type annotation tokens until encountering a break (statement break,
   comma, right parenthesis, ">", "or" or "in"). Clear entire sections that are
   within ( ), [ ] or < >, even if they contain tokens that would otherwise
   cause a break. Assume that brackets are properly nested. Return the first
   token after the run of cleared tokens.

   Just skip any error tokens; do not clear them. */
static AToken *ClearUntilSeparator(AToken *tok)
{
    int parenDepth = 0;
    int squareBracketDepth = 0;
    int angleBracketDepth = 0;

    while (tok->type != TT_NEWLINE) {
        if (parenDepth == 0 && squareBracketDepth == 0 &&
            angleBracketDepth == 0) {
            if (tok->type == TT_COMMA || tok->type == TT_RPAREN ||
                tok->type == TT_OR || tok->type == TT_IN || tok->type == TT_GT)
                break;
        }
        
        switch (tok->type) {
        case TT_LPAREN:
            parenDepth++;
            break;
        case TT_RPAREN:
            parenDepth = AMax(0, parenDepth - 1);
            break;
        case TT_LBRACKET:
            squareBracketDepth++;
            break;
        case TT_RBRACKET:
            squareBracketDepth = AMax(0, squareBracketDepth - 1);
            break;
        case TT_LT:
            angleBracketDepth++;
            break;
        case TT_GT:
            angleBracketDepth = AMax(0, angleBracketDepth - 1);
        default:
            break;
        }

        tok = ClearAnnotationToken(tok);
        
        /* IDEA: Clear lookahead also at the end of token blocks. */
    }

    return tok;
}


static AToken *ClearAnnotationToken(AToken *tok)
{
    if (!AIsErrorToken(tok->type))
        tok->type = TT_ANNOTATION;
    return AAdvanceTok(tok);
}
