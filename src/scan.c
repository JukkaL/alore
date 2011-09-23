/* scan.c - Alore source file scanner (first compiler pass)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* The scanner quickly collects information about an Alore source file before
   compiling it to bytecode. This includes, for example, information about
   global definitions and the structure of classes. Before a file can be
   compiled to bytecode, all the files in the module that includes the file
   and all modules the file imports must have been scanned. */

#include "compile.h"
#include "error.h"
#include "parse.h"
#include "class.h"
#include "ident.h"
#include "memberid.h"
#include "internal.h"
#include "debug_runtime.h"
#include "std_module.h"
#include "output.h"
#include "gc.h"


static AToken *ScanVariableDefinition(AToken *tok, ABool isPrivate);
static AToken *ScanFunctionDefinition(AToken *tok, ABool isPrivate);
static AToken *ScanTypeDefinition(AToken *tok, ABool isPrivate);
static AToken *ScanExpressionUntilNewline(AToken *tok, ABool allowCast);
static AToken *ScanExpressionFragment(AToken *tok, ABool stopAtAs);
static AToken *ScanBlock(AToken *tok);
static AToken *ScanFunctionArguments(AToken *tok, ASymbolInfo *fun);
static AToken *ScanFunctionBody(AToken *tok);
static void ScanExposedVariableDefinitions(AToken *tok);

static AToken *ParseAndStoreInheritanceInfo(ATypeInfo *type, AToken *tok,
                                            int *numInterfaces);
static void ClearForLoopAnnotations(AToken *tok);


static AIntList *InitList;
static ABool HasCreate;
static AUnresolvedNameList *Imports;


#define MIN_VARIABLE_DEF_TOKENS_SIZE 32


/* Pointer to an array of variable name tokens encountered in var definitions
   that might be exposed, indexed by number of local variable (num member of
   ASymbolInfo) */
static AToken **VariableDefTokens;
static int VariableDefTokensSize;

/* Block depths of nested functions, used to keep track of which "end" ends a
   particular nested function */
static int FunDepthBlockDepth[A_MAX_ANON_SUB_DEPTH + 1];

/* This flag is set whenever an anonymous function is encountered during
   scan. */
static ABool EncounteredAnonFunc;


ABool AScan(AModuleId *module, AToken *tok)
{
    ABool isPrivate = FALSE;

    ADebugCompilerMsg(("Start scan '%m'", module));
    ADebugVerifyMemory();

#ifdef HAVE_JIT_COMPILER
    AIsJitModule = AIsJitModuleId(module);
#endif
    
    if (module->numParts > 0)
        ACurModule = module->id[module->numParts - 1];
    else
        ACurModule = ACurMainModule;

    Imports = NULL;
    
    VariableDefTokens = NULL;
    VariableDefTokensSize = 0;
    
    /* Find all global definitions. Don't care about errors. */
    while (tok->type != TT_EOF) {
        switch (tok->type) {
        case TT_IMPORT:
            do {
                tok = AAdvanceTok(tok);
                Imports = AAddUnresolvedName(Imports, tok);
                while (tok->type != TT_NEWLINE && tok->type != TT_COMMA)
                    tok = AAdvanceTok(tok);
            } while (tok->type == TT_COMMA);
            while (tok->type != TT_NEWLINE)
                tok = AAdvanceTok(tok);
            tok = AAdvanceTok(tok);
            break;
            
        case TT_PRIVATE:
            isPrivate = TRUE;
            tok = AAdvanceTok(tok);
            continue;

        case TT_VAR:
        case TT_CONST:
            tok = ScanVariableDefinition(tok, isPrivate);
            break;
            
        case TT_SUB:
        case TT_DEF: {
            /* Function definition. */
            AToken *funcStart = tok;
            EncounteredAnonFunc = FALSE;

            tok = ScanFunctionDefinition(tok, isPrivate);

            /* Scan exposed variables if there is an anonymous function
               used within the function. Mark exposed variable tokens in
               var definitions with different token type
               (TT_ID_EXPOSED). */
            if (EncounteredAnonFunc)
                ScanExposedVariableDefinitions(funcStart);
            break;
        }

        case TT_FOR:
            ClearForLoopAnnotations(tok);

            /* Fall through */
            
        case TT_IF:
        case TT_WHILE:
        case TT_REPEAT:
        case TT_SWITCH:
        case TT_TRY: {
            /* Block start token. Scan until end of block. */
            AToken *start = tok;
            EncounteredAnonFunc = FALSE;
            
            tok = ScanBlock(tok);

            /* Scan exposed variables if there is an anonymous function used
               within the function (see above). */
            if (EncounteredAnonFunc)
                ScanExposedVariableDefinitions(start);
            break;

            break;
        }
           
        case TT_CLASS:
        case TT_INTERFACE:
            tok = ScanTypeDefinition(tok, isPrivate);
            break;

        default:
            /* An expression statement or end/until. These lines may contain
               anonymous functions so scan them as expressions. */
            tok = ScanExpressionUntilNewline(tok, TRUE);
            if (tok->type == TT_NEWLINE)
                tok = AAdvanceTok(tok);
            break;
        }

        isPrivate = FALSE;
    }
    
    /* We're done. */

    AFreeUnresolvedNameList(Imports);
    AFreeStatic(VariableDefTokens);
    
    ADebugVerifyMemory();
    ADebugCompilerMsg(("End scan"));

    return TRUE;
}


