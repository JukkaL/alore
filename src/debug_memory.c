/* debug_memory.c - Heap structure checker

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"

#include <stdlib.h>
#include "runtime.h"
#include "debug_runtime.h"
#include "heap.h"
#include "mem.h"
#include "internal.h"
#include "gc.h"
#include "gc_internal.h"
#include "thread.h"
#include "util.h"
#include "symtable.h"
#include "thread_athread.h"
#include "opcode.h"
#include "class.h"
#include "memberid.h"


/* Only include the heap checker if we have a build with extra debugging
   features. */


#ifdef A_DEBUG

extern int NumFreezableThreads;


/* Static buffer used by FMT* macros */
static char FmtBuffer[128];

/* Helper macros that call sprintf with FmtBuffer as the target and return
   FmtBuffer. Be careful with these since they use a single static buffer!
   The macros only differ on the number of arguments they accept. */
#define FMT(fmt, val) (sprintf(FmtBuffer, fmt, val), FmtBuffer)
#define FMT2(fmt, val, val2) (sprintf(FmtBuffer, fmt, val, val2), FmtBuffer)
#define FMT3(fmt, val, val2, val3) \
    (sprintf(FmtBuffer, fmt, val, val2, val3), FmtBuffer)
#define FMT4(fmt, val, val2, val3, val4) \
    (sprintf(FmtBuffer, fmt, val, val2, val3, val4), FmtBuffer)


#define GetFreeListBlockSize(data) \
    (*(data) - A_FREE_BLOCK_FLAG + sizeof(AValue))


void InitBlockMap(void);
void FreeBlockMap(void);
ABool IsValidValue(AValue v);
ABool IsValidPointer(void *p);
void VerifyFreeLists(int *numExpected);
void CheckValueBlock(AValue *ptr);
void CheckInstanceBlock(AValue *ptr);
void CheckMixedBlock(AValue *ptr);
AValue *VerifyBlock(AValue *data, void *blockEnd, int dump);
void VerifyValue(AValue v);
AValue *NextBlock(AValue *data);
void DumpBlock(AValue *data);
void VerifyThreads(void);
static void VerifyThreadData(AThread *t);
void DumpThreadStack(AThread *t, int num);
void VerifyThreadStack(AThread *t);
void VerifyGlobals(void);
void VerifyClass(ATypeInfo *t);
void VerifyFunction(AFunction *f);
void DumpClass(ATypeInfo *t);
void DumpMemberHashTable(ATypeInfo *t, AMemberTableType tt, char *msg);
void VerifyFinalizeInstances(void);
ABool IsFreeBlock(AValue *p);
AValue GetAnyBlockSize(AValue *p);
static ABool Assert(ABool cond, char *msg);
void SetAndCheckBlockType(AValue *ptr, int type);
void CheckValueTargetType(AValue v);
void VerifyNursery(void);
void VerifyNoReferencesToBlockData(void);
void *VerifyBlockReferences(void *p);


void *ADebug_WatchLocation = NULL;
ABool ADebug_MemoryTrace = 0;
ABool ADebug_MemoryDump = 0;


/* Map from a gc-visible pointer to a block type id. Used to track that all
   pointers to a block have the same type and that there are no gc-visible
   pointers to the middle of a block. Implemented as a bitmap with
   BITS_PER_ENTRY bits per 8 bytes (or block size granularity). */
static unsigned char *BlockMap;
static void *BlockMapStartOffset;
static void *BlockMapEndOffset;

/* Block types in block bitmap. */
enum {
    UNKNOWN_BLOCK,
    VALUE_BLOCK,
    NONPTR_BLOCK,
    OTHER_BLOCK
};

/* Bits per entry in the block type map. This must be enough to encode all
   block types. */
#define BITS_PER_ENTRY 2

#define IND(p) APtrDiff((p), BlockMapStartOffset)
/* Query the type of a heap block using the block type map. Do not check that
   the pointer is valid. */
#define BlockType(p) \
    ((BlockMap[IND(p) >> 5] >> (((IND(p) & 31) >> 3) * 2)) & 3)
/* Set the type of a heap block in the block type map. Do not check that the
   pointer is valid. */
#define SetBlockType(p, t) \
    (BlockMap[IND(p) >> 5] |= ((t) << (((IND(p) & 31) >> 3) * 2)))


/* Check the contents of the garbage collected heap. */
void ADebugVerifyMemory_F(void)
{
    AHeapBlock *b;
    int numFreeBlocks[A_NUM_FREELISTS];
    int i;

    /* Only verify if debugging is currently switched on. */
    if (!ADebugIsOn())
        return;

    ADebugPeriodicCheck();
    
    AFreezeOtherThreadsSilently();
    
    ADebugStatusMsg(("Verifying memory\n"));

    for (i = 0; i < A_NUM_FREELISTS; i++)
        numFreeBlocks[i] = 0;

    InitBlockMap();

    b = AHeap;

    /* Check one heap chunk at a time. */
    while (b != NULL) {
        AValue *data = AGetHeapBlockData(b);
        AValue *end = AGetHeapBlockDataEnd(b);
        AValue *prevData = NULL;

        if (end < data)
            ADebugError_F("Invalid heap block at %s\n", FMT("%p", b));

        if (ADebug_MemoryDump)
            ADebugPrint(("  Heap block at %s\n", FMT("%p", b)));
        
        /* Iterate over all the blocks in the heap chunk and check the blocks
           individually. */
        while (data < end) {
            prevData = data;

            /* Count the number of free blocks of different sizes. */
            if ((data != ACurFreeBlock || ACurFreeBlockSize == 0)
                && AIsFreeListBlock(data)) {
                int size = GetFreeListBlockSize(data);
                numFreeBlocks[AGetFreeListNum(size)]++;
            }
            
            data = VerifyBlock(data, end, 1);
        }

        if (data != end)
            ADebugError_F("Block extends past heap block end at %s\n",
                         FMT("%p", prevData));

        if (ADebug_MemoryDump)
            ADebugPrint(("  End of heap block\n"));
        
        b = b->next;
    }

    /* Verify that the free lists have the correct number of free blocks. We
       got the correct numbers by traversing the entire heap. */
    VerifyFreeLists(numFreeBlocks);

    VerifyGlobals();
    VerifyThreads();
    VerifyFinalizeInstances();

    for (i = 0; i < A_THREAD_ARG_BUFFER_SIZE * 3; i++) {
        if (!IsValidValue(AThreadArgBuffer[i]))
            ADebugError_F("Invalid value in thread arg buffer (%s)\n",
                         FMT("%lx", AThreadArgBuffer[i]));
        VerifyValue(AThreadArgBuffer[i]);
    }
    
    /* IDEA: verify symbol table */
    /* IDEA: verify modules and other global stuff, including dynamic
       modules */
    /* IDEA: verify static blocks? */

    VerifyNursery();
    VerifyNoReferencesToBlockData();

    FreeBlockMap();
    
    if (ADebug_MemoryDump)
        ADebugStatusMsg(("End verifying memory\n"));

    AWakeOtherThreadsSilently();
}


