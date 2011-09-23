/* compile.c - Bytecode compiler high level logic and related functionality

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "compile.h"
#include "runtime.h"
#include "memberid.h"
#include "lex.h"
#include "error.h"
#include "output.h"
#include "symtable.h"
#include "cutil.h"
#include "module.h"
#include "cmodules.h"
#include "files.h"
#include "dynaload.h"
#include "class.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"
#include "debug_runtime.h"
#include "std_module.h"
#include "util.h"

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>


#define INITIAL_INPUT_BLOCK_LENGTH 1024
#define INITIAL_INPUT_BUFFER_LENGTH (1024 + 256)


/* Function pointers to file operations used by the bytecode compiler */
AFileInterface AFileIface;

/* Base module search path. Includes ALOREPATH environment variable,
   user-supplied additional paths (if any) and OS/build-dependent base path. */
char *ADefaultModuleSearchPath;

/* Current module search path. Includes directory of the main source file
   (and potentially additional user-supplied paths) in addition to
   ADefaultModuleSearch. */
char *AModuleSearchPath;

/* Absolute path to the program that is currently being executed. */
char *AProgramPath;

/* Absolute path to the alore interpreter. May be NULL if the program is
   standalone (i.e. compiled to a binary).  */
char *AInterpreterPath;

int ANumActiveFiles;
char *AActiveFiles[A_MAX_COMPILE_DEPTH];
unsigned short ALineNums[A_MAX_COMPILE_DEPTH];

unsigned ACurFileNum;

static unsigned char *InputBuffer;
static unsigned InputBufferLength;
static unsigned InputBlockLength;

ASymbolInfo *ACurMainModule;

ABool AIsDynamicCompile;
ABool AIsStandaloneFlag;

AThread *ACompilerThread;

AUnresolvedSupertype *AUnresolvedSupertypes;

#ifdef HAVE_JIT_COMPILER
ABool AIsJitModule;
#endif


static void DetermineInterpreterPath(ABool isStandalone,
                                     const char *interpreter);
static ABool ReadFile(char *path, AToken **tokPtr);
static ABool InitializeInput(void);
static void DeinitializeInput(void);
static ABool CompileImportedModules(AToken *tok);
static ABool AddFile(AFileList **files, char *path);
static void FailAndExit(const char *msg);
static ABool FindProgram(char *path, const char *src);
static ABool GetRealInterpreterDir(char *path, const char *src);
static ABool MakeAbsolutePath(char *path, const char *src);


#define UNCAUGHT_EXCEPTION_STATUS 3
#define INTERNAL_ERROR_STATUS 4


/* Initialize the VM and compiler and compile program specified by the file
   argument. Return the global num of the function that can be used to run
   the program, or -1 if compilation failed. If there was a compile error,
   display error messages. If there is a serious error, this function may
   forcibly terminate the process using exit(). */
int ALoadAloreProgram(AThread **t, const char *file,
                      const char *interpreter, const char *modulePath,
                      ABool isStandalone, int argc, char **argv,
                      AFileInterface *iface, Asize_t maxHeap)
{
    int num;
    int i;
    char path[A_MAX_PATH_LEN];
    int result;

    if (!AInitializeCompiler(t, modulePath, interpreter, isStandalone,
                             maxHeap))
        FailAndExit("Compiler initialization failed");

    /* Determine program path. */
    if (isStandalone)
        result = FindProgram(path, interpreter);
    else
        result = MakeAbsolutePath(path, file);
    if (!result)
        FailAndExit("Out of memory during initialization");
    AProgramPath = ADupStr(path);
    if (AProgramPath == NULL)
        FailAndExit("Out of memory during initialization");

    /* Determine interpreter path unless running standalone (compiled). */
    DetermineInterpreterPath(isStandalone, interpreter);

    if (iface != NULL)
        ASetupFileInterface(iface);

    /* Populate the symbol table with information about available
       statically-linked C modules. */
    for (i = 0; AAllModules[i] != NULL; i++) {
        if (!ACreateModule(AAllModules[i], FALSE)) {
            FailAndExit("Error in module initialization");
            break;
        }
    }

    /* Old generation garbage collection may interfere with compilation, so
       disable it during compilation. */
    if (!ADisallowOldGenGC())
        FailAndExit("Could not disallow old gen gc");

    if (!ASetupCommandLineArgs(*t, argc, argv))
        FailAndExit("Error setting up arguments");

    if (!ACompileFile(*t, file, "", &num)) {
        ADisplayErrorMessages(ADefDisplay, NULL);
        /* IDEA: Call AAllowOldGenGC()? */
        return -1;
    } else {
        /* Enable old generation garbage collection after compilation. */
        AAllowOldGenGC();
        ADebugVerifyMemory();
        return num;
    }
}


