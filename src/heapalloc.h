/* heapalloc.h - Managing the memory available to the heap

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef HEAPALLOC_H_INCL
#define HEAPALLOC_H_INCL


struct AHeapBlock_;


#ifdef HAVE_MREMAP

void *AMoreHeap_mmap(struct AHeapBlock_ *block, Asize_t growSize,
                     Asize_t *realGrow);
void AFreeHeapBlock_munmap(struct AHeapBlock_ *block);
void *AGrowNursery_mremap(void *oldNursery, Asize_t oldSize, Asize_t newSize);
void AFreeNursery_munmap(void *nursery, Asize_t size);

#elif defined(A_HAVE_VIRTUAL_ALLOC)

void *AMoreHeap_VirtualAlloc(struct AHeapBlock_ *block, Asize_t growSize,
                             Asize_t *realGrow);
void AFreeHeapBlock_VirtualAlloc(struct AHeapBlock_ *block);
void *AGrowNursery_VirtualAlloc(void *oldNursery, Asize_t oldSize,
                                Asize_t newSize);
void AFreeNursery_VirtualAlloc(void *nursery, Asize_t size);

#else

void *AMoreHeap_malloc(struct AHeapBlock_ *block, Asize_t growSize,
                       Asize_t *realGrow);
void AFreeHeapBlock_free(struct AHeapBlock_ *block);
void *AGrowNursery_realloc(void *oldNursery, Asize_t oldSize, Asize_t newSize);
void AFreeNursery_free(void *nursery, Asize_t size);

#endif


#endif