/* Create an empty heap block type map that covers the current heap. The
   block map has to be re-created if the heap size changes, but this is not
   necessary during a single heap check since it cannot change the size of the
   heap. */
void InitBlockMap(void)
{
    AHeapBlock *b;
    int size;

    /* Find the lowest and highest possible heap pointer. */
    BlockMapStartOffset = AHeap;
    BlockMapEndOffset = AHeap;
    for (b = AHeap; b != NULL; b = b->next) {
        void *start = AGetHeapBlockData(b);
        void *end = AGetHeapBlockDataEnd(b);
        
        if (start < BlockMapStartOffset)
            BlockMapStartOffset = start;
        if (end > BlockMapEndOffset)
            BlockMapEndOffset = end;
    }
    if ((void *)ANurseryBegin < BlockMapStartOffset)
        BlockMapStartOffset = ANurseryBegin;
    if ((void *)ANurseryEnd > BlockMapEndOffset)
        BlockMapEndOffset = ANurseryEnd;

    /* Create bitmap. */
    size = (APtrDiff(BlockMapEndOffset, BlockMapStartOffset) * BITS_PER_ENTRY /
            8 / 8);
    BlockMap = malloc(size);
    memset(BlockMap, 0, size);
}


/* Free the block map. */
void FreeBlockMap(void)
{
    free(BlockMap);
}


/* Check if value points to a valid looking memory block with the expected
   type. */   
ABool IsValidValue(AValue v)
{
    AValue *ptr;

    if (AIsShortInt(v))
        return TRUE;

    ptr = AValueToPtr(v);

    if (AIsNewGenGCActive && AIsInNursery(ptr) && !AIsFloat(v)
        && !AIsNewGenBlock(ptr)) {
        /* The block has been moved by the copying collector. */
        v = AGetIndirectValue(v, ptr);
        ptr = AValueToPtr(v);
    }
    
    if (!IsValidPointer(ptr)) {
        ADebugPrint_F("Invalid pointer %p!\n", ptr);
        return FALSE;
    }

    if (AIsFloat(v))
        return TRUE;
    else if (AIsFixArray(v) || AIsMixedValue(v) || AIsSubStr(v))
        return Assert(AIsValueBlock(ptr), "Non-value block for value ref!\n");
    else if (AIsStr(v) || AIsWideStr(v) || AIsGlobalFunction(v)
             || AIsLongInt(v) || AIsConstant(v))
        return Assert(AIsNonPointerBlock(ptr), "Not non-pointer block for "
                      "non-pointer ref!\n");
    else if (AIsInstance(v))
        return Assert(AIsInstanceBlock(ptr), "Not instance block for "
                      "instance ref!\n");
    else if (AIsNonSpecialType(v))
        return Assert(AIsMixedBlock(ptr), "Not mixed block for "
                      "mixed ref!\n");
    else
        return FALSE;
}


/* Check if pointer points to a valid memory area. */
ABool IsValidPointer(void *p)
{
    AHeapBlock *b = AHeap;

    while (b != NULL) {
        if (AIsInsideHeapBlock(p, AGetHeapBlockData(b),
                               AGetHeapBlockDataEnd(b)))
            return TRUE;
        b = b->next;
    }

    if (AIsInNursery(p))
        return TRUE;
    
    return FALSE;
}


