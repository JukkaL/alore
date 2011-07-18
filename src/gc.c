/* gc.c - Generational, incremental mark-sweep/copying garbage collector

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "class.h"
#include "memberid.h"
#include "gc.h"
#include "thread_athread.h"
#include "internal.h"
#include "gc_internal.h"
#include "heap.h"
#include "mem.h"
#include "dynaload.h"
#include "symtable.h"
#include "debug_runtime.h"
#include "std_module.h"


/* Heap state */

/* Estimate of the amount of reachable data. Is is reset after every full gc
   cycle but may sometimes be updated between gc cycles. A new full gc cycle
   is typically started when the current old generation data size grows larger
   than a multiple of LiveDataSize. */
static unsigned long ALiveDataSize;

/* Counter for measuring amount of live data encountered during sweep. */
static unsigned long SweepLiveDataSize;

/* Current old geneneration size (does not include free blocks). */
static unsigned long AOldGenSize;

/* Records value of OldGenSize before beginning sweep. */
static unsigned long OldGenSizeBeforeSweep;


/* Alloc counter used to control incremental mark-sweep. It counts the number
   of bytes allocated after the previous increment. When the counter reaches
   INCREMENTALITY, the next increment is performed and counter is reset. It is
   not used when old generation gc is not active. */
static unsigned long AllocCounter;


/* Counter for amount of data allocated in the main heap (outside nursery).
   Used in deciding when to start new generation garbage collection and to
   keep track of number of bytes retired in new generation gc. */
unsigned long AAllocAmount;


/* List of static blocks. */
static AStaticBlock StaticBlocks;


/* List of available free floats. */
AFloatListNode *AFloatList;


ABool AIsNewGenGCActive = FALSE;


/* True if all thread-specific newRefPtr lists are invalid. This can happen
   if forced full gc is run while new generation gc is active and new gen gc
   fails: full gc can free objects that are referenced in the newRefPtr
   lists, but since new gen gc failed, the newRefPtr lists are not cleared. */
ABool ANewRefPtrListInvalid = FALSE;


/* The new generation state */

/* Size of the available space for allocation in the nursery block in bytes.
   There is also an additional mark-sweep bitfield after the end of this
   space. */
static unsigned long ANurserySize;
/* Pointer to the next free location in the nursery block. */
static char *NurseryPtr;
/* Pointers to the start and end of the allocation space in the nursery
   block. */
char *ANurseryBegin;
char *ANurseryEnd;

/* List of "large" blocks allocated in the ordinary heap in new generation
   (i.e. blocks outside the nursery). All blocks of at least MIN_BIG_BLOCK_SIZE
   bytes are allocated outside nursery.
   
   Note: The list may also contain small blocks that should not be moved by
         gc. */   
static ANewGenNode *NewGenBigBlocks;
/* Size of largest block currently in the new generation, in bytes. This is
   used in determining when to garbage collect the new generation. */
static unsigned long ANewGenLargestBlockSize;


/* State of the incremental mark-sweep collector. Can be GC_NONE (not active),
   GC_MARK (mark phase active), GC_MARK_EXE (mark currently executing) or
   GC_SWEEP (sweep phase). */
int AGCState;


const AValue ADummyOldGenHeader = 0;


/* The next block to be swept. */
static AValue *SweepPtr;
/* Heap block that contains the block stored in SweepPtr. */
static AHeapBlock *SweepBlock;


/* Mark traverse stack */
static unsigned long OldGenStackLength;
static AValue *OldGenStack;
static AValue *OldGenStackTop;

/* Copying gc traverse stack */
static unsigned long NewGenStackLength;
static AValue *NewGenStack;

static AValue *NewGenStackTop;
static ABool NewGenTraceResult;

static ABool (*PushFunc)(AValue *base, unsigned long len);

static int OldGenGCDisallowCount;


void (*AFreeNursery)(void *nursery, unsigned long size);


/* List of instances that have a finalizer method. The list also includes all
   objects that have external data associated them (a finalizer is required for
   such objects). */
AInstance *AOldGenFinalizeInst;
AInstance *ANewGenFinalizeInst;


AGCStatistics AGCStat;


static ABool GrowTraverseStack(AValue **stackPtr, unsigned long *lenPtr,
                               unsigned long newLen);
static void Sweep(long count);
static long Mark(long count, ABool traverseNewGen);
static ABool TraverseNewGen(void);
static ABool CopyAndMark(AValue *top, ABool isOkThisFar);
ABool ACollectNewGen(ABool forceRetire);
static ABool IncrementalGC(long count);
static ABool GetMarkRootSet(void);
static ABool GetMarkUntracedSet(void);
static void ClearUnusedStack(void);
static void MarkGCList(AGCListBlock *list);
static ABool FindUntracedModules(ABool *isFoundPtr);
static void SweepModules(void);
static ABool AddGlobalVarsToOldGenRootSet(void);
static ABool AddStackToOldGenRootSet(AThread *t);
static ABool IsModuleAlive(int modNum);
static ABool MarkModule(int modNum);
static ABool AddModuleGlobalsToRootSet(int num);
static ABool AddGCListToOldGenRootSet(AGCListBlock *first, AGCListBlock *last);
static ABool TryMark(void *ptr);
static ABool AllocFloatBucket(void);
static ABool TerminateGC(void);
static void InitializeSweep(void);
static void AddGlobalsToNewGenRootSet(void);
static ABool PushNewGenStack(AValue *base, unsigned long len);
static ABool PushOldGenStack(AValue *base, unsigned long len);
static ABool CreateUntracedListBlock(AThread *t, AGCListBlock **prev);
static void CreateNewGenBigBlock(ANewGenNode *n, unsigned long size);
static void CheckNewGenFinalizers(void);
static void CheckOldGenFinalizers(void);
static void RunFinalizer(AInstance *inst);
static void *AllocUnmovable_NoLock(unsigned long size);
static void *AllocNewGenBlockFromHeap(AThread *t, unsigned long size);
static ABool PerformGCIncrement(AThread *t);
static unsigned long ResetAllocAmount(void);


/* Allocate a block that can hold at least size bytes. The caller is
   responsible for initializing the block header and contents. Return NULL
   and raise MemoryError if out of memory (even after garbage collection). */
void *AAlloc(AThread *t, unsigned long size)
{
    void *ptr;

#ifdef A_DEBUG_VERIFY_MEMORY_AT_ALLOC
    ADebugVerifyMemory();
#endif

    /* Round the size up. */
    size = AGetBlockSize(size);

#ifdef A_DEBUG_ALLOC_TRACE
    ADebugPrint_F("(%d) Allocate block of size %d (largest newgen %d, "
                 "allocamount %d)\n",
                 (int)ADebugInstructionCounter, (int)size,
                 (int)ANewGenLargestBlockSize, (int)AAllocAmount);
#endif

    ADebugNextAllocation(t);

    /* Can we allocate from the thread-local nursery? */
    if (t->heapPtr + size <= t->heapEnd
        && size < A_MIN_BIG_BLOCK_SIZE) {
        ptr = t->heapPtr;
        t->heapPtr += size;
    } else {
        ALockHeap();

        /* Do we need to perform a gc increment? */
        if (AGCState != A_GC_NONE && AllocCounter > A_INCREMENTALITY) {
            if (!PerformGCIncrement(t)) {
                AReleaseHeap();
                return NULL;
            }
        }
        
        /* Is the block small enough to be allocated from the nursery? */
        if (size < A_MIN_BIG_BLOCK_SIZE) {
            unsigned long allocSize;
        
            /* Is there not enough space in nursery? */
            if (ANurseryEnd - NurseryPtr < size) {
                /* After a successful gc we are known to have enough space. */
                if (!ACollectGarbage()) {
                    /* Out of memory during garbage collection. */
                    AReleaseHeap();
                    ARaisePreallocatedMemoryErrorND(t);
                    return NULL;
                }
            }
            /* Allocate from the nursery. Try to reserve some additional space
               to the thread-local nursery to lessen the need for locking. */
            ptr = NurseryPtr;
            t->heapPtr = APtrAdd(ptr, size);
            allocSize = AMin(ANurseryEnd - NurseryPtr,
                             size + A_MIN_THREAD_HEAP_INCREMENT);
            NurseryPtr += allocSize;
            t->heapEnd = NurseryPtr;

            AGCStat.allocCount += allocSize;
            
            AAllocAmount += allocSize;
            AllocCounter += allocSize;
        } else {
            /* Allocate from the heap. */
            ptr = AllocNewGenBlockFromHeap(t, size);
            if (ptr == NULL) {
                /* Out of memory. */
                AReleaseHeap();
                ARaisePreallocatedMemoryErrorND(t);
                return NULL;
            }
        }
        
        AReleaseHeap();
    }

#ifdef A_DEBUG
    ADebug_MemoryEvent(ptr, "Allocated, new generation");

    /* Check that the result is within the supported address range. */
    if (ptr < A_MEM_START || ptr > A_MEM_END
        || APtrAdd(ptr, size) > A_MEM_END
        || APtrAdd(ptr, size) < ptr)
        ADebugError(("AAlloc result out of valid memory area: %p "
                     "(%d bytes)\n",
                     ptr, (int)size));
#endif
    
    return ptr;
}


/* Allocate a new generation block that will not be moved by the garbage
   collector. Raise MemoryError and return NULL on error.

   NOTE: The block header must be initialized by the caller. */
