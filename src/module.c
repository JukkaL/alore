/* module.c - Managing modules

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "module.h"
#include "parse.h"
#include "class.h"
#include "ident.h"
#include "memberid.h"
#include "mem.h"
#include "gc.h"
#include "dynaload.h"
#include "internal.h"
#include "std_module.h"
#include "debug_runtime.h"
#include "error.h"
#include "util.h"

#include <stdio.h>


#define INITIAL_CMODULES_SIZE 32


static AModuleDef **CModules;
static int CModulesSize;
static int NumCModules;


static ABool RealizeFirstPass(AModuleDef *def, ASymbolInfo *module);
static ABool RealizeSecondPass(AModuleDef *def, ASymbolInfo *module);
static ABool AddCModule(AModuleDef *mod, int *num);
static ABool CreateCFunction(AModuleDef *fnDef, int *num);
static ABool CreateCMethod(ASymbolInfo *class_, AModuleDef *fnDef,
                           unsigned *num);
static ABool CreateCFunctionObject(unsigned num, ASymbolInfo *sym,
                                   AModuleDef *fnDef);
static AModuleDef *CreateCType(AModuleDef *clDef,
                               AUnresolvedNameList *imports);
static AModuleDef *ProcessAndStoreSupertypes(AModuleDef *clDef,
                                             ATypeInfo *type,
                                             AUnresolvedNameList *imports);
static ABool ProcessConstructor(AModuleDef *def, ATypeInfo *type);
static AModuleDef *AddCTypeDefs(AModuleDef *def, ASymbolInfo *symInfo,
                                ATypeInfo *type);
static AModuleDef *FinishCType(AModuleDef *clDef);
static AModuleDef *FinishCTypeDefs(AModuleDef *def, ATypeInfo *type);
static ABool AddMethod(ASymbolInfo *class_, AModuleDef *method,
                       AMemberTableType tableType, unsigned flag);
static ABool AddMemberVar(ATypeInfo *type, const char *name, ABool isWritable,
                          ABool isPrivate);
static ABool ImportModuleUsingName(const char *name);
static ABool HasFinalizerMethod(AModuleDef *def);


#define ModDefName(def) (*(def)->str == '-' ? (def)->str + 1 : (def)->str)
#define IsPrivateModDef(def) (*(def)->str == '-')


ABool AInitializeCModules(void)
{
    CModulesSize = INITIAL_CMODULES_SIZE;

    CModules = AAllocStatic(sizeof(AModuleDef *) * CModulesSize);
    if (CModules == NULL)
        return FALSE;

    NumCModules = 0;

    return TRUE;
}


static AToken *ParseModuleName(AToken *tok, AModuleId *mod)
{
    mod->numParts = 0;

    /* Parse components of module name separated by quotes. */
    for (;;) {
        mod->id[mod->numParts] = (ASymbolInfo *)tok->info.sym;
        mod->numParts++;
        tok = AAdvanceTok(tok);
        if (tok->type != TT_SCOPEOP || (tok + 1)->type != TT_ID)
            break;
        tok = AAdvanceTok(tok);
    }

    return tok;
}


/* Evaluate whether the module whose identifier starts at tok has been
   compiled. Store the module id symbols at *mod. Create empty module id
   symbols for some module name parts as needed. */
