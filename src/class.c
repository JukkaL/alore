/* class.c - Type object related operations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "class.h"
#include "memberid.h"
#include "error.h"
#include "ident.h"
#include "mem.h"
#include "cutil.h"
#include "globals.h"
#include "internal.h"
#include "util.h"
#include "gc.h"
#include "debug_runtime.h"


/* NOTE: Must be 2^n. */
#define MEMBER_LIST_BLOCK_LENGTH 16


typedef struct MemberSymListBlock_ {
    struct MemberSymListBlock_ *next;
    ASymbolInfo *sym[MEMBER_LIST_BLOCK_LENGTH];
} MemberSymListBlock;


static MemberSymListBlock *MemberSyms;
static AMemberNode *TempMemberList[A_NUM_MEMBER_HASH_TABLES];
static int NumMembers[A_NUM_MEMBER_HASH_TABLES];


static ABool SearchHashTable(ATypeInfo *type, AMemberTableType tableType,
                            AFunction *func, ASymbolInfo **sym);
static int GetMemberCount(AValue tableValue, unsigned key);
static void UpdateMemberVars(ATypeInfo *type, int adjust);
static ABool HasTypeMethod(ATypeInfo *type, int methodNum, ABool ignoreObject);
static void VerifyInterfaceMember(ATypeInfo *type, int table, int key,
                                  ATypeInfo *iface, int lineNumber,
                                  ABool ignoreFirst);


/* Creates an empty TypeInfo structure and store it into global variable num.
   Returns a pointer to the structure or NULL if unsuccessful. */
ATypeInfo *ACreateTypeInfo(AThread *t, ASymbolInfo *sym, ABool isInterface)
{
    ATypeInfo *type;
    int i;

    type = AAllocUnmovable(sizeof(ATypeInfo));
    if (type == NULL)
        return NULL;

    AInitMixedBlockOld(&type->header1, sizeof(ATypeInfo),
                       A_TYPE_INFO_VALUE_SIZE);

    for (i = 0; i < A_NUM_MEMBER_HASH_TABLES; i++)
        type->memberTable[i] = AZero;

    type->interfaces = AZero;

    type->sym = sym;
    type->isInterface = isInterface;
    type->super = NULL;
    type->create = 0;
    type->numVars = 0;
    type->totalNumVars = 0;
    type->numInterfaces = 0;
    type->interfacesSize = 0;
    type->hasEqOverload = TRUE;
    type->hasHashOverload = TRUE;
    type->hasMemberInitializer = FALSE;
    type->hasFinalizer = FALSE;
    type->hasFinalizerOrData = FALSE;
    type->memberInitializer = 0; /* Must be a valid global variable index. */
    type->isSuperValid = TRUE;
    type->extDataMember = -1;
    type->dataSize = 0;

    /* The type has still to be initialized (setting up member hash tables
       etc.). */
    type->isValid = FALSE;
    
    if (!ASetConstGlobalValue(t, sym->num, ATypeToValue(type)))
        return NULL;

    return type;
}


/* Creates the member hash tables in type based on TempMemberList.
   NOTE: Generate error if out of memory; no special error return. */
