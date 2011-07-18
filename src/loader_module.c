/* loader_module.c - loader module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This is the implementation of the loader module. It allows dynamically
   compiling and executing code. */

#include "alore.h"
#include "compile.h"
#include "mem.h"
#include "gc.h"
#include "str.h"
#include "array.h"
#include "error.h"
#include "std_module.h"
#include "dynaload.h"
#include "internal.h"
#include "runtime.h"
#include "debug_runtime.h"


static AValue CreateCompileErrorList(AThread *t, AValue *frame);
static ABool IncrementErrorCount(const char *msg, void *data);
static ABool StoreError(const char *msg, void *data);
static int GetGlobalVariableNum(AThread *t, ASymbolInfo *mod,
                                AValue varName, ABool acceptConst);
static ASymbolInfo *GetModuleSymbol(AValue mod);


static int ModuleClassNum;
static int CompileErrorClassNum;
static int ModuleSearchPathNum;


#define NUM_MODULE_IVARS 2


/* Error codes for GetGlobalVariableNum */
#define GLOBAL_MISSING -1
#define GLOBAL_CONSTANT -2
#define GLOBAL_ERROR -3


typedef struct {
    AThread *t;
    AValue *arrayPtr;
    AValue *tempPtr;
    int index;
} MessageInfo;


/* Indices to the array that holds Alore values needed during dynamic
   compilation. */
enum {
    PATH,               /* Path to file */
    SEARCH_PATH,        /* Module search path */
    TMP,                /* Temporary variable */
    TMP2,               /* Temporary variable (for CreateCompileErrorList) */
};


/* IDEA: The module search path separator should be defined somewhere. */

/* IDEA: Add ability to specify custom file access operations for compilation,
         for example by adding additional optional parameters to Load and
         LoadModule functions: file open function and module contents listing
         function. Additionally, it might be useful to be able to specify
         different module to be loaded from the one the source file specifies,
         i.e. renaming of modules, to allow restricted execution etc. */


/* Dynamically load an Alore source file. Return a Module object or raise
   CompileError. Known as loader::Load. */
static AValue LoaderLoad(AThread *t, AValue *frame)
{
    return ALoaderLoadInternal(t, frame, TRUE);
}


AValue ALoaderLoadInternal(AThread *t, AValue *frame, ABool run)
{
    /* FIX: If CreateCompileErrorList or similar runs out of memory, a direct
       exception is raised and AllowOldGenGc etc. might not be called. */
    
    /* IDEA: Make it possible to pass command line arguments to dynamically
       loaded programs. */
    
    char *path;
    int moduleNum;
    int funcNum;
    AValue func;
    AInstance *inst;
    char *searchPath;
    AFileInterface fileIface;
    AValue argsArray;

    /* Remove command line arguments.
       FIX: Add synchronization */
    argsArray = AMakeArray(t, 0);
    ASetConstGlobalByNum(t, GL_COMMAND_LINE_ARGS, argsArray);

    if (!ADisallowOldGenGC())
        return ARaiseMemoryErrorND(t);

    AFreezeOtherThreads();
    
    /* Setup the file interface. */
    AGetDefaultFileInterface(&fileIface);
    ASetupFileInterface(&fileIface);

    path = AAllocCStrFromString(t, frame[PATH], NULL, 0);
    if (path == NULL) {
        AWakeOtherThreads();
        AAllowOldGenGC();
        return AError;
    }

    /* If the module search path is not specified, it will be empty. */
    if (frame[SEARCH_PATH] == ADefault) {
        frame[SEARCH_PATH] = ACreateStringFromCStr(t, "");
        if (frame[SEARCH_PATH] == AError) {
            AWakeOtherThreads();
            AAllowOldGenGC();
            return AError;
        }
    }
    
    searchPath = AAllocCStrFromString(t, frame[SEARCH_PATH], NULL, 0);
    if (searchPath == NULL) {
        AFreeCStrFromString(path);
        AWakeOtherThreads();
        AAllowOldGenGC();
        return AError;
    }

    /* Compile the file and any modules imported by it. */
    if (!ACompileFile(t, path, searchPath, &funcNum)) {
        AValue list;
        AValue exception;

        /* There was a compiler error. */
        
        list = CreateCompileErrorList(t, frame + 2);
        if (AIsError(list)) {
            AWakeOtherThreads();
            goto Failure;
        }

        AWakeOtherThreads();
        
        /* Construct a CompileError object containg the error list and raise
           the object. */
        frame[2] = list;
        exception = ACallValue(t, AGlobalByNum(CompileErrorClassNum),
                               1, frame + 2);
        if (!AIsError(exception))
            ARaiseExceptionValue(t, exception);
        
        goto Failure;
    }

    /* FirstDynamicModule has the location of the main module of the just
       compiled program (in the global variable array). */
    moduleNum = AFirstDynamicModule;

    ADebugStatusMsg(("Compiled dynamic module %d\n", moduleNum));

    AWakeOtherThreads();

    /* Call the initialization function of the compiled file. */
    func = AGlobalByNum(funcNum);

    /* Store a reference to the function so that the module will not be
       garbage collected. */
    frame[TMP] = func;

    AAllowOldGenGC();

    if (run) {
        if (AIsError(ACallValue(t, func, 0, NULL)))
            goto Failure2;
        
        /* Allocate a Module object. */
        /* IDEA: Use a function/macro for creating an instance. */
        inst = AAlloc(t, (1 + NUM_MODULE_IVARS) * sizeof(AValue));
        if (inst == NULL)
            goto Failure2;
        
        /* Initialize the object. */
        AInitInstanceBlock(&inst->type,
                           AValueToType(AGlobalByNum(ModuleClassNum)));
        inst->member[0] = AIntToValue(moduleNum);
        /* Keep a reference to the module contents. Otherwise the module might
           be freed by the garbage collector. */
        inst->member[1] = frame[TMP];
        
        ADebugVerifyMemory();

        AFreeCStrFromString(path);
        AFreeCStrFromString(searchPath);

        /* Return the Module object. */
        return AInstanceToValue(inst);
    } else {
        AFreeCStrFromString(path);
        AFreeCStrFromString(searchPath);
        return ANil;
    }

  Failure:

    /* FIX: when creating traceback array, is it possible for the module to be
            freed? */

    AAllowOldGenGC();

  Failure2:
    
    AFreeCStrFromString(path);
    AFreeCStrFromString(searchPath);

    ADebugVerifyMemory();

    return AError;
}