static AToken *ScanVariableDefinition(AToken *tok, ABool isPrivate)
{
    AIdType type = (tok->type == TT_CONST) ? ID_GLOBAL_CONST : ID_GLOBAL;

    /* Skip "const" or "var". */
    tok = AAdvanceTok(tok);

    while (tok->type == TT_ID) {
        /* FIX: what if out of memory? */
        AAddGlobalVariable(tok->info.sym, ACurModule, type, isPrivate);
        if ((tok + 1)->type != TT_COMMA)
            break;
        tok = AAdvanceTok(AAdvanceTok(tok));
    }

    if (tok->type != TT_NEWLINE)
        tok = ScanExpressionUntilNewline(tok, FALSE);
    
    return AAdvanceTok(tok);
}


static AToken *ScanMemberVariableDefinition(AToken *tok, ATypeInfo *type,
                                           ABool isPrivate)
{
    ATokenType defType;
    int oldNumVars;
    int i;

    defType = tok->type;
    oldNumVars = type->numVars;
    
    /* Skip "var" or "const". */
    tok = AAdvanceTok(tok);

    while (tok->type == TT_ID) {
        ASymbolInfo *sym;        
        unsigned key;
        unsigned item;

        /* FIX: out of memory error? */
        sym = AGetMemberSymbol(tok->info.sym);
        if (sym != NULL) {
            key = sym->num;
            item = type->numVars++;
        
            AAddMember(MT_VAR_GET_PUBLIC + isPrivate, key, item);
            if (defType == TT_VAR)
                AAddMember(MT_VAR_SET_PUBLIC + isPrivate, key, item);
        }
        
        if ((tok + 1)->type != TT_COMMA)
            break;
        
        tok = AAdvanceTok(AAdvanceTok(tok));
    }

    tok = AAdvanceTok(tok);
    if (tok->type == TT_ASSIGN) {
        type->hasMemberInitializer = TRUE;
    } else if (!isPrivate) {
        for (i = oldNumVars; i < type->numVars; i++)
            InitList = AAddIntListEnd(InitList, i);
    }

    tok = ScanExpressionUntilNewline(tok, FALSE);

    return AAdvanceTok(tok);
}


static AToken *ScanFunctionDefinition(AToken *tok, ABool isPrivate)
{
    /* Skip "def". */
    tok = AAdvanceTok(tok);
    
    if (tok->type == TT_ID) {
        ASymbolInfo *fun;
        
        /* Main function is always private. */
        if (strcmp(tok->info.sym->str, "Main") == 0)
            isPrivate = TRUE;
        
        /* FIX: what if out of mem? */
        fun = AAddGlobalVariable(tok->info.sym, ACurModule, ID_GLOBAL_DEF,
                                 isPrivate);
        
        tok = ScanFunctionArguments(AAdvanceTok(tok), fun);
    }

    return ScanFunctionBody(tok);
}


static AToken *ScanMethodDefinition(AToken *tok, ATypeInfo *type,
                                    ABool isPrivate, ASymbolInfo *class_,
                                    ABool isInterface)
{
    ATokenType t1, t2;
    unsigned gvar;
    
    /* Skip "def". */
    tok = AAdvanceTok(tok);

    t1 = tok->type;

    gvar = AAddConstGlobalValue(AZero);
    
    if (t1 == TT_ID) {
        unsigned member;
        AToken *idTok = tok;

        member = AGetMemberSymbol(tok->info.sym)->num;

        /* Clear and skip any <...> after function name. */
        tok = AClearGenericAnnotation(AAdvanceTok(tok));

        t2 = tok->type;
        if (t2 == TT_LPAREN) {
            /* Ordinary method */           
            AAddMember(MT_METHOD_PUBLIC + isPrivate, member, gvar);
            if (idTok->info.sym == ACreateSymbol) {
                HasCreate = TRUE;
                tok = ScanFunctionArguments(tok, class_);
            } else
                tok = ScanFunctionArguments(tok, NULL);
        } else if (t2 == TT_ASSIGN) {
            /* Setter method */
            AAddMember(MT_VAR_SET_PUBLIC + isPrivate, member,
                       gvar | A_VAR_METHOD);
            tok = AAdvanceTok(tok);
            if (tok->type == TT_ID)
                tok = AAdvanceTok(tok);
            tok = AClearTypeAnnotation(tok);
        } else if (t2 == TT_NEWLINE || t2 == TT_AS) {
            /* Getter method */
            AAddMember(MT_VAR_GET_PUBLIC + isPrivate, member,
                       gvar | A_VAR_METHOD);
            tok = AClearTypeAnnotation(tok);
        }
    }

    tok = AClearTypeAnnotation(tok);
    
    /* FIX skip until newline? */

    if (isInterface)
        return AAdvanceTok(tok); /* Skip newline. */
    else
        return ScanFunctionBody(tok);
}