void *AAllocLocked(AThread *t, unsigned long size)
{
    void *ptr;
    
    size = AGetBlockSize(size);

    ALockHeap();

    /* Do we need to perform a gc increment? */
    if (AGCState != A_GC_NONE && AllocCounter > A_INCREMENTALITY) {
        if (!PerformGCIncrement(t)) {
            AReleaseHeap();
            return NULL;
        }
    }
    
    ptr = AllocNewGenBlockFromHeap(t, size);
    if (ptr == NULL) {
        /* Out of memory. */
        AReleaseHeap();
        ARaisePreallocatedMemoryErrorND(t);
        return NULL;
    }

    AReleaseHeap();

    return ptr;
}


/* Allocate a new generation block from the global heap (instead of the
   nursery).

   NOTE: The heap must be locked. */   
static void *AllocNewGenBlockFromHeap(AThread *t, unsigned long size)
{
    ANewGenNode *node;
    void *ptr;
    unsigned long allocSize;
    
    /* Is it time for gc? */
    if (AIsItTimeForNewGenGC()) {
        if (!ACollectGarbage())
            return NULL;
    }
    
    /* The block is put into a linked list of new generation blocks.
       Allocate enough space for the block plus some extra for the
       linked list node. */
    allocSize = size + A_NEW_GEN_NODE_HEADER_SIZE;
    node = AGlobalAlloc(allocSize);
    
    /* Was the allocation successful? */
    if (node != NULL) {
        ptr = AGetBigBlockData(node);
        
        CreateNewGenBigBlock(node, size);
        
        if (size > ANewGenLargestBlockSize) {
            ANewGenLargestBlockSize = size;
            
            /* We know that the live data size is at least the size of
               the largest single block allocated + some overhead. */
            if (size + A_MIN_LIVE_DATA_SIZE > ALiveDataSize)
                ALiveDataSize = size + A_MIN_LIVE_DATA_SIZE;
        }
    } else
        ptr = NULL;
    
    AGCStat.allocCount += allocSize;
    
    AAllocAmount += allocSize;
    AllocCounter += allocSize;

    return ptr;     
}


/* Disallow old geneneration garbage collection from running until
   AAllowOldGenGC is called again. It must be called once for each call to
   ADisallowOldGenGC. Return FALSE failed due to running out of memory. */
ABool ADisallowOldGenGC(void)
{
    ABool result;
    
    ADebugStatusMsg(("Disallow old gen gc\n"));
    
    ALockHeap();

    result = TRUE;
    
    /* Allow DisallowOldGenGC() to be called even if gc is already disallowed.
       In that case, nothing is done. */
    if (OldGenGCDisallowCount == 0) {
        /* If incremental garbage collection is active, we need to finish it
           first. */
        if (AGCState != A_GC_NONE) {
            AFreezeOtherThreads();

            result = ACollectNewGen(FALSE);
            if (result)
                result = IncrementalGC(A_SHORT_INT_MAX - 1);
            /* FIX: A_SHORT_INT_MAX above is wrong */
            
            AWakeOtherThreads();

            AllocCounter = 0;
        }

        if (result)
            OldGenGCDisallowCount = 1;
    } else
        OldGenGCDisallowCount++;
    
    AReleaseHeap();

    return result;
}


/* Enable old generation garbage collection. Old gen gc must be disabled when
   calling this function. */
void AAllowOldGenGC(void)
{
    ADebugStatusMsg(("Allow old gen gc\n"));
    
    ALockHeap();

    if (OldGenGCDisallowCount <= 0)
        AEpicInternalFailure("OldGenGCDisallowCount would go negative");
    
    OldGenGCDisallowCount--;
    if (OldGenGCDisallowCount == 0 && AIsItTimeForFullGC()) {
        ACollectGarbage();
        /* If the garbage collection fails, we do not return an error
           condition since nothing serious happened (we did not try to
           allocate any memory). */
    }
    
    AReleaseHeap();
}


/* Allocates block in old generation that won't be moved by the gc. */
void *AAllocUnmovable(unsigned long size)
{
    void *ptr;
    
    ALockHeap();

    ptr = AllocUnmovable_NoLock(size);

    AReleaseHeap();

    return ptr;
}


/* Allocates block in old generation that won't be moved by the gc. */
void *AllocUnmovable_NoLock(unsigned long size)
{
    void *ptr;
    
    size = AGetBlockSize(size);

    AOldGenSize += size;
    AGCStat.oldGenSize = AOldGenSize;

    /* Since unmovable blocks will likely be alive for a long time, increase
       AllocAmount only by half of the size. This will reduce the number of
       gc passes. */
    AAllocAmount += size / 2;
    AllocCounter += size;

    /* FIX: perhaps might wanna start full gc.. */

    ptr = AGlobalAlloc(size);

    if (AGCState == A_GC_SWEEP && ptr != NULL && !AIsSwept(ptr))
        TryMark(ptr);

#ifdef A_DEBUG
    if (ptr != NULL)
        ADebug_MemoryEvent(ptr, "Allocated, unmovable");
#endif

    return ptr;
}


void *AAllocStatic(unsigned long size)
{
    AStaticBlock *block;
    unsigned long blockSize;

    ALockHeap();

    blockSize = size + A_STATIC_BLOCK_HEADER_SIZE;
    block = AllocUnmovable_NoLock(blockSize);
    if (block == NULL) {
        AReleaseHeap();
        return NULL;
    }

    AInitNonPointerBlockOld(&block->header, blockSize - sizeof(AValue));

    block->next = StaticBlocks.next;
    block->next->prev = block;
    
    StaticBlocks.next = block;
    block->prev = &StaticBlocks;

    AReleaseHeap();
    
    return APtrAdd(block, A_STATIC_BLOCK_HEADER_SIZE);
}


/* Free a static block. Do nothing if ptr is NULL. */
void AFreeStatic(void *ptr)
{
    AStaticBlock *block;
    unsigned long size;

    if (ptr != NULL) {
        ALockHeap();

        block = APtrSub(ptr, A_STATIC_BLOCK_HEADER_SIZE);

#ifdef A_DEBUG
        ADebug_MemoryEvent(block, "Freed static");
#endif
        
        block->prev->next = block->next;
        block->next->prev = block->prev;

        size = AGetNonPointerBlockSize(&block->header);
        AOldGenSize -= size;
        AGCStat.oldGenSize = AOldGenSize;

        /* FIX: is this necessary? */
        if (AGCState != A_GC_MARK)
            AAddFreeBlock(block, size);
    
        AReleaseHeap();
    }
}


void *AGrowStatic(void *ptr, unsigned long newSize)
{
    void *block;

    /* FIX: we -could- check if we can grow the block.. but do we want to? */
    
    block = AAllocStatic(newSize);
    if (block == NULL)
        return NULL;

    if (ptr != NULL) {
        ACopyMem(block, ptr, AGetNonPointerBlockSize(
                (AValue *)APtrSub(ptr, A_STATIC_BLOCK_HEADER_SIZE)) -
                (A_STATIC_BLOCK_HEADER_SIZE - sizeof(AValue)));
        AFreeStatic(ptr);
    }
    
    return block;
}


/* Initialize heap and garbage collector. The argument specifies the maximum
   size of the global heap, in bytes (use 0 for the maximum supported heap
   size). */
ABool AInitializeGarbageCollector(unsigned long maxHeap)
{
    /* Store maximum heap size. */
    if (maxHeap == 0)
        AMaxHeapSize = A_DEFAULT_MAX_HEAP_SIZE;
    else
        AMaxHeapSize = maxHeap;
    
    if (!AInitializeHeap(A_INITIAL_OLD_GEN_SIZE))
        return FALSE;
    
    /* Initialize the new generation. */
    
    ANurserySize = A_INITIAL_NURSERY_SIZE;
    ANurseryBegin = AGrowNursery(NULL, 0, ANurserySize);
    if (ANurseryBegin == NULL) {
        AFreeHeap();
        return FALSE;
    }
    NurseryPtr = ANurseryBegin;
    ANurseryEnd = NurseryPtr + AGetBitFieldIndex(ANurserySize);
    AGCStat.nurserySize = ANurseryEnd - ANurseryBegin;
    
    NewGenBigBlocks = NULL;
    ANewGenLargestBlockSize = 0;

    ALiveDataSize = A_MIN_LIVE_DATA_SIZE;
    
    AOldGenSize = 0;

    AFloatList = NULL;

    StaticBlocks.next = &StaticBlocks;
    StaticBlocks.prev = &StaticBlocks;

    /* SweepLiveDataSize and OldGenSizeBeforeSweep do not need to be
       initialized. */
    
    AAllocAmount = 0;

    AllocCounter = 0;

    AGCState = A_GC_NONE;

    NewGenStack = NULL;
    OldGenStack = NULL;
    if (!GrowTraverseStack(&NewGenStack, &NewGenStackLength,
                           A_NEW_GEN_STACK_INITIAL_LENGTH)
        || !GrowTraverseStack(&OldGenStack, &OldGenStackLength,
                              A_OLD_GEN_STACK_INITIAL_LENGTH)) {
        ADeinitializeGarbageCollector();
        return FALSE;
    }

    AOldGenFinalizeInst = NULL;
    ANewGenFinalizeInst = NULL;

    OldGenGCDisallowCount = 0;

    return TRUE;
}


void ADeinitializeGarbageCollector(void)
{
    /* FIX: call all destructors */
    
    AFreeHeap();
    AFreeNursery(ANurseryBegin, ANurserySize);
}


ABool ACollectAllGarbage(void)
{
    ABool status;
    
    ADebugStatusMsg(("Collect all garbage\n"));

    AWaitForHeap();
    AFreezeOtherThreads();
    
    /* Perform ordinary garbage collection at first. */
    status = ACollectGarbage();

    /* If it succeeded, collect all garbage. This may stall all threads for a
       long time! */
    if (status)
        status = ACollectGarbageForced();
    
    AWakeOtherThreads();
    AReleaseHeap();
    
    ADebugStatusMsg(("End collect all garbage\n"));

    return status;
}


