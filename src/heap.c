/* heap.c - Managing blocks within heap

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Heap management. Alore implements its own memory management using a heap
   that is separate from the ordinary C heap used by malloc, free, etc. */

#include "runtime.h"
#include "gc.h" /* FIX?? */
#include "gc_internal.h" /* FIX?? */
#include "heap.h"
#include "internal.h"
#include "debug_runtime.h"


static ABool GrowHeap(unsigned long reqSize);


/* Linked list of heap blocks */
AHeapBlock *AHeap;

/* Maximum total size of the global heap, including mark bitfields */
unsigned long AMaxHeapSize;

/* Total size of the heap blocks, including mark bitfields */
static unsigned long HeapSize;

AFreeListNode *AFreeList[A_NUM_FREELISTS];

void *ACurFreeBlock;
unsigned long ACurFreeBlockSize;

void *ALastBlock;
void *ALastBlockEnd;


void *(*AGrowNursery)(void *oldNursery, Asize_t oldSize,
                      Asize_t newSize);
void *(*AMoreHeap)(AHeapBlock *block, Asize_t growSize,
                   Asize_t *realGrow);
void (*AFreeHeapBlock)(AHeapBlock *block);


AFreeListNode AListTerminator = { -1UL, NULL, NULL, NULL };
AFreeListNode AHeapTerminator = { -1UL, NULL, NULL, NULL };


/* Allocate a block from the global heap. The size must be rounded up by the
   caller to a valid internal block size. Return NULL if failed.
   
   If AGCState is A_GC_MARK_EXE, never perform garbage collection (since
   garbage collection is already active).
   IDEA: The previous case might be bproblematic, as it might cause garbage
   collection to fail in some (rare) cases.
   
   NOTE: The heap must be locked. */
void *AGlobalAlloc(unsigned long size)
{
    static ABool alreadyTried = FALSE;
    
    AFreeListNode *node;
    AFreeListNode *result;
    unsigned listNum;

    /* Allocate from CurFreeBlock if it is small enough. */
    if (AIsLargeEnoughFreeBlock(ACurFreeBlockSize, size)
            && ACurFreeBlockSize <= 512) {
        ACurFreeBlockSize -= size;
        result = ACurFreeBlock;
        ACurFreeBlock = APtrAdd(result, size);
    } else {
        if (AIsInsideLastBlock(ACurFreeBlock))
            ALastBlock = ACurFreeBlock;
        
        if (ACurFreeBlockSize > 0)
            AAddFreeBlockNoFill(ACurFreeBlock, ACurFreeBlockSize);
        ACurFreeBlockSize = 0;
        
        listNum = AGetFreeListNum(size);

        if (AFreeList[listNum] != &AListTerminator) {
            /* Try to find a block from the list with enough room. If no large
               enough block is found, the loop is terminated when the list
               terminator node (with maximal block size) is encountered. */
            node = AFreeList[listNum];
            while (!AIsLargeEnoughFreeBlock(
                       AGetFreeListBlockSize(&node->header),
                       size))
                node = node->next;
            if (node == &AListTerminator) {
                /* Could not find any. Find the next list with a large enough
                   free block. Only the first block of each list is consulted.
                   The smallest suitable block is not always found if the
                   minimal block size is different from the allocation unit.
                   IDEA: Always find the smallest suitable block! */
                do {
                    listNum++;
                    node = AFreeList[listNum];
                } while (node == &AListTerminator
                         || !AIsLargeEnoughFreeBlock(
                             AGetFreeListBlockSize(&node->header), size));
            }
        } else {
            /* Find the next list with free blocks. Only the first block of
               each list is consulted. See the comment above for some
               caveats. */
            do {
                listNum++;
                node = AFreeList[listNum];
            } while (node == &AListTerminator
                     || !AIsLargeEnoughFreeBlock(
                         AGetFreeListBlockSize(&node->header), size));
        }

        /* If there are no large enough free blocks, */
        if (node == &AHeapTerminator) {
            /* First try to allocate more space for the heap. If it fails,
               try garbage collecting for more space. */
            if (GrowHeap(size))
                result = AGlobalAlloc(size);
            else if (!alreadyTried && AGCState != A_GC_MARK_EXE
                     && ACollectGarbageForced()) {
                /* Try allocating again after garbage collection; mark that
                   we already have done gc to avoid infinite recursion. */
                alreadyTried = TRUE;
                result = AGlobalAlloc(size);
                alreadyTried = FALSE;
            } else
                result = NULL;
        } else {
            unsigned long blockSize;
            
            blockSize = AGetFreeListBlockSize(&node->header);
            ACurFreeBlockSize = blockSize - size;
            
#ifdef A_DEBUG
            if (ACurFreeBlockSize > 0
                    && AGetBlockSize(ACurFreeBlockSize) != ACurFreeBlockSize)
                ADebugPrint(("Current free block size invalid (%d)\n",
                             (int)ACurFreeBlockSize));
#endif

            if (blockSize >= A_SMALLEST_NONUNIFORM_LIST_SIZE
                && node->child != NULL) {
                result = node->child;
                node->child = result->child;
                if (result->child != NULL)
                    result->child->prev = node;
            } else {
                if (blockSize >= sizeof(AFreeListNode)) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                } else
                    AFreeList[listNum] = node->next;
        
                result = node;
            }

            ACurFreeBlock = APtrAdd(result, size);
        }
    }

    return result;
}