AToken *AFindModule(AToken *tok, AModuleId *mod, ABool *isCompiled)
{
    /* IDEA: Document the interface and refactor a bit. */
    int i;

    /* tok->type is assumed to be TT_ID. */

    tok = ParseModuleName(tok, mod);

    i = 0;

    /* Find out whether some or all of the module id parts have already
       been generated. */
    {
        ASymbolInfo *sym = NULL;

        /* Find the last existing part of module id. */
        for (i = 0; i < mod->numParts; i++) {
            /* Is there a module symbol whose module == sym? */
            ASymbolInfo *nextSym = mod->id[i]->next;
            while (AIsModuleId(nextSym->type)
                   || nextSym->type == ID_MEMBER) {
                if (AIsModuleId(nextSym->type) && nextSym->sym == sym) {
                    mod->id[i] = nextSym;
                    goto FoundPart;
                }
                nextSym = nextSym->next;
            }

            /* Exit the loop because there wasn't a match. */
            break;

          FoundPart:

            sym = nextSym;
        }

        /* Now i is the number of found parts. In particular, if
           i == mod.numParts, every part of the module id could be found and
           the module exists unless the last part is empty. */
    }

    /* Do we have to compile the module? */
    if (i != mod->numParts
        || mod->id[i - 1]->type == ID_GLOBAL_MODULE_EMPTY
        || mod->id[i - 1]->type == ID_GLOBAL_MODULE_SUB_EMPTY) {
        /* The module hasn't been compiled yet. i points to the point
           at which we have to start generating new module symbols. */

        for (; i < mod->numParts; i++) {
            /* Generate an empty module. */
            ASymbolInfo *sym = AAddIdentifier((ASymbol *)mod->id[i],
                                            (i == 0) ? ID_GLOBAL_MODULE
                                                     : ID_GLOBAL_MODULE_SUB);
            if (sym != NULL) {
                sym->sym  = (i == 0) ? NULL : mod->id[i - 1];
                sym->type = (i == 0) ? ID_GLOBAL_MODULE_EMPTY
                                     : ID_GLOBAL_MODULE_SUB_EMPTY;
                sym->info.module.isActive   = FALSE;
                sym->info.module.isImported = FALSE;
                sym->info.module.cModule    = A_CM_NON_C;
                mod->id[i] = sym;
            } else
                return NULL;
        }

        /* Mark the last part of module id as compiled. */
        mod->id[i - 1]->type = (i == 1) ? ID_GLOBAL_MODULE
                                        : ID_GLOBAL_MODULE_SUB;

        *isCompiled = FALSE;
    } else
        *isCompiled =
            mod->id[mod->numParts - 1]->info.module.cModule == A_CM_NON_C;

    return tok;
}


/* Create a C module. */
ABool ACreateModule(AModuleDef *module, ABool existsAlready)
{
    AToken *tok;
    ABool isCompiled;
    AModuleId modId;
    int num;

    if (module->type != MD_MODULE_NAME)
        return FALSE;

    if (!ATokenizeStr(module->str, &tok))
        return FALSE;

    module++;

    if (!AddCModule(module, &num))
        return FALSE;

    /* Create the symbols for the module name parts. */
    if (!AFindModule(tok, &modId, &isCompiled)
        || (!existsAlready && isCompiled)) {
        NumCModules--;
        return FALSE;
    }

    /* Mark the module as a C module with a specific index. */
    modId.id[modId.numParts - 1]->info.module.cModule = num | 1024;

    return TRUE;
}


/* Create the contents of a C module (the symbol table + global variables).
   NOTE: Compilation must be active. */
ABool ARealizeModule(ASymbolInfo *module)
{
    AModuleDef *modDef;
    int firstVar;
    int firstConst;
    ABool status;

    ADebugCompilerMsg(("Realize '%i'", module));

    modDef = CModules[module->info.module.cModule & ~1024];

    if (AIsDynamicCompile && !AAllocateModuleGlobals(&firstVar, &firstConst))
        return FALSE;

    if (!RealizeFirstPass(modDef, module))
        return FALSE;

    /* FIX: What if EndDynamicModuleCompilation fails? */

    if (AIsDynamicCompile && !AEndDynamicModuleCompilation(firstVar,
                                                           firstConst))
        return FALSE;

    /* Realize/compile any other modules needed by the current module. */
    for (; modDef->type == MD_IMPORT; modDef++) {
        if (!ImportModuleUsingName(modDef->str))
            return FALSE;
    }

    ADebugCompilerMsg(("Realize 2nd pass"));

    status = RealizeSecondPass(modDef, module);

    ADebugCompilerMsg(("End realize"));

    return status;
}


/* First pass of realizing a C module; roughly corresponds to scanning of an
   Alore source file. */
