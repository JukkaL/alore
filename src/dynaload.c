/* dynaload.c - Dynamic loading of Alore code

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "dynaload.h"
#include "mem.h"
#include "gc.h"
#include "symtable.h"
#include "compile.h"
#include "parse.h"
#include "ident.h"
#include "cutil.h"
#include "internal.h"


#define INITIAL_MODULE_BUFFER_SIZE 16


static AValue *ModuleBuf;
static unsigned ModuleBufSize;
static unsigned ModuleBufIndex;

/* List of ids of compiled dynamic modules in the current compiler run. */
static AIntList *DynamicCompiledModules;


/* Prepare for dynamic module compilation. Must be called before calling any
   of the other functions in this module related to compiling modules.
   Typically called before each compiling session but doesn't have to be called
   for each individual compiled module. */
ABool AInitializeDynamicModuleCompilation(void)
{
    ModuleBufSize = INITIAL_MODULE_BUFFER_SIZE;
    ModuleBuf = AAllocStatic(ModuleBufSize * sizeof(AValue));

    DynamicCompiledModules = NULL;

    return ModuleBuf != NULL;
}


/* Prepare for compiling a new module dynamically. */
void ABeginDynamicModuleCompilation(void)
{
    ModuleBufIndex = 0;
}


/* Record a module that is imported by the module currently being compiled. The
   information is used by the gc. */
ABool AAddDynamicImportedModule(ASymbolInfo *sym)
{
    int i;
    AValue symVal;

    /* FIX: Implement a function to grow buffers? */
    if (ModuleBufIndex + 1 == ModuleBufSize) {
        AValue *newBuf = AGrowStatic(ModuleBuf, 2 * ModuleBufSize *
                                   sizeof(AValue));
        if (newBuf == NULL)
            return FALSE;

        ModuleBuf = newBuf;
        ModuleBufSize *= 2;
    }

    symVal = APtrToIntValue(sym);

    i = AFirstDynamicModule;
    while (i != 0) {
        ADynaModule *mod = AValueToPtr(AGlobalByNum(i + 1));

        if (mod->symbol == symVal) {
            ModuleBuf[ModuleBufIndex] = AIntToValue(i);
            ModuleBufIndex++;
        }

        i = AValueToInt(mod->nextDynaModule);
    }

    return TRUE;
}


/* End the dynamic compilation of a module. Store the module information (e.g.
   which modules are imported by the module). Only called if the compilation
   was successful. */
ABool AEndDynamicModuleCompilation(int firstVar, int firstConst)
{
    ADynaModule *mod;
    unsigned long modSize;
    int i;

    modSize = sizeof(ADynaModule) - sizeof(AValue) + ModuleBufIndex *
              sizeof(AValue);

    mod = AAllocUnmovable(modSize);
    if (mod == NULL)
        return FALSE;

    AInitValueBlockOld(&mod->header, modSize - sizeof(AValue));

    mod->symbol = APtrToIntValue(ACurModule);
    mod->nextDynaModule = AIntToValue(AFirstDynamicModule);
    mod->globalVarIndex = AIntToValue(firstVar);
    mod->globalConstIndex = AIntToValue(firstConst);

    for (i = 0; i < ModuleBufIndex; i++)
        mod->importedModules[i] = ModuleBuf[i];

    AFirstDynamicModule = firstConst;

    DynamicCompiledModules = AAddIntList(DynamicCompiledModules,
                                         AFirstDynamicModule);

    return ASetConstGlobalValue(ACompilerThread, AFirstDynamicModule + 1,
                               AValueBlockToValue(mod));
}


/* Free all modules compiled during the current compiler run. */
void AFreeDynamicModules(void)
{
    AIntList *node;
    for (node = DynamicCompiledModules; node != NULL; node = node->next)
        AFreeDynamicModule(node->data);
}


/* Free any resources allocated for dynamic module compilation. */
void ADeinitializeDynamicModuleCompilation(void)
{
    AFreeStatic(ModuleBuf);
    AFreeIntList(DynamicCompiledModules);
    DynamicCompiledModules = NULL;
}


/* Free a dynamically compiled module. Only call when it is known that the
   module is not in use. */
void AFreeDynamicModule(int modNum)
{
    ADynaModule *mod;
    ASymbolInfo *sym;

    /* Remove from the list of dynamic modules. */
    if (modNum == AFirstDynamicModule)
        AFirstDynamicModule = AGetNextModule(modNum);
    else {
        int i;

        for (i = AFirstDynamicModule;
             AGetNextModule(i) != modNum; i = AGetNextModule(i));

        ASetNextModule(i, AGetNextModule(modNum));
    }

    mod = AGetModuleInfo(modNum);
    sym = AIntValueToPtr(mod->symbol);

    /* Clear any identifiers from the symbol table. */
    AFreeModuleIdentifiers(sym);

    /* Free global values. */
    AFreeGlobals(AValueToInt(mod->globalVarIndex));
    AFreeGlobals(AValueToInt(mod->globalConstIndex));

    /* Free file paths. */
    /* FIX: Implement! Memory leak. */
}
