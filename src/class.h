/* class.h - Type object related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Various definitions related to dealing with ATypeInfo structures. These
   definitions include only generic definitions and definitions used during
   compilation. Operations that are only used runtime are defined in other
   header files. */

#ifndef CLASS_H_INCL
#define CLASS_H_INCL

#include "symtable.h"
#include "value.h"
#include "thread.h"


/* MemberNode - single item in class member hashtable. */
typedef struct AMemberNode_ {
    struct AMemberNode_ *next;
    unsigned key;
    unsigned item;
} AMemberNode;


typedef struct {
    AValue header;
    unsigned long size;
    AMemberNode item[1];
} AMemberHashTable;


/* In various places this flag can be or'ed with a member variable id to make
   it refer to a getter/setter method instead of a member slot id. */
/* FIX: This value is ok only for 32-bit opcodes. It should be 1<<15 if 16-bit
        opcodes, since this is used by the OP_ASSIGN_MDL opcode at least.
        Alternatively, the format of the opcode could be modified. */
#define A_VAR_METHOD (1U << 31)


/* Functions for constructing ATypeInfo structures */
ABool AInitializeClassOutput(void);
ATypeInfo *ACreateTypeInfo(AThread *t, ASymbolInfo *sym, ABool isInterface);
ABool AAllocateInterfacesInTypeInfo(ATypeInfo *type, unsigned numInterfaces);
ABool AAddInterface(ATypeInfo *type, ATypeInfo *interface, ABool allocSpace);
ABool AAddMember(AMemberTableType type, unsigned key, unsigned item);
void ABuildTypeInfoMembers(ATypeInfo *type);

/* Helper functions for updating individual ATypeInfo members */
void AUpdateTypeTotalNumVars(ATypeInfo *type);
void ACalculateInstanceSize(ATypeInfo *type);
void AUpdateInheritedMiscTypeData(ATypeInfo *type);

/* Functions for querying ATypeInfo structures, used by the compiler */
ABool AIsClassConstructor(AValue func);
ABool AIsSubType(AValue sub, AValue type);
int ATotalNumMemberVars(ATypeInfo *type);
int AGetMinArgs(struct ASymbolInfo_ *sym);
int AGetMaxArgs(struct ASymbolInfo_ *sym);

/* Functions for querying ATypeInfo member hash tables */
A_APIFUNC AValue ALookupMemberTable(ATypeInfo *class_, AMemberTableType type,
                                    unsigned key);
int AGetMemberVar(ABool isPrivate, unsigned key);

/* Return a pointer to a member hash table belonging to a type. */
#define AGetMemberTable(type, tableType) \
    ((AMemberHashTable *)AValueToPtr((type)->memberTable[tableType]))

/* Functions for querying/constructing member symbols */
ASymbolInfo *AGetMemberSymbol(ASymbol *s);
A_APIFUNC ASymbolInfo *AGetMemberSymbolByKey(int key);

/* Helper function used in generating error messages */
void AFormatMethodSymbol(char *buf, int bufLen, AFunction *func);

/* Query whether a symbol represents a valid super class. */
ABool AIsValidSuperClass(ASymbolInfo *sym);

void AVerifyInterfaces(ATypeInfo *type, int lineNumber);
void AVerifyInterface(ATypeInfo *type, ATypeInfo *interface, int lineNumber,
                      ABool ignoreFirst);

#define AInterfaceListPtr(interfaces, i) \
    ((ATypeInfo **)APtrAdd(AValueToPtr(interfaces), \
                           sizeof(AValue) + (i) * sizeof(ATypeInfo *)))

/* ATypeInfo *ATypeInterface(ATypeInfo *type, Assize_t index);
   Return an interface that is directly implemented by the type. */
#define ATypeInterface(type, i) \
    (*AInterfaceListPtr((type)->interfaces, i))


#endif
