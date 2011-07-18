/* mem.h - Low-level macros related to the physical structure of the heap

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This header includes constants and macros related to the physical structure
   of memory blocks in heap. */

#ifndef MEM_H_INCL
#define MEM_H_INCL

#include "value.h"


/* Each memory block has a header (an integer of type AValue) that defines
   the size and the type of the block. The header is essentially a bit field
   with a 2-bit field reseved for the block type and a 1-bit generation flag
   (new/old gen). The interpretation of the rest of the bits depends on the
   block type. The definitions below are related to block headers. */


/* Block header flag set in new generation blocks */
#define A_NEW_GEN_FLAG (1UL << (A_VALUE_BITS - 1))

/* Block header flag set in free blocks */
#define A_FREE_BLOCK_FLAG (1UL << (A_VALUE_BITS - 2))


/* The lower 2 bits of the block header contain a block type tag.
   NOTE: Do not modify! Otherwise several things will break! */
#define A_VALUE_BLOCK_TAG          0  /* Block contains values (this is also
                                         used for free blocks) */
#define A_NON_POINTER_BLOCK_TAG    1  /* Block contains raw binary data */
#define A_INSTANCE_BLOCK_TAG       2  /* Block contains a class instance */
#define A_MIXED_BLOCK_TAG          3  /* Block contains values + raw data */

#define A_BLOCK_TYPE_TAG_MASK 3

/* Bit mask for getting block type tag and gc flag from a block header. */
#define A_HEADER_MASK (A_NEW_GEN_FLAG | A_BLOCK_TYPE_TAG_MASK)


/* Return the rounded block size corresponding to a request of size bytes. */
#define AGetBlockSize(size) \
    (A_ALLOC_UNIT != A_MIN_BLOCK_SIZE \
     ? (size < A_MIN_BLOCK_SIZE       \
        ? A_MIN_BLOCK_SIZE            \
        : ((size) + A_ALLOC_UNIT - 1) & ~(A_ALLOC_UNIT - 1)) \
     : ((size) + A_ALLOC_UNIT - 1) & ~(A_ALLOC_UNIT - 1))


/* Is the block at valPtr a value block? */
#define AIsValueBlock(valPtr) \
    ((*(valPtr) & A_BLOCK_TYPE_TAG_MASK) == A_VALUE_BLOCK_TAG)

/* Return the length of value data in the block at valPtr (in bytes). */
#define AGetValueBlockDataLength(valPtr) \
    (*(valPtr) & ~A_HEADER_MASK)

/* Return the length of the value block at valPtr (in bytes). */
#define AGetValueBlockSize(valPtr) \
    AGetBlockSize(AGetValueBlockDataLength(valPtr) + sizeof(AValue))


/* Is block at valPtr a non-pointer block? */
#define AIsNonPointerBlock(valPtr) \
    ((*(valPtr) & A_BLOCK_TYPE_TAG_MASK) == A_NON_POINTER_BLOCK_TAG)

/* Return the length of binary data in the block at valPtr (in bytes). */
#define AGetNonPointerBlockDataLength(valPtr) \
    ((*(valPtr) & ~A_HEADER_MASK) >> 2)

/* Return the length of binary data in the block at valPtr (in bytes, as an
   Int value). */
#define AGetNonPointerBlockDataLengthAsInt(valPtr) \
    (*(valPtr) & ~A_HEADER_MASK) /* FIX: works always? */

/* Return the length of the value block at valPtr (in bytes). */
#define AGetNonPointerBlockSize(valPtr) \
    AGetBlockSize(AGetNonPointerBlockDataLength(valPtr) + sizeof(AValue))


/* Is block at valPtr a mixed block? */
#define AIsMixedBlock(valPtr) \
    ((*(valPtr) & A_BLOCK_TYPE_TAG_MASK) == A_MIXED_BLOCK_TAG)

/* Return the length of all data in the block at valPtr (in bytes). The two
   header values are included in the count. */
#define AGetMixedBlockDataLength(valPtr) \
    (((*(valPtr) & ~A_HEADER_MASK) >> 2))

/* Return the length of value data in the mixed block at valPtr
   (in bytes). */
#define AGetMixedBlockValueDataLength(valPtr) \
    ((valPtr)[1])

/* Return the length of the mixed block at valPtr (in bytes). */
#define AGetMixedBlockSize(valPtr) \
    AGetBlockSize(AGetMixedBlockDataLength(valPtr))

    