/* Initialize AInterpreterPath to point to the alore interpreter; its value
   will be NULL if running standalone. */
static void DetermineInterpreterPath(ABool isStandalone,
                                     const char *interpreter)
{
    char path[A_MAX_PATH_LEN];
    if (!isStandalone) {
        if (!FindProgram(path, interpreter))
            FailAndExit("Could not determine path to interpreter");
        AInterpreterPath = ADupStr(path);
        if (AInterpreterPath == NULL)
            FailAndExit("Out of memory during initialization");
    } else
        AInterpreterPath = NULL;
}


/* This function is called after running an Alore program. Check the return
   value of an Alore program and display a stack traceback (if needed) and
   call any exit callbacks. The val argument should be the return value from
   the Main function. Return the process exit status for the program
   (0 == no error). */
int AEndAloreProgram(AThread *t, AValue val)
{
    /* IDEA: Check what happens if en exception is raised here. */
    
    int returnValue = 0;

    ADebugVerifyMemory();
    
    if (t->contextIndex != 0) {
        char buf[200];
        sprintf(buf, "Internal error: contextIndex == %d at program exit",
                t->contextIndex);
        ADefDisplay(buf, NULL);
        return INTERNAL_ERROR_STATUS;
    }

    if (ATry(t)) {
        ADefDisplay("alore: Uncaught exception at program exit", NULL);
        return UNCAUGHT_EXCEPTION_STATUS;
    }
    
    if (AIsError(val)) {
        if (AIsOfType(t->exception,
                      AGlobalByNum(AErrorClassNum[EX_EXIT_EXCEPTION]))
            == A_IS_TRUE) {
            /* The ExitException was raised. Fetch the return value and
               don't generate a stack trace. */
            returnValue = AGetInt(t,
                                  AValueToInstance(t->exception)->
                                  member[A_NUM_EXCEPTION_MEMBER_VARS]);
            val = ANil;
        } else {
            if (!ACreateTracebackArray(t)) {
                /* We ran out of memory while creating traceback array.
                   IDEA: Display at least exception name!
                   IDEA: It should be possible display a traceback without
                         allocating memory. */
                AEndTry(t);
                ADefDisplay("Out of memory at program exit", NULL);
                return UNCAUGHT_EXCEPTION_STATUS;
            }

            returnValue = UNCAUGHT_EXCEPTION_STATUS;
        }
    }
    
    if (AIsError(val))
        ADisplayStackTraceback(t, ADefDisplay, NULL);
    
    if (!AExecuteExitHandlers(t))
        ADefDisplay("alore: Uncaught exception raised by an exit handler",
                    NULL);
    
    AEndTry(t);
    
    return returnValue;
}


/* Convert src to an absolute path and store it at the path buffer, if src is
   a relative path. If src is already an absolute path, copy src to path.
   Return FALSE if failed. */
static ABool MakeAbsolutePath(char *path, const char *src)
{
    strcpy(path, src);

    /* Create an absolute path if needed. */
    if (!A_IS_ABS(path)) {
        if (getcwd(path, A_MAX_PATH_LEN - strlen(src) - 7) == NULL)
            return FALSE;
        if (!A_IS_DIR_SEPARATOR(path[strlen(path) - 1]))
            strcat(path, A_DIR_SEPARATOR_STRING);
        if (src[0] == '.' && A_IS_DIR_SEPARATOR(src[1]))
            strcat(path, src + 2);
        else
            strcat(path, src);
    }
    
    return TRUE;
}


