/* cmem.c - Compiler memory management

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Compiler memory management. A separate heap is used for compiler data
   structures since it can be used without locking in the compiler (it is
   locked during an entire compiler run).

   Note that AAllocStatic etc. can be used in the compiler as well. */

#include "cutil.h"
#include "symtable.h"
#include "gc.h"
#include "internal.h"


#define CALLOC_UNIT sizeof(AValue)

#define NUM_CFREELISTS 8
#define CALLOC_BUCKET_LEN 8

#define MIN_CALLOC_SIZE ARoundUp(AMax(sizeof(ASymbol), sizeof(ASymbolInfo)), \
                                 CALLOC_UNIT)
#define MAX_QUICK_SIZE (MIN_CALLOC_SIZE + (NUM_CFREELISTS - 2) * CALLOC_UNIT)

#define BLOCK_SIZE 1024


#define GetCFreeListNum(size) \
    ((unsigned long)((size) - MIN_CALLOC_SIZE) / CALLOC_UNIT)


typedef struct CFreeListNode_ {
    struct CFreeListNode_ *next;
    unsigned long size;
} CFreeListNode;


static CFreeListNode CFreeList[NUM_CFREELISTS];


void *ACAlloc(unsigned long size)
{
    CFreeListNode *node;
    CFreeListNode *new;

    /* Handle small blocks quickly as a special case. */
    if (size <= MIN_CALLOC_SIZE) {
        if (CFreeList[0].next != NULL) {
            new = CFreeList[0].next;
            CFreeList[0].next = new->next;
            return new;
        }
        size = MIN_CALLOC_SIZE; 
    } else
        size = ARoundUp(size, CALLOC_UNIT);

    if (size <= MAX_QUICK_SIZE) {
        CFreeListNode *bucket;
        int list;
        int i;

        list = GetCFreeListNum(size);
        if (CFreeList[list].next != NULL) {
            new = CFreeList[list].next;
            CFreeList[list].next = new->next;
            return new;
        }

        /* Add CALLOC_BUCKET_LEN blocks to the free list. */
        
        bucket = ACAlloc(size * CALLOC_BUCKET_LEN);
        if (bucket == NULL)
            return NULL;
        
        node = bucket;
        for (i = 0; i < CALLOC_BUCKET_LEN - 2; i++) {
            node->next = APtrAdd(node, size);
            node = node->next;
        }

        node->next = NULL;
        CFreeList[list].next = bucket;

        return APtrAdd(node, size);
    }

    /* Try to find a large enough free block. */
    node = &CFreeList[NUM_CFREELISTS - 1];
    while (node->next != NULL && node->next->size < size)
        node = node->next;

    /* Did we find one? */
    if (node->next != NULL) {
        unsigned long sizeLeft;

        sizeLeft = node->next->size - size;
        new = APtrAdd(node->next, sizeLeft);
        
        if (sizeLeft <= MAX_QUICK_SIZE) {
            CFreeListNode *oldNext = node->next;
            
            node->next = node->next->next;
            
            if (sizeLeft >= MIN_CALLOC_SIZE) {
                int list = GetCFreeListNum(sizeLeft);
                oldNext->next = CFreeList[list].next;
                CFreeList[list].next = oldNext;
            }
        } else
            node->next->size = sizeLeft;

        return new;
    } else {
        /* Grow the heap. */
        
        unsigned long growSize;

        /* Grow by at least BLOCK_SIZE bytes. */
        growSize = AMax(BLOCK_SIZE, size);
        
        node = AAllocStatic(growSize);
        if (node == NULL)
            return NULL;
        
        /* Add the block to the last free list. */
        node->next = CFreeList[NUM_CFREELISTS - 1].next;
        node->size = growSize;
        CFreeList[NUM_CFREELISTS - 1].next = node;

        /* The recursive call will succeed without further recursion. */
        return ACAlloc(size);
    }
}


void ACFree(void *block, unsigned long size)
{
    CFreeListNode *node;

    node = block;

    if (size <= MIN_CALLOC_SIZE) {
        node->next = CFreeList[0].next;
        CFreeList[0].next = node;
    } else {
        int list;
        
        size = ARoundUp(size, CALLOC_UNIT);
        if (size <= MAX_QUICK_SIZE)
            list = GetCFreeListNum(size);
        else {
            list = NUM_CFREELISTS - 1;
            node->size = size;
        }
        
        node->next = CFreeList[list].next;
        CFreeList[list].next = node;
    }
}


void AInitializeCAlloc(void)
{
    int i;

    for (i = 0; i < NUM_CFREELISTS; i++)
        CFreeList[i].next = NULL;
}
