/* heap.h - Managing blocks within heap

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef HEAP_H_INCL
#define HEAP_H_INCL

#include "mem.h"
#include "debug_runtime.h"


#define A_NUM_FREELISTS 64


/* If we do not have mmap and mremap, it is costly to grow the heap. Therefore
   grow it a large chunk at a time. */
#if !defined(HAVE_REMAP)
/* Grow the heap by at least this many 1/32's at a time. */
#define A_MIN_HEAP_GROW_FACTOR 5
#else
/* Grow the heap by at least this many bytes at a time. */
#define A_MIN_HEAP_GROW 65536
#endif


/* Return the rounded block size corresponding to a request of size bytes. */
#define AGetBlockSize(size) \
    (A_ALLOC_UNIT != A_MIN_BLOCK_SIZE \
     ? (size < A_MIN_BLOCK_SIZE       \
        ? A_MIN_BLOCK_SIZE : ((size) + A_ALLOC_UNIT - 1) & \
          ~(A_ALLOC_UNIT - 1)) \
     : ((size) + A_ALLOC_UNIT - 1) & ~(A_ALLOC_UNIT - 1))


/* Smallest block size that is put into a free list with multiple block
   sizes. */
#define A_SMALLEST_NONUNIFORM_LIST_SIZE 64

/* Index of the first free list that may contain blocks of multiple sizes. */
#define A_FIRST_NONUNIFORM_FREE_LIST \
    (A_SMALLEST_NONUNIFORM_LIST_SIZE / A_ALLOC_UNIT)

/* NOTE: Assumes 8 byte allocation unit */
#define AGetFreeListNum(size) \
    ((size) < 64     ? (size) / 8          : \
     (size) < 512    ? (size) / 32    + 6  : \
     (size) < 2048   ? (size) / 128   + 18 : \
     (size) < 8192   ? (size) / 1024  + 32 : \
     (size) < 32768  ? (size) / 4096  + 38 : \
     (size) < 311296 ? (size) / 16384 + 44 : 63)


/* This structure represents a free block in a list of free blocks. There are
   multiple free lists, and each free list contains blocks in a specific size
   range to make is faster to access free blocks. The smallest free blocks
   are stored in a different kind of free list that does not contain child and
   prev pointers, since those blocks are too small to contain a AFreeListNode
   structure in its entirety. */
typedef struct AFreeListNode_ {
    AValue header;
    struct AFreeListNode_ *next;
    struct AFreeListNode_ *child;
    struct AFreeListNode_ *prev;
} AFreeListNode;


/* Index of the first free list that contains block large enough to contain
   back pointers (i.e. it is a double linked list). The rest of the lists
   (in practice the first list in use) contains a singly linked list of
   blocks. */
#define A_FIRST_BACKLINKED_FREELIST AGetFreeListNum(sizeof(AFreeListNode))


/* The heap is composed of one or more heap blocks. Each heap block may
   contain multiple memory blocks (both allocated and free) and always contains
   a separate mark bitmap. If possible, the entire heap is stored in a single
   heap block, and that block is simply enlarged if more space is needed. */
typedef struct AHeapBlock_ {
    struct AHeapBlock_ *next;
    unsigned long size;
} AHeapBlock;


/* Return a pointer to the data area of a heap block. */
#define AGetHeapBlockData(bp) \
    ((AValue *)APtrAdd(bp, AGetBlockSize(sizeof(AHeapBlock))))

#define AGetHeapBlockEnd(bp) \
    APtrAdd(bp, (bp)->size)


void *AGlobalAlloc(unsigned long size);

ABool AInitializeHeap(unsigned long minSize);
void AFreeHeap(void);

#ifndef A_DEBUG

void AAddFreeBlock(void *block, unsigned long size);
#define AAddFreeBlockNoFill AAddFreeBlock

#else

void AAddFreeBlock_Debug(void *block, unsigned long size, ABool isFill);
#define AAddFreeBlock(block, size) AAddFreeBlock_Debug(block, size, TRUE)
#define AAddFreeBlockNoFill(block, size) \
    AAddFreeBlock_Debug(block, size, FALSE)

#endif


/* List of heap blocks */
extern AHeapBlock *AHeap;

extern unsigned long AMaxHeapSize;


extern AFreeListNode *AFreeList[A_NUM_FREELISTS];

extern AFreeListNode AListTerminator;
extern AFreeListNode AHeapTerminator;

/* The free block that was last accessed */
extern void *ACurFreeBlock;
extern unsigned long ACurFreeBlockSize;


/* Counter keeping track of how many bytes are allocated */
extern unsigned long AAllocAmount;


/* Both equality in <= is ok, since the block may be 0-sized. */
#define AIsInsideLastBlock(ptr) \
    ((void *)(ptr) >= ALastBlock && (void *)(ptr) <= ALastBlockEnd)


/* Return the size of bit field for a heap block of size bytes. */
/* FIX: This will probably depend on several factors, at least 64-bit Alore
        might require a different value. */
#define AGetBitFieldSize(size) \
    ((size) >> 6)

/* Return the byte index of bit field for a heap block of size bytes. */
#define AGetBitFieldIndex(size) \
    ((size) - AGetBitFieldSize(size))


/* Return pointer to the head node for free list num. */
#define AGetFreeList(num) \
    ((AFreeListNode *)&AFreeList[(num) - 1])


void AInactivateCurFreeBlock(void);


/* Determine if a free block is large enough to hold an allocated block.
   The somewhat tricky part is the case where the free block is only slighly
   larger than than the allocated block: if the remaining free space is less
   than the minimum block size, the free block cannot be used even though it is
   larger than the requested amount of space. */
#if A_MIN_BLOCK_SIZE == A_ALLOC_UNIT
# define AIsLargeEnoughFreeBlock(freeBlockSize, blockSize) \
    ((freeBlockSize) >= (blockSize))
#else
# define AIsLargeEnoughFreeBlock(freeBlockSize, blockSize) \
    ((freeBlockSize) >= (blockSize) + A_MIN_BLOCK_SIZE     \
     || (freeBlockSize) == (blockSize))
#endif


void ARemoveBlockFromFreeList(void *ptr);
void ARemoveSmallBlocksFromFreeList(void);


extern void *ALastBlock;
extern void *ALastBlockEnd;


#endif