/* Look up the absolute path to a program (the src argument). Use the PATH
   environment variable to find the path, if src does not include a directory.
   The result is stored in the buffer pointed to by path. Return FALSE if
   failed. The path buffer should have at least A_MAX_PATH_LEN characters. */
static ABool FindProgram(char *path, const char *src)
{
    int i;

    if (strlen(src) >= A_MAX_PATH_LEN)
        return FALSE;
    
    strcpy(path, "");

    for (i = 0; src[i] != '\0' && !A_IS_DIR_SEPARATOR(src[i]); i++);
    
    if (src[i] == '\0') {
        /* No path is given; we assume that we can find the interpreter
           directory using the PATH environment variable. */
        const char *env = getenv("PATH");
        char *s;
        char comp[A_MAX_PATH_LEN];
        char fp[A_MAX_PATH_LEN];

        if (env == NULL)
            return TRUE;
        
        do {
            s = strchr(env, A_PATH_SEPARATOR);
            i = s == NULL ? strlen(env) : s - env;
            if (i >= A_MAX_PATH_LEN)
                return FALSE;
            strncpy(comp, env, i);
            comp[i] = '\0';
            env = s + 1;
            if (!AJoinPath(fp, comp, src))
                return FALSE;
            if (AIsFile(fp)) {
                strcpy(path, fp);
                return TRUE;
            }
        } while (s != NULL);
        
        return TRUE;
    }

    return MakeAbsolutePath(path, src);
}


/* Return the directory (which may be relative or absolute, but never an empty
   path) of the interpreter. The src argument specifies the interpreter, and
   it can be a path or a program name (which is looked up using the PATH
   environment variable). Store the result in path. Return FALSE if failed.
   The path buffer should have at least A_MAX_PATH_LEN characters. */
static ABool GetRealInterpreterDir(char *path, const char *src)
{
    int i;

    if (!FindProgram(path, src))
        return FALSE;
    
    /* Remove the last component from the path (the program name). */
    i = strlen(path);
    while (i > 0 && !A_IS_DIR_SEPARATOR(path[i - 1]))
        i--;
    path[i] = '\0';

    return TRUE;
}


/* Return library path derived from the interpreter path. Store this path in
   the path argument. Determine if the interpreter is running in a build
   directory or it has been installed, and construct the path accordingly.
   Return FALSE if failed. */
static ABool GetInterpreterLibPath(char *path, const char *interpreter)
{
    if (!GetRealInterpreterDir(path, interpreter))
        return FALSE;

#ifndef A_HAVE_WINDOWS
    /* Non-Windows implementation */
    if (AEndsWith(path, "/") && strlen(path) > 1)
        path[strlen(path) - 1] = '\0';
    if (AEndsWith(path, "/bin")) {
        path[strlen(path) - 3] = '\0';
        if (!AJoinPath(path, path, "lib/alore"))
            strcpy(path, "");
    } else {
        if (!AJoinPath(path, path, "lib"))
            strcpy(path, "");
    }
#else
    /* Windows implementation */
    /* Remove trailing / or \ (unless the path refers to a root directory). */
    if (APathEndsWith(path, "/") && strlen(path) > 1 &&
        (path[1] != ':' || strlen(path) > 3))
        path[strlen(path) - 1] = '\0';
    
    if (APathEndsWith(path, "/src")) {
        path[strlen(path) - 3] = '\0';
        strcat(path, "lib");
    } else {
        if (!AJoinPath(path, path, "lib"))
            strcpy(path, "");
    }
#endif
    
    return TRUE;
}