/* FIX: This is wrong location for this variable. */
static int SweepIncr;


/* Perform garbage collection. Always collects the new generation and
   potentially starts incremental mark-sweep collection.
   
   NOTE: The heap must be locked. */
ABool ACollectGarbage(void)
{
    ABool result;
#ifdef A_DEBUG
    unsigned long counter;
#endif

    ADebugStatusMsg(("Collect garbage\n"));

    AFreezeOtherThreads();

#ifdef A_DEBUG
    counter = ADebugInstructionCounter;
#endif

    result = ACollectNewGen(FALSE);

    /* Possibly begin an incremental full gc. */
    if (AGCState == A_GC_NONE && AIsItTimeForFullGC() && result
        && OldGenGCDisallowCount == 0) {
        AGCStat.fullCollectCount++;

        AllocCounter = 0;

        ADebugStatusMsg(("Start incremental gc\n"));

        /* Initialize the root set and if successful, begin mark-sweep. */
        if (GetMarkRootSet())
            result = IncrementalGC(A_INCREMENTALITY / 2);
        else
            result = FALSE;
    }
    
#ifdef A_DEBUG
    if (counter != ADebugInstructionCounter)
        ADebugError_F("Instruction counter modified during gc!\n");
#endif
    
    AWakeOtherThreads();

#ifdef A_DEBUG
    if (ADebugIsOn()) {
        if (AGCState == A_GC_NONE)
            ADebugStatusMsg(("End collect garbage\n"));
        else
            ADebugStatusMsg(("End collect garbage (incremental GC active)\n"));
    }
#endif
    
    return result;
}


/* Perform full mark-sweep collection without retiring new generation. May be
   called during new generation collection. Return FALSE if failed.

   If called during new gen collection, also traces old gen -> new gen
   references. Otherwise, new generation must be empty. */
ABool ACollectGarbageForced(void)
{
    AThread *t;
    ABool isUntracedModules;
    
    AFreezeOtherThreads();

    ADebugStatusMsg(("Collect garbage forced\n"));

    /* If old generation gc is disallowed, do nothing, but do not return
       failure. */
    if (OldGenGCDisallowCount > 0) {
        AWakeOtherThreads();
        return TRUE;
    }
    
#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif
    
    /* Terminate mark-sweep gc if it is active. */
    TerminateGC();

    if (!GetMarkRootSet())
        goto Fail;
    
    /* GetMarkRootSet() leaves some parts of the root set out. Get the rest of
       the root set. */
    for (t = AGetFirstThread();
         t != NULL; t = AGetNextThread(t)) {
        if (!PushOldGenStack(t->tempStack,
                             t->tempStackPtr - t->tempStack))
            goto Fail;
        
        if (!PushOldGenStack(t->regExp, A_NUM_CACHED_REGEXPS * 2))
            goto Fail;
        
        if (!PushOldGenStack(&t->exception, 1))
            goto Fail;

        if (AIsNewGenGCActive) {
            /* We need to trace old gen -> new gen references as well, since
               otherwise the reference list may become corrupt, causing the
               new generation gc to trace freed memory (-> crash!). */
            AGCListBlock *newRefVal = t->newRefValues;
            for (;;) {
                if (!PushOldGenStack(newRefVal->data.val, newRefVal->size))
                    goto Fail;
                if (newRefVal == t->curNewRefValues)
                    break;
                newRefVal = newRefVal->next;
            }
        }
    }

    ADebugStatusMsg(("Begin mark\n"));

  MarkAgain:
    
    /* Mark all objects, including the new generation. */
    if (Mark(A_SHORT_INT_MAX - 1, TRUE) == A_MARK_FAILED)
        goto Fail;
    
    /* Clear untraced lists. Note that untraced lists are not needed during
       full gc as the entire heap is always traced. */
    for (t = AGetFirstThread();
         t != NULL; t = AGetNextThread(t)) {
        t->untracedPtr = t->untraced->data.val;
        t->untracedEnd = t->untraced->data.val + A_GC_LIST_BLOCK_LENGTH;
        t->untraced->next = NULL;
    }
    
    /* FIX mark all the new gen big blocks.. but maybe not? maybe we oughta
       free some of them as well.. */
    
#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif

    PushFunc = PushOldGenStack;
    if (!FindUntracedModules(&isUntracedModules))
        goto Fail;

    if (isUntracedModules) {
        ADebugStatusMsg(("Mark again (untraced modules)\n"));
        goto MarkAgain;
    }

    /* Sweep things that need special processing. */
    SweepModules();
    ASweepOldGenHashMappings();
    
    /* Initialize sweep of everything else. */
    InitializeSweep();
    
    /* FIX what about new gen finalizers.. well, maybe they won't be freed? */
    CheckOldGenFinalizers();

    /* Sweep the entire heap. */
    Sweep(A_SHORT_INT_MAX);

    /* Make sure everything is cleared properly. */
    TerminateGC();

#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif
    
    ADebugStatusMsg(("End of forced gc\n"));
    
    AGCStat.fullCollectCount++;
    AGCStat.forcedCollectCount++;
    AGCStat.fullIncrementCount++;
    
    AWakeOtherThreads();

    return TRUE;

  Fail:

    /* Forced gc failed. */
    TerminateGC();
    AWakeOtherThreads();
    return FALSE;
}


/* Garbage collect the new generation. Return FALSE if failed.
   FIX: forceRetire is unused, remove it. */
ABool ACollectNewGen(ABool forceRetire)
{
    ABool result;
    unsigned long retired;

    ADebugStatusMsg(("Collect new generation\n"));

#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif

    AGCStat.newGenCollectCount++;
    
    /* Start counting the number of retired bytes. */
    ResetAllocAmount();

    /* Copy the nursery and mark live big blocks. */
    result = TraverseNewGen();
    
    /* Update OldGenSize to include all the retired objects. */
    retired = ResetAllocAmount();
    AOldGenSize += retired;
    AGCStat.oldGenSize = AOldGenSize;
    AGCStat.retireCount += retired;

    if (result) {
        /* Copying and marking was successful. */
        AThread *t;
        ANewGenNode *bigBlock;
        
        /* Empty newRef lists and thread local heaps. */
        for (t = AGetFirstThread();
             t != NULL; t = AGetNextThread(t)) {
            t->newRefPtr = t->newRef->data.valPtr;
            t->newRefEnd = t->newRefPtr + A_GC_LIST_BLOCK_LENGTH;
            t->curNewRefValues = t->newRefValues;

            t->heapPtr = t->heapEnd = A_EMPTY_THREAD_HEAP_PTR;
        }

        /* Mark newRefPtr lists valid (they are empty now). */
        ANewRefPtrListInvalid = FALSE;
        
        /* Run finalizers for new generation objects. */
        CheckNewGenFinalizers();
        
        AMoveNewGenHashMappingsToOldGen();

        /* Sweep the big blocks. Retire everything. */
        bigBlock = NewGenBigBlocks;
        while (bigBlock != NULL) {
            AValue *block;
            ANewGenNode *next;

            next = bigBlock->next;
        
            /* Is the current block alive? */
            block = AGetBigBlockData(bigBlock);
            if (!AIsNewGenBlock(block)) {
                /* Yes. Add only the stub as a free block, keep the rest. */
                AOldGenSize += bigBlock->size;
                AGCStat.oldGenSize = AOldGenSize;
                AGCStat.retireCount += bigBlock->size;
                AAddFreeBlock(bigBlock, A_NEW_GEN_NODE_HEADER_SIZE);
            } else {
                /* No. Free the block including the stub. */
                AAddFreeBlock(bigBlock, A_NEW_GEN_NODE_HEADER_SIZE +
                              bigBlock->size);
            }
            
            bigBlock = next;
        }

        NewGenBigBlocks = NULL;
        ANewGenLargestBlockSize = 0;

        if (AIsItTimeToGrowNursery()) {
            ADebugStatusMsg(("Grow nursery\n"));
            
            /* At this point the nursery is empty. */
            
            if (GrowTraverseStack(&NewGenStack, &NewGenStackLength,
                                  2 * NewGenStackLength)) {
                void *newNursery;

                newNursery = AGrowNursery(ANurseryBegin, ANurserySize,
                                          2 * ANurserySize);
                if (newNursery != NULL) {
                    ANurserySize *= 2;
                    ANurseryBegin = newNursery;
                    ANurseryEnd = APtrAdd(newNursery,
                                          AGetBitFieldIndex(ANurserySize));
                    AGCStat.nurserySize = ANurseryEnd - ANurseryBegin;
                } else {
                    NewGenStackLength /= 2;
                    result = FALSE;
                }
            } else
                result = FALSE;
        }
    
        NurseryPtr = ANurseryBegin;
    } else {
        /* New generation copy-and-mark gc failed. The only possible reason for
           this is that the global heap was full. Some live new generation
           blocks may be left in the nursery, and newRefPtr-values in threads
           may point to freed objects, but otherwise the heap will be in a
           consistent state. */
        
        ANewGenNode *bigBlock;

        /* FIX: Run finalizers? Move hash mappings? */

        /* The newRefPtr lists in threads may contain invalid references. The
           next gc run must not touch the lists. */
        ANewRefPtrListInvalid = TRUE;
        
        /* Turn on the new generation flag for all the original new generation
           blocks in the global heap, since they are still included in the
           NewGenBigBlocks list. As the failure might have resulted from lack
           of space for untraced lists, it's safest to keep them this way. */
        for (bigBlock = NewGenBigBlocks;
             bigBlock != NULL; bigBlock = bigBlock->next)
            *(AValue *)AGetBigBlockData(bigBlock) |= A_NEW_GEN_FLAG;
    }

#ifdef A_DEBUG_FILL_FREE_BLOCKS
    if (result) {
        /* Fill nursery with a dummy value. */
        int i;
        for (i = 0; i < ANurserySize / sizeof(AValue); i++)
            ((AValue *)ANurseryBegin)[i] = 0xdeadbeef;
    }
#endif

#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif
    
    ADebugStatusMsg(("End collect new generation\n"));
    
    return result;
}