static ABool RealizeFirstPass(AModuleDef *modDef, ASymbolInfo *module)
{
    int num;
    AUnresolvedNameList *imports = NULL;

    if (AIsDynamicCompile)
        ABeginDynamicModuleCompilation();

    ACurModule = module;

    for (; modDef->type == MD_IMPORT; modDef++)
        imports = AAddUnresolvedNameStr(imports, modDef->str);

    for (; modDef->type != MD_END_MODULE; modDef++) {
        switch (modDef->type) {
        case MD_DEF:
            if (!CreateCFunction(modDef, &num)) {
                AReportModuleError(module, "Could not create %s", modDef->str);
                goto Fail;
            }

            /* FIX: Handle error? */
            if (strcmp(modDef->str, "Main") == 0
                || strcmp(modDef->str, "-Main") == 0)
                AInitFunctions = AAddIntListEnd(AInitFunctions, num);

            if (modDef->numPtr != NULL)
                *modDef->numPtr = num;
            break;

        case MD_VAR:
        case MD_CONST:
        case MD_EMPTY_CONST: {
            AIdType type;
            ASymbol *sym;
            ASymbolInfo *symInfo;
            const char *str = ModDefName(modDef);
            ABool isPrivate = IsPrivateModDef(modDef);

            if (!AGetSymbol(str, strlen(str), &sym))
                goto Fail;

            type = modDef->type == MD_VAR ? ID_GLOBAL : ID_GLOBAL_CONST;
            symInfo = AAddGlobalVariable(sym, module, type, isPrivate);
            if (symInfo == NULL)
                goto Fail;

            /* FIX: check return value of both CreateConstant and SetConst.. */
            if (modDef->type == MD_CONST)
                ASetConstGlobalValue(ACompilerThread, symInfo->num,
                                     ACreateConstant(symInfo));

            if (modDef->numPtr != NULL)
                *modDef->numPtr = symInfo->num;
            break;
        }

        case MD_CLASS:
        case MD_INTERFACE: {
            AModuleDef *orig = modDef;
            modDef = CreateCType(modDef, imports);
            if (modDef == NULL) {
                AReportModuleError(module, "Could not create %s", orig->str);
                goto Fail;
            }
            break;
        }

        default:
            AReportModuleError(module, "Invalid definition type %d",
                              modDef->type);
            break;
        }
    }

    /* Mark the C module as active. Note that it is fully initialized only
       after all the realization passes have been finished. */
    module->info.module.cModule = A_CM_ACTIVE;

    AFreeUnresolvedNameList(imports);
    return TRUE;

  Fail:

    AFreeUnresolvedNameList(imports);
    return FALSE;
}


static ABool RealizeSecondPass(AModuleDef *modDef, ASymbolInfo *module)
{
    ACurModule = module;

    for (; modDef->type != MD_END_MODULE; modDef++) {
        switch (modDef->type) {
        case MD_DEF:
        case MD_VAR:
        case MD_CONST:
        case MD_EMPTY_CONST:
            /* Nothing to do. */
            break;

        case MD_CLASS:
        case MD_INTERFACE:
            modDef = FinishCType(modDef);
            if (modDef == NULL)
                return FALSE;
            break;
        }
    }

    return TRUE;
}


#define MinArgs(def) ((def)->num1)
#define MaxArgs(def) ((def)->num2)
#define ANumLocals(def) ((def)->num3)


/* FIX: what if something fails? what are we going to do? */
static ABool CreateCFunction(AModuleDef *fnDef, int *num)
{
    ASymbol *sym;
    ASymbolInfo *symInfo;
    const char *str;
    ABool isPrivate;

    str = ModDefName(fnDef);
    isPrivate = IsPrivateModDef(fnDef);

    /* The Main function is always private. */
    if (strcmp(str, "Main") == 0)
        isPrivate = TRUE;

    if (!AGetSymbol(str, strlen(str), &sym))
        return FALSE;

    symInfo = AAddGlobalVariable(sym, ACurModule, ID_GLOBAL_DEF, isPrivate);
    if (symInfo == NULL)
        return FALSE;

    if (num != NULL)
        *num = symInfo->num;

    symInfo->info.global.minArgs = MinArgs(fnDef);
    symInfo->info.global.maxArgs = MaxArgs(fnDef);

    return CreateCFunctionObject(symInfo->num, symInfo, fnDef);
}