/* Initialize the compiler and VM. Return FALSE if failed. */
ABool AInitializeCompiler(AThread **t, const char *additModuleSearchPath,
                          const char *interpreter, ABool isStandalone,
                          unsigned long maxHeap)
{
    AFileInterface iface;
    char searchPath[A_MAX_PATH_LEN];

    AGetDefaultFileInterface(&iface);
    ASetupFileInterface(&iface);

#ifdef HAVE_MREMAP
    AMoreHeap = AMoreHeap_mmap;
    AFreeHeapBlock = AFreeHeapBlock_munmap;
    AGrowNursery = AGrowNursery_mremap;
    AFreeNursery = AFreeNursery_munmap;
#elif defined(A_HAVE_VIRTUAL_ALLOC)
    AMoreHeap = AMoreHeap_VirtualAlloc;
    AFreeHeapBlock = AFreeHeapBlock_VirtualAlloc;
    AGrowNursery = AGrowNursery_VirtualAlloc;
    AFreeNursery = AFreeNursery_VirtualAlloc;
#else
    AMoreHeap = AMoreHeap_malloc;
    AFreeHeapBlock = AFreeHeapBlock_free;
    AGrowNursery = AGrowNursery_realloc;
    AFreeNursery = AFreeNursery_free;
#endif

    /* Construct non-default library search path. */
    if (!GetInterpreterLibPath(searchPath, interpreter))
        return FALSE;
    if (strlen(searchPath) + strlen(additModuleSearchPath) + 1 >= PATH_MAX)
        return FALSE;
    if (*additModuleSearchPath != '\0') {
        if (*searchPath != '\0')
            strcat(searchPath, A_PATH_SEPARATOR_STRING);
        strcat(searchPath, additModuleSearchPath);
    }

    if (!AInitializeGarbageCollector(maxHeap))
        return FALSE;

    /* FIX: remove interrupthandler afterwards.. perhaps? */
    if (!ASetKeyboardInterruptHandler())
        return FALSE;

    *t = ACreateMainThread();
    if (*t == NULL)
        goto Fail;

    AMainThread = *t;
    ACompilerThread = *t;

    AInitializeCAlloc();
    
    AInitializeLexicalAnalyzer();

    AInitializeExitHandlers();

    AIsDynamicCompile = FALSE;
    AIsStandaloneFlag = isStandalone;
    
    AInitFunctions = NULL;
    
    if (!AInitializeDefaultModuleSearchPath(searchPath))
        return FALSE;
    
    if (!AInitializeGlobals())
        goto Fail;

    if (!AInitializeClassOutput())
        goto Fail;

    if (!AInitializeSymbolTable())
        goto Fail;

    if (!AInitializeFileList())
        goto Fail;

    if (!AInitializeCModules())
        goto Fail;

    if (!AInitializeHashValueMapping())
        goto Fail;

    if (!ACreateModule(AstdModuleDef, FALSE))
        goto Fail;

    if (!AInitializeModule("std", TRUE))
        goto Fail;

    if (!AInitializeStdTypes())
        goto Fail;

    if (!AInitializeStdExceptions(ACompilerThread))
        goto Fail;

    if (!ACreateAndInitializeModule("encodings", AencodingsModuleDef))
        goto Fail;

    if (!ACreateAndInitializeModule("io", AioModuleDef))
        goto Fail;

    if (!ACreateAndInitializeModule("errno", AerrnoModuleDef))
        goto Fail;

    ADebugCompilerMsg(("End initialize"));

    return TRUE;

  Fail:

    ADeinitializeGarbageCollector();
    
    return FALSE;
}


void ASetupFileInterface(AFileInterface *fileIface)
{
    /* Store the file interface. */
    ACopyMem(&AFileIface, fileIface, sizeof(AFileInterface));
}


/* Return the default file interface used by the compiler to read the source
   files. */
void AGetDefaultFileInterface(AFileInterface *fileIface)
{
    fileIface->initCompilation = ADefInitCompilation;
    fileIface->openFile = ADefOpenFile;
    fileIface->read = ADefRead;
    fileIface->closeFile = ADefCloseFile;
    fileIface->findModule = ADefFindModule;
    fileIface->mapModule = ADefMapModule;
    fileIface->openDir = ADefOpenDir;
    fileIface->readDir = ADefReadDir;
    fileIface->closeDir = ADefCloseDir;
    fileIface->deinitCompilation = ADefDeinitCompilation;
    fileIface->param = NULL;
}