/* Check the contents of the free lists. */
void VerifyFreeLists(int *numExpected)
{
    int i;
    
    for (i = 1; i < A_NUM_FREELISTS; i++) {
        AFreeListNode *node = AFreeList[i];
        AFreeListNode *prev = AGetFreeList(i);
        int n = 0;
        int prevSize = 0;
        
        while (node != &AListTerminator && node != &AHeapTerminator) {
            unsigned long size;

            n++;
            
            if (!AIsFreeListBlock(&node->header))
                ADebugError_F("No A_FREE_BLOCK_FLAG in free list\n");
            
            size = node->header - A_FREE_BLOCK_FLAG + sizeof(AValue);
            if (size < prevSize)
                ADebugError_F("Free list not sorted according to block "
                              "size\n");
            prevSize = size;
            
            if (AGetFreeListNum(size) != i)
                ADebugError_F("Wrong size in free list");
            
            if (i > A_FIRST_BACKLINKED_FREELIST && node->prev != prev)
                ADebugError_F("Wrong back pointer in free list %d\n", i);

            if ((void *)node < A_MEM_START || (void *)node >= A_MEM_END
                || APtrAdd(node, size) > A_MEM_END)
                ADebugError_F("Free list block is out of bounds at %p\n",
                              node);
            
            if (i >= 8 /* FIX A_FIRST_NONUNIFORM_FREE_LIST */) {
                AFreeListNode *child = node->child;
                AFreeListNode *childPrev = node;
                while (child != NULL) {
                    n++;
                    
                    if (child->header != node->header)
                        ADebugError_F("Bad header in child list\n");
                    if (child->prev != childPrev)
                        ADebugError_F("Wrong back pointer in child list\n");
                    if (child->next != NULL)
                        ADebugError_F("Child has non-NULL next pointer\n");

                    childPrev = child;
                    child = child->child;
                }
            }

            prev = node;
            node = node->next;
        }

        if (n != numExpected[i]) {
            ADebugPrint_F(A_ERR_HEADER
                          "Wrong number of free blocks in free list "
                          "%d!\n", i);
            ADebugPrint_F(A_ERR_HEADER "Found %d, expected %d.\n", n,
                          numExpected[i]);
            ADebugPrint_F(A_ERR_HEADER "Blocks:\n");
            for (node = AFreeList[i]; node != &AListTerminator
                     && node != &AHeapTerminator; node = node->next) {
                ADebugPrint_F("\t\t");
                DumpBlock((AValue *)node);
                if (i >= 8 /* FIX A_FIRST_NONUNIFORM_FREE_LIST */) {
                    AFreeListNode *child = node->child;
                    while (child != NULL) {
                        ADebugPrint_F("\t\t");
                        DumpBlock((AValue *)node);
                        child = child->child;
                    }
                }
            }
            ADebugPrint_F("\t\tCurrentFreeBlock: %s (size %d)\n",
                          FMT("%p", ACurFreeBlock), ACurFreeBlockSize);
        }
    }
}


/* Check the contents of a value block starting at ptr. */
void CheckValueBlock(AValue *ptr)
{
    int len;
    int i;

    SetAndCheckBlockType(ptr, VALUE_BLOCK);
    
    len = AGetValueBlockDataLength(ptr) / sizeof(AValue);

    for (i = 0; i < len; i++) {
        if (!IsValidValue(ptr[i + 1])) {
            ADebugPrint_F(A_ERR_HEADER "At value block %s: ",
                          FMT("%p", ptr));
            ADebugPrint_F("%s at ", FMT("%lx", ptr[i + 1]));
            ADebugPrint_F("%s\n", FMT("%p", &ptr[i + 1]));
            ADebugError_F("Invalid value!\n");
        }
        CheckValueTargetType(ptr[i + 1]);
    }
}


/* Check the contents of an instance block. */
void CheckInstanceBlock(AValue *ptr)
{
    AInstance *inst;
    ATypeInfo *type;
    int i;

    SetAndCheckBlockType(ptr, OTHER_BLOCK);

    inst = (AInstance *)ptr;
    type = AGetInstanceType(inst);

    if (!IsValidPointer(type) || !AIsMixedBlock(&type->header1))
        ADebugError_F("Invalid typeinfo in instance block %s!\n",
                      FMT("%p", inst));
    
    /* FIX: check typeinfo structure? */

    for (i = 0; i < type->totalNumVars; i++) {
        if (!IsValidValue(inst->member[i])) {
            char s[100];

            ADebugPeriodicCheck();
            AFormatMessage(s, 100, "\"%q\"", AGetInstanceType(inst)->sym);
            ADebugError_F("Invalid value at instance block %s!\n",
                          FMT4("%p (member %d=0x%lx), type %s", inst, i,
                               inst->member[i], s));
        }
        CheckValueTargetType(inst->member[i]);
    }
}


/* Check the contents of a mixed block. */
void CheckMixedBlock(AValue *ptr)
{
    SetAndCheckBlockType(ptr, OTHER_BLOCK);

    /* FIX */
}


/* Set the type of a block. Also see if the block type had been set previously,
   and verify that the new type matches the previous one. */
void SetAndCheckBlockType(AValue *ptr, int type)
{
    int prevType = BlockType(ptr);
    if (prevType != UNKNOWN_BLOCK && prevType != type)
        ADebugError_F("Invalid block type at %s!\n", FMT("%p", ptr));
    SetBlockType(ptr, type);
}


/* Check that the type of a block referred to by a value is correct. Accept
   also an unknown block type. As a side effect, set the block type to the
   expected value. */ 
void CheckValueTargetType(AValue v)
{
    int type;
    void *ptr;
    
    if (AIsShortInt(v))
        return;

    ptr = AValueToPtr(v);
    type = BlockType(ptr);

    if (AIsFloat(v)) {
        if (type != UNKNOWN_BLOCK)
            ADebugError_F("Invalid float value (%s)\n", FMT("%lx", v));
    } else if (AIsFixArray(v) || AIsMixedValue(v) || AIsSubStr(v)) {
        if (type != UNKNOWN_BLOCK && type != VALUE_BLOCK)
            ADebugError_F("Invalid value-ptr value (%s)\n", FMT("%lx", v));
        SetBlockType(ptr, VALUE_BLOCK);
    } else if (AIsStr(v) || AIsWideStr(v) || AIsGlobalFunction(v)
               || AIsLongInt(v) || AIsConstant(v)) {
        if (type != UNKNOWN_BLOCK && type != NONPTR_BLOCK)
            ADebugError_F("Invalid nonptr value (%s)\n", FMT("%lx", v));
        SetBlockType(ptr, NONPTR_BLOCK);
    } else if (AIsInstance(v) || AIsNonSpecialType(v)) {
        if (type != UNKNOWN_BLOCK && type != OTHER_BLOCK)
            ADebugError_F("Invalid instance value (%s)\n", FMT("%lx", v));
        SetBlockType(ptr, OTHER_BLOCK);
    } else
        ADebugError_F("Invalid value type (%s)\n", FMT("%lx", v));
}