static ABool CreateCMethod(ASymbolInfo *class_, AModuleDef *fnDef,
                           unsigned *num)
{
    *num = AAddConstGlobalValue(AZero);
    return CreateCFunctionObject(*num, class_, fnDef);
}


static ABool CreateCFunctionObject(unsigned num, ASymbolInfo *sym,
                                   AModuleDef *fnDef)
{
    AFunction *func;

    func = AAllocUnmovable(sizeof(AFunction));
    if (func == NULL)
        return FALSE;

    AInitNonPointerBlockOld(&func->header, sizeof(AFunction) -
                           sizeof(AValue) - 1);

    func->sym = sym;
    func->minArgs = MinArgs(fnDef);
    func->maxArgs = MaxArgs(fnDef);
    func->stackFrameSize = (3 + ANumLocals(fnDef)) * sizeof(AValue);
    func->codeLen = 0; /* Dummy initializer. */
#ifdef HAVE_JIT_COMPILER
    func->num = num;
#endif
    func->code.cfunc = fnDef->func;

    return ASetConstGlobalValue(ACompilerThread, num, AFunctionToValue(func));
}


/* FIX: what about errors? */
static AModuleDef *CreateCType(AModuleDef *clDef, AUnresolvedNameList *imports)
{
    ASymbol *sym;
    ASymbolInfo *symInfo;
    ATypeInfo *type;
    const char *str;
    ABool isPrivate;
    ABool isInterface;
    AModuleDef *def;

    ADebugCompilerMsg(("Create C type '%s'", clDef->str));
    ADebugVerifyMemory();

    str = ModDefName(clDef);
    isPrivate = IsPrivateModDef(clDef);
    isInterface = clDef->type == MD_INTERFACE;

    if (!AGetSymbol(str, strlen(str), &sym))
        return NULL;

    symInfo = AAddGlobalVariable(sym, ACurModule,
                                 isInterface ? ID_GLOBAL_INTERFACE :
                                 ID_GLOBAL_CLASS, isPrivate);
    if (symInfo == NULL)
        return NULL;

    type = ACreateTypeInfo(ACompilerThread, symInfo, isInterface);
    if (type == NULL)
        return NULL;

    type->numVars = clDef->num1;

    /* Find supertype. */
    def = ProcessAndStoreSupertypes(clDef, type, imports);
    if (def == NULL)
        return NULL;

    if (!isInterface) {
        /* Find constructor. */
        if (!ProcessConstructor(def, type)) {
            AReportModuleError(ACurModule,
                               "Could not create constructor of %i",
                               symInfo);
            return FALSE;
        }

        /* Check if there is a #f method. If it is present, allocate member
           slot 0 for the finalize object list and setup some flags. */
        if (HasFinalizerMethod(clDef)) {
            /* Found it. Allocate a slot. Logically, the slot 0 is
               allocated for the list, but we do not need to care about it
               at this point. */
            type->hasFinalizer = TRUE;
            type->hasFinalizerOrData = TRUE;
            type->numVars++;
        }
    }

    def = AddCTypeDefs(def, symInfo, type);
    if (def == NULL)
        return NULL;

    ABuildTypeInfoMembers(type);

    if (clDef->numPtr != NULL)
        *clDef->numPtr = symInfo->num;

    /* The type is ready to be used, unless we ran out of memory at some
       point. */
    type->isValid = !AIsOutOfMemoryError;

    ADebugVerifyMemory();
    ADebugCompilerMsg(("End type"));

    return def;
}