/* Create an array of compile errors. */
static AValue CreateCompileErrorList(AThread *t, AValue *frame)
{
    MessageInfo info;
    int num;
    AValue tmp;

    /* Figure out the number of error messages. */
    num = 0;
    if (!ADisplayErrorMessages(IncrementErrorCount, &num))
        return ARaiseMemoryErrorND(t);

    /* Create an empty array for the error messages. */
    tmp = AMakeArray(t, num);
    if (AIsError(tmp))
        return AError;
    frame[0] = tmp;

    /* Initialize MessageInfo structure. */
    info.t = t;
    info.arrayPtr = frame;
    info.tempPtr = frame + 1;
    info.index = 0;

    /* Store the error messages to the array. */
    if (!ADisplayErrorMessages(StoreError, &info))
        return ARaiseMemoryErrorND(t);

    return frame[0];
}


/* Increment the integer pointed to by the data argument. Callback function
   for DisplayErrorMessages. */
static ABool IncrementErrorCount(const char *msg, void *data)
{
    int *ptr = data;
    (*ptr)++;
    return TRUE;
}


/* Store an error message to an array. Callback function for
   DisplayErrorMessages. */
static ABool StoreError(const char *msg, void *data)
{
    MessageInfo *info;

    info = data;
    *info->tempPtr = ACreateStringFromCStr(info->t, msg);
    if (*info->tempPtr == AError)
        return FALSE;
    
    info->index++;
    return ASetArrayItemND(info->t, *info->arrayPtr, info->index - 1,
                        *info->tempPtr);
}


/* Return the value of a global variable in the module. Raise KeyError if the
   name is not a public global variable in the module. */
static AValue Module_get(AThread *t, AValue *frame)
{    
    int num = GetGlobalVariableNum(t, GetModuleSymbol(frame[0]), frame[1],
                                   TRUE);
    if (num == GLOBAL_MISSING)
        return ARaiseByNum(t, AErrorClassNum[EX_KEY_ERROR],
                           "Undefined identifier");
    else if (num == GLOBAL_ERROR)
        return AError;
    else
        return AGlobalByNum(num);
}


/* Set the value of a global variable in the module. Raise KeyError if the
   name is not a public global variable in the module. */
static AValue Module_set(AThread *t, AValue *frame)
{
    int num = GetGlobalVariableNum(t, GetModuleSymbol(frame[0]), frame[1],
                                   FALSE);
    if (num == GLOBAL_MISSING)
        return ARaiseByNum(t, AErrorClassNum[EX_KEY_ERROR], NULL);
    else if (num == GLOBAL_ERROR)
        return AError;
    else if (num == GLOBAL_CONSTANT)
        return ARaiseByNum(t, AErrorClassNum[EX_KEY_ERROR], NULL);
    else {
        ASetGlobalByNum(num, frame[2]);
        return ANil;
    }
}


/* Get the number of global variable with name varName (string) in module
   represented by mod. Return GLOBAL_MISSING if not found or GLOBAL_CONSTANT
   if the variable is constant and acceptConst is false. */