/* Report an event for memory location ptr. The msg argument describes the
   event. This can be used for tracing memory events. */
void ADebug_MemoryEvent(void *ptr, char *msg)
{
    if (ADebug_MemoryTrace || ptr == ADebug_WatchLocation)
        ADebugPrint((A_MSG_HEADER "%s block %s\n", msg, FMT("%p", ptr)));
}


/* Verify the contents of a block and return a pointer to the next byte
   after block end (usually the start of the next block). */
AValue *VerifyBlock(AValue *data, void *blockEnd, int dump)
{
    if (ADebug_MemoryDump && dump)
        DumpBlock(data);

    if (data == ACurFreeBlock && ACurFreeBlockSize > 0)
        return APtrAdd(data, ACurFreeBlockSize);
    else if (AIsFreeListBlock(data)) {
        if (AIsMarked(data))
            ADebugError_F("Free list block has mark bit at %s\n",
                          FMT("%p", data));
        return APtrAdd(data, GetFreeListBlockSize(data));
    } else if (IsFreeBlock(data))
        return APtrAdd(data, GetAnyBlockSize(data));
    else if (AIsNonPointerBlock(data)) {
        SetAndCheckBlockType(data, NONPTR_BLOCK);
        return APtrAdd(data, AGetNonPointerBlockSize(data));
    } else if (AIsValueBlock(data)) {
        unsigned size = AGetValueBlockSize(data);
        AValue *next = APtrAdd(data, AGetValueBlockSize(data));
        if (size > 200*1024*1024
            || (blockEnd != NULL && (void *)next > blockEnd)) {
            ADebugError_F("Invalid value block header at %s: "
                          "extends past block end\n  "
                          "(length is %d)\n", FMT("%p", data), size);
        }
        CheckValueBlock(data);
        return APtrAdd(data, AGetValueBlockSize(data));
    } else if (AIsInstanceBlock(data)) {
        CheckInstanceBlock(data);
        return APtrAdd(data, AGetInstanceBlockSize(data));
    } else if (AIsMixedBlock(data)) {
        CheckMixedBlock(data);
        return APtrAdd(data, AGetMixedBlockSize(data));
    } else {
        ADebugError_F("Invalid block type in header at %s\n", FMT("%p", data));
        /* Not reached */
        return NULL;
    }
}


void VerifyValue(AValue v)
{
    if (AIsShortInt(v))
        return;
    else if (AIsFloat(v)) {
        /* FIX: check floats */
        return;
    } else {
        AValue *p = AValueToPtr(v);

        if (AIsNewGenGCActive && AIsInNursery(p) && !AIsNewGenBlock(p)) {
            /* The block has been moved by the copying collector. */
            v = AGetIndirectValue(v, p);
            p = AValueToPtr(v);
        }
        
        VerifyBlock(p, NULL, 0);
        if (!IsValidPointer(APtrAdd(p, GetAnyBlockSize(p) - 1)) ||
                GetAnyBlockSize(p) > 100000000) /* FIX: bad check */
            ADebugError_F("Block extends past valid data area (%s)\n",
                          FMT("%p", p));
    }
}


/* Return a pointer to the next block after the given block. Assume that data
   points to the start of a heap-allocated gc-visible block (i.e. block
   header). */
AValue *NextBlock(AValue *data)
{
    if (data == ACurFreeBlock && ACurFreeBlockSize > 0)
        return APtrAdd(data, ACurFreeBlockSize);
    if (AIsFreeListBlock(data))
        return APtrAdd(data, GetFreeListBlockSize(data));
    if (IsFreeBlock(data))
        return APtrAdd(data, GetAnyBlockSize(data));
    if (AIsNonPointerBlock(data))
        return APtrAdd(data, AGetNonPointerBlockSize(data));
    else if (AIsValueBlock(data))
        return APtrAdd(data, AGetValueBlockSize(data));
    else if (AIsInstanceBlock(data))
        return APtrAdd(data, AGetInstanceBlockSize(data));
    else if (AIsMixedBlock(data))
        return APtrAdd(data, AGetMixedBlockSize(data));
    else {
        ADebugError_F("Invalid block type in header at %s\n", FMT("%p", data));
        /* Not reached */
        return NULL;
    }
}


/* Get the size of a gc-visible block based on the block header. */
AValue GetAnyBlockSize(AValue *data)
{
    if (AIsNonPointerBlock(data))
        return AGetNonPointerBlockSize(data);
    else if (AIsValueBlock(data))
        return AGetValueBlockSize(data);
    else if (AIsInstanceBlock(data))
        return AGetInstanceBlockSize(data);
    else if (AIsMixedBlock(data))
        return AGetMixedBlockSize(data);
    else {
        ADebugError_F("Queried size of an invalid block type!\n");
        return 0;
    }
}


/* Display information about a block (location, type and size). */
void DumpBlock(AValue *data)
{
    ADebugPrint_F("%s: ", FMT("%p", data));

    if (data == ACurFreeBlock && ACurFreeBlockSize > 0)
        ADebugPrint_F("Current free block (%d)", (int)ACurFreeBlockSize);
    else if (AIsFreeListBlock(data))
        ADebugPrint_F("Free block (%d)", (int)GetFreeListBlockSize(data));
    else if (IsFreeBlock(data))
        ADebugPrint_F("Free block (%d)", (int)GetAnyBlockSize(data));
    else if (AIsNonPointerBlock(data))
        ADebugPrint_F("Non-pointer block (%d)",
                     (int)AGetNonPointerBlockSize(data));
    else if (AIsValueBlock(data))
        ADebugPrint_F("AValue block (%d)", (int)AGetValueBlockSize(data));
    else if (AIsInstanceBlock(data))
        ADebugPrint_F("AInstance block (%d)",
                      (int)AGetInstanceBlockSize(data));
    else if (AIsMixedBlock(data))
        ADebugPrint_F("Mixed block (%d)", (int)AGetMixedBlockSize(data));

    ADebugPrint_F("\n");
}