static AModuleDef *ProcessAndStoreSupertypes(AModuleDef *clDef,
                                             ATypeInfo *type,
                                             AUnresolvedNameList *imports)
{
    AUnresolvedNameList *super;
    AUnresolvedNameList *interfaces;
    int numInterfaces;
    AModuleDef *def;

    def = clDef + 1;

    if (clDef->numPtr == &AStdObjectNum)
        super = NULL; /* std::Object has no superclass. */
    else {
        /* Default to inheriting from Object. */
        if (def->type != MD_INHERIT && !type->isInterface)
            super = AAddUnresolvedNameStr(NULL, "std::Object");
        else if (def->type != MD_INHERIT && type->isInterface)
            super = NULL;
        else {
            super = AAddUnresolvedNameStr(NULL, def->str);
            def++;
        }
    }

    /* Find implemented interfaces. */
    interfaces = NULL;
    numInterfaces = 0;
    for (; def->type == MD_IMPLEMENT; def++) {
        interfaces = AAddUnresolvedNameStr(interfaces, def->str);
        numInterfaces++;
    }

    if (super != NULL || interfaces != NULL) {
        AStoreInheritanceInfo(type, imports, super, interfaces);
        /* FIX: check return value */

        if (numInterfaces > 0) {
            if (!AAllocateInterfacesInTypeInfo(type, numInterfaces))
                return NULL;
        }
    }

    return def;
}


static ABool ProcessConstructor(AModuleDef *def, ATypeInfo *type)
{
    AModuleDef *create = def;
    ASymbolInfo *symInfo = type->sym;

    while (create->type != MD_END_TYPE &&
           (create->type != MD_METHOD ||
            strcmp(create->str, "create") != 0))
        create++;

    if (create->type != MD_END_TYPE) {
        /* Explicit create; update argument counts. */
        symInfo->info.global.minArgs = create->num1 - 1;
        symInfo->info.global.maxArgs = create->num2 - 1;

        if (!CreateCMethod(symInfo, create, &type->create))
            return FALSE;
        AAddMember(MT_METHOD_PUBLIC, AM_CREATE, type->create);
    } else {
        /* Inherited create; we do not know the number of arguments the
           class constructor takes yet. */
        symInfo->info.global.minArgs = A_UNKNOWN_MIN_ARGS;
        symInfo->info.global.maxArgs = 0;
    }

    return TRUE;
}


static AModuleDef *AddCTypeDefs(AModuleDef *def, ASymbolInfo *symInfo,
                                ATypeInfo *type)
{
    for (; def->type != MD_END_TYPE; def++) {
        const char *str = ModDefName(def);

        if (IsPrivateModDef(def) && def->type != MD_VAR)
            return NULL;

        switch (def->type) {
        case MD_METHOD:
            if (strcmp(def->str, "create") != 0) {
                if (!AddMethod(symInfo, def, MT_METHOD_PUBLIC, 0))
                    return NULL;
            }
            break;

        case MD_GETTER:
            if (!AddMethod(symInfo, def, MT_VAR_GET_PUBLIC, A_VAR_METHOD))
                return NULL;
            break;

        case MD_SETTER:
            if (!AddMethod(symInfo, def, MT_VAR_SET_PUBLIC, A_VAR_METHOD))
                return NULL;
            break;

        case MD_VAR:
            if (!AddMemberVar(type, str, TRUE, IsPrivateModDef(def)))
                return NULL;
            break;

        case MD_EMPTY_CONST:
            if (!AddMemberVar(type, def->str, FALSE, FALSE))
                return NULL;
            break;

        case MD_BINARY_DATA:
            type->dataSize += def->num1;
            break;

        case MD_EXTERNAL_DATA:
            if (!AddMemberVar(type, "#ext", TRUE, TRUE))
                return NULL;
            break;

        case MD_IMPLEMENT:
            AReportModuleError(ACurModule,
                         "A_IMPLEMENT must appear before other declarations");
            return NULL;

        default:
            AReportModuleError(ACurModule, "Invalid definition type %d",
                               def->type);
            return NULL;
        }
    }

    return def;
}