ABool PerformGCIncrement(AThread *t)
{
    ABool status;
    
    AFreezeOtherThreads();
    
    status = IncrementalGC(AllocCounter);
    if (!status)
        ARaisePreallocatedMemoryErrorND(t);
    
    AWakeOtherThreads();
    
    AllocCounter = 0;

    return status;
}


/* Perform approximately count steps of garbage collection. */
static ABool IncrementalGC(long count)
{
    /* FIX: instead of returning on error, goto to end of the func */
    
    ADebugStatusMsg(("Begin gc increment\n"));
    
#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif

    AGCStat.fullIncrementCount++;
    
    if (AGCState == A_GC_MARK) {
        ClearUnusedStack();

      MarkBegin:

        ADebugStatusMsg(("Mark\n"));
        
        /* Mark incrementally. */
        count = Mark(count, FALSE);
        if (count == A_MARK_FAILED)
            return TerminateGC();

        /* Did we complete the mark phase? */
        if (count > 0) {
            /* The new generation is marked only after retiring. */
            if (!ACollectNewGen(TRUE))
                return TerminateGC();

            /* If CollectNewGen performed a full gc pass, we're done. */
            if (AGCState == A_GC_NONE)
                return TRUE;

            /* Mark objects retired from the new generation or caught by the
               write barrier. */
            
            if (!GetMarkUntracedSet())
                return TerminateGC();

            count = Mark(count, FALSE);
            if (count == A_MARK_FAILED)
                return TerminateGC();

            /* If we completed the mark successfully, we may begin the sweep
               phase unless there are additional dynamic modules to be
               marked. */
            if (count > 0) {
                ABool isFound;
                
                if (!FindUntracedModules(&isFound))
                    return TerminateGC();

                if (isFound)
                    goto MarkBegin;

                SweepModules();
                ASweepOldGenHashMappings();
                
                InitializeSweep();
                CheckOldGenFinalizers();
            }
        }
    }
    
    if (AGCState == A_GC_SWEEP)
        Sweep(count);

#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif

    ADebugStatusMsg(("End gc increment\n"));

    return TRUE;
}


/* Traverse all objects in the new generation and copy all objects from the
   nursery to the global heap. Return FALSE if failed. In this case, some
   objects may still remain in the nursery. */
static ABool TraverseNewGen(void)
{
    AThread *t;
    AGCListBlock *newRef;
    AGCListBlock *newRefVal;
    int i;

    AIsNewGenGCActive = TRUE;

    NewGenStackTop = APushThreadArgBuffer(NewGenStack + 1);
    NewGenTraceResult = TRUE;

    PushFunc = PushNewGenStack;

    PushNewGenStack(AExitHandlers, AExitBlockInd);

    /* FIX: Why on earth would we wanna do this? For efficiency, perhaps */
    /* InactivateCurFreeBlock(); */
    /* FIX: I guess this is useful --- less memory fragmentation, remove
       comments */

    AddGlobalsToNewGenRootSet();
    
    /* Generate root set. */

    /* Loop over all threads. */
    for (t = AGetFirstThread();
         t != NULL; t = AGetNextThread(t)) {
        /* FIX: combine these somehow with the old gen versions? */

        t->curNewRefValues->size = A_GC_LIST_BLOCK_LENGTH -
            (t->newRefEnd - t->newRefPtr);

        /* Copy old gen -> new gen references and include them in the root
           set. */
        newRef = t->newRef;
        newRefVal = t->newRefValues;
        for (;;) {
            /* Copy values pointer to by stored pointers unless the pointers
               are invalid. */
            if (!ANewRefPtrListInvalid) {
                for (i = 0; i < newRefVal->size; i++)
                    newRefVal->data.val[i] = *newRef->data.valPtr[i];
            }
            
            PushNewGenStack(newRefVal->data.val, newRefVal->size);

            if (newRefVal == t->curNewRefValues)
                break;

            newRef = newRef->next;
            newRefVal = newRefVal->next;
        }

        /* Push additional thread-specific values. */
        PushNewGenStack(t->stackPtr, t->stackTop - t->stackPtr);
        PushNewGenStack(t->tempStack,
                        t->tempStackPtr - t->tempStack);
        
        PushNewGenStack(t->regExp, 2 * A_NUM_CACHED_REGEXPS);
        PushNewGenStack(&t->exception, 1);
    }

    /* Perform the actual copying and marking. Note that if this fails, some
       objects in the nursery have not been copied. */
    NewGenTraceResult &= CopyAndMark(NewGenStackTop, NewGenTraceResult);
    
    /* Perform post-collection cleanup for all threads. */
    for (t = AGetFirstThread(); t != NULL; t = AGetNextThread(t)) {
        newRef = t->newRef;
        newRefVal = t->newRefValues;

        /* Loop over all old gen -> new gen reference blocks. */
        for (;;) {
            int i;

            /* Update moved values. */
            for (i = 0; i < newRefVal->size; i++)
                *newRef->data.valPtr[i] = newRefVal->data.val[i];

            if (AGCState == A_GC_MARK) {
                /* Add all retired new generation blocks as untraced for mark-
                   sweep gc. FIX: bad comment FIX: can this fail?? yes, I
                   think so. In that case, full gc has been attempted... and
                   failed. So no prob... */

                /* FIX: doesn't work with gc.. */

                /* Add all new generation blocks referenced from the old
                   generation to the untraced list. */
                for (i = 0; i < newRefVal->size; i++) {
                    if (t->untracedPtr == t->untracedEnd) {
                        AGCListBlock *cur = AGetGCListBlock(t->untracedEnd);

                        cur->size = A_GC_LIST_BLOCK_LENGTH;
                        
                        if (!CreateUntracedListBlock(t, &cur->next)) {
                            NewGenTraceResult = FALSE;
                            break;
                        }
                    }

                    *t->untracedPtr++ = newRefVal->data.val[i];
                }
            }

            if (newRefVal == t->curNewRefValues)
                break;
            
            newRef = newRef->next;
            newRefVal = newRefVal->next;
        }
    }

    AIsNewGenGCActive = FALSE;

    return NewGenTraceResult;
}


static ABool GetMarkRootSet(void)
{
    AThread *t;

    AGCState = A_GC_MARK_EXE;

    /* Add global variables in the main modules. */
    OldGenStackTop = APush(OldGenStack + 1, AGlobalVars,
                           AGlobalVars + ANumMainGlobals);

    PushOldGenStack(AThreadArgBuffer, A_THREAD_ARG_BUFFER_SIZE * 3);
    
    /* Add exit functions. */
    PushOldGenStack(AExitHandlers, AExitBlockInd);

    /* Loop over all threads. */
    for (t = AGetFirstThread(); t != NULL; t = AGetNextThread(t)) {
        if (!AddStackToOldGenRootSet(t)) {
            AGCState = A_GC_NONE;
            return FALSE;
        }

        /* If new generation gc is active, also include values in the old gen
           -> new gen reference lists. */
        if (AIsNewGenGCActive) {
            if (!AddGCListToOldGenRootSet(t->newRefValues,
                                          t->curNewRefValues)) {
                AGCState = A_GC_NONE;
                return FALSE;
            }
        }
    }

    AGCState = A_GC_MARK;

    return TRUE;
}


static ABool GetMarkUntracedSet(void)
{
    AThread *t;

    OldGenStackTop = APushThreadArgBuffer(OldGenStack + 1);

    /* Add exit functions. */
    PushOldGenStack(AExitHandlers, AExitBlockInd);
    
    PushFunc = PushOldGenStack;

    if (!AddGlobalVarsToOldGenRootSet())
        return FALSE;
    
    for (t = AGetFirstThread();
         t != NULL; t = AGetNextThread(t)) {
        if (!PushOldGenStack(t->tempStack,
                             t->tempStackPtr - t->tempStack))
            return FALSE;

        if (!PushOldGenStack(t->regExp, 2 * A_NUM_CACHED_REGEXPS))
            return FALSE;
        if (!PushOldGenStack(&t->exception, 1))
            return FALSE;
    
        if (!AddStackToOldGenRootSet(t))
            return FALSE;

        if (t->untracedPtr != t->untraced->data.val) {
            AGCListBlock *last;

            last = AGetGCListBlock(t->untracedEnd);

            last->size = A_GC_LIST_BLOCK_LENGTH - (t->untracedEnd -
                                                 t->untracedPtr);

            if (!AddGCListToOldGenRootSet(t->untraced, last))
                return FALSE;;

            if (!CreateUntracedListBlock(t, &t->untraced))
                return FALSE;
        }
    }

    return TRUE;
}


/* Clear regions in thread stacks that were active and thus were scheduled to
   be scanned in the start of the incremental garbage collection cycle, but
   later have become unused (since the thread stack pointer has changed due to
   fewer or smaller function stack frames in the stack). */
void ClearUnusedStack(void)
{
    AThread *t;
    AValue *ptr;

    for (t = AGetFirstThread();
         t != NULL; t = AGetNextThread(t)) {
        /* If the thread was created during garbage collection, it doesn't
           have markStackBottom set. Because it thus was not in the root set,
           there cannot be anything worth clearing. */
        if (t->markStackBottom == NULL)
            continue;

        /* Clear the region between original stack pointer and the current
           stack pointer. */
        for (ptr = t->markStackBottom; ptr < t->stackPtr; ptr++) {
            /* Convert the value to an integer value, but keep the original
               information (other than type info) intact. This way the values
               are accessible for stack traceback generation. */
            /* IDEA: Is this really needed? This is ugly. */
            *ptr = *ptr & ~A_INTEGER_MASK;
        }
    }
}