/* Check thread-related information. */
void VerifyThreads(void)
{
    AThread *t;
    int i;

    for (t = AGetFirstThread(), i = 1;
         t != NULL; t = AGetNextThread(t), i++) {
#ifdef A_DEBUG_DISPLAY_STACK_TRACE
        DumpThreadStack(t, i);
#endif
        if (t->tempStackPtr < t->tempStack || t->tempStackPtr >
            t->tempStackEnd)
            ADebugError_F("Invalid thread temp stack ptr!\n");
        
        VerifyThreadStack(t);
        VerifyThreadData(t);
    }
}


void VerifyThreadData(AThread *t)
{
    int i;
    AGCListBlock *b;
    AGCListBlock *last;
    
    /* Verify tempStack. */
    for (i = 0; t->tempStack + i < t->tempStackPtr; i++) {
        if (!IsValidValue(t->tempStack[i]))
            ADebugError_F("Invalid value in tempStack!\n");
        VerifyValue(t->tempStack[i]);
    }
    
    /* Verify regExp. */
    for (i = 0; i < A_NUM_CACHED_REGEXPS * 2; i++) {
        if (!IsValidValue(t->regExp[i]))
            ADebugError_F("Invalid regular expression in thread!\n");
        VerifyValue(t->regExp[i]);
    }
    
    /* Verify untraced. */
    last = AGetGCListBlock(t->untracedEnd);
    for (b = t->untraced; b != last; b = b->next) {
        for (i = 0; i < A_GC_LIST_BLOCK_LENGTH; i++) {
            if (!IsValidValue(b->data.val[i]))
                ADebugError_F("Invalid value in untraced list!\n");
            VerifyValue(b->data.val[i]);
        }
    }
    for (i = 0; b->data.val + i < t->untracedPtr; i++) {
        if (!IsValidValue(b->data.val[i]))
            ADebugError_F("Invalid value in untraced list!\n");
        VerifyValue(b->data.val[i]);
    }
    
    /* Verify newRef (old generation -> new generation references). */
    if (!ANewRefPtrListInvalid) {
        last = AGetGCListBlock(t->newRefEnd);
        for (b = t->newRef; b != last; b = b->next) {
            for (i = 0; i < A_GC_LIST_BLOCK_LENGTH; i++) {
                if (!IsValidValue(*b->data.valPtr[i]))
                    ADebugError_F("Invalid value in newRef list!\n");
                VerifyValue(*b->data.valPtr[i]);
            }
        }
        for (i = 0; b->data.valPtr + i < t->newRefPtr; i++) {
            if (!IsValidValue(*b->data.valPtr[i]))
                ADebugError_F("Invalid value in newRef list!\n");
            VerifyValue(*b->data.valPtr[i]);
        }
    }
    
    /* Verify heapPtr and heapEnd. */
    /* FIX */

    /* FIX regExpFlags */
    /* FIX verify other thread information */
}


void DumpThreadStack(AThread *t, int num)
{
    AValue *sp = t->stackPtr;
    AValue *top = t->stackTop;
    AValue *bottom = t->stack;
    
    ADebugPrint_F(A_MSG_HEADER "Thread %d stack dump:\n", num);

    if ((void *)sp < (void *)bottom || sp >= top) {
        ADebugPrint_F("\tInvalid stack pointer, not within stack!\n");
        ADebugPrint_F("\t  bottom = %s\n", FMT("%p", bottom));
        ADebugPrint_F("\t  top    = %s\n", FMT("%p", top));
        ADebugPrint_F("\t  sp     = %s\n", FMT("%p", sp));
    }
    
    for (;;) {
        int frameSize = sp[0] / sizeof(AValue);
        int i;

        if (frameSize == 0) {
            ADebugPrint_F("\t  (empty stack frame)\n");
            break;
        }
        if (frameSize < 0 || frameSize > 20000)
            ADebugPrint_F("\t  Invalid frame size (%d)!\n", frameSize);
        
        ADebugPrint_F("\t");
        ADebugPrint_F("[%s] ", FMT("%p", sp));
        ADebugPrintValue(sp[1]);
        ADebugPrint_F(":\n");

        if (frameSize == 3)
            ADebugPrint_F("\t  (empty)\n");
        
        for (i = 3; i < frameSize; i++) {
            ADebugPrint_F("\t  %d: ", i);
            ADebugPrintValueWithType(sp[i]);
            ADebugPrint_F("\n");
        }

        /* Try to go to the next stack frame. */
        sp = ANextFrame(sp, sp[0]);
        if (sp[0] == AZero) {
            if (sp + 1 != top)
                ADebugPrint_F("\t  (not at top, %d diff)\n",
                             APtrDiff(top, sp + 1));
            
            /* We are at the bottom of the stack. */
            break;
        }
    }
    
    ADebugPrint_F(A_MSG_HEADER "End of stack dump.\n");
}