void ADeinitializeCompiler(void)
{
    /* FIX: stop all threads?? */
    
    ADeinitializeGarbageCollector();
}


/* Compile the main file in a compilation. The parameter global will point to
   the global variable number of the module initialization function.
   NOTE: The calling thread must be the only thread currently able to access
         the compiler state (symbol table, etc.).
   NOTE: Old generation garbage collection must be disallowed before calling
         CompileFile(). */
ABool ACompileFile(AThread *t, const char *path, char *moduleSearchPath,
                   int *global)
{
    AFileList *file;
    AModuleId mainModule;
    ABool result;
    AIntList *node;

    /* FIX: why disallowgc?? Isn't this a bit strange? Aren't threads
            disallowed by default?.. maybe to avoid gc while in the compiler,
            which might free some stuff that should not be freed.. yeah */
    ADebugCompilerMsg(("Compile file"));

    /* Initialize global variables needed during compilation. */

    ACompilerThread = t;

    AFirstError = NULL;
    ALastError = (ACompileError *)&AFirstError;

    ANumActiveFiles = 0;

    ACurClass = NULL;
    ACurFunction = NULL;
    ACurMember = AM_NONE;
    ACurMainModule = NULL;
    AUnresolvedSupertypes = NULL;

#ifdef HAVE_JIT_COMPILER
    AIsJitModule = FALSE;
#endif

    AFunDepth = 0;

    ANumAccessedExposedVariables = 0;
    AAccessedExposedVariablesSize = 0;
    AAccessedExposedVariables = NULL;

    if (AIsDynamicCompile)
        AInitFunctions = NULL; /* FIX: why this?? */

    ADebugCompilerMsg(("Begin initialize"));
    
    /* Initialize bytecode output. */
    if (!AInitializeOutput()) {
        AGenerateOutOfMemoryError();
        return FALSE;
    }

    /* Initialize source code input. */
    if (!InitializeInput())
        goto NoMemory;

    /* NOTE: Some memory may be leaked theoretically. */
    if (AIsDynamicCompile && !AInitializeDynamicModuleCompilation())
        goto NoMemory;

    /* FIX: is this freed somewhere?? */
    ACurMainModule = ACAlloc(sizeof(ASymbolInfo));
    if (ACurMainModule == NULL)
        goto NoMemory;

    /* Initialize symbol that represents the module formed by the main source
       file. */
    ACurMainModule->type = ID_GLOBAL_MODULE_MAIN;
    ACurMainModule->info.module.isActive = FALSE;
    ACurMainModule->info.module.isImported = FALSE;
    ACurMainModule->info.module.cModule = A_CM_NON_C;
    
    file = ACAlloc(sizeof(AFileList));
    if (file == NULL)
        goto NoMemory;
    
    if (!AFileIface.initCompilation(path, moduleSearchPath,
                                    AFileIface.param)) {
        ACFree(file, sizeof(AFileList));
        goto NoMemory;
    }

    file->next = NULL;
    file->tok  = NULL;

    file->path = ADupStr(path); /* FIX: is this always freed? */    
    if (file->path == NULL) {
        ACFree(file, sizeof(AFileList));
        AFileIface.deinitCompilation(AFileIface.param);
        goto NoMemory;
    }
    
    mainModule.numParts = 0;

    ADebugCompilerMsg(("Begin compile"));
    result = ACompileFiles(file, &mainModule, global);
    ADebugCompilerMsg(("End compile"));

    if (result) {
        /* Fix missing superclass references. Only do this if there were no
           major errors, since otherwise this may cause erratic behaviour. */
        ADebugCompilerMsg(("Fix superclasses"));
        AFixSupertypes();
        
        AEnterSection();
    
        ANumLocals = 4;
    
        /* Emit calls for all the module Main functions. */
        for (node = AInitFunctions;
             node != NULL; node = ARemoveIntList(node)) {
            /* How many arguments does the Main function take? */
            if (AValueToFunction(AGlobalByNum(node->data))->minArgs == 0)
                AEmitOpcode3Args(OP_CALL_G, node->data, 0, A_NO_RET_VAL);
            else {
                /* Read the command line argument array. */
                AEmitOpcode2Args(OP_ASSIGN_GL, GL_COMMAND_LINE_ARGS, 3);
                /* Call the Main function, giving the command line arguments
                   as an argument. */
                AEmitOpcode3Args(OP_CALL_G, node->data, 1, 3);
                AEmitArg(A_NO_RET_VAL);
            }
        }

        AEmitOpcode(OP_HALT);
        
        if (!ASetConstGlobalValue(ACompilerThread, *global,
                                  ALeaveSection(NULL, 0, 0, 0)))
            result = FALSE;
    }

    ADebugCompilerMsg(("Compiled all files, finalizing"));

    AFileIface.deinitCompilation(AFileIface.param);

    DeinitializeInput();
    ADeinitializeOutput();

    if (AIsDynamicCompile) {
        /* If there was an error, free all dynamic modules compiled in this
           compiler run. */
        if (!result || AFirstError != NULL)
            AFreeDynamicModules();
        
        ADeinitializeDynamicModuleCompilation();
    } else
        ARecordNumMainGlobals();

    AFreeStatic(AAccessedExposedVariables);
    
    ADebugVerifyMemory();
    
    /* Only the first compilation is not dynamic. */
    AIsDynamicCompile = TRUE;

    ADebugCompilerMsg(("End compile file"));

    return result && AFirstError == NULL;

  NoMemory:

    /* Handle out of memory error condition encountered before starting actual
       compilation. */
    
    AGenerateOutOfMemoryError();

    if (ACurMainModule != NULL)
        ACFree(ACurMainModule, sizeof(ASymbolInfo));

    DeinitializeInput();
    ADeinitializeOutput();

    ADebugCompilerMsg((A_MSG_HEADER "End compile file\n"));

    return FALSE;
}