static int GetGlobalVariableNum(AThread *t, ASymbolInfo *mod,
                                AValue varName, ABool acceptConst)
{
    char buf[128];
    char *id;
    ASymbol *sym;
    ASymbolInfo *info;
    int num;

    id = AAllocCStrFromString(t, varName, buf, sizeof(buf));
    if (id == NULL)
        return GLOBAL_ERROR;
    
    ALockInterpreter();

    num = GLOBAL_MISSING;
    
    /* Try to find the symbol of the variable. */
    sym = AFindSymbol(id, strlen(id));
    if (sym == NULL)
        goto Leave;

    /* IDEA: Maybe the following code should reside somewhere else? */
    
    /* Try to find a variable that is defined in the module. */
    for (info = sym->info; AIsId(info->type); info = info->next) {
        if (AIsGlobalId(info->type)
            && info->sym == mod
            && !info->info.global.isPrivate) {
            if (acceptConst || info->type == ID_GLOBAL)
                num = info->num;
            else
                num = GLOBAL_CONSTANT;
            break;
        }
    }

  Leave:

    AUnlockInterpreter();

    AFreeCStrFromString(id);
    
    return num;
}


/* Return an array of all global identifiers defined in the module. */
static AValue ModuleContents(AThread *t, AValue *frame)
{
    /* IDEA: Store global variables in module information. This should be
             doable without much overhead. Functions, classes and maybe
             symbolic constants are already stored sequentially and their names
             can be figured out. */

    int i;
    int n;
    ASymbolInfo *module;
    
    ALockInterpreter();

    /* Unlock the interpreter lock if a direct exception is raised. */
    if (ATry(t)) {
        AUnlockInterpreter();
        return AError;
    }
    
    module = GetModuleSymbol(frame[0]);

    /* Loop over all symbol table entries and calculate the number of
       identifiers in the module. */
    n = 0;
    for (i = 0; i < ASymSize; i++) {
        ASymbol *sym;
        for (sym = ASym[i]; sym != NULL; sym = sym->next) {
            ASymbolInfo *symInfo;
            for (symInfo = sym->info;
                 symInfo != (ASymbolInfo *)sym;
                 symInfo = symInfo->next) {
                if (AIsGlobalId(symInfo->type) && symInfo->sym == module
                    && !symInfo->info.global.isPrivate)
                    n++;
            }
        }
    }

    /* Create an array for the symbol names. */
    frame[1] = AMakeArray(t, n);
    
    /* Loop over all symbol table entries and collect all of the names in the
       module to the array. */
    n = 0;
    for (i = 0; i < ASymSize; i++) {
        ASymbol *sym;
        for (sym = ASym[i]; sym != NULL; sym = sym->next) {
            ASymbolInfo *symInfo;
            for (symInfo = sym->info;
                 symInfo != (ASymbolInfo *)sym;
                 symInfo = symInfo->next) {
                if (AIsGlobalId(symInfo->type) && symInfo->sym == module
                    && !symInfo->info.global.isPrivate) {
                    /* Create a string object representing the name of the
                       symbol. */
                    frame[2] = ACreateStringFromCStr(t, (char *)sym->str);
                    if (AIsError(frame[2])) {
                        AUnlockInterpreter();
                        return AError;
                    }

                    if (!ASetArrayItemND(t, frame[1], n, frame[2])) {
                        AUnlockInterpreter();
                        return AError;
                    }
                    n++;
                }
            }
        }
    }

    AEndTry(t);

    AUnlockInterpreter();
    
    return frame[1];
}


/* Initialization function of the loader module. */
static AValue LoaderMain(AThread *t, AValue *frame)
{
    AValue path = AMakeStr(t, ADefaultModuleSearchPath);
    ASetConstGlobalByNum(t, ModuleSearchPathNum, path);
    return ANil;
}


/* Constructor for CompileError. */
static AValue CompileErrorCreate(AThread *t, AValue *frame)
{
    if (!AIsArray(frame[1]))
        return AError;
    if (!ASetInstanceMember(t, AValueToInstance(frame[0]),
                            A_NUM_EXCEPTION_MEMBER_VARS, frame + 1))
        return AError;
    return frame[0];
}


/* Get the SymbolInfo structure of a Module instance. */
static ASymbolInfo *GetModuleSymbol(AValue mod)
{
    int num = AValueToInt(AValueToInstance(mod)->member[0]);
    ADynaModule *modInfo = AGetModuleInfo(num);
    return AIntValueToPtr(modInfo->symbol);
}


A_MODULE(loader, "loader")
    A_DEF("Main", 0, 0, LoaderMain)
    A_DEF_OPT("Load", 1, 2, 2, LoaderLoad)
    A_EMPTY_CONST_P("ModuleSearchPath", &ModuleSearchPathNum)

    A_CLASS_PRIV_P("Module", NUM_MODULE_IVARS, &ModuleClassNum)
        A_METHOD("_get", 1, 0, Module_get)
        A_METHOD("_set", 2, 0, Module_set)
        A_METHOD("contents", 0, 2, ModuleContents)
    A_END_CLASS()

    A_CLASS_P("CompileError", &CompileErrorClassNum)
        A_INHERIT("std::Exception")
        A_METHOD("create", 1, 0, CompileErrorCreate)
        A_EMPTY_CONST("errorMessages")
    A_END_CLASS()
A_END_MODULE()