static AModuleDef *FinishCType(AModuleDef *clDef)
{
    ASymbol *sym;
    ASymbolInfo *symInfo;
    ATypeInfo *type;
    const char *str;
    AModuleDef *def;

    /* Look up the symbol of the type. */
    str = ModDefName(clDef);
    if (!AGetSymbol(str, strlen(str), &sym))
        return NULL;
    symInfo = sym->info;
    while (symInfo->sym != ACurModule)
        symInfo = symInfo->next;

    /* Look up the TypeInfo. */
    type = AValueToType(AGlobalByNum(symInfo->num));

    /* Calculate the number of arguments in case the haven't been defined
       previously (as a side effect). */
    AGetMinArgs(symInfo);

    /* Initialize the constructor of the type if it wasn't defined
       previously. */
    if (!type->isInterface && type->create == 0) {
        ATypeInfo *sup = ASuperType(type);
        while (sup->create == 0)
            sup = ASuperType(sup);
        type->create = sup->create;
    }

    AUpdateInheritedMiscTypeData(type);
    AUpdateTypeTotalNumVars(type);

    /* Now type->totalNumVars do not include any non-inherited slots. We
       need to add them in the same order as during the first pass. */

    /* Account for private slots and finalizer slot. */
    type->totalNumVars += clDef->num1;
    if (HasFinalizerMethod(clDef))
        type->totalNumVars++;

    /* Account for member variables. */
    def = FinishCTypeDefs(clDef, type);

    /* Figure binary data offsets. */
    type->dataSize = 0;
    for (def = clDef; def->type != MD_END_TYPE; def++) {
        if (def->type == MD_BINARY_DATA) {
            if (def->numPtr != NULL) {
                int offset = 0;
                ATypeInfo *super = ASuperType(type);
                while (super != NULL) {
                    offset += super->dataSize;
                    super = ASuperType(super);
                }
                *def->numPtr = offset + type->dataSize;
            }
            type->dataSize += def->num1;
            break;
        }
    }

    ACalculateInstanceSize(type);

    return def;
}


static AModuleDef *FinishCTypeDefs(AModuleDef *def, ATypeInfo *type)
{
    for (; def->type != MD_END_TYPE; def++) {
        switch (def->type) {
        case MD_VAR:
        case MD_EMPTY_CONST:
            if (def->numPtr != NULL)
                *def->numPtr = type->totalNumVars;
            type->totalNumVars++;
            break;

        case MD_EXTERNAL_DATA:
            type->extDataMember = type->totalNumVars++;
            break;
        }
    }
    return def;
}


static ABool AddMethod(ASymbolInfo *class_, AModuleDef *method,
                       AMemberTableType tableType, unsigned flag)
{
    /* FIX: error checking sucks */

    ASymbol *sym;
    ASymbolInfo *symInfo;
    unsigned memberNum;
    unsigned globalNum;

    if (method->str[0] != '#') {
        if (!AGetSymbol(method->str, strlen(method->str),
                       &sym))
            return FALSE;

        symInfo = AGetMemberSymbol(sym);
        if (symInfo == NULL)
            return FALSE;

        memberNum = symInfo->num;
    } else {
        if (method->str[1] == 'i')
            memberNum = AM_INITIALIZER;
        else
            memberNum = AM_FINALIZER;
    }

    if (!CreateCMethod(class_, method, &globalNum))
        return FALSE;

    AAddMember(tableType, memberNum, globalNum | flag);

    return TRUE;
}


static ABool AddMemberVar(ATypeInfo *type, const char *name, ABool isWritable,
                          ABool isPrivate)
{
    /* FIX: out of memory? */
    ASymbol *sym;
    ASymbolInfo *symInfo;
    unsigned key;
    unsigned item;

    if (!AGetSymbol(name, strlen(name), &sym))
        return FALSE;

    symInfo = AGetMemberSymbol(sym);
    if (symInfo == NULL)
        return FALSE;

    key = symInfo->num;
    item = type->numVars++;

    AAddMember(isPrivate ? MT_VAR_GET_PRIVATE : MT_VAR_GET_PUBLIC, key, item);
    if (isWritable)
        AAddMember(isPrivate ? MT_VAR_SET_PRIVATE : MT_VAR_SET_PUBLIC, key,
                   item);

    return TRUE;
}