void ABuildTypeInfoMembers(ATypeInfo *type)
{
    int i;

    for (i = 0; i < A_NUM_MEMBER_HASH_TABLES; i++) {
        int tableSize;
        AMemberHashTable *table;
        AMemberNode *node;
        unsigned long blockSize;
        int j;

        /* Find the smallest power of 2 that is >= NumMembers[i]. */
        for (tableSize = 1; tableSize < NumMembers[i]; tableSize *= 2);

        /* tableSize is 2^n-1. */
        tableSize--;

        /* Allocate the hash table. */
        blockSize = sizeof(AMemberHashTable) + tableSize * sizeof(AMemberNode);

        /* Large tables must be unmovable, since they may contain pointers
           between nodes that are not seen by the gc. */
        if (tableSize >= 1)
            table = AAllocUnmovable(blockSize);
        else
            table = AAlloc(ACompilerThread, blockSize);

        if (table == NULL) {
            AGenerateOutOfMemoryError();
            return;
        }

        if (tableSize >= 1)
            AInitNonPointerBlockOld(&table->header,
                                    blockSize - sizeof(AValue));
        else
            AInitNonPointerBlock(&table->header, blockSize - sizeof(AValue));

        table->size = tableSize;

        /* Clear the hash table. */
        for (j = 0; j <= tableSize; j++) {
            table->item[j].key = 0;
            table->item[j].item = 0;
            table->item[j].next = NULL;
        }

        if (i <= MT_VAR_GET_PRIVATE) {
            /* Put the getter/setter methods first before variables in the
               same hash chain, since they have a higher priority. The
               algorithm works by moving each found method to the start of the
               list. */
            node = (AMemberNode *)&TempMemberList[i];
            while (node->next != NULL) {
                AMemberNode *nextNode = node->next;
                
                if ((nextNode->item & A_VAR_METHOD)
                    && TempMemberList[i] != nextNode) {
                    node->next = nextNode->next;
                    nextNode->next = TempMemberList[i];
                    TempMemberList[i] = nextNode;
                } else
                    node = nextNode;
            }
        }

        /* Store all nodes that don't cause hash collisions. */
        node = (AMemberNode *)&TempMemberList[i];
        while (node->next != NULL) {
            AMemberNode *nextNode;
            int nextKey;
            int nextItem;

            nextNode = node->next;
            nextKey = nextNode->key;
            nextItem = nextNode->item;
                
            if (table->item[nextKey & tableSize].key == 0) {
                table->item[nextKey & tableSize].key  = nextKey;
                table->item[nextKey & tableSize].item = nextItem;
                node->next = nextNode->next;
                ACFree(nextNode, sizeof(AMemberNode));
            } else
                node = node->next;
        }

        /* Store the collision nodes. */
        j = 0;
        node = TempMemberList[i];
        while (node != NULL) {
            AMemberNode *nextNode = node->next;
            AMemberNode *curNode;

            /* Find a free node. */
            while (table->item[j].key != 0)
                j++;
            
            table->item[j].key  = node->key;
            table->item[j].item = node->item;
            table->item[j].next = NULL;

            /* Find the last node in the hash chain. */
            for (curNode = &table->item[node->key & tableSize];
                 curNode->next != NULL;
                 curNode = curNode->next);

            /* Insert the new node as the last node in the chain. */
            curNode->next = &table->item[j];

            ACFree(node, sizeof(AMemberNode));

            node = nextNode;
        }

        TempMemberList[i] = NULL;
        NumMembers[i] = 0;

        *ACompilerThread->tempStack = ANonPointerBlockToValue(table);
        if (!AModifyOldGenObject(ACompilerThread, &type->memberTable[i],
                                 ACompilerThread->tempStack)) {
            AGenerateOutOfMemoryError();
            return;
        }
    }
}


/* Set type->hasEqOverload to TRUE if the type has an overloaded
   equality (==) operator, overwise set it to FALSE. */
static void UpdateTypeHasSpecialMethods(ATypeInfo *type)
{
    type->hasEqOverload = HasTypeMethod(type, AM_EQ, TRUE);
    type->hasHashOverload = HasTypeMethod(type, AM__HASH, TRUE);
}


static ABool HasTypeMethod(ATypeInfo *type, int methodNum, ABool ignoreObject)
{
    do {
        /* Ignore method defined Object if asked to do so. */
        if (ignoreObject && ASuperType(type) == NULL)
            return FALSE;
        
        if (GetMemberCount(type->memberTable[MT_METHOD_PUBLIC],
                           methodNum) > 0)
            return TRUE;
        type = ASuperType(type);
    } while (type != NULL);
    return FALSE;
}


/* Set type->hasFinalizer (and type->hasFinalizerOrData) if any of the super
   classes has a finalizer. */
static void UpdateTypeHasFinalizer(ATypeInfo *type)
{
    ATypeInfo *cur;

    cur = ASuperType(type);
    while (cur != NULL) {
        if (cur->hasFinalizer) {
            type->hasFinalizer = TRUE;
            type->hasFinalizerOrData = TRUE;
        }
        cur = ASuperType(cur);
    }
}


