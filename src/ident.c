/* ident.c - Managing symbols

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "ident.h"
#include "error.h"
#include "cutil.h"
#include "internal.h"


/* Type should be one of ID_LOCAL, ID_MEMBER, ID_GLOBAL_MODULE,
   ID_GLOBAL_MODULE_SUB, ID_GLOBAL. The returned pointer may be movable! */
ASymbolInfo *AAddIdentifier(ASymbol *sym, AIdType type)
{
    ASymbolInfo *info;
    ASymbolInfo *newInfo;

    newInfo = ACAlloc(sizeof(ASymbolInfo));
    if (newInfo == NULL) {
        AGenerateOutOfMemoryError();
        return NULL;
    }

    for (info = (ASymbolInfo *)sym;
         info->next->type > type; info = info->next);

    newInfo->next = info->next;
    info->next = newInfo;

    return newInfo;
}


void ARemoveIdentifier(ASymbolInfo *sym)
{
    ASymbolInfo *next = sym->next;

    while (next->next != sym)
        next = next->next;

    next->next = sym->next;
    ACFree(sym, sizeof(ASymbolInfo));
}


char *AGetSymbolName(ASymbolInfo *sym)
{
    return AGetSymbolFromSymbolInfo(sym)->str;
}


ASymbol *AGetSymbolFromSymbolInfo(ASymbolInfo *sym)
{
    do {
        sym = sym->next;
    } while (AIsId(sym->type));

    return (ASymbol *)sym;
}


void AFreeModuleIdentifiers(ASymbolInfo *module)
{
    int i;

    for (i = 0; i <= ASymSize; i++) {
        ASymbol *sym;

        for (sym = ASym[i]; sym != NULL; sym = sym->next) {
            ASymbolInfo *symInfo;

            for (symInfo = sym->info;
                 AIsId(symInfo->type); symInfo = symInfo->next) {
                if (symInfo->sym == module && AIsGlobalId(symInfo->type))
                    ARemoveIdentifier(symInfo);
                else if (symInfo->type == ID_MEMBER) {
                    /* Clear member cache since it may refer to a class in the
                       removed module. */
                    symInfo->sym = NULL;
                }
            }
        }
    }

    /* Flag the module SymbolInfo structure as empty. Note that it is not
       freed.

       IDEA: Free the module SymbolInfo structure unless there is a very good
             reason why it cannot or should not be done. */
    module->info.module.cModule = A_CM_NON_C;
    if (module->type == ID_GLOBAL_MODULE)
        module->type = ID_GLOBAL_MODULE_EMPTY;
    else
        module->type = ID_GLOBAL_MODULE_SUB_EMPTY;
}