static ABool AddStackToOldGenRootSet(AThread *t)
{
    t->markStackBottom = t->stackPtr;
    
    if (!PushOldGenStack(t->stackPtr,
                         t->stackTop - t->stackPtr))
        return FALSE;
    
    return TRUE;
}


static ABool AddGCListToOldGenRootSet(AGCListBlock *first, AGCListBlock *last)
{
    AGCListBlock *block;

    block = first;
    for (;;) {
        if (!PushOldGenStack(block->data.val, block->size))
            return FALSE;

        if (block == last)
            break;

        block = block->next;
    }

    return TRUE;
}


static void InitializeSweep(void)
{
    AStaticBlock *block;
    AThread *t;

    ADebugStatusMsg(("Begin sweep\n"));
    
#ifdef A_DEBUG_VERIFY_MEMORY_AROUND_GC
    ADebugVerifyMemory_F();
#endif
    
    AGCState = A_GC_SWEEP;
    
    SweepBlock = AHeap;
    SweepPtr = AGetHeapBlockData(AHeap);

    /* FIX: possibly should mark at least some of the floats.. */
    AFloatList = NULL;
    
    OldGenSizeBeforeSweep = AOldGenSize;
    SweepLiveDataSize = 0;

    /* Mark some things that were left unmarked. */
    TryMark(OldGenStack);
    TryMark(NewGenStack);

    /* Mark thread specific storage. */
    for (t = AGetFirstThread(); t != NULL;
         t = AGetNextThread(t)) {
        MarkGCList(t->newRef);
        MarkGCList(t->newRefValues);
        MarkGCList(t->untraced);
    }

    /* Mark static blocks. */
    for (block = StaticBlocks.next; block != &StaticBlocks;
         block = block->next)
        TryMark(block);
}


static void MarkGCList(AGCListBlock *list)
{
    do {
        TryMark(list);
        list = list->next;
    } while (list != NULL);
}


/* Sweep approximately count objects. */
static void Sweep(long count)
{
    unsigned long liveData;
    AHeapBlock *block;
    AValue *ptr;

    liveData = SweepLiveDataSize;
    block = SweepBlock;
    ptr = SweepPtr;
    
    AInactivateCurFreeBlock();
    
    /* Smallest blocks can't be removed from the free list during sweep since
       they are too small to contain back pointers, and back pointers are
       required for removing a block from the middle of a free list. We solve
       the problem by changing all small blocks to allocated nonpointer blocks
       before sweep. This causes some extra garbage to be generated but since
       they are very small blocks, it's unlikely that there are a very large
       number of them around. */
    ARemoveSmallBlocksFromFreeList();
    
    SweepIncr++;

    count++;

    do {
        AValue *blockEnd;
        unsigned long bitInd;

        blockEnd = AGetHeapBlockDataEnd(block);
        bitInd = AGetBitWordIndex(block, ptr);

        while (ptr < blockEnd && count > 0) {
            if (!AIsMarkedBounded(ptr, block, blockEnd)
                && !AIsNewGenBlock(ptr)) {
                AValue *begin;

                begin = ptr;

                do {
                    /* Remove free list blocks from free list so that they can
                       be combined with other free blocks. */
                    if (AIsFreeListBlock(ptr))
                        ARemoveBlockFromFreeList(ptr);
                    
                    if (AIsValueBlock(ptr))
                        ptr = APtrAdd(ptr, AGetValueBlockSize(ptr));
                    else if (AIsInstanceBlock(ptr))
                        ptr = APtrAdd(ptr, AGetInstanceBlockSize(ptr));
                    else if (AIsNonPointerBlock(ptr))
                        ptr = APtrAdd(ptr, AGetNonPointerBlockSize(ptr));
                    else if (AIsFloatBlock(ptr)) {
                        /* Float buckets are handled here, because they might
                           be empty. If the current bucket is non-empty, mark
                           it allocated and we will leave the inner loop. */
                        
                        AValue bitMask;
                        unsigned long floatBitWordInd;
                        int floatBitInd;
                        AFloatListNode *bucket;

                        bucket = APtrAdd(ptr, A_FLOAT_BUCKET_HEADER_SIZE);
                        
                        floatBitWordInd = AGetBitWordIndex(block, bucket);
                        floatBitInd = AGetBitIndex(block, bucket);

                        if (floatBitWordInd != bitInd)
                            blockEnd[bitInd] = 0;

                        if (AGetBitWordIndex(block, APtrAdd(bucket,
                                (A_FLOAT_BUCKET_LENGTH - 1) * A_FLOAT_SIZE))
                                != floatBitWordInd) {
                            bitMask = AGetFloatBucketBitMask(
                                block, blockEnd, bucket,
                                floatBitWordInd, floatBitInd);
                            blockEnd[floatBitWordInd] = 0;
                            bitInd = floatBitWordInd + 1;
                        } else {
                            /* Bitmask can be got in just one part. */
                            bitMask = AGetFloatBucketBitMaskSimple(
                                block,blockEnd, bucket,
                                floatBitWordInd, floatBitInd);
                            bitInd = floatBitWordInd;
                        }

                        /* Is the bucket unempty? */
                        if (bitMask != 0) {
                            /* Is the bucket not full? */
                            if (bitMask != A_FLOAT_BUCKET_BITMASK) {
                                AValue bit;

                                bit = 1;
                                for (;;) {
                                    if (!(bitMask & bit)) {
                                        bucket->next = AFloatList;
                                        AFloatList = bucket;
                                    }
                                    
                                    if (bit ==
                                        (1 << ((A_FLOAT_BUCKET_LENGTH - 1)
                                               / A_BITMAP_UNITS_PER_FLOAT)))
                                        break;
                                        
                                    bucket++;
                                    bit *= 2 * A_FLOAT_SIZE /
                                        A_MARK_BITMAP_UNIT;
                                }
                            }

                            /* We have found the end of current free block. */
                            break;
                        } else
                            ptr = APtrAdd(ptr, A_FLOAT_BUCKET_SIZE);
                    } else
                        ptr = APtrAdd(ptr, AGetMixedBlockSize(ptr));

                    count--;
                } while (ptr < blockEnd
                         && !AIsMarkedBounded(ptr, block, blockEnd)
                         && !AIsNewGenBlock(ptr));

                /* Is the free block unempty? */
                if (ptr != begin) {
                    unsigned long size;

                    size = APtrDiff(ptr, begin);

                    if (ALastBlock > (void *)begin
                            && ALastBlock <= APtrAdd(begin, size))
                        ALastBlock = begin;
                    
                    AAddFreeBlock(begin, size);

                    if (ptr == blockEnd)
                        break;
                }
            } /* if (!marked) */

            {
                unsigned long size;
                unsigned long newBitInd;

                newBitInd = AGetBitWordIndex(block, ptr);
                if (newBitInd > bitInd) {
                    blockEnd[bitInd] = 0;
                    bitInd = newBitInd;
                }
            
                if (AIsValueBlock(ptr))
                    size = AGetValueBlockSize(ptr);
                else if (AIsInstanceBlock(ptr))
                    size = AGetInstanceBlockSize(ptr);
                else if (AIsNonPointerBlock(ptr))
                    size = AGetNonPointerBlockSize(ptr);
                else
                    size = AGetMixedBlockSize(ptr);

                liveData += size;
                ptr = APtrAdd(ptr, size);
            }

            count--;
        } /* while (ptr < blockEnd && count > 0) */

        /* At this point, clear stuff upto pointer.. */
        if (AGetBitWordIndex(block, ptr) != bitInd)
            blockEnd[bitInd] = 0;
        else
            AClearMarksUntil(ptr, block, blockEnd);
        
        if (count > 0) {
            /* The current sweep block has been finished. */
            block = block->next;
            if (block == NULL) {
                /* Completed sweep. */
                ALiveDataSize = liveData;
                AOldGenSize = liveData + (AOldGenSize - OldGenSizeBeforeSweep);

                AGCStat.oldGenSize = AOldGenSize;
                AGCStat.lastLiveSize = ALiveDataSize;
                
                AGCState = A_GC_NONE;
                ADebugStatusMsg(("Sweep end\n"));
                //AShowGCStats();
                return;
            } else
                ptr = (AValue *)APtrAdd(block,
                                        AGetBlockSize(sizeof(AHeapBlock)));
        }
    } while (count > 0);

    SweepPtr = ptr;
    SweepBlock = block;
    
    SweepLiveDataSize = liveData;
}


/* Mark objects based on the root set stored in the old genenration traverse
   stack. Perform approximately count steps and then return, leaving the
   remaining of the work in the traverse stack. If traverseNewGen is TRUE, also
   trace all live new generation blocks. */