/* Compile a list of files that form a module. */
ABool ACompileFiles(AFileList *file, AModuleId *module, int *globalPtr)
{
    AFileList *fileIter;
    int firstVar;
    int firstConst;
    ABool result;

    /* FIX: Is error checking ok? */

    /* If we are compiling a dynamically compiled module, allocate global
       variables for the module. */
    if (AIsDynamicCompile) {
        result = AAllocateModuleGlobals(&firstVar, &firstConst);
        if (!result)
            goto Leave;
    }

    /* Read, tokenize and scan the files. */
    for (fileIter = file; fileIter != NULL; fileIter = fileIter->next) {
        result = ReadFile(fileIter->path, &fileIter->tok);
        if (!result)
            goto Leave;

        result = AScan(module, fileIter->tok);
        if (!result)
            goto Leave;
    }

    if (globalPtr != NULL) {
        *globalPtr = AAddConstGlobalValue(AZero);
        if (*globalPtr == -1) {
            result = FALSE;
            goto Leave;
        }
    }

    ADebugCompilerMsg(("Compile imported"));

    /* Compile modules imported in the files that haven't been compiled yet. */
    for (fileIter = file; fileIter != NULL; fileIter = fileIter->next) {
        AActiveFiles[ANumActiveFiles] = fileIter->path;
        ANumActiveFiles++;
        
        if (!CompileImportedModules(fileIter->tok)) {
            ADebugCompilerMsg(("Failure compiling imports"));
            result = FALSE;
            ANumActiveFiles--;
            goto Leave;
        }

        ANumActiveFiles--;
    }

    ADebugCompilerMsg(("Begin parse"));
    
    ABeginNewModule(module);

    if (AIsDynamicCompile)
        ABeginDynamicModuleCompilation();

    AIsMainDefined = FALSE;

    /* Compile all the files to bytecode. */
    for (fileIter = file; fileIter != NULL; fileIter = fileIter->next) {
        ACurFileNum = AAddNewFile(fileIter->path);
        
#ifdef HAVE_JIT_COMPILER
        AIsJitModule = AIsJitModuleId(module);
#endif

        if (!AParse(module, fileIter->path, fileIter->tok)) {
            result = FALSE;
            goto Leave;
        }
    }

    ADebugCompilerMsg(("End parse"));

    if (AIsMainDefined)
        AInitFunctions = AAddIntListEnd(AInitFunctions, AMainFunctionNum);

    /* FIX: what if parse unsuccessful? */
    
    result = AEndModule();

    if (AIsDynamicCompile)
        result &= AEndDynamicModuleCompilation(firstVar, firstConst);
    
  Leave:

    for (fileIter = file; fileIter != NULL; fileIter = fileIter->next)
        AFreeTokens(fileIter->tok);
    
    AFreeFileList(file);
    
    return result;
}


