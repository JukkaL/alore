/* symtable.h - Alore symbol table

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef SYMTABLE_H_INCL
#define SYMTABLE_H_INCL

#include "common.h"


struct ASymbolInfo_;


/* A symbol is an identifier or a keyword. ASymbolInfo structure is used by
   identifiers only. It is a list of all the possible meanings of an
   identifier. The last structure in the list has a pointer to the Symbol
   structure, forming a circular list. */
typedef struct ASymbol_ {
    struct ASymbol_ *next;     /* NOTE: has to be the first field. */
    struct ASymbolInfo_ *info; /* NOTE: has to be the second field. */
    int len;
    unsigned char type;        /* Type of the symbol (TT_x)
                                  NOTE: has to be the fourth field. */
    char str[1];
} ASymbol;


typedef struct ASymbolInfo_ {
    struct ASymbolInfo_ *sym;     /* Module, next local var or class */
    struct ASymbolInfo_ *next;    /* NOTE: has to be the second field */
    int num;
    unsigned char type;           /* NOTE: has to be the fourth field */

    /* The active union member depends on the type member. */
    union {
        int blockDepth; /* Block depth of a local variable */
        unsigned memberValue; /* An integer + some of A_MEMBER_* flags */
        /* Information related to global definitions */
        struct {
            unsigned char isPrivate;
            char minArgs;
            unsigned short maxArgs;
        } global;
        /* Information related to module symbols */
        struct {
            unsigned char isActive;     /* Is this an active prefix */
            unsigned char isImported;   /* Has the module been imported */
            /* Status of the module; one of the following values:
                 A_CM_NON_C (not a C module)
                 A_CM_ACTIVE (realized C module)
                 A_CM_AUTO_IMPORT (C module that is automatically imported;
                                   currently only std)
                 index to CModule array | 1024 (unrealized C module) */
            short cModule;
        } module;
    } info;
} ASymbolInfo;


#define A_UNKNOWN_MIN_ARGS -1


/* Symbol table */
A_APIVAR extern int ASymSize; /* NOTE: This is the actual size - 1! Actual
                                 size is always a power of 2. */
A_APIVAR extern ASymbol **ASym;

extern ASymbol *AMainSymbol;
extern ASymbol *AUtf8Symbol;
extern ASymbol *AAsciiSymbol;
extern ASymbol *ALatin1Symbol;
extern ASymbol *ACreateSymbol;
extern ASymbolInfo *ACreateMemberSymbol;
extern ASymbolInfo *AAnonFunctionDummySymbol;


ABool AInitializeSymbolTable(void);

ABool AAddSymbol(unsigned hashValue, const unsigned char *idBeg,
                 unsigned idLen, ASymbol **sym);
ABool AGetSymbol(const char *id, unsigned idLen, ASymbol **symRet);
ASymbol *AFindSymbol(const char *id, unsigned idLen);

ASymbolInfo *ARealTypeSymbol(ASymbolInfo *sym);
ASymbolInfo *AInternalTypeSymbol(ASymbolInfo *sym);


#define A_CM_NON_C       -1
#define A_CM_AUTO_IMPORT -2
#define A_CM_ACTIVE      -3


#endif
