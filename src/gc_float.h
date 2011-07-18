/* gc_float.h - Definitions related to garbage collection of Float values

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef GC_FLOAT_H_INCL
#define GC_FLOAT_H_INCL


#if A_FLOAT_SIZE != A_VALUE_SIZE && A_FLOAT_SIZE != A_VALUE_SIZE * 2 && \
    A_FLOAT_SIZE != A_VALUE_SUZE * 4
/* Unsupported FLOAT_SIZE or A_VALUE_BITS */
error!;
#endif


/* Float bucket is implemented as a block of FLOAT_BUCKET_SIZE bytes. */
#define A_FLOAT_BUCKET_SIZE 256

#define A_FLOAT_BUCKET_HEADER_SIZE AMax(A_FLOAT_SIZE, 2 * A_VALUE_SIZE)


/* It must be possible to cover the contents of a single float bucket with a
   single bitmap integer (AMarkBitmapInt). */
#if (A_FLOAT_BUCKET_SIZE - A_FLOAT_BUCKET_HEADER_SIZE) / A_FLOAT_SIZE > \
    A_MARK_BITMAP_INT_BITS
/* FLOAT_BUCKET_SIZE too large */
error!;
#endif


/* Number of Float objects in a single float bucket. */
#define A_FLOAT_BUCKET_LENGTH \
    ((A_FLOAT_BUCKET_SIZE - A_FLOAT_BUCKET_HEADER_SIZE) / A_FLOAT_SIZE)


/* The first words of a float bucket block in the new generation (mixed block
   header) */
#define A_FLOAT_BUCKET_HEADER \
    (A_MIXED_BLOCK_TAG | (A_FLOAT_BUCKET_SIZE << 2))
#define A_FLOAT_BUCKET_SUB_HEADER 0


/* Check if ptr points at a float bucket. */
#define AIsFloatBlock(ptr) \
    ((ptr)[0] == A_FLOAT_BUCKET_HEADER && \
     (ptr)[1] == A_FLOAT_BUCKET_SUB_HEADER)


#if A_LONG_BITS == 32

#if A_FLOAT_SIZE == A_MARK_BITMAP_UNIT
#define A_FLOAT_BUCKET_BITMASK \
    ((AMarkBitmapInt)0xffffffffU >> \
     (A_MARK_BITMAP_INT_BITS - A_FLOAT_BUCKET_LENGTH))
#elif A_FLOAT_SIZE == A_MARK_BITMAP_UNIT * 2
#define A_FLOAT_BUCKET_BITMASK \
    ((AMarkBitmapInt)0x55555555U >> \
     (A_MARK_BITMAP_INT_BITS - A_FLOAT_BUCKET_LENGTH))
#else
/* Unsupported FLOAT_SIZE or MARK_BITMAP_UNIT */
error!;
#endif

#elif A_LONG_BITS == 64

#if A_FLOAT_SIZE == A_MARK_BITMAP_UNIT
#define A_FLOAT_BUCKET_BITMASK \
    ((AMarkBitmapInt)0xffffffffffffffffUL >> \
     (A_MARK_BITMAP_INT_BITS - A_FLOAT_BUCKET_LENGTH))
#else
/* Unsupported FLOAT_SIZE or MARK_BITMAP_UNIT */
error!;
#endif

#else
/* Not supported */
error!;
#endif


#define A_BITMAP_UNITS_PER_FLOAT (A_FLOAT_SIZE / A_MARK_BITMAP_UNIT)


#define AGetFloatBucketBitMask(begin, bitmap, bucket, bitmapInd, bitInd) \
    ((((bitmap)[bitmapInd] >> (bitInd)) |                                \
      ((bitmap)[(bitmapInd) + 1] << (A_MARK_BITMAP_INT_BITS - (bitInd)))) \
     & A_FLOAT_BUCKET_BITMASK)

#define AGetFloatBucketBitMaskSimple(begin, bitmap, bucket, bitmapInd, bitInd)\
    (((bitmap)[bitmapInd] >> (bitInd)) & A_FLOAT_BUCKET_BITMASK)


typedef struct AFloatListNode_ {
    struct AFloatListNode_ *next;
#if A_FLOAT_SIZE != A_POINTER_SIZE
    char filler[A_FLOAT_SIZE - A_POINTER_SIZE];
#endif
} AFloatListNode;


extern AFloatListNode *AFloatList;


#endif