/* Set type->extDataMember if any of the superclasses has it defined. */
static void UpdateTypeExtDataMember(ATypeInfo *type)
{
    ATypeInfo *cur;

    cur = ASuperType(type);
    while (cur != NULL) {
        if (cur->extDataMember != -1) {
            type->extDataMember = cur->extDataMember;
            break;
        }
        cur = ASuperType(cur);
    }
}


void AUpdateInheritedMiscTypeData(ATypeInfo *type)
{
    UpdateTypeHasSpecialMethods(type);
    UpdateTypeHasFinalizer(type);
    UpdateTypeExtDataMember(type);
}


ABool AInitializeClassOutput(void)
{
    int i;
    
    for (i = 0; i < A_NUM_MEMBER_HASH_TABLES; i++) {
        TempMemberList[i] = NULL;
        NumMembers[i] = 0;
    }

    MemberSyms = AAllocStatic(sizeof(MemberSymListBlock));

    return MemberSyms != NULL;
}


ABool AAddMember(AMemberTableType type, unsigned key, unsigned item)
{
    AMemberNode *node;

    node = ACAlloc(sizeof(AMemberNode));
    if (node == NULL)
        return FALSE;

    node->next = TempMemberList[type];
    node->key  = key;
    node->item = item;

    TempMemberList[type] = node;

    NumMembers[type]++;

    return TRUE;
}


static void UpdateMemberVars(ATypeInfo *type, int adjust)
{
    AMemberTableType tableType;

    for (tableType = MT_VAR_SET_PUBLIC;
         tableType <= MT_VAR_GET_PRIVATE; tableType++) {
        int i;
        AMemberHashTable *table;

        table = AGetMemberTable(type, tableType);
        for (i = 0; i <= table->size; i++) {
            if (!(table->item[i].item & A_VAR_METHOD))
                table->item[i].item += adjust;
        }
    }
}


/* Get member value for member key from member table specified by type.
   Return -1UL (unsigned!) if not found. Superclass is not consulted. */
AValue ALookupMemberTable(ATypeInfo *class_, AMemberTableType type,
                          unsigned key)
{
    AMemberHashTable *table;
    AMemberNode *node;

    table = (AMemberHashTable *)AValueToPtr(class_->memberTable[type]);
    node = &table->item[key & table->size];

    if (node->key == key)
        return node->item;

    while (node->next != NULL) {
        node = node->next;
        if (node->key == key)
            return node->item;
    }

    return -1;
}


/* Get member variable value for member key. Search private or public member
   variables depending on the value of isPrivate. Do not return getter/setter
   methods. Return -1 if not found. */
int AGetMemberVar(ABool isPrivate, unsigned key)
{
    AMemberHashTable *table;
    AMemberNode *node;

    table = (AMemberHashTable *)
            AValueToPtr(AType->memberTable[MT_VAR_GET_PUBLIC + isPrivate]);
    node = &table->item[key & table->size];

    if (node->key == key && node->item < A_VAR_METHOD)
        return node->item;

    while (node->next != NULL) {
        node = node->next;
        if (node->key == key && node->item < A_VAR_METHOD)
            return node->item;
    }

    return -1;
}
 
 
ASymbolInfo *AGetMemberSymbol(ASymbol *s)
{
    ASymbolInfo *sym = (ASymbolInfo *)s;

    while (AIsLocalId(sym->next->type))
        sym = sym->next;

    if (sym->next->type != ID_MEMBER) {
        sym = AAddIdentifier(s, ID_MEMBER);
        if (sym == NULL)
            return NULL;
        
        sym->type = ID_MEMBER;
        sym->num  = ANumMemberIds;
        sym->sym  = NULL;

        if ((ANumMemberIds & (MEMBER_LIST_BLOCK_LENGTH - 1)) == 0) {
            MemberSymListBlock *new = AAllocStatic(sizeof(MemberSymListBlock));
            if (new == NULL)
                return NULL;
            
            new->next = MemberSyms;
            MemberSyms = new;
        }

        MemberSyms->sym[ANumMemberIds & (MEMBER_LIST_BLOCK_LENGTH - 1)] = sym;

        ANumMemberIds++;
        
        return sym;
    } else
        return sym->next;
}