/* Scan function arguments. The argument tok should point to the next token
   after the function name. Also scan any number of alternative signatures
   introduced with "or". */
static AToken *ScanFunctionArguments(AToken *tok, ASymbolInfo *fun)
{
    /* Is this the first signature for this function? */
    ABool isFirst = TRUE;
    
    for (;;) {
        /* Clear and skip any <...> after function name. */
        tok = AClearGenericAnnotation(tok);
        
        if (tok->type == TT_LPAREN) {
            int minArgs = 0;
            int maxArgs = 0;
            
            do {
                tok = AAdvanceTok(tok);
                
                if (tok->type == TT_ASTERISK) {
                    maxArgs = (maxArgs + 1) | A_VAR_ARG_FLAG;
                    tok = AAdvanceTok(tok);
                    if (tok->type == TT_ID)
                        tok = AAdvanceTok(tok);
                } else if (AIsIdTokenType(tok->type)) {
                    maxArgs++;
                    tok = AAdvanceTok(tok);
                    if (tok->type == TT_ASSIGN)
                        tok = ScanExpressionFragment(AAdvanceTok(tok), TRUE);
                    else
                        minArgs++;
                } else
                    break;
                
                tok = AClearTypeAnnotationUntilSeparator(tok);
            } while (tok->type == TT_COMMA);

            /* Update the number of arguments for the related function, but
               only for the first signature. */
            if (fun != NULL && isFirst) {
                fun->info.global.minArgs = minArgs;
                fun->info.global.maxArgs = maxArgs;
            }
            
            if (tok->type == TT_RPAREN)
                tok = AAdvanceTok(tok);
        } else {
            /* Could not process the signature; it is probably invalid. This
               will be detected during parsing. */
            if (fun != NULL) {
                /* We don't know the programmer's intentions, so make the
                   function accept any number of arguments. */
                fun->info.global.minArgs = 0;
                fun->info.global.maxArgs = A_VAR_ARG_FLAG;
            }
        }
        
        tok = AClearTypeAnnotationUntilSeparator(tok);

        if (tok->type != TT_OR)
            break;

        tok = AAdvanceTok(tok);

        /* Only store valid argument counts during the first iteration (for
           first alternative signature). */
        isFirst = FALSE;
    }

    return tok;
}


/* Scan the body of a function. The tok argument should point to a token after
   the function header. Return the token after the line containing
   the final "end". */
static AToken *ScanFunctionBody(AToken *tok)
{
    for (;;) {
        tok = ScanBlock(tok);

        if (tok->type == TT_END || tok->type == TT_EOF)
            break;

        tok = AAdvanceTok(tok);
    }

    if (tok->type == TT_EOF)
        return tok;

    do {
        tok = AAdvanceTok(tok);
    } while (tok->type != TT_NEWLINE);

    return AAdvanceTok(tok);
}


/* Scan a type definition. The tok argument should point to the initial
   "class" or "interface" token (not "private"). */