/* Add a free block of specified size to the list of free blocks. The size
   argument accounts for the entire block, including header, and must be
   rounded up to a valid block size. */
#ifndef A_DEBUG
void AAddFreeBlock(void *block, unsigned long size)
#else
void AAddFreeBlock_Debug(void *block, unsigned long size, ABool isFill)
#endif
{
    unsigned listNum;
    AFreeListNode *cur;
    AFreeListNode *node;
    AValue header;

#ifdef A_DEBUG
    /* Verify that the free block is not too small and is rounded properly. */
    if (AGetBlockSize(size) != size)
        ADebugError(("Add free block of invalid size (%d)\n", (int)size));
#endif

#ifdef A_DEBUG_FILL_FREE_BLOCKS
    if (isFill) {
        AValue *ptr = block;
        int i;
        
        for (i = 0; i < size / sizeof(AValue); i++)
            ptr[i] = 0xdeadbeef;
    }
#endif

    node = block;

    header = (size - sizeof(AValue)) | A_FREE_BLOCK_FLAG;
    node->header = header;
    
    if (size < A_SMALLEST_NONUNIFORM_LIST_SIZE) {
        listNum = size >> ALog2(A_ALLOC_UNIT);

        if (size < sizeof(AFreeListNode)) {
            node->next = AFreeList[listNum];
            AFreeList[listNum] = node;
        } else {
            node->prev = AGetFreeList(listNum);
            node->next = AFreeList[listNum];
            node->next->prev = node;
            AFreeList[listNum] = node;
        }
    } else {
        listNum = AGetFreeListNum(size);

        cur = AGetFreeList(listNum);

        while (cur->next->header < header)
            cur = cur->next;

        if (cur->next->header == header) {
            cur = cur->next;
            node->next = NULL;
            node->prev = cur;
            node->child = cur->child;
            if (node->child != NULL)
                node->child->prev = node;
            cur->child = node;
        } else {
            node->child = NULL;
            node->next = cur->next;
            node->prev = cur;
            node->next->prev = node;
            cur->next = node;
        }
    }
}


ABool AInitializeHeap(unsigned long minSize)
{
    int i;

    for (i = 0; i < A_NUM_FREELISTS - 1; i++)
        AFreeList[i] = &AListTerminator;

    AFreeList[i] = &AHeapTerminator;

    if (!GrowHeap(minSize))
        return FALSE;
    
    return TRUE;
}


void AFreeHeap(void)
{
    if (AFreeHeapBlock != NULL) {
        AHeapBlock *heap;

        heap = AHeap;
        while (heap != NULL) {
            AHeapBlock *next = heap->next;
            AFreeHeapBlock(heap);
            heap = next;
        }
    }
}


