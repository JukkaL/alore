/* ident.h - Managing symbols

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef IDENT_H_INCL

#include "symtable.h"
#include "token.h"


ASymbolInfo *AAddIdentifier(ASymbol *sym, AIdType type);

void ARemoveIdentifier(ASymbolInfo *sym);

A_APIFUNC char *AGetSymbolName(ASymbolInfo *sym);

ASymbol *AGetSymbolFromSymbolInfo(ASymbolInfo *sym);

void AFreeModuleIdentifiers(ASymbolInfo *module);


#endif