static AToken *ScanTypeDefinition(AToken *tok, ABool isPrivate)
{
    ABool isInterface;
    ABool isPrivateMember;
    ATypeInfo *type;
    ASymbolInfo *class_;
    ABool isInherited;

    isInterface = tok->type == TT_INTERFACE;

    /* Skip "class" or "interface". */
    tok = AAdvanceTok(tok);

    if (tok->type == TT_ID) {
        class_ = AAddGlobalVariable(tok->info.sym, ACurModule,
                                    isInterface ? ID_GLOBAL_INTERFACE
                                                : ID_GLOBAL_CLASS,
                                    isPrivate);
        if (class_ == NULL)
            return tok;
        
        ADebugCompilerMsg(("Scan type '%q'", class_));
        ADebugVerifyMemory();

        isInherited = FALSE;
        
        type = ACreateTypeInfo(ACompilerThread, class_, isInterface);
        if (type == NULL) {
            AGenerateOutOfMemoryError();
            return tok;
        }

        /* FIX: error checking? */
        type->create = AAddConstGlobalValue(AZero);

        /* By default we do not know the number of arguments the class
           constructor takes. */
        class_->info.global.minArgs = A_UNKNOWN_MIN_ARGS;
        class_->info.global.maxArgs = 0;

        /* Skip class name. */
        tok = AAdvanceTok(tok);

        /* Clear and skip any <...> after class name. */
        tok = AClearGenericAnnotation(tok); 

        /* If the class inherits another class, store information about the
           inheritance so that the superclass name can be resolved after all
           dependant modules have been scanned. */
        if (tok->type == TT_IS || tok->type == TT_IMPLEMENTS) {
            int numInterfaces;
            
            isInherited = tok->type == TT_IS;
            /* FIX: errors? */
            tok = ParseAndStoreInheritanceInfo(type, tok, &numInterfaces);
            
            /* FIX: out of memory? */
            if (numInterfaces > 0)
                AAllocateInterfacesInTypeInfo(type, numInterfaces);
        } else if (!isInterface) {
            /* Default to Object as the superclass. */
            AValue objectType = AGlobalByNum(AStdObjectNum);
            type->super = AValueToType(objectType);
        }
    } else {
        /* No class name - do not process the class definition. */
        /* FIX: this is severely broken.. I guess? */
        ADebugCompilerMsg(("Scan invalid class"));
        do {
            tok = ScanBlock(tok);
        } while (tok->type != TT_END && tok->type != TT_EOF);
        return tok;
    }

    while (tok->type != TT_NEWLINE)
        tok = AAdvanceTok(tok);
    
    /* Skip newline. */
    tok = AAdvanceTok(tok);

    isPrivateMember = FALSE;
    InitList = NULL;
    HasCreate = FALSE;
    
    while (tok->type != TT_END && tok->type != TT_EOF) {
        switch (tok->type) {
        case TT_PRIVATE:
            isPrivateMember = TRUE;
            tok = AAdvanceTok(tok);
            continue;
            
        case TT_VAR:
        case TT_CONST:
            tok = ScanMemberVariableDefinition(tok, type, isPrivateMember);
            break;
            
        case TT_SUB:
        case TT_DEF: {
            AToken *funcStart = tok;
            EncounteredAnonFunc = FALSE;

            tok = ScanMethodDefinition(tok, type, isPrivateMember, class_,
                                       isInterface);
            
            /* Scan exposed variables if there is an anonymous function
               used within the method. Mark exposed variable tokens in var
               definitions with different token type (TT_ID_EXPOSED). */
            if (EncounteredAnonFunc)
                ScanExposedVariableDefinitions(funcStart);
            
            break;
        }

        default:
            /* Handle errors and bind declarations. */
            do {
                tok = AAdvanceTok(tok);
            } while (tok->type != TT_NEWLINE);
            tok = AAdvanceTok(tok);
            break;
        }

        isPrivateMember = FALSE;
    }

    if (type->hasMemberInitializer)
        type->memberInitializer = AAddConstGlobalValue(AZero);

    if (!HasCreate && InitList != NULL && !isInherited && !isInterface) {
        AValue createVal;
        int createNum;
        AIntList *n;
        int i;
        
        AEnterSection();

        AEmitAbsoluteLineNumber(tok);

        i = 4;
        for (n = InitList; n != NULL; n = n->next) {
            AEmitOpcode2Args(OP_ASSIGN_LV, n->data, i);
            i++;
        }
        
        AEmitOpcode(OP_RET);
        ANumLocals = i;
        ANumLocalsActive = i;

        createNum = AAddConstGlobalValue(AZero);        
        createVal = ALeaveSection(class_, i - 3, i - 3, createNum);
        if (!ASetConstGlobalValue(ACompilerThread, createNum, createVal)) {
            AGenerateOutOfMemoryError();
            return tok;
        }
        
        AAddMember(MT_METHOD_PUBLIC, AM_CREATE, createNum);
        class_->info.global.minArgs = i - 4;
        class_->info.global.maxArgs = i - 4;

        AFreeIntList(InitList);
    }
    
    ABuildTypeInfoMembers(type);

    /* The type is now ready to be used, unless we ran out of memory. In the
       latter case some data structures (member tables) might not be
       initialized properly. */
    type->isValid = !AIsOutOfMemoryError;

    ADebugVerifyMemory();
    ADebugCompilerMsg(("End type '%q'", class_));

    return tok;
}


/* Repeatedly call ScanExpressionFragment until encountering a newline
   token. Also clear any type annotation just before the end of the line unless
   allowCast is TRUE. */