/* Store a C module in the global module list. Store the index of the module
   in *num. Return FALSE if out of memory. */
static ABool AddCModule(AModuleDef *mod, int *num)
{
    if (NumCModules == CModulesSize) {
        CModulesSize *= 2;
        CModules = AGrowStatic(CModules, sizeof(AModuleDef *) * CModulesSize);
        if (CModules == NULL)
            return FALSE;
    }

    *num = NumCModules;
    CModules[NumCModules] = mod;

    NumCModules++;

    return TRUE;
}


AValue AEmptyCreate(AThread *t, AValue *args)
{
    return args[0];
}


AValue AEmptyMain(AThread *t, AValue *args)
{
    return ANil;
}


ABool ACreateAndInitializeModule(char *name, AModuleDef *def)
{
    if (!ACreateModule(def, FALSE))
        return FALSE;
    return AInitializeModule(name, FALSE);
}


ABool AInitializeModule(char *name, ABool isAutoImport)
{
    AToken *tok;
    ABool isCompiled;
    AModuleId modId;
    ASymbolInfo *sym;

    if (!ATokenizeStr(name, &tok))
        return FALSE;

    if (!AFindModule(tok, &modId, &isCompiled) || isCompiled)
        return FALSE;

    sym = modId.id[modId.numParts - 1];

    if (isAutoImport) {
        ASymbolInfo *s = sym;
        sym->info.module.isImported = TRUE;
        do {
            s->info.module.isActive = TRUE;
            s = s->sym;
        } while (s != NULL);
    }

    if (!ARealizeModule(sym))
        return FALSE;

    if (isAutoImport)
        sym->info.module.cModule = A_CM_AUTO_IMPORT;

    return TRUE;
}


static ABool ImportModuleUsingName(const char *name)
{
    AToken *tok;
    if (!ATokenizeStr(name, &tok))
        return FALSE;
    return AImportModule(tok, TRUE) != NULL;
}


AToken *AImportModule(AToken *tok, ABool isFromC)
{
    AModuleId impMod;
    ABool isCompiled;
    ASymbolInfo *sym;

    tok = AFindModule(tok, &impMod, &isCompiled);
    if (tok == NULL)
        return NULL;

    sym = impMod.id[impMod.numParts - 1];

    if (isCompiled)
        goto SkipUntilNewlineOrComma;

    if (!isFromC) {
        /* Record the line number of the import statement so that
           it is available to be displayed in error messages. */
        ALineNums[ANumActiveFiles - 1] = tok->lineNumber;
    }

    if (sym->info.module.cModule == A_CM_NON_C) {
        /* Try to compile the module. */
        if (!ACompileModule(&impMod))
            return NULL;
    } else if (sym->info.module.cModule >= 0) {
        /* Non-realized C module must be realized. */
        if (!ARealizeModule(sym))
            return NULL;
    }

    if (tok->type != TT_NEWLINE && tok->type != TT_COMMA)
        goto SkipUntilNewlineOrComma;

    return tok;

  SkipUntilNewlineOrComma:

    while (tok->type != TT_NEWLINE && tok->type != TT_COMMA)
        tok = AAdvanceTok(tok);

    return tok;
}


static ABool HasFinalizerMethod(AModuleDef *def)
{
    for (def++; def->type != MD_END_TYPE; def++) {
        if (def->type == MD_METHOD && strcmp(def->str, "#f") == 0)
            return TRUE;
    }
    return FALSE;
}


/* Report an error in a C module. These are all considered internal errors and
   are reported to help debugging. The arguments are similar to
   AFormatMessage(). */
void AReportModuleError(ASymbolInfo *module, const char *format, ...)
{
    char buf[1024], buf2[1024];
    va_list args;

    va_start(args, format);
    AFormatMessageVa(buf, sizeof(buf), format, args);
    va_end(args);

    AFormatMessage(buf2, sizeof(buf2),
                   "Error initializing module \"%q\":\n    %s",
                   module, buf);

    fprintf(stderr, "%s\n", buf2);
}