static long Mark(long count, ABool traverseNewGen)
{
    AValue *top;
    AHeapBlock *heapBegin;
    AValue *heapEnd;

    AGCState = A_GC_MARK_EXE;
    
    top = OldGenStackTop;

    /* Store the extents of the first heap block in local variables for faster
       access. Often there's only a single heap block. */
    heapBegin = AHeap;
    heapEnd   = AGetHeapBlockDataEnd(heapBegin);

    /* Mark things until the stack is empty or the counter gets zero. */
    while (*top != A_STACK_BOTTOM && count > 0) {
        AValue *ptr;
        AValue *end;

        /* Get the topmost pointer range from the stack. Short ranges use
           compressed encoding to save space. */
        if ((*top & 3) != 0) {
            ptr = (AValue *)(*top & ~3);
            end = ptr + (*top & 3);
            top--;
        } else {
            ptr = (AValue *)*top;
            end = (AValue *)*(top - 1);
            top -= 2;
        }

        /* Trace the contents of pointer range. Note that the pointer range
           may be changed within the loop body. */
        do {
            AValue val;

            val = *ptr;

            /* Process all objects except short integers. */
            if (!AIsShortInt(val)) {
                ABool isMarked;
                AValue *block;

                block = AValueToPtr(val);

                isMarked = FALSE;

                /* Trace only old-generation objects unless explicitly asked
                   to trace new generation as well. Float objects need special
                   handling, since they don't have any block header. Note that
                   new-generation floats are always in nursery. */
                if ((!AIsFloat(val) && !AIsNewGenBlock(block))
                    || (AIsFloat(val) && !AIsInNursery(block))
                    || traverseNewGen) {
                    /* Check mark state and mark object if it's unmarked.
                       Optimize objects in the first heap block as a special
                       case. */
                    if (AIsInsideHeapBlock(block, heapBegin, heapEnd)) {
                        if (AIsMarkedBounded(block, heapBegin, heapEnd)) {
                            isMarked = FALSE;
                        } else {
                            ASetMarkBounded(block, heapBegin, heapEnd);
                            isMarked = TRUE;
                        }
                    } else {
                        /* Check if the block is in nursery and has been
                           moved already by the copying gc. */
                        if (traverseNewGen && AIsInNursery(block)
                            && !AIsFloat(val) && !AIsNewGenBlock(block)) {
                            /* Use indirect value. */
                            val = AGetIndirectValue(val, block);
                            block = AValueToPtr(val);
                        }
                        
                        /* Use the general algorithm for marking. */
                        isMarked = TryMark(block);
                    }
                }

                /* If an object was just marked, push it to the traverse stack
                   unless it's a pointer-free object. */
                if (isMarked && !AIsNonPtrValue(val)) {
                    ptr++;

                    /* If this was not the last value in the original object,
                       we need to store the previous entry back to the
                       stack. */
                    if (ptr != end) {
                        /* First make sure there is space for an additional
                           entry in the stack. */
                        
                        top++;
                        
                        /* Need to grow the stack? */
                        if (top[1] == A_STACK_TOP) {
                            unsigned long stackIndex = top - OldGenStack;

                            if (!GrowTraverseStack(&OldGenStack,
                                                   &OldGenStackLength,
                                                   2 * OldGenStackLength)) {
                                AGCState = A_GC_MARK;
                                return A_MARK_FAILED;
                            }
                            
                            heapBegin = AHeap;
                            heapEnd   = AGetHeapBlockDataEnd(heapBegin);
                            
                            top = OldGenStack + stackIndex;
                        }

                        /* Push the rest of the original block data to the
                           stack as a pointer range. Small pointer ranges have
                           a special encoding. */
                        if (end - ptr < 4)
                            *top = (AValue)ptr | (end - ptr);
                        else {
                            *top++ = (AValue)end;
                            *top   = (AValue)ptr;
                        }
                    }

                    /* The target object becomes the current object
                       (depth-first search). */
                    if (AIsValueBlockValue(val)) {
                        ptr = block;
                        end = APtrAdd(block + 1,
                                      AGetValueBlockDataLength(block));
                    } else if (
                        AIsNonPtrAndNonValueBlockValueInstanceBlockValue(val)
                        ) {
                        ptr = block - 1;
                        end = APtrAdd(block + 1,
                                      AGetInstanceBlockDataLength(block));
                    } else {
                        ptr = block + 1;
                        end = APtrAdd(block + 2,
                                      AGetMixedBlockValueDataLength(block));
                    }
                }
            } /* if (!AIsShortInt(val)) */
            
            ptr++;
            count--;
        } while (ptr < end);
    }

    OldGenStackTop = top;

    AGCState = A_GC_MARK;

    return count;
}


/* Copy and mark items in the new generation using the top as the top of the
   traverse stack. Only new generation objects are traced. If successful, all
   objects in the nursery have been retired. If the return value is FALSE,
   retiring was unsuccessful due to running out of memory. In this case, some
   objects are still in the nursery, but the heap is otherwise in a consistent
   state. */
static ABool CopyAndMark(AValue *top, ABool isOkThisFar)
{
    ABool result;
    unsigned counter = 0; /* Counter for checking things every now and then. */

    /* If there have been problems previously, we pessimistically assume a
       failure. Otherwise we could perform multiple forced full gcs which could
       cause a long stall. */
    result = isOkThisFar;
    
    /* Continue until the stack is empty. */
    while (*top != A_STACK_BOTTOM) {
        AValue *ptr;
        AValue *end;

        /* Pop a pointer range from the stack. Short ranges use a different
           data format. */
        if (*top & 3) {
            ptr = (AValue *)(*top & ~3);
            end = ptr + (*top & 3);
            top--;
        } else {
            ptr = (AValue *)*top;
            end = (AValue *)*(top - 1);
            top -= 2;
        }

        /* Loop over a pointer range. Note that the pointer range may be
           modified within the loop body. */
        do {
            AValue val;

            val = *ptr;

            /* Process reference values only (objects other than short
               integers). */
            if (!AIsShortInt(val)) {
                AValue *block;

                /* Get pointer to the start of the block that contains the
                   referenced object. */
                block = AValueToPtr(val);
                
                if (AIsFloat(val)) {
                    if (AIsInNursery(block)) {
                        if (result
                            && (AFloatList != NULL || AllocFloatBucket())) {
                            AValue *floatDst = (AValue *)AFloatList;
                            AFloatList = AFloatList->next;
                            
                            *ptr = AFloatPtrToValue(floatDst);

                            floatDst[0] = block[0];
                            if (sizeof(AValue) < sizeof(double))
                                floatDst[1] = block[1];

                            if (AGCState == A_GC_SWEEP && !AIsSwept(floatDst))
                                TryMark(floatDst);
                        } else
                            result = FALSE;
                    }
                } else {
                    /* Ordinary block with a header */
                    AValue blockHeader;

                    blockHeader = *block;
                    
                    if (AIsNewGenBlock(block)) {
                        /* Untraversed new generation block */
                        unsigned long blockSize;
                        long beginOffs;
                        unsigned long len;
                        
                        /* Calculate the extents of pointer data within the
                           block. */
                        if (AIsInstanceBlock(block)) {
                            beginOffs = 0;
                            len = AGetInstanceBlockDataLength(block);
                            blockSize = AGetInstanceBlockSize(block);
                        } else if (AIsValueBlock(block)) {
                            beginOffs = 0;
                            len = AGetValueBlockDataLength(block);
                            blockSize = AGetValueBlockSize(block);
                        } else if (AIsNonPointerBlock(block)) {
                            blockSize = AGetNonPointerBlockSize(block);
                            AAvoidWarning_M(beginOffs = 0);
                            AAvoidWarning_M(len = 0);
                        } else {
                            /* Mixed block */
                            beginOffs = sizeof(AValue);
                            len = AGetMixedBlockValueDataLength(block);
                            blockSize = AGetMixedBlockSize(block);
                        }

                        if (AIsInNursery(block)) {
                            /* An untraced block in the nursery */
                            AValue *dstBlock;

                            /* Increment counter. */
                            counter++;

                            /* Allocate the copy target block from the global
                               heap. Every now and then try to find a new free
                               block from the heap (using counter). */
                            if (AIsLargeEnoughFreeBlock(ACurFreeBlockSize,
                                                        blockSize)
                                && ((counter & 0x1f) != 0)) {
                                /* Optimized special case. */
                                /* IDEA: This will likely cause heap
                                   fragmentation. We should find the smallest
                                   matching free block. */
                                ACurFreeBlockSize -= blockSize;
                                dstBlock = ACurFreeBlock;
                                ACurFreeBlock = APtrAdd(ACurFreeBlock,
                                                        blockSize);
                            } else {
                                if (result) {
                                    /* NOTE: This call may cause a forced
                                       full garbage collection run. */
                                    dstBlock = AGlobalAlloc(blockSize);
                                } else
                                    dstBlock = NULL;
                            }
                            
                            AAllocAmount += blockSize;

                            if (dstBlock != NULL) {
                                if (AGCState == A_GC_SWEEP
                                        && !AIsSwept(dstBlock))
                                    TryMark(dstBlock);

                                /* IDEA: Try to optimize the CopyMem call for
                                   small block sizes? */
                                ACopyMem(dstBlock, block, blockSize);

                                /* Copy block header to target block. */
                                *dstBlock = blockHeader & ~A_NEW_GEN_FLAG;

                                /* Modify original reference to point to the
                                   new block. */
                                *ptr = ADisplaceValue(val, dstBlock);
                                /* Set an indirection indicator in the header
                                   of the original block in the nursery. */
                                ASetIndirectValue(block, dstBlock);
                                
                                block = dstBlock;
                            } else
                                result = FALSE;
                        } else {
                            /* An untraced new gen block in the global heap
                               (mark only, no copying). */
                            *block = blockHeader & ~A_NEW_GEN_FLAG;
                            if (AGCState == A_GC_SWEEP && !AIsSwept(block))
                                TryMark(block);
                        }

                        /* Traverse the contents of a block that contains
                           references. */
                        if (!AIsNonPointerBlock(block)) {
                            ptr++;

                            /* If the previous pointer range is not exhausted,
                               we need to push a new stack entry. Otherwise,
                               we can just replace the old one at the top. */ 
                            if (ptr != end) {
                                top++;

                                if (end - ptr < 4)
                                    *top = (AValue)ptr | (end - ptr);
                                else {
                                    *top++ = (AValue)end;
                                    *top   = (AValue)ptr;
                                }
                            }

                            /* Update pointer range to the new block. */
                            ptr = APtrAdd(block, beginOffs);
                            end = APtrAdd(ptr, len + sizeof(AValue));
                        }
                    } else {
                        /* The block doesn't have new generation flag on it.
                           If it is in nursery, it has been moved already and
                           we have to redirect *ptr. */
                        if (AIsInNursery(block))
                            *ptr = AGetIndirectValue(val, block);
                    }
                }
            } /* if (!AIsShortInt(val)) */
            
            ptr++;
        } while (ptr < end);
    } /* while (*top != STACK_BOTTOM) */

    return result;
}