static AToken *ScanExpressionUntilNewline(AToken *tok, ABool allowCast)
{
    int parenDepth = 0;
    while (tok->type != TT_NEWLINE && tok->type != TT_EOF
           && (parenDepth > 0 || allowCast ||
               !(tok->type == TT_AS && (tok + 1)->type != TT_LT))) {
        if (tok->type == TT_LPAREN || tok->type == TT_LBRACKET) {
            parenDepth++;
            tok = AAdvanceTok(tok);
        } else if (tok->type == TT_RPAREN || tok->type == TT_RBRACKET) {
            parenDepth--;
            tok = AAdvanceTok(tok);
        } else if (AIsDefToken(tok->type))
            tok = ScanExpressionFragment(tok, parenDepth == 0);
        else if (tok->type == TT_AS && (tok + 1)->type == TT_LT)
            tok = AClearBracketedTypeAnnotation(tok);
        else
            tok = AAdvanceTok(tok);
    }
    tok = AClearTypeAnnotation(tok);
    return tok;
}


/* Scans an expression and clear any encountered type annotations. Parentheses
   and brackets are matched and anonymous functions are skipped. Scan until
   encountering a comma or a newline or a right paren/bracket or a "as"
   (stopAtColon is TRUE).

   Set EncounteredAnonFunc if any anon functions are encountered in the
   expression. This side effect is somewhat ugly, but it avoids an additional
   scan pass. */
static AToken *ScanExpressionFragment(AToken *tok, ABool stopAtAs)
{
    ATokenType prev[A_MAX_SUBEXPRESSION_DEPTH];
    int i = 0;

    while ((tok->type != TT_COMMA || i > 0) && tok->type != TT_NEWLINE) {
        switch (tok->type) {
        case TT_LPAREN:
        case TT_LBRACKET:
            if (i == A_MAX_SUBEXPRESSION_DEPTH - 1) {
                while ((tok + 1)->type != TT_NEWLINE)
                    tok = AAdvanceTok(tok);
            } else
                prev[i++] = tok->type;
            break;
            
        case TT_RPAREN:
        case TT_RBRACKET:
            if (i == 0)
                return tok;
            if (prev[--i] != tok->type + TT_LPAREN - TT_RPAREN) {
                while ((tok + 1)->type != TT_NEWLINE)
                    tok = AAdvanceTok(tok);
            }
            break;

        case TT_AS:
            /* Is it an inline annotation? */
            if ((tok + 1)->type == TT_LT) {
                tok = AClearBracketedTypeAnnotation(tok);
                continue;
            }
            if (i == 0 && stopAtAs)
                return tok;
            break;
            
        case TT_SUB:
        case TT_DEF:
            /* Toggle flag to indicate that we have encountered an anonymous
               function. */
            EncounteredAnonFunc = TRUE;

            /* Scan anonymous function. */
            tok = AAdvanceTok(tok);
            /* Scan argument definition, ignoring any results. */
            tok = ScanFunctionArguments(tok, NULL);
            do {
                tok = ScanBlock(tok);
            } while (tok->type != TT_END && tok->type != TT_EOF);
            if (tok->type == TT_EOF)
                return tok;
        }
        
        tok = AAdvanceTok(tok);
    }

    return tok;
}


/* Scans a block of statements until one of the following tokens is
   encountered: TT_END, TT_UNTIL, TT_EOF. The first line is assumed be
   the block header and it is unconditionally skipped. */
static AToken *ScanBlock(AToken *tok)
{
    for (;;) {
        if (tok->type != TT_EOF) {
            /* Skip tokens until an end of line or end of file is
               encountered. */
            while (tok->type != TT_NEWLINE && tok->type != TT_EOF) {
                /* Skip all other tokens except "def". We have to scan
                   contents of an anonymous function a bit more carefully. */
                if (AIsDefToken(tok->type))
                    tok = ScanExpressionFragment(tok, FALSE);
                else if (tok->type == TT_AS && (tok + 1)->type == TT_LT)
                    tok = AClearBracketedTypeAnnotation(tok);
                else
                    tok = AAdvanceTok(tok);
            }
            /* Skip newline token. */
            if (tok->type == TT_NEWLINE)
                tok = AAdvanceTok(tok);
        }

        /* The current token is either the first token of a statement /
           definition or the end of the file. */

        switch (tok->type) {
        case TT_FOR:
            ClearForLoopAnnotations(tok);

            /* Fall through */
            
        case TT_IF:
        case TT_WHILE:
        case TT_SWITCH:
        case TT_TRY:
            do {
                tok = ScanBlock(tok);
            } while (tok->type != TT_END && tok->type != TT_EOF);
            break;

        case TT_REPEAT:
            do {
                tok = ScanBlock(tok);
            } while (tok->type != TT_UNTIL && tok->type != TT_EOF);
            break;

        case TT_END:
        case TT_UNTIL:
        case TT_EOF:
            return tok;

        case TT_VAR:
            tok = ScanExpressionUntilNewline(tok, FALSE);
            break;

        default:
            break;
        }
    }
}
    

