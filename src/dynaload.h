/* dynaload.h - Dynamic loading of Alore code

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef DYNALOAD_H_INCL
#define DYNALOAD_H_INCL

#include "value.h"


struct ASymbolInfo_;


/* Heap-allocated structure containing information about a single dynamically
   compiled module. The structure is always stored in a value block stored in
   the old generation. All the values in the block are short int values.
   A reference to this block is stored in the first global const block of a
   module, as the second item (index 1). */
typedef struct {
    AValue header;              /* Block header */
    AValue symbol;              /* APtrToIntValue(module ASymbolInfo ptr) */
    AValue nextDynaModule;      /* Global variable index of the next module */
    AValue globalVarIndex;      /* Index of the first global variable */
    AValue globalConstIndex;    /* Index of the first global constant */
    AValue importedModules[1];  /* Indexes of imported modules */
} ADynaModule;


ABool AInitializeDynamicModuleCompilation(void);
void ABeginDynamicModuleCompilation(void);
ABool AAddDynamicImportedModule(struct ASymbolInfo_ *sym);
ABool AEndDynamicModuleCompilation(int firstVar, int firstConst);
void AFreeDynamicModules(void);
void ADeinitializeDynamicModuleCompilation(void);
void AFreeDynamicModule(int modNum);


#define AGetModuleInfo(num) \
    ((ADynaModule *)AValueToPtr(AGlobalByNum((num) + 1)))

/* Return the index of the next dynamic module or 0 if num is the last
   module. */
#define AGetNextModule(num) \
    AValueToInt(AGetModuleInfo(num)->nextDynaModule)

#define ASetNextModule(num, next) \
    (AGetModuleInfo(num)->nextDynaModule = AIntToValue(next))

#define AGetNumImportedModules(mod) \
    ((AGetValueBlockDataLength(&(mod)->header) - sizeof(ADynaModule) + \
      2 * sizeof(AValue)) / sizeof(AValue))

#define AIsModuleMarked(num) \
    (AGlobalByNum((num) + 2) != AZero)


#endif