static ABool GrowTraverseStack(AValue **stackPtr, unsigned long *lenPtr,
                               unsigned long newLen)
{
    AValue *new;
    unsigned long oldLen;
    unsigned long newSize;
    int i;

    oldLen = *lenPtr;
    newSize = newLen * sizeof(AValue);

    new = AGlobalAlloc(newSize);
    if (new == NULL)
        return FALSE;

    if (AGCState == A_GC_SWEEP && !AIsSwept(new))
        TryMark(new);

    AInitNonPointerBlockOld(new, newSize - sizeof(AValue));
    
    new[1] = A_STACK_BOTTOM;

    if (*stackPtr != NULL)
        ACopyMem(new + 2, *stackPtr + 2,
                 (oldLen - 4) * sizeof(AValue));
    else
        oldLen = 4;
    
    for (i = oldLen - 2; i < newLen - 2; i++)
        new[i] = A_STACK_FILLER;
    
    new[newLen - 2] = A_STACK_TOP;
    new[newLen - 1] = A_STACK_TOP;

    *stackPtr = new;
    *lenPtr = newLen;

    return TRUE;
}


/* Check if a block belonging to the old generation is marked. */
AMarkBitmapInt AIsMarked(void *ptr)
{
    AHeapBlock *block;

    block = AHeap;
    for (;;) {
        void *blockEnd = AGetHeapBlockDataEnd(block);
        
        if (AIsInsideHeapBlock(ptr, block, blockEnd))
            return AIsMarkedBounded(ptr, block, blockEnd);

        block = block->next;

#ifdef A_DEBUG
        if (block == NULL)
            ADebugError(("Pointer outside heap\n"));
#endif
    }

    /* Not reached */
}


/* Return a boolean indicating whether block at ptr is marked. If it was not
   marked, mark it. ptr may point to any block within the global heap or
   nursery. */
static ABool TryMark(void *ptr)
{
    AHeapBlock *block;
    
    for (block = AHeap; block != NULL; block = block->next) {
        void *blockEnd;

        blockEnd = AGetHeapBlockDataEnd(block);
        if (AIsInsideHeapBlock(ptr, block, blockEnd)) {
            if (AIsMarkedBounded(ptr, block, blockEnd))
                return FALSE;
            else {
                ASetMarkBounded(ptr, block, blockEnd);
                return TRUE;
            }
        }
    }

    if (AIsMarkedBounded(ptr, ANurseryBegin, ANurseryEnd))
        return FALSE;
    else {
        ASetMarkBounded(ptr, ANurseryBegin, ANurseryEnd);
        return TRUE;
    }
}


ABool AIsSwept(void *ptr)
{
    AHeapBlock *block;

    block = AHeap;
    for (;;) {
        ABool inBlock = AIsInsideHeapBlock(ptr, block,
                                         AGetHeapBlockDataEnd(block));

        if (block == SweepBlock)
            return inBlock && (void *)SweepPtr > ptr;
        else if (inBlock)
            return TRUE;

        block = block->next;
    }
}


static ABool AllocFloatBucket(void)
{
    AValue *block;
    AFloatListNode *floats;
    int i;

    block = AGlobalAlloc(A_FLOAT_BUCKET_SIZE);
    if (block == NULL)
        return FALSE;

    block[0] = A_FLOAT_BUCKET_HEADER;
    block[1] = A_FLOAT_BUCKET_SUB_HEADER;

    floats = APtrAdd(block, A_FLOAT_BUCKET_HEADER_SIZE);
    
    for (i = 0; i < A_FLOAT_BUCKET_LENGTH - 1; i++)
        floats[i].next = &floats[i + 1];

    floats[i].next = AFloatList;
    AFloatList = floats;

    if (AGCState == A_GC_SWEEP && !AIsSwept(block)) {
        /* FIX: this is very slow */
        for (i = 0; i < A_FLOAT_BUCKET_LENGTH; i++)
            TryMark(floats + i);
    }

    AAllocAmount += A_FLOAT_BUCKET_SIZE;
    AllocCounter += A_FLOAT_BUCKET_SIZE;
    
    return TRUE;
}


/* Stop mark-sweep gc. Clear mark bit fields, including nursery bit field.
   Return FALSE. */
static ABool TerminateGC(void)
{
    AHeapBlock *block;
    
    for (block = AHeap; block != NULL; block = block->next)
        AClearMem(APtrAdd(block, AGetBitFieldIndex(block->size)),
                  AGetBitFieldSize(block->size));

    AClearMem(ANurseryEnd, AGetBitFieldSize(ANurserySize));

    AGCState = A_GC_NONE;

    return FALSE;
}


static void AddGlobalsToNewGenRootSet(void)
{
    int modNum;

    AddModuleGlobalsToRootSet(AFirstMainGlobalVariable);
    
    for (modNum = AFirstDynamicModule;
         modNum != 0; modNum = AGetNextModule(modNum))
        AddModuleGlobalsToRootSet(
            AValueToInt(AGetModuleInfo(modNum)->globalVarIndex));
}


static ABool AddGlobalVarsToOldGenRootSet(void)
{
    int modNum;

    if (!AddModuleGlobalsToRootSet(AFirstMainGlobalVariable))
        return FALSE;

    for (modNum = AFirstDynamicModule;
         modNum != 0; modNum = AGetNextModule(modNum)) {
        if (AIsModuleMarked(modNum)) {
            if (!AddModuleGlobalsToRootSet(
                    AValueToInt(AGetModuleInfo(modNum)->globalVarIndex)))
                return FALSE;
        }
    }

    return TRUE;
}


static ABool AddModuleGlobalsToRootSet(int num)
{
    do {
        if (!PushFunc(AGlobalVars + num + 1, A_GLOBAL_BUCKET_SIZE - 1))
            return FALSE;

        num = AGetNextGlobalBucket(num);
    } while (num != 0);
    
    return TRUE;
}


static ABool PushNewGenStack(AValue *base, unsigned long len)
{
    NewGenStackTop = APush(NewGenStackTop, base, base + len);

    if (NewGenStackTop == NewGenStack + A_NEW_GEN_ROOT_SET_SIZE + 1) {
        NewGenTraceResult &= CopyAndMark(NewGenStackTop, NewGenTraceResult);
        NewGenStackTop = NewGenStack + 1;
    }

    return TRUE;
}


static ABool PushOldGenStack(AValue *base, unsigned long len)
{
    if (OldGenStackTop[2] == A_STACK_TOP) {
        unsigned stackIndex = OldGenStackTop - OldGenStack;

        if (!GrowTraverseStack(&OldGenStack, &OldGenStackLength,
                               2 * OldGenStackLength))
            return FALSE;

        OldGenStackTop = OldGenStack + stackIndex;
    }

    OldGenStackTop = APush(OldGenStackTop, base, base + len);

    return TRUE;
}


/* Mark all unmarked dynamic modules that are alive. Set *isFoundPtr to TRUE if
   any such modules were found. */
static ABool FindUntracedModules(ABool *isFoundPtr)
{
    int modNum;
    ABool isFound;
    ABool endLoop;

    isFound = FALSE;

    /* Repeat the outermost loop until no new untraced modules have been
       found. This implementation is not very efficient, O(n^2) where n is
       the number of modules. A proper graph travelsal would be a better idea,
       but this was slightly easier to implement. If the number of dynamic
       modules is small, the inefficiency does not matter. */
    do {
        endLoop = TRUE;

        /* Loop through all the dynamic modules. */
        for (modNum = AFirstDynamicModule;
             modNum != 0; modNum = AGetNextModule(modNum)) {
            if (!AIsModuleMarked(modNum)) {
                /* The module is not marked. Mark it if it's alive. */
                
                if (IsModuleAlive(modNum)) {
                    if (!MarkModule(modNum))
                        return FALSE;
                    
                    isFound = TRUE;
                    endLoop = FALSE;
                }
            } else {
                /* Module is already marked. Check if any dynamic modules
                   imported by the current module are unmarked and mark
                   them if any are found. */
                ADynaModule *mod;
                int len;
                int i;

                mod = AGetModuleInfo(modNum);
                len = AGetNumImportedModules(mod);

                for (i = 0; i < len; i++) {
                    if (!AIsModuleMarked(
                            AValueToInt(mod->importedModules[i]))) {
                        if (!MarkModule(AValueToInt(mod->importedModules[i])))
                            return FALSE;
                    
                        isFound = TRUE;
                        endLoop = FALSE;
                    }
                }
            }
        }
    } while (!endLoop);

    *isFoundPtr = isFound;
    
    return TRUE;
}


/* Check if there are any references to the module, assuming that everything
   has been marked before calling the function. */