/* Is the block at valPtr a class instance block? */
#define AIsInstanceBlock(valPtr) \
    ((*(valPtr) & A_BLOCK_TYPE_TAG_MASK) == A_INSTANCE_BLOCK_TAG)

/* Return the length of value data in the instance block at valPtr (in
   bytes), NOT including the header. */
#define AGetInstanceBlockDataLength(valPtr) \
    (AGetInstanceType(valPtr)->totalNumVars * sizeof(AValue))

/* Return pointer to binary data in given instance. */
#define AGetInstanceData(instPtr) \
    APtrAdd(instPtr, AGetInstanceType(instPtr)->dataOffset)

/* Return the length of the instance block at valPtr (in bytes). */
#define AGetInstanceBlockSize(valPtr) \
    (AGetInstanceType(valPtr)->instanceSize)


/* It the block at ptr a free list block? Free blocks are implemented as value
   blocks with the free block flag defined to distinguish them from actual
   value blocks. Note that free list blocks are really not value blocks, i.e
   they are a separate block type. The value block tag is only resused for
   them. */
#define AIsFreeListBlock(valPtr) \
    ((*(valPtr) & (A_FREE_BLOCK_FLAG | A_BLOCK_TYPE_TAG_MASK)) == \
     (A_VALUE_BLOCK_TAG | A_FREE_BLOCK_FLAG))

/* Return the size of the free list block at valPtr (in bytes). */
#define AGetFreeListBlockSize(valPtr) \
    ((*(valPtr) & ~(A_FREE_BLOCK_FLAG | A_BLOCK_TYPE_TAG_MASK)) + \
     sizeof(AValue))


/* Initialize a non-pointer block that contains size bytes + header. */
#define AInitNonPointerBlock(block, size) \
    (*(block) = ((size) << 2) | A_NON_POINTER_BLOCK_TAG | A_NEW_GEN_FLAG)

/* Initialize a non-pointer block in old generation. */
#define AInitNonPointerBlockOld(block, size) \
    (*(block) = ((size) << 2) | A_NON_POINTER_BLOCK_TAG)

/* Initialize a value block that contains size bytes worth of values
   (size == length * sizeof(AValue)). */
#define AInitValueBlock(block, size) \
    (*(block) = (size) | A_VALUE_BLOCK_TAG | A_NEW_GEN_FLAG)

/* Initialize a value block in old generation (size == length *
   sizeof(AValue)). */
#define AInitValueBlockOld(block, size) \
    (*(block) = (size) | A_VALUE_BLOCK_TAG)

/* Initialize a mixed block of size bytes, including header, and valueLen
   values. */
#define AInitMixedBlock(block, size, valueLen) \
    (*(block) = ((size) << 2) | A_MIXED_BLOCK_TAG | A_NEW_GEN_FLAG, \
     (block)[1] = (valueLen) * sizeof(AValue))

/* Initialize a mixed block in old generation . */
#define AInitMixedBlockOld(block, size, valueLen) \
    (*(block) = ((size) << 2) | A_MIXED_BLOCK_TAG, \
     (block)[1] = (valueLen) * sizeof(AValue))


/* Initialize an instance block with the given type as either a new generation
   block (gen == A_NEW_GEN_FLAG) or old generation block (gen == 0). */
#define AInitInstanceBlockGen(block, type, gen)                            \
    ((block)[0] = ((AValue)(type) & ~A_HEAP_PTR_MASK) | A_INSTANCE_BLOCK_TAG |\
     A_NEW_GEN_FLAG)


/* Initialize an instance block with the given type. */
#define AInitInstanceBlock(block, type) \
    AInitInstanceBlockGen(block, type, A_NEW_GEN_FLAG)


#define AIsNewGenBlock(block) \
    (*(block) & A_NEW_GEN_FLAG)

#define AIsInNursery(ptr) \
    ((void *)(ptr) >= (void *)ANurseryBegin \
     && ((void *)(ptr) < (void *)ANurseryEnd))


extern char *ANurseryBegin;
extern char *ANurseryEnd;
    

/* FIX */
#define A_EMPTY_THREAD_HEAP_PTR NULL


struct AThread_;


/* FIX: Move the declarations below somewhere else! */
ABool AGrowNewRefList(struct AThread_ *t);
ABool AGrowUntracedList(struct AThread_ *t);


ABool ADisallowOldGenGC(void);
void AAllowOldGenGC(void);


#endif