ASymbolInfo *AGetMemberSymbolByKey(int key)
{
    MemberSymListBlock *block = MemberSyms;
    int curBlockKey = (ANumMemberIds - 1) & ~(MEMBER_LIST_BLOCK_LENGTH - 1);

    /* Three special members don't have any symbols. */
    if (key == AM_NONE || key == AM_INITIALIZER || key == AM_FINALIZER)
        return NULL;
    
    while (key < curBlockKey) {
        block = block->next;
        curBlockKey -= MEMBER_LIST_BLOCK_LENGTH;
    }

    return block->sym[key & (MEMBER_LIST_BLOCK_LENGTH - 1)];
}


static int GetMemberCount(AValue tableValue, unsigned key)
{
    int numFound;

    AMemberHashTable *table;
    AMemberNode *node;

    table = (AMemberHashTable *)AValueToPtr(tableValue);
    node = &table->item[key & table->size];

    numFound = 0;
    
    if (node->key == key)
        numFound++;

    while (node->next != NULL) {
        node = node->next;
        if (node->key == key)
            numFound++;
    }
    
    return numFound;
}


static int GetMemberVariableCount(AValue tableValue, unsigned key,
                                  ABool isMethod)
{
    int numFound;

    AMemberHashTable *table;
    AMemberNode *node;

    table = (AMemberHashTable *)AValueToPtr(tableValue);
    node = &table->item[key & table->size];

    numFound = 0;

    if (isMethod)
        isMethod = A_VAR_METHOD;
    
    if (node->key == key && (node->item & A_VAR_METHOD) == isMethod)
        numFound++;

    while (node->next != NULL) {
        node = node->next;
        if (node->key == key && (node->item & A_VAR_METHOD) == isMethod)
            numFound++;
    }
    
    return numFound;
}


static void CheckNoInterfaceDefinition(ATypeInfo *type, unsigned key,
                                       AToken *tok, int table)
{
    int i;
    
    /* Check that no interface defines member of the specified type with the
       specified name. */
    for (i = 0; i < type->numInterfaces; i++) {
        ATypeInfo *iface = ATypeInterface(type, i);
        do {
            if (GetMemberCount(iface->memberTable[table], key) > 0) {
                AGenerateError(tok->lineNumber,
                               ErrIncompatibleWithDefinitionIn, tok,
                               iface->sym);
                return;
            }
            iface = iface->super;
        } while (iface != NULL);
    }
}


void ACheckMethodDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                            AToken *tok)
{
    /* Check if there are two variables with the same visibility. */
    if (GetMemberCount(type->memberTable[MT_METHOD_PUBLIC + isPrivate],
                       key) > 1
        || GetMemberCount(type->memberTable[MT_METHOD_PRIVATE - isPrivate],
                          key) > 0)
        AGenerateError(tok->lineNumber, ErrRedefined, tok);

    CheckNoInterfaceDefinition(type, key, tok, MT_VAR_GET_PUBLIC);
    if (isPrivate)
        CheckNoInterfaceDefinition(type, key, tok, MT_METHOD_PUBLIC);

    while (ASuperType(type) != NULL) {
        type = type->super;

        if (GetMemberCount(type->memberTable[MT_VAR_GET_PUBLIC], key) > 0
            || (isPrivate &&
                GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) > 0))
        {
            AGenerateError(tok->lineNumber, ErrIncompatibleWithDefinitionIn,
                           tok, type->sym);
            return;
        }
    }
}


