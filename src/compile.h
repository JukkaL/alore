/* compile.h - Bytecode compiler related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef COMPILE_H_INCL
#define COMPILE_H_INCL

#include "common.h"
#include "lex.h"


struct AToken_;
struct ASymbolInfo_;
struct AThread_;


#define A_MODULE_NAME_MAX_PARTS 8
#define A_MODULE_NAME_MAX_LEN (A_MODULE_NAME_MAX_PARTS * 128)

/* The maximum number of active recursive compilations supported. */
/* FIX: should probably be increased?? */
#define A_MAX_COMPILE_DEPTH 16


typedef struct AFileList_ {
    struct AFileList_ *next;
    char *path;
    struct AToken_ *tok;
} AFileList;


typedef struct AModuleId_ {
    int numParts;
    struct ASymbolInfo_ *id[A_MODULE_NAME_MAX_PARTS];
} AModuleId;


typedef ABool (*AAddFileFunc)(AFileList **files, char *path);


typedef struct AFileInterface_ {
    ABool (*initCompilation)(const char *path, char *moduleSearchPath,
                             void *param);
    void *(*openFile)(char *path, void *param);
    ABool (*read)(void *file, unsigned char *buf, unsigned len,
                  Assize_t *actualLen, void *param);
    ABool (*closeFile)(void *file, void *param);
    ABool (*findModule)(struct AFileInterface_ *iface, AModuleId *module,
                        AAddFileFunc addfile, AFileList **files);
    void *(*openDir)(const char *path);
    const char *(*readDir)(void *dir);
    void (*closeDir)(void *dir);
    ABool (*mapModule)(char *moduleName, char *newModuleName);
    void (*deinitCompilation)(void *param);
    void *param;
} AFileInterface;


int ALoadAloreProgram(struct AThread_ **t, const char *file,
                      const char *interpreter, const char *modulePath,
                      ABool isStandalone, int argc, char **argv,
                      AFileInterface *iface, Asize_t maxHeap);
int AEndAloreProgram(struct AThread_ *t, AValue mainReturnValue);


ABool AInitializeCompiler(struct AThread_ **t,
                          const char *additModuleSearchPath,
                          const char *interpreter, ABool isStandalone,
                          unsigned long maxHeap);
void ADeinitializeCompiler(void);

void AFreeCompiler(void);

void AFreeErrors(void);

ABool ACompileFiles(AFileList *file, AModuleId *module, int *global);
ABool ACompileModule(AModuleId *module);
ABool ACompileFile(struct AThread_ *t, const char *path,
                   char *moduleSearchPath, int *global);

void AFreeFileList(AFileList *files);

ABool AInitializeDefaultModuleSearchPath(const char *additPath);

void ASetupFileInterface(AFileInterface *fileIface);
void AGetDefaultFileInterface(AFileInterface *fileIface);
    
ABool ADefInitCompilation(const char *path, char *moduleSearchPath,
                          void *param);
void *ADefOpenFile(char *path, void *param);
ABool ADefRead(void *file, unsigned char *buf, unsigned len,
               Assize_t *actualLen, void *param);
ABool ADefCloseFile(void *file, void *param);
ABool ADefFindModule(AFileInterface *iface, AModuleId *module,
                     AAddFileFunc addFile, AFileList **files);
ABool ADefMapModule(char *moduleName, char *newModuleName);
void *ADefOpenDir(const char *path);
const char *ADefReadDir(void *dir);
void ADefCloseDir(void *dir);
void ADefDeinitCompilation(void *param);

void AInitFunctionParseState(void);

ABool AScan(AModuleId *module, struct AToken_ *tok);
ABool AParse(AModuleId *module, char *path, struct AToken_ *tok);

void ACheckMainFunction(AModuleId *module);

ABool ASetKeyboardInterruptHandler(void); /* FIX: not here?? */

AValue ACreateConstant(struct ASymbolInfo_ *sym);


A_APIFUNC AValue ALoaderLoadInternal(struct AThread_ *t, AValue *args,
                                     ABool run);


extern AFileInterface AFileIface;
extern char *ADefaultModuleSearchPath;
extern char *AModuleSearchPath;
extern char *AProgramPath;
/* Path to the Alore interpreter (argv[0] of the interpreter). NULL if running
   as standalone. */
extern char *AInterpreterPath;

extern int ANumActiveFiles;
extern char *AActiveFiles[A_MAX_COMPILE_DEPTH];
extern unsigned short ALineNums[A_MAX_COMPILE_DEPTH];

extern unsigned ACurFileNum;

extern struct AThread_ *ACompilerThread;

extern struct ASymbolInfo_ *ACurMainModule;

extern ABool AIsDynamicCompile;

#ifdef HAVE_JIT_COMPILER
extern ABool AIsJitModule;
#endif


typedef struct AUnresolvedNameList_ {
    struct AUnresolvedNameList_ *next;
    int isQuotePrefix;
    int numParts;
    struct ASymbol_ *name[A_MODULE_NAME_MAX_PARTS + 1];
} AUnresolvedNameList;


typedef struct AUnresolvedSupertype_ {
    struct AUnresolvedSupertype_ *next;
    struct ATypeInfo_ *type;
    AUnresolvedNameList *modules;
    AUnresolvedNameList *super;
    AUnresolvedNameList *interfaces;
} AUnresolvedSupertype;


AUnresolvedNameList *AAddUnresolvedName(AUnresolvedNameList *list,
                                        struct AToken_ *tok);
AUnresolvedNameList *AAddUnresolvedNameStr(AUnresolvedNameList *list,
                                           const char *str);
AUnresolvedNameList *ACloneUnresolvedNameList(AUnresolvedNameList *list);
void AFreeUnresolvedNameList(AUnresolvedNameList *list);
void AExpandUnresolvedName(const AUnresolvedNameList *name,
                           struct AToken_ *expanded);
void AFixSupertypes(void);
void AFixSupertype(AUnresolvedSupertype *resolv);
void AStoreInheritanceInfo(ATypeInfo *type, AUnresolvedNameList *imports,
                           AUnresolvedNameList *super,
                           AUnresolvedNameList *interfaces);


#ifdef HAVE_JIT_COMPILER
ABool AIsJitModuleId(AModuleId *id);
#endif


extern AUnresolvedSupertype *AUnresolvedSupertypes;


extern ABool AIsStandaloneFlag;


#endif