ASymbolInfo *AAddGlobalVariable(ASymbol *sym, ASymbolInfo *module,
                              AIdType type, ABool isPrivate)
{
    ASymbolInfo *var = AAddIdentifier(sym, ID_GLOBAL);

    if (var == NULL)
        return NULL;

    var->type = type;
    var->sym  = module;
    var->info.global.isPrivate = isPrivate;

    if (type == ID_GLOBAL)
        var->num = AAddGlobalValue(ANil);
    else
        var->num = AAddConstGlobalValue(ANil);

    return var;
}


static AToken *ParseAndStoreInheritanceInfo(ATypeInfo *type, AToken *tok,
                                            int *numInterfaces)
{
    AUnresolvedNameList *super = NULL;
    AUnresolvedNameList *interfaces = NULL;

    if (tok->type == TT_IS) {
        tok = AAdvanceTok(tok);
        super = AAddUnresolvedName(NULL, tok);
        while (tok->type == TT_ID || tok->type == TT_SCOPEOP)
            tok = AAdvanceTok(tok);
        tok = AClearGenericAnnotation(tok);
    }

    *numInterfaces = 0;

    if (tok->type == TT_IMPLEMENTS) {
        tok = AAdvanceTok(tok);
        while (tok->type == TT_ID || tok->type == TT_SCOPEOP) {
            interfaces = AAddUnresolvedName(interfaces, tok);
            (*numInterfaces)++;
            while (tok->type == TT_ID || tok->type == TT_SCOPEOP)
                tok = AAdvanceTok(tok);
            tok = AClearGenericAnnotation(tok);
            if (tok->type != TT_COMMA)
                break;
            tok = AAdvanceTok(tok);
        }
    }

    AStoreInheritanceInfo(type, Imports, super, interfaces);

    return tok;
}


/* Store information about the supertypes (direct supertype and implemented
   interfaces) of a type so that references to these the types can later be
   resolved to point to the correct ATypeInfo structures. Supertype name
   resolution must be done later since the supertype Type objects may not have
   been created when the types are processed initially.

   To resolve the type names, we also need to store a list of imported
   modules. The supertype names do not have to be fully qualified, and
   partially qualified names can only be resolved in the correct import
   context.

   Functions AGetResolveSupertype and AFixSupertype are involved in the
   resolution process. They can only be used after the first phase of
   compilation has been completed for a compilation unit and its
   dependencies. */
void AStoreInheritanceInfo(ATypeInfo *type, AUnresolvedNameList *imports,
                           AUnresolvedNameList *super,
                           AUnresolvedNameList *interfaces)
{
    /* FIX: Check out of memory conditions. */

    AUnresolvedSupertype *resolv =
        AAllocStatic(sizeof(AUnresolvedSupertype));

    resolv->type = type;
    resolv->modules = ACloneUnresolvedNameList(imports);
    resolv->super = super;
    resolv->interfaces = interfaces;
    resolv->next = AUnresolvedSupertypes;
    AUnresolvedSupertypes = resolv;

    /* The information about supertypes is not yet valid. This will be changed
       after they have been resolved. */
    type->isSuperValid = FALSE;
}


/* Add a name to an unresolved list.
   NOTE: Generate error and return list unmodified on error conditions. */
AUnresolvedNameList *AAddUnresolvedName(AUnresolvedNameList *list, AToken *tok)
{
    AUnresolvedNameList *name;
    ABool isQuotePrefix = FALSE;
    int i;

    if (tok->type == TT_SCOPEOP && (tok + 1)->type == TT_ID) {
        isQuotePrefix = TRUE;
        tok = AAdvanceTok(tok);
    } else if (tok->type != TT_ID)
        return list;
    
    name = AAllocStatic(sizeof(AUnresolvedNameList));
    if (name == NULL) {
        AGenerateOutOfMemoryError();
        return list;
    }
    
    i = 0;
    while (tok->type == TT_ID && i <= A_MODULE_NAME_MAX_PARTS) {
        name->name[i] = tok->info.sym;
        i++;
        tok = AAdvanceTok(tok);
        if (tok->type != TT_SCOPEOP)
            break;
        tok = AAdvanceTok(tok);
    }

    name->isQuotePrefix = isQuotePrefix;
    name->numParts = i;
    name->next = list;

    return name;
}