void ACheckMemberVariableDefinition(ATypeInfo *type, unsigned key,
                                    ABool isPrivate, AToken *tok)
{
    /* Check if there is another variable or method with the same name. */
    if (GetMemberVariableCount(type->memberTable[MT_VAR_GET_PUBLIC +
                                                isPrivate], key, FALSE) > 1
        || (GetMemberVariableCount(type->memberTable[MT_VAR_GET_PRIVATE -
                                                    isPrivate], key, FALSE) +
            GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) +
            GetMemberCount(type->memberTable[MT_METHOD_PRIVATE], key) > 0))
        AGenerateError(tok->lineNumber, ErrRedefined, tok);

    CheckNoInterfaceDefinition(type, key, tok, MT_METHOD_PUBLIC);
    if (isPrivate)
        CheckNoInterfaceDefinition(type, key, tok, MT_VAR_GET_PUBLIC);

    while (ASuperType(type) != NULL) {
        type = type->super;

        if (GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) +
            GetMemberVariableCount(type->memberTable[MT_VAR_GET_PUBLIC], key,
                                   FALSE) > 0
            || (isPrivate &&
                GetMemberCount(type->memberTable[MT_VAR_GET_PUBLIC], key) > 0))
        {
            AGenerateError(tok->lineNumber, ErrIncompatibleWithDefinitionIn,
                           tok, type->sym);
            return;
        }
    }
}


void ACheckSetterDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                            AToken *tok)
{
    /* Check that the setter is not for a const variable. */
    {
        ATypeInfo *t = type;
        ABool hasSetter = TRUE;
        while (ASuperType(t) != NULL) {
            t = t->super;
            if (GetMemberCount(t->memberTable[MT_VAR_GET_PUBLIC], key) > 0)
                hasSetter = GetMemberCount(t->memberTable[MT_VAR_SET_PUBLIC],
                                           key) > 0;
        }
        if (!hasSetter)
            AGenerateError(tok->lineNumber, ErrIncompatibleWithSuperClass,
                           tok);
    }
    
    /* Check if there is another setter or method with the same name. */
    if (GetMemberVariableCount(type->memberTable[MT_VAR_SET_PUBLIC +
                                                isPrivate], key, TRUE) > 1
        || (GetMemberVariableCount(type->memberTable[MT_VAR_SET_PRIVATE -
                                                    isPrivate], key, TRUE) +
            GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) +
            GetMemberCount(type->memberTable[MT_METHOD_PRIVATE], key) > 0))
        AGenerateError(tok->lineNumber, ErrRedefined, tok);
    
    CheckNoInterfaceDefinition(type, key, tok, MT_METHOD_PUBLIC);
    if (isPrivate)
        CheckNoInterfaceDefinition(type, key, tok, MT_VAR_GET_PUBLIC);

    /* Check if there is a variable or a constant with the same name. */
    if (GetMemberCount(type->memberTable[MT_VAR_GET_PRIVATE - isPrivate],
                       key) > 0
        || GetMemberVariableCount(type->memberTable[MT_VAR_GET_PUBLIC +
                                                    isPrivate],
                                  key, FALSE) > 0)
        AGenerateError(tok->lineNumber, ErrRedefined, tok);
    else {
        /* Check that there is a getter or a variable for the member. */
        ATypeInfo *t = type;
        ABool found = FALSE;
        while (t != NULL) {
            if (GetMemberCount(t->memberTable[MT_VAR_GET_PUBLIC + isPrivate],
                               key) > 0) {
                found = TRUE;
                break;
            }
            t = ASuperType(t);
        }
        if (!found)
            AGenerateError(tok->lineNumber, ErrWriteOnly, tok);
    }

    while (ASuperType(type) != NULL) {
        type = type->super;

        if (GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) > 0
            || (isPrivate &&
                GetMemberCount(type->memberTable[MT_VAR_GET_PUBLIC], key) +
                GetMemberCount(type->memberTable[MT_VAR_SET_PUBLIC], key) > 0))
        {
            AGenerateError(tok->lineNumber, ErrIncompatibleWithDefinitionIn,
                           tok, type->sym);
            return;
        }
    }
}


void ACheckGetterDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                            AToken *tok)
{
    /* Check if there is another getter or method with the same name. */
    if (GetMemberVariableCount(type->memberTable[MT_VAR_GET_PUBLIC +
                                                 isPrivate], key, TRUE) > 1
        || (GetMemberVariableCount(type->memberTable[MT_VAR_GET_PRIVATE -
                                                     isPrivate], key, TRUE) +
            GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) +
            GetMemberCount(type->memberTable[MT_METHOD_PRIVATE], key) > 0))
        AGenerateError(tok->lineNumber, ErrRedefined, tok);

    /* Check if there are variables or contant definitions with the same
       name. */
    if (GetMemberVariableCount(type->memberTable[MT_VAR_GET_PUBLIC +
                                                 isPrivate], key, FALSE) > 0
        || GetMemberVariableCount(type->memberTable[MT_VAR_GET_PRIVATE -
                                                 isPrivate], key, FALSE) > 0)
        AGenerateError(tok->lineNumber, ErrRedefined, tok);
    
    CheckNoInterfaceDefinition(type, key, tok, MT_METHOD_PUBLIC);
    if (isPrivate)
        CheckNoInterfaceDefinition(type, key, tok, MT_VAR_GET_PUBLIC);
    
    while (ASuperType(type) != NULL) {
        type = type->super;

        if (GetMemberCount(type->memberTable[MT_METHOD_PUBLIC], key) > 0
            || (isPrivate &&
                GetMemberCount(type->memberTable[MT_VAR_GET_PUBLIC], key) +
                GetMemberCount(type->memberTable[MT_VAR_SET_PUBLIC], key) > 0))
        {
            AGenerateError(tok->lineNumber, ErrIncompatibleWithDefinitionIn,
                           tok, type->sym);
            return;
        }
    }
}


int ATotalNumMemberVars(ATypeInfo *type)
{
    int num = 0;
    
    for (; type != NULL; type = ASuperType(type))
        num += type->numVars;

    return num;
}


void AUpdateTypeTotalNumVars(ATypeInfo *type)
{
    type->totalNumVars = ATotalNumMemberVars(ASuperType(type));
    if (type->totalNumVars > 0)
        UpdateMemberVars(type, type->totalNumVars);
}


void ACalculateInstanceSize(ATypeInfo *type)
{
    ATypeInfo *cur;

    cur = ASuperType(type);
    while (cur != NULL && cur->dataSize == 0)
        cur = ASuperType(cur);

    if (cur != NULL)
        type->dataSize += cur->dataSize;

    type->dataOffset = (1 + type->totalNumVars) * sizeof(AValue);
    type->instanceSize = AGetBlockSize((1 + type->totalNumVars) *
                                       sizeof(AValue) + type->dataSize);
    
    if (type->dataSize > 0)
        type->hasFinalizerOrData = TRUE;
}


void AFormatMethodSymbol(char *buf, int bufLen, AFunction *func)
{
    ASymbolInfo *sym;
    ATypeInfo *type;

    type = AValueToType(AGlobalByNum(func->sym->num));

    for (;;) {
        if (SearchHashTable(type, MT_METHOD_PUBLIC, func, &sym)
            || SearchHashTable(type, MT_METHOD_PRIVATE, func, &sym)
            || SearchHashTable(type, MT_VAR_GET_PUBLIC, func, &sym)
            || SearchHashTable(type, MT_VAR_GET_PRIVATE, func, &sym)) {
            if (sym == NULL)
                AFormatMessage(buf, bufLen, "(nil)");
            else
                AFormatMessage(buf, bufLen, "%i", sym);
            return;
        }

        if (SearchHashTable(type, MT_VAR_SET_PUBLIC, func, &sym)
            || SearchHashTable(type, MT_VAR_SET_PRIVATE, func, &sym)) {
            AFormatMessage(buf, bufLen, "%i=", sym);
            return;
        }

        type = ASuperType(type);
    }
}


static ABool SearchHashTable(ATypeInfo *type, AMemberTableType tableType,
                            AFunction *func, ASymbolInfo **sym)
{
    AMemberHashTable *table;
    int i;
    ABool direct;

    direct = tableType == MT_METHOD_PUBLIC || tableType == MT_METHOD_PRIVATE;
    table = AGetMemberTable(type, tableType);
    for (i = 0; i <= table->size; i++) {
        if ((direct || (table->item[i].item & A_VAR_METHOD))
            && AValueToFunction(AGlobalByNum(table->item[i].item &
                                              ~A_VAR_METHOD)) == func) {
            *sym = AGetMemberSymbolByKey(table->item[i].key);
            return TRUE;
        }
    }

    return FALSE;
}


