/* std_hashvalue.c - Helpers used by std::Hash()

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This file contains an implementation of identity-based hash values. Tables
   are used to map pointers to hash values. Calculating hash values indirectly
   allows moving objects with hash values by the garbage collector. */

#include "alore.h"
#include "thread_athread.h"
#include "gc.h"
#include "gc_internal.h"


/* A node in the identity-based hash table */
typedef struct HashValueNode_ {
    void *ptr;
    AValue hashValue;
    struct HashValueNode_ *next;
} HashValueNode;


typedef struct {
    HashValueNode **table;
    int size;
    int num;
} HashMappingTable;


#define PtrHashValue(p) ((unsigned long)(p) >> 3)


#define INITIAL_HASH_MAPPING_TABLE_SIZE 128
#define HASH_NODE_BUCKET_SIZE 64


static ABool InitializeTable(HashMappingTable *t);
static AValue GetHashMapping(HashMappingTable *t, void *address);
static HashValueNode *AllocNode(void *ptr);
static void FreeNode(HashValueNode *n);
static ABool GrowHashTable(HashMappingTable *t);


static unsigned long HashValueCounter;

/* There are two tables; one for new objects in the nuresry, and one for old
   generation (unmovalbe) objects. */
static HashMappingTable NewGenTable;
static HashMappingTable OldGenTable;

static HashValueNode *HashFreeList;


/* Initialize the identity-based hash tables. */
ABool AInitializeHashValueMapping(void)
{
    HashValueCounter = 1;
    return InitializeTable(&NewGenTable)
        && InitializeTable(&OldGenTable);
}


/* Get the identity-based hash value for an object reference at *v. This may
   cause the object to move, thus updating *v. */
AValue AGetIdHashValue(AThread *t, AValue *v)
{
    void *p = AValueToPtr(*v);
    HashMappingTable *table;
    AValue hash;

    if (AIsInNursery(p))
        table = &NewGenTable;
    else
        table = &OldGenTable;

    ALockHashMappings();
    hash = GetHashMapping(table, p);
    if (AIsError(hash)) {
        /* Could not find a mapping -- insert a new hash mapping. */

        int i;
        HashValueNode *n;

        /* Allocate a new hash node. Note that this may cause the object to be
           moved, but we'll handle that case later. */
        n = AllocNode(p);
        if (n == NULL) {
            /* Out of memory. */
            AUnlockHashMappings();
            return ARaiseMemoryErrorND(t);
        }

        /* Check if it is time to grow the hash table. This may cause the
           object to be moved. */
        if (table->num > table->size) {
            if (!GrowHashTable(table)) {
                /* Out of memory. */
                FreeNode(n);
                AUnlockHashMappings();
                return ARaiseMemoryErrorND(t);
            }
        }

        /* If the target value moved to the old generation, try again. */
        if (p != AValueToPtr(*v)) {
            FreeNode(n);
            AUnlockHashMappings();
            return AGetIdHashValue(t, v);
        }

        /* Calculate hash value for the internal hash table. */
        i = PtrHashValue(p) & (table->size - 1);

        /* Initialize the new table entry. */
        n->next = table->table[i];
        table->num++;
        table->table[i] = n;
        hash = n->hashValue;
    }
    AUnlockHashMappings();

    return hash;
}


/* Initialize an identity-based hash table. */
static ABool InitializeTable(HashMappingTable *t)
{
    int i;
    t->size = INITIAL_HASH_MAPPING_TABLE_SIZE;
    t->num = 0;
    t->table = AAllocStatic(sizeof(HashValueNode *) * t->size);
    for (i = 0; i < t->size; i++)
        t->table[i] = NULL;
    return TRUE;
}


static HashValueNode *AllocNode(void *ptr)
{
    if (HashFreeList != NULL) {
        HashValueNode *n = HashFreeList;
        HashFreeList = HashFreeList->next;
        n->ptr = ptr;
        n->hashValue = AIntToValue(HashValueCounter++);
        n->next = NULL;
        return n;
    } else {
        HashValueNode *n = AAllocStatic(sizeof(HashValueNode) *
                                        HASH_NODE_BUCKET_SIZE);
        int i;

        if (n == NULL)
            return NULL;

        for (i = HASH_NODE_BUCKET_SIZE - 1; i >= 0; i--) {
            n[i].next = HashFreeList;
            HashFreeList = n + i;
        }
        return AllocNode(ptr);
    }
}


static void FreeNode(HashValueNode *n)
{
    n->next = HashFreeList;
    HashFreeList = n;
}


static AValue GetHashMapping(HashMappingTable *t, void *address)
{
    int i = PtrHashValue(address) & (t->size - 1);
    HashValueNode *n = t->table[i];
    while (n != NULL) {
        if (n->ptr == address)
            return n->hashValue;
        n = n->next;
    }
    return AError;
}


/* Retire all references in the nursery hash table to the old gen hash
   table. This modified the nodes in-place and never fails. */
void AMoveNewGenHashMappingsToOldGen(void)
{
    int i;
    HashMappingTable *nt = &NewGenTable;
    HashMappingTable *ot = &OldGenTable;

    for (i = 0; i < nt->size; i++) {
        HashValueNode *n = nt->table[i];
        while (n != NULL) {
            HashValueNode *nn = n;
            n = n->next;

            if (!AIsNewGenBlock((AValue *)nn->ptr)) {
                void *oldAddress;
                int oi;

                oldAddress = AGetIndirectPointer(nn->ptr);
                oi = PtrHashValue(oldAddress) & (ot->size - 1);
                nn->ptr = oldAddress;
                nn->next = ot->table[oi];
                ot->table[oi] = nn;
            }
        }
        nt->table[i] = NULL;
    }

    ot->num += nt->num;
    nt->num = 0;
}


/* Remove references from the old gen hash table that refer to objects that
   were just freed (swept) by the garbage collector. */
void ASweepOldGenHashMappings(void)
{
    int i;
    HashMappingTable *t = &OldGenTable;

    for (i = 0; i < t->size; i++) {
        HashValueNode *n = t->table[i];
        HashValueNode *p = NULL;
        while (n != NULL) {
            HashValueNode *nn = n->next;

            if (!AIsMarked(n->ptr)) {
                n->next = HashFreeList;
                HashFreeList = n;
                if (p != NULL)
                    p->next = nn;
                else
                    t->table[i] = nn;
            } else
                p = n;
            n = nn;
        }
    }
}


static ABool GrowHashTable(HashMappingTable *t)
{
    HashValueNode **a;
    int i;
    int newSize = t->size * 2;

    a = AAllocStatic(sizeof(HashValueNode *) * newSize);
    if (a == NULL)
        return FALSE;

    for (i = 0; i < newSize; i++)
        a[i] = NULL;

    for (i = 0; i < t->size; i++) {
        HashValueNode *n = t->table[i];
        while (n != NULL) {
            HashValueNode *nn = n->next;
            int ai = PtrHashValue(n->ptr) & (newSize - 1);
            n->next = a[ai];
            a[ai] = n;
            n = nn;
        }
    }

    AFreeStatic(t->table);
    t->table = a;
    t->size = newSize;

    return TRUE;
}