/* Compile a module. The module must not be a main module. */
ABool ACompileModule(AModuleId *module)
{
    AFileList *files;

    files = NULL;
    if (!AFileIface.findModule(&AFileIface, module, AddFile, &files)) {
        AGenerateError(ALineNums[ANumActiveFiles - 1],
                       ErrModuleCouldNotBeImported, module);
        return FALSE;
    }

#ifdef A_HAVE_DYNAMIC_C_MODULES
    if (AEndsWith(files->path, DYNAMIC_C_MODULE_EXTENSION)) {
        /* Dynamic C module. */
        AModuleDef *def;

        if (!ALoadCModule(files->path, &def)
            || !ACreateModule(def, TRUE)
            || !ARealizeModule(module->id[module->numParts - 1])) {
            AGenerateError(ALineNums[ANumActiveFiles - 1],
                           ErrModuleCouldNotBeImported, module);
            return FALSE;
        }
        
        return TRUE;
    } else
#endif
        return ACompileFiles(files, module, NULL);
}


void AFreeFileList(AFileList *files)
{
    AFileList *prev;
    while (files != NULL) {
        prev = files;
        files = files->next;
        AFreeStatic(prev->path);
        ACFree(prev, sizeof(AFileList));
    }
}


static ABool InitializeInput(void)
{
    InputBufferLength = INITIAL_INPUT_BUFFER_LENGTH;
    InputBlockLength = INITIAL_INPUT_BLOCK_LENGTH;
    
    InputBuffer = AAllocStatic(InputBufferLength);

    return InputBuffer != NULL;
}


static void DeinitializeInput(void)
{
    AFreeStatic(InputBuffer);
}


/* Tokenize the contents of a file. */
static ABool ReadFile(char *path, AToken **tokPtr)
{
    void *file;
    AToken *tok;
    AEncoding encoding;
    
    /* Number of characters left in buffer from previous block */
    Assize_t bufIndex;

    file = AFileIface.openFile(path, AFileIface.param);
    if (file == NULL) {
        AGenerateError(-1, ErrErrorReadingFile, path);
        return FALSE;
    }

    *tokPtr = NULL;

    bufIndex = 0;

    /* UTF-8 is the default encoding. */
    encoding = AENC_UTF8;

    /* FIX: CR/LF pair may generate two newlines?? */

    for (;;) {
        Assize_t numRead;
        unsigned char *ptr;

        for (;;) {
            Assize_t count;

            /* Calculate maximum number of chars to be read from file. */
            count = AMin(InputBlockLength, InputBufferLength - bufIndex - 1);

            if (!AFileIface.read(file, InputBuffer + bufIndex, count,
                                 &numRead, AFileIface.param)) {
                AGenerateError(-1, ErrErrorReadingFile, path);
                goto Fail;
            }

            bufIndex += numRead;
            ptr = InputBuffer + bufIndex;

            /* Make sure that there is a newline at end of file by inserting
               it if end of file was reached. */
            if (numRead < count) {
                bufIndex++;
                *ptr++ = '\n';
            }

            /* Find the last newline. */
            while (ptr > InputBuffer && ptr[-1] != '\n' && ptr[-1] != '\r')
                ptr--;

            /* If a newline was found, no need to read more. */
            if (ptr != InputBuffer)
                break;

            /* If the buffer is full, grow it. Then continue reading until a
               newline is found. */
            if (bufIndex >= InputBufferLength - 1) {
                void *newBuf = AGrowStatic(InputBuffer,
                                           InputBufferLength * 2);
                if (newBuf != NULL) {
                    InputBlockLength *= 2;
                    InputBufferLength *= 2;
                    InputBuffer = newBuf;
                } else {
                    AGenerateOutOfMemoryError();
                    goto Fail;
                }
            }
        }

        /* Break if at end of file and input buffer is empty unless token
           list is empty. */
        if (numRead == 0 && ptr <= InputBuffer + 1 && *tokPtr != NULL)
            break;
        
        if (!ATokenize(InputBuffer, ptr, tokPtr, &tok, &encoding)) {
            /* The caller will have to free the tokens. */
            AGenerateOutOfMemoryError();
            goto Fail;
        }

        /* Copy the remaining stuff at the beginning of the buffer. */
        AMoveMem(InputBuffer, ptr, (InputBuffer + bufIndex) - ptr);

        bufIndex -= ptr - InputBuffer;
    }

    if (!AAddEofToken(tok)) {
        AGenerateOutOfMemoryError();
        goto Fail;
    }

    if (!AFileIface.closeFile(file, AFileIface.param))
        return FALSE;

    return TRUE;

  Fail:

    AFileIface.closeFile(file, AFileIface.param);
    
    return FALSE;
}