void VerifyThreadStack(AThread *t)
{
    AValue *sp = t->stackPtr;
    AValue *top = t->stackTop;
    AValue *bottom = t->stack;

    if (sp < bottom || sp > top)
        ADebugError_F("Stack pointer points outside stack\n");

    for (;;) {
        int frameSize;
        int i;

        if (sp[0] == AZero || sp == top - 1) {
            /* If we are at the bottom of the stack, break. */
            if (sp == top - 1)
                break;

            ADebugError_F("Corrupted end-of-stack marker\n");
        }
        
        if (!AIsShortInt(sp[0]))
            ADebugError_F("Invalid stack frame size at %s! (%d)\n",
                         FMT("%p", sp), (int)sp[0]);
        if ((!AIsGlobalFunction(sp[1]) || !IsValidValue(sp[1]))
            && sp[1] != A_THREAD_BOTTOM_FUNCTION)
            ADebugError_F("Invalid frame function at %s!\n", FMT("%p", sp));
        if (!AIsShortInt(sp[2]))
            ADebugError_F("Invalid frame return address at %s!\n",
                         FMT("%p", sp));
        
        frameSize = sp[0] / sizeof(AValue);

        if (sp + frameSize >= top)
            ADebugError_F("Corrupted stack: frame extends past stack top\n");
        
        for (i = 3; i < frameSize; i++) {
            if (!IsValidValue(sp[i]))
                ADebugError_F("Invalid value in frame %d at %d!\n",
                              (int)(top - sp), i);
            VerifyValue(sp[i]);
        }
        
        sp = ANextFrame(sp, sp[0]);
    }
}


static const char *ValueType(AValue v)
{
    if (AIsInstance(v))
        return "instance";
    else if (AIsShortInt(v))
        return "int";
    else if (AIsLongInt(v))
        return "longint";
    else if (AIsStr(v))
        return "str";
    else
        return "<not implemented>";
}


/* Verify Alore global variables. */
void VerifyGlobals(void)
{
    int i;
    int n;

    if (ANumMainGlobals == 0) {
        if (AFreeGlobalBlock == -1)
            n = AGlobalListLength;
        else
            n = AFreeGlobalBlock;
    } else
        n = ANumMainGlobals;

    for (i = 0; i < n; i++) {
        AValue v = AGlobalVars[i];
        
        if (!IsValidValue(v))
            ADebugError_F("Invalid global variable %s!\n",
                         FMT3("%d (0x%lx, type %s)", i, v, ValueType(v)));

        VerifyValue(v);

        /* FIX verify functions */
    }

#ifdef A_DEBUG_DUMP_GLOBALS
    ADebugPrint_F(A_MSG_HEADER "Global variables:\n");
    for (i = 0; i < n; i++) {
        AValue v = AGlobalVars[i];
        
        ADebugPrint_F("\t%d: ", i);
        ADebugPrintValueWithType(v);
        ADebugPrint_F("\n");
#ifdef A_DEBUG_DUMP_CLASSES
        if (AIsNonSpecialType(v))
            DumpClass(AValueToType(v));
#endif
    }
#endif
}


/* Check a type info structure. */
void VerifyClass(ATypeInfo *t)
{
    int i;
    
    /* FIX verify block sizes */
    /* FIX verify symbol */

    if (t->super != NULL && !AIsMixedBlock((AValue *)t->super))
        ADebugError_F("Invalid superclass!\n");

    /* Do nothing if the class has not been fully constructed yet. */
    if (AGlobalVars[t->create] == AZero)
        return;

    if (!AIsGlobalFunction(AGlobalVars[t->create]))
        ADebugError_F("Invalid constructor in class! (%s)\n",
                     FMT("%p", &t->create));

    /* Verify number of variables. */
    if (t->numVars > 1000)
        ADebugError_F("Invalid number of variables in class!\n");
    if (t->super != NULL
        && t->numVars != t->totalNumVars - t->super->totalNumVars)
        ADebugError_F("Inconsistent number of variables in class!\n");
    if (t->super == NULL && t->totalNumVars != t->numVars)
        ADebugError_F("Inconsistent number of variables in class!\n");

    /* Verify binary data sizes. */
    if (t->dataSize > 10000)
        ADebugError_F("Invalid dataSize in class!\n");
    if (t->dataOffset > t->dataSize + (t->totalNumVars + 1) * sizeof(AValue))
        ADebugError_F("Invalid dataOffset in class (offset=%d, numVars=%d)!\n",
                     t->dataOffset, t->totalNumVars);
    if (t->super != NULL && t->dataOffset != t->super->dataSize +
        (t->totalNumVars + 1) * sizeof(AValue))
        ADebugError_F("Inconstent dataOffset in class (%d)!\n", t->dataOffset);
    if (t->super == NULL
        && t->dataOffset != (t->totalNumVars + 1) * sizeof(AValue))
        ADebugError_F("Inconsistent dataOffset in class "
                     "(offset=%d), numVars=%d!\n", t->dataOffset,
                     t->totalNumVars);

    /* Verify instance size. */
    if (t->instanceSize != AGetBlockSize((1 + t->totalNumVars) *
                                         sizeof(AValue) + t->dataSize))
        ADebugError_F("Invalid instanceSize in class!\n");

    /* Verify member hash tables. */
    for (i = 0; i < A_NUM_MEMBER_HASH_TABLES; i++) {
        AValue v = t->memberTable[i];
        AMemberHashTable *m;
        int j;
        int n1, n2;

        /* Verify types. */
        if (!AIsStr(v) || !IsValidValue(v))
            ADebugError_F("Invalid member hash table reference %d in class!\n",
                          i);

        /* Verify block size. */
        m = AValueToPtr(v);
        if (AGetNonPointerBlockDataLength((AValue *)m) !=
                sizeof(AMemberHashTable)
                - sizeof(AValue) + m->size * sizeof(AMemberNode))
            ADebugError_F("Invalid member hash table %d size in class!\n", i);

        /* Verify trivially hash table mappings. */
        for (j = 0; j <= m->size; j++) {
            AMemberNode *n = m->item[j].next;
            int item;
            
            if (n != NULL && (n < m->item || n > m->item + m->size))
                ADebugError_F(
                    "Invalid next pointer in member hash table %d in "
                    "class!\n", i);
            if (m->item[j].key > 200000)
                ADebugError_F("Invalid key in member hash table %d in "
                             "class!\n", i);

            if (m->item[j].key == AM_NONE)
                continue;
            
            item = m->item[j].item;
            if ((item & A_VAR_METHOD)
                || i == MT_METHOD_PUBLIC || i == MT_METHOD_PRIVATE) {
                /* Verify that the method actually exists. */
                AValue g = AGlobalByNum(item & ~A_VAR_METHOD);
                if (!AIsGlobalFunction(g))
                    ADebugError_F("Method in class member hash table is not a "
                                 "function!\n");
                if (AValueToFunction(g)->sym != t->sym)
                    ADebugError_F("Method in class member hash table has not "
                                 "the correct symbol!\n");
            } else {
                if (item >= t->totalNumVars)
                    ADebugError_F("Invalid item in member hash table %d in "
                                 "class! (0x%s at %d)\n", i,
                                 FMT("%x", m->item[j].item), j);
            }
        }

        /* Verify hash table mapping semantics. */

        /* Calculate number of non-empty nodes using two different algorithms.
           They should give the same answers. */
        n1 = 0;
        n2 = 0;
        for (j = 0; j <= m->size; j++) {
            /* Only calculate non-empty hash nodes. */
            if (m->item[j].key != AM_NONE) {
                n1++;
                /* Is this the start of a hash chain? */
                if ((m->item[j].key & m->size) == j) {
                    AMemberNode *n;
                    /* Go through the hash chain and verify that all method
                       members are before all variable members. */
                    for (n = &m->item[j]; n != NULL; n = n->next) {
                        if (n->next != NULL && !(n->key & A_VAR_METHOD)
                            && (n->next->key & A_VAR_METHOD))
                            ADebugError_F("Direct access variable before "
                                         "method in class member hash "
                                         "table!\n");

                        /* All members in the chain should have the same hash
                           value. */
                        if ((n->key & m->size) != j)
                            ADebugError_F("Invalid item in class member hash "
                                         "chain!\n");
                        n2++;
                    }
                }
            }
        }
        if (n1 != n2)
            ADebugError_F("Corrupted class member hash table!\n");
    }
}


