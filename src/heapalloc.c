/* heapalloc.c - Managing the memory available to the heap

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Functions for managing heap blocks. Three different implementations are
   provided: the first is based on mmap/mremap (for Linux), the second uses
   VirtualAlloc (for Windows), and the last uses ANSI C memory management
   functions (malloc and friends). */

#include "aconfig.h"

#ifdef HAVE_MREMAP
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>
#endif

#ifdef A_HAVE_VIRTUAL_ALLOC
#include <windows.h>
#endif

#include <stdlib.h>
#include "runtime.h"
#include "heap.h"
#include "internal.h"
#include "gc_internal.h"


#define PAGE_SIZE 4096


#define IsValidAddress(ptr) \
    ((void *)(ptr) >= A_MEM_START && (void *)(ptr) <= A_MEM_END)

#define IsValidAddressRange(ptr, size) \
    (IsValidAddress(ptr) && IsValidAddress(APtrAdd(ptr, size)))


#ifdef HAVE_MREMAP


void *AMoreHeap_mmap(AHeapBlock *block, unsigned long growSize,
                     unsigned long *realGrow)
{
    void *preferred;
    void *new;

    growSize = (growSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    *realGrow = growSize;
    
    if (block == NULL)
        preferred = A_PREFERRED_OLD_GEN_OFFSET;
    else
        preferred = APtrAdd(block, block->size);

    new = mmap(preferred, growSize, PROT_EXEC | PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (new == (void *)-1 || !IsValidAddressRange(new, growSize))
        return NULL;
    else
        return new;
}


void AFreeHeapBlock_munmap(AHeapBlock *block)
{
    munmap((void *)block, block->size);
}


void *AGrowNursery_mremap(void *oldNursery, unsigned long oldSize,
                          unsigned long newSize)
{
    void *new;
    
    if (oldNursery == 0)
        new = mmap(A_PREFERRED_NEW_GEN_OFFSET, newSize,
                   PROT_EXEC | PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    else
        new = mremap(oldNursery, oldSize, newSize, MREMAP_MAYMOVE);

    if (new == (void *)-1 || !IsValidAddressRange(new, newSize))
        return NULL;
    else
        return new;
}


void AFreeNursery_munmap(void *nursery, unsigned long size)
{
    munmap(nursery, size);
}


#elif defined(A_HAVE_VIRTUAL_ALLOC)


#define MINIMUM_RESERVE (128 * 1024 * 1024)


static void *ReserveStart;
static void *ReserveEnd;


static void *ReserveVirtual(void *start, unsigned long size)
{
    void *ptr;
    
    if (start >= ReserveStart && APtrAdd(start, size) <= ReserveEnd)
        return start;

    if (size < MINIMUM_RESERVE)
        size = MINIMUM_RESERVE;
    
    ptr = VirtualAlloc(start, size, MEM_RESERVE, PAGE_READWRITE);
    if (ptr == NULL && start != NULL)
        ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
    if (ptr != NULL) {
        ReserveStart = ptr;
        ReserveEnd = APtrAdd(ptr, size);
    }
    
    return ptr;
}


static ABool CommitVirtual(void *start, unsigned long size)
{
    return VirtualAlloc(start, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}


static void FreeVirtual(void *start, unsigned long size)
{
    VirtualFree(start, size, MEM_RELEASE);
}


void *AMoreHeap_VirtualAlloc(struct AHeapBlock_ *block, unsigned long growSize,
                             unsigned long *realGrow)
{
    void *preferred;
    void *new;

    growSize = (growSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    *realGrow = growSize;
    
    if (block == NULL)
        preferred = A_PREFERRED_OLD_GEN_OFFSET;
    else
        preferred = APtrAdd(block, block->size);

    new = ReserveVirtual(preferred, growSize);
    if (new == NULL || !IsValidAddressRange(new, growSize))
        return NULL;

    if (!CommitVirtual(new, growSize))
        return NULL;

    return new;
}


void AFreeHeapBlock_VirtualAlloc(struct AHeapBlock_ *block)
{
    FreeVirtual(block, block->size);
}


void *AGrowNursery_VirtualAlloc(void *oldNursery, unsigned long oldSize,
                                unsigned long newSize)
{
    if (oldNursery == NULL) {
        /* Reserve space for nursery. */
        oldNursery = ReserveVirtual(A_PREFERRED_NEW_GEN_OFFSET,
                                    A_MAX_NURSERY_SIZE);
        if (oldNursery == NULL)
            return NULL;
        oldSize = 0;
    }

    if (!CommitVirtual(APtrAdd(oldNursery, oldSize), newSize - oldSize))
        return NULL;

    return oldNursery;
}


void AFreeNursery_VirtualAlloc(void *nursery, unsigned long size)
{
    FreeVirtual(nursery, size);
}


#else /* !HAVE_MREMAP && !A_HAVE_VIRTUAL_ALLOC */


/* Extra space is requested from the OS in multiples of MIN_HEAP_INCREMENT
   bytes. */
#define MIN_HEAP_INCREMENT 4096


#if 0
void *MoreHeap_sbrk(AHeapBlock *block, unsigned long growSize,
                    unsigned long *realGrow)
{
    AHeapBlock *new;

    *realGrow = growSize;
    
    new = sbrk(growSize);

    if (new == (void *)-1 || !IsValidAddressRange(new, growSize))
        return NULL;
    else
        return new;
}
#endif


static void *Align(char *ptr)
{
    int padding;
    int delta;

    if (ptr == NULL)
        return NULL;
    
    padding = (AValue)ptr & (A_ALLOC_UNIT - 1);
    delta = A_ALLOC_UNIT - padding;
    ptr[delta - 1] = delta;
    return ptr + delta;
}


static void *Unalign(void *ptr)
{
    if (ptr == NULL)
        return NULL;
    
    return APtrAdd(ptr, -((char *)ptr)[-1]);
}


void *AMoreHeap_malloc(AHeapBlock *block, unsigned long growSize,
                       unsigned long *realGrow)
{
    AHeapBlock *new;

    growSize = (growSize + MIN_HEAP_INCREMENT - 1) & ~(MIN_HEAP_INCREMENT - 1);
    *realGrow = growSize;
    
    new = Align(malloc(growSize + A_ALLOC_UNIT));
    
    if (new != NULL && !IsValidAddressRange(new, growSize)) {
        free(Unalign(new));
        return NULL;
    } else
        return new;
}


void AFreeHeapBlock_free(AHeapBlock *block)
{
    free(Unalign(block));
}


void *AGrowNursery_realloc(void *oldNursery, unsigned long oldSize,
                           unsigned long newSize)
{
    void *ptr = Align(realloc(Unalign(oldNursery), newSize + A_ALLOC_UNIT));

    if (ptr != NULL && !IsValidAddressRange(ptr, newSize)) {
        free(Unalign(ptr));
        return NULL;
    } else
        return ptr;
}


void AFreeNursery_free(void *nursery, unsigned long size)
{
    free(Unalign(nursery));
}


#endif /* !HAVE_MREMAP && !A_HAVE_VIRTUAL_ALLOC */