/* NOTE: Generate error and return list unmodified on error conditions. */
AUnresolvedNameList *AAddUnresolvedNameStr(AUnresolvedNameList *list,
                                           const char *str)
{
    AToken *tok;

    if (!ATokenizeStr(str, &tok)) {
        AGenerateOutOfMemoryError();
        return list;
    }

    list = AAddUnresolvedName(list, tok);
    AFreeTokens(tok);

    return list;
}


AUnresolvedNameList *ACloneUnresolvedNameList(AUnresolvedNameList *list)
{
    /* FIX: Check out of memory conditions. */
    
    AUnresolvedNameList *head;

    if (list == NULL)
        return NULL;

    head = AAllocStatic(sizeof(AUnresolvedNameList));
    if (head == NULL)
        return NULL;
    head->isQuotePrefix = list->isQuotePrefix;
    head->numParts = list->numParts;
    memcpy(head->name, list->name, sizeof(head->name));
    head->next = ACloneUnresolvedNameList(list->next);

    return head;
}


void AFreeUnresolvedNameList(AUnresolvedNameList *list)
{
    AUnresolvedNameList *prev;

    while (list != NULL) {
        prev = list;
        list = list->next;
        AFreeStatic(prev);
    }
}


void AExpandUnresolvedName(const AUnresolvedNameList *name, AToken *expanded)
{
    int i;

    if (name->isQuotePrefix) {
        expanded[0].type = TT_SCOPEOP;
        expanded[1].type = TT_ID;
        expanded[1].info.sym = name->name[0];
        expanded[2].type = TT_NEWLINE;
        expanded[3].type = TT_EOF;
    } else {
        for (i = 0; i < name->numParts; i++) {
            expanded[i * 2].type = TT_ID;
            expanded[i * 2].info.sym = name->name[i];
            if (i + 1 < name->numParts)
                expanded[i * 2 + 1].type = TT_SCOPEOP;
            else
                expanded[i * 2 + 1].type = TT_NEWLINE;
        }
        expanded[i * 2].type = TT_EOF;
    }
}


static void AddPotentiallyExposedLocalVariable(AToken *tok)
{
    /* Grow exposed variable list if needed. May return directly if out of
       memory. */
    while (ANumLocalsActive >= VariableDefTokensSize) {
        AToken **toks;
        int newSize;
        
        newSize = AMax(MIN_VARIABLE_DEF_TOKENS_SIZE,
                       VariableDefTokensSize * 2);
        toks = AGrowStatic(VariableDefTokens, newSize * sizeof(AToken *));
        if (toks == NULL) {
            /* Do nothing if ran out of memory. */
            AGenerateOutOfMemoryError();
            return;
        } else {
            VariableDefTokens = toks;
            VariableDefTokensSize = newSize;
        }
    }
    
    AAddLocalVariableSymbol(tok->info.sym, ID_LOCAL, ANumLocalsActive);

    VariableDefTokens[ANumLocalsActive] = tok;
    ANumLocalsActive++;
}


static AToken *AddVariableList(AToken *tok)
{
    while (tok->type == TT_ID || tok->type == TT_COMMA) {
        if (tok->type == TT_ID)
            AddPotentiallyExposedLocalVariable(tok);
        tok = AAdvanceTok(tok);
    }
    return tok;
}


static void AddFunctionArgumentVariables(AToken *tok)
{
    /* Skip "def" at the start of a function definition or an anonymous
       function expression. */
    if (AIsDefToken(tok->type))
        tok = AAdvanceTok(tok);
    
    /* Skip function name if present. */
    if (tok->type == TT_ID)
        tok = AAdvanceTok(tok);

    /* Skip and clear generic function type arguments if present. */
    if (tok->type == TT_LT)
        tok = AClearGenericAnnotation(tok);
    
    if (tok->type == TT_LPAREN) {
        /* Ordinary argument list */
        tok = AAdvanceTok(tok);

        for (;;) {
            if (tok->type == TT_ASTERISK)
                tok = AAdvanceTok(tok);
            
            if (tok->type != TT_ID)
                break;
            
            AddPotentiallyExposedLocalVariable(tok);

            /* Skip until next comma or right paren. This will skip any
               initialization expressions as well without any semantic
               processing. This will also clear any encountered annotations. */
            tok = ScanExpressionFragment(tok, FALSE);

            /* Skip commas that separate arguments. */
            if (tok->type == TT_COMMA)
                tok = AAdvanceTok(tok);
        }
    } else if (tok->type == TT_ASSIGN) {
        /* Setter method */
        tok = AAdvanceTok(tok);
        if (tok->type == TT_ID)
            AddPotentiallyExposedLocalVariable(tok);
    }
}


#define IsDefinedBelowCurrentDef(sym) \
    ((sym)->info.blockDepth < FunDepthBlockDepth[AFunDepth])