/* Dump a type. */
void DumpClass(ATypeInfo *t)
{
    ADebugPrint_F("\t  Dump of class %q:\n", t->sym);
    
    ADebugPrint_F("\t    Super:        ");
    if (t->super != NULL)
        ADebugPrint_F("%q\n", t->super->sym);
    else
        ADebugPrint_F("(none)\n");

    ADebugPrint_F("\t    create:       %d\n", t->create);
    ADebugPrint_F("\t    numVars:      %d\n", t->numVars);
    ADebugPrint_F("\t    totalNumVars: %d\n", t->totalNumVars);
    ADebugPrint_F("\t    dataSize:     %d\n", t->dataSize);
    ADebugPrint_F("\t    dataOffset:   %d\n", t->dataOffset);
    ADebugPrint_F("\t    instanceSize: %d\n", t->instanceSize);

    DumpMemberHashTable(t, MT_VAR_SET_PUBLIC, "public var set");
    DumpMemberHashTable(t, MT_VAR_SET_PRIVATE, "private var set");
    DumpMemberHashTable(t, MT_VAR_GET_PUBLIC, "public var get");
    DumpMemberHashTable(t, MT_VAR_GET_PRIVATE, "private var get");
    DumpMemberHashTable(t, MT_METHOD_PUBLIC, "public method");
    DumpMemberHashTable(t, MT_METHOD_PRIVATE, "private method");
}


/* Dump a type member hash table. */
void DumpMemberHashTable(ATypeInfo *t, AMemberTableType tt, char *msg)
{
    int i;
    AMemberHashTable *h;
    
    ADebugPrint_F("\t  Hash table for %s:\n", msg);
    
    h = AGetMemberTable(t, tt);
    for (i = 0; i <= h->size; i++) {
        int k;
        
        ADebugPrint_F("\t    %d: ", i);

        k = h->item[i].key;
        if (k == AM_NONE)
            ADebugPrint_F("(empty)");
        else {
            ADebugPrint_F("%M(%d) -> ", k, k);
            ADebugPrint_F("%d", h->item[i].item & ~A_VAR_METHOD);
            if (h->item[i].item & A_VAR_METHOD)
                ADebugPrint_F("(m)");
            if (h->item[i].next != NULL)
                ADebugPrint_F(" (next %d)", h->item[i].next - h->item);
        }
        ADebugPrint_F("\n");
    }
}


void VerifyFunction(AFunction *f)
{
    if (!AIsNonPointerBlock(&f->header))
        ADebugError_F("Invalid block header in function! (%s)\n",
                      FMT("%p", f));
    if (f->sym != NULL && !IsValidPointer(f->sym))
        ADebugError_F("Invalid symbol in function! (%s)\n", FMT("%p", f));
    /* FIX verify sym better */
    if ((f->minArgs & ~A_VAR_ARG_FLAG) > 100
        || (f->maxArgs & ~A_VAR_ARG_FLAG) > 100)
        ADebugError_F("Invalid number of arguments in function! (%s)\n",
                     FMT("%p", f));
    if (f->maxArgs < f->minArgs)
        ADebugError_F("minArgs > maxArgs in function! (%s)\n", FMT("%p", f));
    if (f->stackFrameSize > 65536)
        ADebugError_F("Invalid stack frame size in function! (%s)\n",
                     FMT("%p", f));
    if (f->codeLen > 4*1024*1024)
        ADebugError_F("Invalid codeLen in function! (%s)\n", FMT("%p", f));
    /* FIX verify fileNum */
    /* FIX verify opcodes */
}