/* Compile the modules imported by a source file. Do not flag syntax errors
   in the source file that contains the import statements. */
static ABool CompileImportedModules(AToken *tok)
{
    if (tok->type == TT_BOM)
        tok = AAdvanceTok(tok);
    
    if (tok->type == TT_NEWLINE)
        tok = AAdvanceTok(tok);
    
    /* Skip module headers and encoding declarations. */
    while (tok->type == TT_MODULE || tok->type == TT_ENCODING) {
        while (tok->type != TT_NEWLINE)
            tok = AAdvanceTok(tok);
        tok = AAdvanceTok(tok);
    }

    while (tok->type == TT_IMPORT) {
        do {
            tok = AAdvanceTok(tok);
            if (tok->type == TT_ID)
                tok = AImportModule(tok, FALSE);
            if (tok == NULL)
                return FALSE;
        } while (tok->type == TT_COMMA);

        /* Skip newline. */
        tok = AAdvanceTok(tok);
    }

    return TRUE;
}


static ABool AddFile(AFileList **files, char *path)
{
    AFileList *file = ACAlloc(sizeof(AFileList));
    char *newPath = AAllocStatic(strlen(path) + 1);

    if (file == NULL || newPath == NULL) {
        if (file != NULL)
            ACFree(file, sizeof(AFileList));
        AFreeStatic(newPath);
        AFreeFileList(*files);
        return FALSE;
    }
    
    strcpy(newPath, path);
    
    file->path = newPath;
    file->next = *files;
    *files = file;

    return TRUE;
}


AValue ACreateConstant(ASymbolInfo *sym)
{
    AConstant *c = AAllocUnmovable(sizeof(AConstant));

    if (c == NULL)
        return AZero;

    AInitNonPointerBlockOld(&c->header, sizeof(AConstant) - sizeof(AValue));
    c->sym = sym;
    
    return AConstantToValue(c);
}


/* FIX: Use a better error handling mechanism. */
static void FailAndExit(const char *msg)
{
    fprintf(stderr, "alore: %s\n", msg);
    exit(3);
}


#ifdef HAVE_JIT_COMPILER
/* Return a boolean indicating whether a module is needed by the JIT compiler
   (i.e., if it cannot be compiled by the JIT compiler). */
ABool AIsJitModuleId(AModuleId *id)
{
    char str[100];
    AFormatMessage(str, sizeof(str), "%m", id);
    return strcmp(str, "compiler") == 0
        || strcmp(str, "interp") == 0
        || strcmp(str, "pack") == 0
        || strcmp(str, "x86") == 0
        || strcmp(str, "string") == 0
        || strcmp(str, "re") == 0
        || strcmp(str, "__asmdef") == 0;
}
#endif