/* Mark all variable name tokens in variable definitions within a block or
   non-anonymous function definition (including any anonymous functions that
   might be included in the function) that refer to an exposed variable (the
   token type will be changed to TT_ID_EXPOSED). The tok argument should point
   to the start of a function or method definition.

   The results might not be correct if there are errors in the block, but
   these errors will not cause problems since in that case the code will never
   be run. */
static void ScanExposedVariableDefinitions(AToken *tok)
{
    ATokenType prev;

    ABlockDepth = 1;
    
    FunDepthBlockDepth[AFunDepth] = 0;

    ANumLocalsActive = 0;

    /* Record function argument variables (they might be exposed). */
    AddFunctionArgumentVariables(tok);

    /* Skip the block-initial token to avoid it being registered as an
       additional block. */
    tok = AAdvanceTok(tok);
    /* Skip function header. */
    tok = ScanFunctionArguments(tok, NULL);

    /* Initialize prev (arbitrarily to TT_DEF). */
    prev = TT_DEF;

    /* Loop over the tokens that form the function definition, recording any
       variable definitions and checking for any references to local variables
       which might reference variables defined in a surrounding function
       definition from within an anonymous function, and mark these variables
       as exposed. */
    while (tok->type != TT_EOF && ABlockDepth > 0) {
        /* tok points to the first token of a statement or a newline. */
        
        switch (tok->type) {
        case TT_VAR:
            tok = AddVariableList(AAdvanceTok(tok));
            break;

        case TT_EXCEPT:
            tok = AAdvanceTok(tok);
            if (tok->type == TT_ID && (tok + 1)->type == TT_IS)
                AddPotentiallyExposedLocalVariable(tok);
            break;
            
        case TT_FOR:
            ABlockDepth++;
            tok = AddVariableList(AAdvanceTok(tok));
            break;
            
        case TT_IF:
        case TT_WHILE:
        case TT_REPEAT:
        case TT_SWITCH:
        case TT_TRY:
            /* Block start token (must be matched by a block end token) */
            ABlockDepth++;
            break;
            
        case TT_END:
        case TT_UNTIL:
            /* Block end token */

            /* Leave anonymous function definition? */
            if (FunDepthBlockDepth[AFunDepth] == ABlockDepth)
                AFunDepth--;

            /* Free local variables defined in the current block and update
               state variables related to block nesting. */
            ALeaveBlock();
            break;
        }

        /* Now loop over token untils we reach a newline. Process any
           identifiers and anonymous functions that we meet. */

        while (tok->type != TT_NEWLINE) {
            switch (tok->type) {
            case TT_SUB:
            case TT_DEF:
                /* Anonymous function */
                
                ABlockDepth++;
                /* Check that depth does not overflow. */
                if (AFunDepth < A_MAX_ANON_SUB_DEPTH) {
                    AFunDepth++;
                    FunDepthBlockDepth[AFunDepth] = ABlockDepth;
                    
                    AddFunctionArgumentVariables(tok);            
                    /* tok still points to the start of the function header so
                       that any default argument expressions will be processed
                       next. */
                    break;
                }
                
            case TT_ID: {
                /* Identifier (potentially a reference to an exposed local
                   variable) */
                ASymbolInfo *sym;
                
                sym = tok->info.sym->info;
                
                /* Process all references to exposed local variables. */
                if (AIsLocalId(sym->type) && IsDefinedBelowCurrentDef(sym)) {
                    /* An identifier after :: or . and before :: does not
                       refer to a local variable. */
                    if (prev != TT_SCOPEOP && prev != TT_DOT
                        && AAdvanceTok(tok)->type != TT_SCOPEOP) {
                        /* Mark the original token at variable definition
                           exposed. */
                        VariableDefTokens[sym->num]->type = TT_ID_EXPOSED;
                    }
                }
                
                break;
            }
            }

            /* Advance to the next token. */
            prev = tok->type;
            tok = AAdvanceTok(tok);            
        }

        /* Skip newline. */
        prev = tok->type;
        tok = AAdvanceTok(tok);
    }

    /* Free any variables that might be left around if the function has invalid
       syntax. */
    while (ABlockDepth > 0)
        ALeaveBlock();

    AFunDepth = 0;
}


/* If tok points to a "for" token, clear any annotations for loop variables
   (for that for loop only; ignore any nested loops). */
static void ClearForLoopAnnotations(AToken *tok)
{
    if (tok->type == TT_FOR) {
        tok = AAdvanceTok(tok);
        while (tok->type == TT_ID) {
            tok = AAdvanceTok(tok);
            tok = AClearTypeAnnotationUntilSeparator(tok);
            if (tok->type != TT_COMMA)
                break;
            tok = AAdvanceTok(tok);
        }
    }
}