ABool AIsClassConstructor(AValue funcVal)
{
    AFunction *f = AValueToFunction(funcVal);
    return f->sym != NULL && f->sym->type == ID_GLOBAL_CLASS &&
        f == AValueToFunction(AGlobalByNum(AValueToType(
            AGlobalByNum(f->sym->num))->create));
}


/* Return the minimum number of arguments for a symbol representing a function
   or a class. */
int AGetMinArgs(ASymbolInfo *sym)
{
    if (sym->info.global.minArgs != A_UNKNOWN_MIN_ARGS)
        return sym->info.global.minArgs;
    else {
        ATypeInfo *type = AValueToType(AGlobalByNum(sym->num));

        for (type = ASuperType(type); type != NULL &&
                 type->sym->info.global.minArgs == A_UNKNOWN_MIN_ARGS;
             type = ASuperType(type));

        if (type != NULL
            && type->sym->info.global.minArgs != A_UNKNOWN_MIN_ARGS) {
            sym->info.global.minArgs = type->sym->info.global.minArgs;
            sym->info.global.maxArgs = type->sym->info.global.maxArgs;
            return sym->info.global.minArgs;
        } else
            return 0;
    }
}


/* Return the minimum number of arguments for a symbol representing a function
   or a class. */
int AGetMaxArgs(ASymbolInfo *sym)
{
    if (sym->info.global.minArgs != A_UNKNOWN_MIN_ARGS)
        return sym->info.global.maxArgs;
    else {
        /* Try to find out the number of arguments (as a side effect of
           GetMinArgs). */
        AGetMinArgs(sym);
        if (sym->info.global.minArgs != A_UNKNOWN_MIN_ARGS)
            return sym->info.global.maxArgs;
        else {
            /* If we don't the number of arguments yet, accept any number of
               arguments. */
            return 1 | A_VAR_ARG_FLAG;
        }
    }
}


ABool AIsSubType(AValue subVal, AValue typeVal)
{
    ATypeInfo *subType = AValueToType(subVal);
    ATypeInfo *type = AValueToType(typeVal);
    
    do {
        if (subType == type)
            return TRUE;
        
        subType = subType->super;
    } while (subType != NULL);
    
    return FALSE;
}


/* Allocate an interfaces block in a type info that can hold at least
   numInterfaces interfaces.

   IDEA: Allow growing the block. */
ABool AAllocateInterfacesInTypeInfo(ATypeInfo *type, unsigned numInterfaces)
{
    Assize_t dataSize;
    AValue *block;
    unsigned i;

    dataSize = sizeof(ATypeInfo *) * numInterfaces;
    block = AAllocUnmovable(sizeof(AValue) + dataSize);
    if (block == NULL)
        return FALSE;

    /* Initialize block header. */
    AInitNonPointerBlockOld(block, dataSize);
    /* Copy values from the old list block. */
    for (i = 0; i < type->numInterfaces; i++)
        *AInterfaceListPtr(ANonPointerBlockToValue(block), i) =
            ATypeInterface(type, i);
    
    ACompilerThread->tempStack[0] = ANonPointerBlockToValue(block);
    if (!AModifyOldGenObject(ACompilerThread, &type->interfaces,
                             ACompilerThread->tempStack))
        return FALSE;
    type->interfacesSize = numInterfaces;
    
    return TRUE;
}


/* Add an interface to the list of interfaces implemented by a class. If
   allocSpace is FALSE, assume that the interfaces block has enough space.
   Otherwise, allocate extra space in the interface block. Return FALSE on
   error, but failure is possible only if allocSpace is TRUE.

   The interfaces implemented directly by a class are allocated during type
   object construction and do not need allocation. Bound interfaces always
   require allocSpace to be TRUE. */
ABool AAddInterface(ATypeInfo *type, ATypeInfo *interface, ABool allocSpace)
{
    int n;
    int i;

    n = type->numInterfaces;

    /* Do nothing if the interface already exists in the list. */
    for (i = 0; i < n; i++) {
        if (ATypeInterface(type, i) == interface)
            return TRUE;
    }

    if (allocSpace) {
        /* Grow the interface list if needed. This may fail. */
        if (!AAllocateInterfacesInTypeInfo(type, type->interfacesSize + 1))
            return FALSE;
    }
    
    *AInterfaceListPtr(type->interfaces, n) = interface;
    type->numInterfaces++;

    return TRUE;
}