ABool GrowHeap(unsigned long reqSize)
{
    AHeapBlock *new;
    Asize_t size;
    Asize_t bitInd;
    Asize_t bitSize;

    ADebugStatusMsg(("Grow heap by %d bytes\n", (int)reqSize));
#ifdef A_DEBUG_VERIFY_MEMORY
    if (!AIsNewGenGCActive)
        ADebugVerifyMemory();
#endif

    /* Scale the requested size so that there will be enough space for the mark
       bitfield and heap block header. */
    reqSize = reqSize + 2 * AGetBitFieldSize(reqSize) + sizeof(AHeapBlock);

    /* Disallow growing the heap past the maximum size. */
    if (HeapSize + reqSize > AMaxHeapSize) {
        ADebugStatusMsg(("Heap max size check failed\n"));
        return FALSE;
    }
    
    /* Round requested size up to reduce the number of needed MoreHeap()
       calls. */
#ifdef A_MIN_HEAP_GROW_FACTOR
    if (reqSize < AScale(HeapSize, A_MIN_HEAP_GROW_FACTOR))
        reqSize = AScale(HeapSize, A_MIN_HEAP_GROW_FACTOR);
#else
    if (reqSize < A_MIN_HEAP_GROW)
        reqSize = A_MIN_HEAP_GROW;
#endif

    /* Do not grow the heap past the maximum heap size. */
    reqSize = AMin(reqSize, AMaxHeapSize - HeapSize);
    
    new = AMoreHeap(AHeap, reqSize, &size);
    if (new == NULL)
        return FALSE;

    HeapSize += size;
    AGCStat.heapSize = AGetBitFieldIndex(HeapSize);

#ifdef A_DEBUG_SHOW_HEAP_POINTERS
    printf("DEBUG: Heap block at %p\n", new);
#endif
    
#ifdef A_DEBUG
    /* Check that the new heap block is within the supported address range. */
    if ((void *)new < A_MEM_START || (void *)new >= A_MEM_END
        || APtrAdd(new, size) > A_MEM_END)
        ADebugError(("AMoreHeap result out of valid memory area: %p "
                     "(%d bytes)\n", new, (int)size));
#endif
    
    /* Can we grow the last heap block? */
    if (AHeap != NULL && new == AGetHeapBlockEnd(AHeap)) {
        Asize_t oldBitInd;
        Asize_t oldBitSize;
        
        oldBitInd  = AGetBitFieldIndex(AHeap->size);
        oldBitSize = AGetBitFieldSize(AHeap->size);

        AHeap->size += size;

        bitInd  = AGetBitFieldIndex(AHeap->size);
        bitSize = AGetBitFieldSize(AHeap->size);

        /* FIX: if not gc active, no need to copy bit fields.. */
        AMoveMem(APtrAdd(AHeap, bitInd), APtrAdd(AHeap, oldBitInd),
                 oldBitSize);
        AClearMem(APtrAdd(AHeap, bitInd + oldBitSize), bitSize - oldBitSize);
        
        AInactivateCurFreeBlock();

        /* If the last block in the original heap block is free and it can
           be grown, concatenate the newly allocated free space to this free
           block. */
        if (ALastBlockEnd == APtrAdd(AHeap, oldBitInd)
            && APtrDiff(ALastBlockEnd, ALastBlock) >= sizeof(AFreeListNode)) {
            ARemoveBlockFromFreeList(ALastBlock);
            ALastBlockEnd = APtrAdd(AHeap, bitInd);
        } else {
            /* Cannot concatenate blocks. Construct a new last block. */
            ALastBlock = APtrAdd(AHeap, oldBitInd);
            ALastBlockEnd = APtrAdd(ALastBlock, bitInd - oldBitInd);
        }

        AAddFreeBlock(ALastBlock, APtrDiff(ALastBlockEnd, ALastBlock));
    } else {
        /* Create a new free heap block. */

        new->next = AHeap;
        new->size = size;
        AHeap = new;

        bitInd  = AGetBitFieldIndex(size);
        bitSize = AGetBitFieldSize(size);
        AClearMem(APtrAdd(new, bitInd), bitSize);

        /* Create a new last block. */
        ALastBlock = new + 1;/* FIX */
        ALastBlockEnd = APtrAdd(new, bitInd);

        AAddFreeBlock(new + 1, bitInd - sizeof(AHeapBlock)); /* FIX */
    }

    ADebugStatusMsg(("End grow heap\n", reqSize));
#ifdef A_DEBUG
    if (!AIsNewGenGCActive)
        ADebugVerifyMemory();
#endif

    return TRUE;
}


void AInactivateCurFreeBlock(void)
{
    if (AIsInsideLastBlock(ACurFreeBlock))
        ALastBlock = ACurFreeBlock;

    if (ACurFreeBlockSize > 0) {
        AAddFreeBlock(ACurFreeBlock, ACurFreeBlockSize);

        ACurFreeBlockSize = 0;
    }

    ACurFreeBlock = NULL;
}


void ARemoveBlockFromFreeList(void *ptr)
{
    AFreeListNode *node;

    node = ptr;
    node->header ^= A_FREE_BLOCK_FLAG;

#ifdef A_DEBUG
    if (AGetFreeListBlockSize(&node->header) < sizeof(AFreeListNode))
        ADebugError(("Remove too small block from free list (%p, size %d)\n",
                     ptr, AGetFreeListBlockSize(&node->header)));
    if (node->header & A_FREE_BLOCK_FLAG)
        ADebugError(("Tried to remove non-free-block from free list (%p)\n",
                    ptr));
#endif
    
    if (node->next != NULL) {
        if (node->header >= 64 - sizeof(AValue)/*FIX*/
                && node->child != NULL) {
            node->prev->next = node->child;
            node->next->prev = node->child;
            node->child->prev = node->prev;
            node->child->next = node->next;
        } else {
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }
    } else {
        if (node->child != NULL)
            node->child->prev = node->prev;
        node->prev->child = node->child;
    }
}


/* Remove all free blocks from free lists that do not have back pointers (i.e.
   they are singly linked). */
void ARemoveSmallBlocksFromFreeList(void)
{
    int i;

    for (i = 1; i < A_FIRST_BACKLINKED_FREELIST; i++) {
        /* Remove all blocks from the free list and change them into non-
           pointer blocks. This is necessary since otherwise the blocks
           might contain garbage pointers. */
        while (AFreeList[i] != &AListTerminator) {
            /* Remove free block flag. */
            AFreeList[i]->header ^= A_FREE_BLOCK_FLAG;
            /* Convert free block length to non-pointer block length. */
            AFreeList[i]->header <<= 2;
            /* Add non-pointer block tag. */
            AFreeList[i]->header |= A_NON_POINTER_BLOCK_TAG;
            /* Remove from list. */
            AFreeList[i] = AFreeList[i]->next;
        }
    }
}