void VerifyFinalizeInstances(void)
{
    AInstance *i;

    for (i = AOldGenFinalizeInst;
         i != NULL; i = AIntValueToPtr(i->member[0])) {
        if (!IsValidPointer(i))
            ADebugError_F("Invalid pointer in old gen finalizer list! (%s)\n",
                         FMT("%p", i));
        if (!AIsInstanceBlock(&i->type))
            ADebugError_F("Non-instance block in old gen finalizer list! "
                         "(%s)\n", FMT("%p", i));
        CheckInstanceBlock((AValue *)i);
    }

    for (i = ANewGenFinalizeInst;
         i != NULL; i = AIntValueToPtr(i->member[0])) {
        if (!IsValidPointer(i))
            ADebugError_F("Invalid pointer in new gen finalizer list! (%s)\n",
                         FMT("%p", i));
        if (!AIsInstanceBlock(&i->type))
            ADebugError_F("Non-instance block in new gen finalizer list! "
                         "(%s)\n", FMT("%p", i));
        CheckInstanceBlock((AValue *)i);
    }
}


/* Check the nursery. Only blocks whose type is known can be checked in
   the nursery; others have to be skipped. */
void VerifyNursery(void)
{
    void *p;
    int prevCount;
    int count = 0;

    do {
        prevCount = count;
        count = 0;
        
        p = ANurseryBegin;
        while (p < (void *)ANurseryEnd) {
            if (BlockType(p) != UNKNOWN_BLOCK) {
                if (!AIsNewGenGCActive || AIsNewGenBlock((AValue *)p))
                    p = VerifyBlock(p, ANurseryEnd, 0);
                else {
                    /* Indirect block. Skip it. */
                    void *p2 = AGetIndirectPointer(p);
                    p = APtrAdd(p, GetAnyBlockSize(p2));
                }
                    
                count++;
            } else
                p = APtrAdd(p, A_ALLOC_UNIT);
        }

        if (p > (void *)ANurseryEnd)
            ADebugError_F("Nursery block extends past nursery end\n");
    } while (count > prevCount);

    /* Iterate one final time, this time dumping contents if dump is active. */
    if (!AIsNewGenGCActive) {
        p = ANurseryBegin;
        while (p < (void *)ANurseryEnd) {
            if (BlockType(p) != UNKNOWN_BLOCK)
                p = VerifyBlock(p, ANurseryEnd, 1);
            else
                p = APtrAdd(p, A_ALLOC_UNIT);
        }
    }
}


/* Go through all live heap and nursery blocks and verify that there are no
   pointers that point inside the data area of any of the blocks. */
void VerifyNoReferencesToBlockData(void)
{
    AHeapBlock *b;
    void *p;

    for (b = AHeap; b != NULL; b = b->next) {
        p = AGetHeapBlockData(b);
        while (p < AGetHeapBlockDataEnd(b))
            p = VerifyBlockReferences(p);
    }

    /* Verify nursery only is new gen gc is not active. If it's active, the
       block tags are not valid all the time.
       IDEA: Fix the tags in the latter case. */
    if (!AIsNewGenGCActive) {
        p = ANurseryBegin;
        while (p < (void *)ANurseryEnd) {
            if (BlockType(p) != UNKNOWN_BLOCK)
                p = VerifyBlockReferences(p);
            else
                p = APtrAdd(p, A_ALLOC_UNIT);
        }
    }
}


/* Verify that there are no references to the middle of the block at p. */
void *VerifyBlockReferences(void *p)
{
    void *n = NextBlock(p);

    for (;;) {
        p = APtrAdd(p, A_ALLOC_UNIT);
        if (p >= n)
            break;
        if (BlockType(p) != UNKNOWN_BLOCK)
            ADebugError_F("Reference to contents of a block at %s!\n",
                         FMT("%p", p));
    }

    return n;
}


/* Does p point to a free block? */ 
ABool IsFreeBlock(AValue *p)
{
    if (AGCState == A_GC_SWEEP && !AIsInNursery(p) && !AIsSwept(p)
            && !AIsMarked(p))
        return TRUE;
    else
        return AIsFreeListBlock(p);
}


/* If cond is FALSE, display message. */
ABool Assert(ABool cond, char *msg)
{
    if (!cond)
        ADebugPrint_F(A_MSG_HEADER "%s", msg);
    return cond;
}


#if 0
void PrintDynaModuleInfo(void)
{
    int modNum;
    int i;
    int c, v;

    modNum = AFirstDynamicModule;
    while (modNum != 0) {       
        ADynaModule *mod = AValueToPtr(AGlobalByNum(modNum + 1));
        int len = AGetNumImportedModules(mod);
        
        printf("Module %d:\n", modNum);
        printf("  submods: ");
        for (i = 0; i < len; i++)
            printf("%d ", (int)AValueToInt(mod->importedModules[i]));
        printf("\n  consts: ");
        c = modNum;
        if (c != AValueToInt(mod->globalConstIndex))
            printf("cosh! ");
        do {
            printf("%d ", c);
            c = AValueToInt(AGlobalByNum(c));
        } while (c != 0);
        printf("\n  vars: ");
        v = AValueToInt(mod->globalVarIndex);
        do {
            printf("%d ", v);
            v = AValueToInt(AGlobalByNum(v));
        } while (v != 0);
        printf("\n");

        modNum = AValueToInt(mod->nextDynaModule);
    }
}
#endif


#endif /* DEBUG */