static ABool IsModuleAlive(int modNum)
{
    int bucket;
    ASymbolInfo *sym;

    /* Do not free active C modules (they cannot be garbage collected). */
    sym = AIntValueToPtr(AGetModuleInfo(modNum)->symbol);
    if (sym->info.module.cModule == A_CM_ACTIVE)
        return TRUE;

    /* Go through each global value bucket that belongs to the module and check
       if anything referenced by the module has been marked. */
    for (bucket = modNum; bucket != 0; bucket = AGetNextGlobalBucket(bucket)) {
        int i;
        
        for (i = 1; i < A_GLOBAL_BUCKET_SIZE; i++) {
            AValue val = AGlobalByNum(bucket + i);

            if (!AIsShortInt(val) && AIsMarked(AValueToPtr(val)))
                return TRUE;
        }
    }

    ADebugStatusMsg(("Dynamic module %d is not alive\n", modNum));
    
    return FALSE;
}


/* Schedule the module to be marked so that it won't be garbage collected.
   Return FALSE if failed. The module globals are added to the mark root
   set. */
static ABool MarkModule(int modNum)
{
    if (!AddModuleGlobalsToRootSet(modNum))
        return FALSE;

    /* Set mark flag in the module. */
    ASetGlobalByNum(modNum + 2, AIntToValue(1));

    return TRUE;
}


/* Free any dynamic modules that are no longer alive. Assume that everything
   has been properly marked. */
static void SweepModules(void)
{
    int num;

    num = AFirstDynamicModule;
    while (num != 0) {
        int next = AGetNextModule(num);

        if (!AIsModuleMarked(num)) {
            ADebugStatusMsg(("Free dynamic module %d\n", num));
            AFreeDynamicModule(num);
        } else {
            /* Clear mark flag in the module. */
            ASetGlobalByNum(num + 2, AZero);
        }

        num = next;
    }
}


ABool AModifyObject(AThread *t, const AValue *header, AValue *objPtr,
                  AValue *newValPtr)
{
    ABool result;
    
    AModifyObject_M(t, header, objPtr, newValPtr, result);

    if (!result)
        ARaisePreallocatedMemoryErrorND(t);
    
    return result;
}


static ABool CreateUntracedListBlock(AThread *t,
                                    AGCListBlock **prev)
{
    AGCListBlock *block;
    
    block = AGlobalAlloc(AGetBlockSize(sizeof(AGCListBlock)));
    if (block == NULL)
        return FALSE;

    AInitNonPointerBlockOld(&block->header,
                           sizeof(AGCListBlock) - sizeof(AValue));
    block->next = NULL;

    *prev = block;
    t->untracedPtr = block->data.val;
    t->untracedEnd = block->data.val + A_GC_LIST_BLOCK_LENGTH;

    return TRUE;
}


static void CreateNewGenBigBlock(ANewGenNode *node, unsigned long size)
{
    /* Add the block to the list of new generation blocks. */
    AInitNonPointerBlock(&node->header,
                        sizeof(ANewGenNode) - sizeof(AValue));
    node->next = NewGenBigBlocks;
    node->size = size;
    NewGenBigBlocks = node;
}


/* FIX: Does anybody call this? */
void *AAllocKeep(AThread *t, unsigned long size, AValue *keep)
{
    void *ptr;
    
    size = AGetBlockSize(size);
    
    if (t->heapPtr + size <= t->heapEnd
        && size < A_MIN_BIG_BLOCK_SIZE) {
        ptr = t->heapPtr;
        t->heapPtr += size;     
    } else {
        *t->tempStack = *keep;
        ptr = AAlloc(t, size);
        *keep = *t->tempStack;
        *t->tempStack = AZero;
    }

    return ptr;
}


static void CheckNewGenFinalizers(void)
{
    AInstance *inst;

    inst = ANewGenFinalizeInst;
    while (inst != NULL) {
        AInstance *next;

        next = AIntValueToPtr(inst->member[0]);
        
        if (AIsNewGenBlock(&inst->type))
            RunFinalizer(inst);
        else {
            int extDataMember;
            
            /* The instance was previously in nursery, so it has been retired
               to the old generation. Figure out the new location. */
            inst = AValueToInstance(
                    AGetIndirectValue(AInstanceToValue(inst), &inst->type));
            
            /* Move the instance to the old generation list. */
            inst->member[0] = APtrToIntValue(AOldGenFinalizeInst);
            AOldGenFinalizeInst = inst;

            /* Add the size of external data to old gen size, if any. */
            extDataMember = AGetInstanceType(inst)->extDataMember;
            if (extDataMember >= 0 && inst->member[extDataMember] != ANil)
                AOldGenSize += AValueToInt(inst->member[extDataMember]);
        }

        inst = next;
    }

    ANewGenFinalizeInst = NULL;
}


static void CheckOldGenFinalizers(void)
{
    AInstance *inst;
    AInstance *prev;

    /* FIX: What if all free blocks were overwritten with 0xdeadbeef? We can't
       do this any more. */

    prev = NULL;
    for (inst = AOldGenFinalizeInst;
         inst != NULL; inst = AIntValueToPtr(inst->member[0])) {
        if (!AIsMarked(inst)) {
            RunFinalizer(inst);
            
            /* Remove from the list. */
            if (prev)
                prev->member[0] = inst->member[0];
            else
                AOldGenFinalizeInst = AIntValueToPtr(inst->member[0]);
        } else {
            int extDataMember;
            
            prev = inst;

            /* Add the size of external data to live data size, if any. */
            extDataMember = AGetInstanceType(inst)->extDataMember;
            if (extDataMember >= 0 && inst->member[extDataMember] != ANil)
                SweepLiveDataSize += AValueToInt(inst->member[extDataMember]);
        }
    }
}


/* Run the finalizer method of given instance. */
static void RunFinalizer(AInstance *inst)
{
    AFunction *finalizer;
    AValue instVal;
    ATypeInfo *type;

    /* This function is a kludge (but it works and is fairly efficient). */

    /* Lookup the finalizer method and update finalizer variable. */
    type = AGetInstanceType(inst);
    for (;;) {
        int num = ALookupMemberTable(type, MT_METHOD_PUBLIC, AM_FINALIZER);
        if (num != -1) {
            finalizer = AValueToFunction(AGlobalByNum(num));
            break;
        }
        type = type->super;
        if (type == NULL) {
            /* Something is broken. */
            /* IDEA: Somehow generate an error message and quit? */
            return;
        }
    }
    
    instVal = AInstanceToValue(inst);
    finalizer->code.cfunc(NULL, &instVal);
}


/* Add a class instance to a list of objects with a finalize methods so that
   the finalizer can be run after the object has been freed. */
void AAddFinalizeInst(AInstance *inst)
{
    /* This will not block since AFinalizerMutex must be released quickly. */
    athread_mutex_lock(&AFinalizerMutex);
    
    /* There are separate lists for new generation and old generation
       objects. */
    if (AIsNewGenBlock(&inst->type)) {
        inst->member[0] = APtrToIntValue(ANewGenFinalizeInst);
        ANewGenFinalizeInst = inst;
    } else {
        inst->member[0] = APtrToIntValue(AOldGenFinalizeInst);
        AOldGenFinalizeInst = inst;
    }

    athread_mutex_unlock(&AFinalizerMutex);
}


/* Note: Size must fit in a short int. */
ABool ASetExternalDataSize(AThread *t, AValue obj, Asize_t size)
{
    AValue *temp;
    AInstance *inst;
    ATypeInfo *type;
    int member;
    AValue sizeVal;
    int sizeDelta;

    temp = AAllocTemp(t, obj);
    
    type = AGetInstanceType(AValueToInstance(*temp));
    member = type->extDataMember;
    if (member < 0 || size > A_SHORT_INT_MAX) {
        ARaiseRuntimeError(t, "External data size cannot be set");
        AFreeTemp(t);
        return FALSE;
    }
    
    ALockHeap();

    /* Check if we should garbage collect new generation. */
    if (AIsItTimeForNewGenGC()) {
        if (!ACollectGarbage()) {
            AReleaseHeap();
            ARaisePreallocatedMemoryErrorND(t);
            AFreeTemp(t);
            return FALSE;
        }
    }

    /* If the external data size has been previously set, we need to calculate
       the delta from the previous value. If the delta is negative (i.e. the
       new size is smaller) make the delta 0. */
    sizeDelta = size;
    sizeVal = AMemberDirect(*temp, type->extDataMember);
    if (!AIsNil(sizeVal))
        sizeDelta = AMax(0, size - AValueToInt(sizeVal));
    
    /* Store the size of external data in object. */
    ASetMemberDirect(t, *temp, type->extDataMember, AIntToValue(size));
    
    inst = AValueToInstance(*temp);
    if (AIsNewGenBlock(&inst->type)) {
        /* Include additional external data size in accounting. */
        AAllocAmount += sizeDelta;
        AllocCounter += sizeDelta;
        
        /* Update largest object size. */
        if (size > ANewGenLargestBlockSize)
            ANewGenLargestBlockSize = size;
    } else {
        /* Add size delta to the size of old generation data. */
        AOldGenSize += sizeDelta;
    }

    AReleaseHeap();

    AFreeTemp(t);

    return TRUE;
}


/* Try to truncate a block. Block sizes must be rounded up properly. If 
   truncation is not possible, silently do nothing. */ 
void ATruncateBlock(void *block, unsigned long oldSize, unsigned long newSize)
{
    if (!AIsInNursery(block) && newSize < oldSize
            && newSize + A_MIN_BLOCK_SIZE <= oldSize) {
        ALockHeap();
        
        if (AIsNewGenBlock((AValue *)block)) {
            ANewGenNode *bigBlock = APtrSub(
                block, AGetBlockSize(sizeof(ANewGenNode)));
            bigBlock->size = newSize;
        }

        AAddFreeBlock(APtrAdd(block, newSize), oldSize - newSize);

        AReleaseHeap();
    }
}


static unsigned long ResetAllocAmount(void)
{
    unsigned long oldAllocated = AAllocAmount;
    AAllocAmount = 0;
    return oldAllocated;
}