void AVerifyInterfaces(ATypeInfo *type, int lineNumber)
{
    int i;
    AResolveTypeInterfaces(type);
    for (i = 0; i < type->numInterfaces; i++) {
        ATypeInfo *iface = ATypeInterface(type, i);
        do {
            AVerifyInterface(type, iface, lineNumber, TRUE);
            iface = ASuperType(iface);
        } while (iface != NULL);
    }
}


/* Verify that a class properly implements an interface. If ignoreFirst is
   TRUE, perform special processing for members implemented directly by type
   to avoid reporting multiple errors for errors already reported elsewhere. */
void AVerifyInterface(ATypeInfo *type, ATypeInfo *interface,
                      int lineNumber, ABool ignoreFirst)
{
    AMemberHashTable *t;
    int i;

    /* Verify methods. */
    t = AGetMemberTable(interface, MT_METHOD_PUBLIC);
    for (i = 0; i <= t->size; i++) {
        int key = t->item[i].key;
        if (key != AM_NONE)
            VerifyInterfaceMember(type, MT_METHOD_PUBLIC, key, interface,
                                  lineNumber, ignoreFirst);
    }

    /* Verify getters. */
    t = AGetMemberTable(interface, MT_VAR_GET_PUBLIC);
    for (i = 0; i <= t->size; i++) {
        int key = t->item[i].key;
        if (key != AM_NONE)
            VerifyInterfaceMember(type, MT_VAR_GET_PUBLIC, key, interface,
                                  lineNumber, ignoreFirst);
    }

    /* Verify setters. */
    t = AGetMemberTable(interface, MT_VAR_SET_PUBLIC);
    for (i = 0; i <= t->size; i++) {
        int key = t->item[i].key;
        if (key != AM_NONE)
            VerifyInterfaceMember(type, MT_VAR_SET_PUBLIC, key, interface,
                                  lineNumber, ignoreFirst);
    }
}


/* Verify that a single interface member is correctly implemented by a class.
   If ignoreFirst is TRUE, perform special processing for members implemented
   directly by type to avoid reporting multiple errors for a single problem. */
static void VerifyInterfaceMember(ATypeInfo *type, int table, int key,
                                  ATypeInfo *iface, int lineNumber,
                                  ABool ignoreFirst)
{
    ABool found = FALSE;
    ABool isFirst = TRUE;
    ATypeInfo *curType = type;
    do {
        if (isFirst && ignoreFirst) {
            /* If there is a definition type mismatch (e.g. method implemented
               as a variable), it will be caught elsewhere, but only for
               members in the class that directly implements an interface.
               The tests below allow us to not report errors twice. */
            if (GetMemberCount(
                    curType->memberTable[MT_METHOD_PUBLIC], key) > 0)
            found = TRUE;
            if (table == MT_METHOD_PUBLIC
                && GetMemberCount(
                    curType->memberTable[MT_VAR_GET_PUBLIC], key)
                + GetMemberCount(
                    curType->memberTable[MT_VAR_SET_PUBLIC], key) > 0)
                found = TRUE;
            if (table != MT_METHOD_PUBLIC
                && GetMemberCount(curType->memberTable[table], key) > 0)
                found = TRUE;
        } else {
            /* Inherited member must match exactly, since the condition
               described above does not hold. */
            if (GetMemberCount(curType->memberTable[table], key) > 0)
                found = TRUE;
        }
        curType = ASuperType(curType);
        isFirst = FALSE;
    } while (!found && curType != NULL);
    if (!found) {
        const char *desc = "";
        if (table == MT_VAR_SET_PUBLIC)
            desc = " setter";
        else if (table == MT_VAR_GET_PUBLIC)
            desc = " getter";
        AGenerateError(lineNumber,
                       "\"%i\" does not implement%s \"%M\" defined in \"%i\"",
                       type->sym, desc, key, iface->sym);
    }
}
