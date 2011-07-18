/* gc_internal.h - Some internal GC related defines and declarations

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef GC_INTERNAL_H_INCL
#define GC_INTERNAL_H_INCL


#include "mem.h"
#include "heap.h"
#include "internal.h"


/* There is a bit in a mark bitmap for every this many bytes of heap. */
#define A_MARK_BITMAP_UNIT AMin(A_ALLOC_UNIT, A_FLOAT_SIZE)

/* Number of bits in a single bitmap primitive item. */
#define A_MARK_BITMAP_INT_BITS (8 * A_MARK_BITMAP_INT_SIZE)


/* Thread heap is grown in multiples of MIN_THREAD_HEAP_INCREMENT bytes. */
#define A_MIN_THREAD_HEAP_INCREMENT 2048 /* 1024 */


/* FIX: are the following ok? New gen seems a little largish.. */
#define A_INITIAL_OLD_GEN_SIZE (64 * 1024 + A_ALORE_STACK_SIZE)
#define A_INITIAL_NURSERY_SIZE (64 * 1024)


/* FIX: ok? */
/* Large enough traverse stack size for new generation initially. Whenever
   nursery is enlarged, the stack must be enlarged by the same factor. This
   stack cannot be grown while new generation gc is active, so it must be
   large enough for arbitrary new generation contents.

   In the worst case scenario, nursery is filled with a linked list of
   instances with just two members (3 AValues per object, including header),
   maximizing traverse stack load. */
#define A_NEW_GEN_STACK_INITIAL_LENGTH \
    (A_INITIAL_NURSERY_SIZE / AGetBlockSize(sizeof(AValue) * 3) +        \
     2 * AScale(A_INITIAL_NURSERY_SIZE, A_MAX_BIG_BLOCK_RELATIVE_SIZE) / \
     A_MIN_BIG_BLOCK_SIZE + A_NEW_GEN_ROOT_SET_SIZE + 8)

/* Assume that there is always at least MIN_LIVE_DATA_SIZE bytes of live
   data. If there is less, it might not be garbage collected. */
#define A_MIN_LIVE_DATA_SIZE (A_INITIAL_OLD_GEN_SIZE / 2)


/* Initial length of the mark traverse stack FIX: is it large enough?
   What if there are lots of threads? */
#define A_OLD_GEN_STACK_INITIAL_LENGTH 64


/* Special value returned by Mark if it runs out of memory */
#define A_MARK_FAILED A_SHORT_INT_MAX


/* FIX: this might not be ok? */
#define AGetIndirectPointer(block) \
    ((void *)(*(AValue *)(block) | A_HEAP_PTR_MASK))
/* Returns a value that has the same type as val and contents of *block. */
#define AGetIndirectValue(val, block) \
    (((val) & A_GENERIC_MASK) | (*(block) & ~A_HEAP_PTR_MASK))

#define ASetIndirectValue(ptr, dst) \
    (*(ptr) = (AValue)(dst) & ~A_HEAP_PTR_MASK)


/* Return a value with the type of val that points at disp. */
#define ADisplaceValue(val, disp) \
    (((val) & A_GENERIC_MASK) | ((AValue)(disp) & ~A_HEAP_PTR_MASK))


#define A_STACK_TOP (AValue)NULL
#define A_STACK_BOTTOM (AValue)NULL
#define A_STACK_FILLER (A_STACK_TOP + 4) /* An arbitrary int != A_STACK_TOP */


/* Smallest block size that is never allocated from the nursery */
#define A_MIN_BIG_BLOCK_SIZE 1024


/* This block acts as a header for new generation big blocks. The big blocks
   are linked together using these. */
typedef struct ANewGenNode_ {
    AValue header;
    AValue size;
    struct ANewGenNode_ *next;
} ANewGenNode;

#define A_NEW_GEN_NODE_HEADER_SIZE AGetBlockSize(sizeof(ANewGenNode))


/* Return the actual block associated with a NewGenNode. */
#define AGetBigBlockData(node) \
    APtrAdd(node, AGetBlockSize(sizeof(ANewGenNode)))


/* Return the word index to the mark-sweep bit field for block ptr if the
   heap block starts at begin. */
#define AGetBitWordIndex(begin, ptr) \
    (APtrDiff(ptr, begin) >> \
     (ALog2(8) + ALog2(A_MARK_BITMAP_UNIT) + ALog2(A_MARK_BITMAP_INT_SIZE)))

#define AGetBitIndex(begin, ptr) \
    ((APtrDiff(ptr, begin) >> ALog2(A_MARK_BITMAP_UNIT)) & \
     (A_MARK_BITMAP_INT_BITS - 1))


/* Is ptr marked in a heap block beginning at begin and ending at end?
   The bit field starts at end. */
#define AIsMarkedBounded(ptr, begin, end) \
    (((AMarkBitmapInt *)(end))[AGetBitWordIndex(begin, ptr)] & \
     (1UL << ((APtrDiff(ptr, begin) >> ALog2(A_MARK_BITMAP_UNIT)) & \
              (A_MARK_BITMAP_INT_BITS - 1))))

/* FIX: perhaps use something else.. */
#define AClearMarksUntil(ptr, begin, end) \
    (((AMarkBitmapInt *)(end))[AGetBitWordIndex(begin, ptr)] &= \
     -1UL << ((APtrDiff(ptr, begin) >> ALog2(A_MARK_BITMAP_UNIT)) & \
              (A_MARK_BITMAP_INT_BITS - 1)))

#define ASetMarkBounded(ptr, begin, end) \
    (((AMarkBitmapInt *)(end))[AGetBitWordIndex(begin, ptr)] |= \
     (1UL << ((APtrDiff(ptr, begin) >> ALog2(A_MARK_BITMAP_UNIT)) &     \
              (A_MARK_BITMAP_INT_BITS - 1))))


/* Is the block already copied during copying gc? */
#define AIsCopied(ptr) \
    (!AIsNewGenBlock(ptr))


#define AGetHeapBlockDataEnd(block) \
    APtrAdd((block), AGetBitFieldIndex((block)->size))

#define AIsInsideHeapBlock(block, begin, end) \
    ((void *)(block) < (void *)(end) && (void *)(block) >= (void *)(begin))


#define A_MAX_NURSERY_SIZE (128/*256*/ * 1024)

/* The minimum size of nursery in 1/32's of OldGenSize */
#define A_MIN_NURSERY_RELATIVE_SIZE 2

/* The maximum size of big blocks in new generation in 1/32's of NurserySize */
#define A_MAX_BIG_BLOCK_RELATIVE_SIZE 32

/* Maximum old generation size in 1/32's of previously known live data size */
#define A_MAX_OLD_GEN_GROW_BEFORE_GC 64


#define AIsItTimeToGrowNursery() \
    (AScale(AOldGenSize, A_MIN_NURSERY_RELATIVE_SIZE) >= ANurserySize \
     && ANurserySize < A_MAX_NURSERY_SIZE)


#define AIsItTimeForFullGC() \
    (AOldGenSize > AScale(ALiveDataSize, A_MAX_OLD_GEN_GROW_BEFORE_GC))

#define AIsItTimeForNewGenGC() \
    ((int)AAllocAmount - 2 * (int)ANewGenLargestBlockSize > \
         (int)AScale(ANurserySize, A_MAX_BIG_BLOCK_RELATIVE_SIZE))

/* How much work is done in one incremental pass */
#define A_INCREMENTALITY (128 * 1024)


extern int AGCState;
extern ABool ANewRefPtrListInvalid;


/* Push the span between begin and end to a AValue stack at top. */
#define APush(top, begin, end) \
    ((begin) != (end)          \
     ? (top)[1] = (AValue)(end), (top)[2] = (AValue)(begin), top + 2 \
     : top)

#define APushThreadArgBuffer(top) \
    APush(top, AThreadArgBuffer, AThreadArgBuffer + \
          A_THREAD_ARG_BUFFER_SIZE * 3)


typedef struct AStaticBlock_ {
    AValue header;
    struct AStaticBlock_ *next;
    struct AStaticBlock_ *prev;
} AStaticBlock;

#define A_STATIC_BLOCK_HEADER_SIZE AGetBlockSize(sizeof(AStaticBlock))


/* FIX: perhaps should be somewhere else.. and what about the value? */
#define A_NEW_GEN_ROOT_SET_SIZE 16


#include "gc_float.h"


#endif
