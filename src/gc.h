/* gc.h - Garbage collector

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef GC_H_INCL
#define GC_H_INCL

#include "thread.h"
#include "heapalloc.h"
#include "debug_params.h"


ABool AInitializeGarbageCollector(unsigned long maxHeap);
void ADeinitializeGarbageCollector(void);

/* FIX: perhaps move these away? */
void *AAlloc(AThread *t, unsigned long size);
void *AAllocLocked(AThread *t, unsigned long size);

#ifdef A_DEBUG_GC_EVERY_NTH
#define AAlloc_M(t, size, ptr, result) \
    do {                               \
        (ptr) = AAlloc(t, size);       \
        (result) = (ptr) != NULL;      \
    } while (0)
#else
#define AAlloc_M(t, size, ptr, result) \
    do {                                             \
        if ((t)->heapPtr + (size) <= (t)->heapEnd) { \
            (ptr) = (void *)(t)->heapPtr;            \
            (t)->heapPtr += (size);                  \
            (result) = TRUE;                         \
        } else {                                     \
            (ptr) = AAlloc(t, size);                 \
            (result) = (ptr) != NULL;                \
        }                                            \
    } while (0)
#endif

void *AAllocUnmovable(unsigned long size);
void *AGrowStatic(void *ptr, unsigned long newSize);
void *AAllocKeep(AThread *t, unsigned long size, AValue *keep);

void *AAllocStatic(unsigned long size);
void AFreeStatic(void *ptr);

void ATruncateBlock(void *block, unsigned long oldSize, unsigned long newSize);

/* NOTE: These two functions assume that heap has already been locked by the
         caller! */
A_APIFUNC ABool ACollectGarbage(void);
ABool ACollectGarbageForced(void);

/* Unlike the above functions, this does not have any special preconditions. */
A_APIFUNC ABool ACollectAllGarbage(void);

/* FIX: Remove this? When can this be called? */
ABool ACollectNewGen(ABool forceRetire);

ABool AGrowNewRefList(AThread *t);
ABool AGrowUntracedList(AThread *t);


struct AHeapBlock_;


extern void *(*AMoreHeap)(struct AHeapBlock_ *block, unsigned long growSize,
                         unsigned long *realGrow);
extern void (*AFreeHeapBlock)(struct AHeapBlock_ * block);
extern void *(*AGrowNursery)(void *oldNursery, unsigned long oldSize,
                            unsigned long newSize);
extern void (*AFreeNursery)(void *nursery, unsigned long size);


#define AVerifyUntracedList(t) \
    ((t)->untracedPtr != (t)->untracedEnd \
     || AAdvanceUntracedList(t))


/* FIX: move to another place? */
/* FIX: take instance as value? */
/* FIX: rename! this is misleading */
#define ASetInstanceMember(t, inst, memberIndex, newValPtr) \
    AModifyObject(t, &(inst)->type, inst->member + \
                 (memberIndex), newValPtr)


/* *newValPtr must be unmovable and tracable by the gc. */
ABool AModifyObject(AThread *t, const AValue *header, AValue *objPtr,
                  AValue *newValPtr);

#define AModifyOldGenObject(t, objPtr, newValPtr) \
    AModifyObject(t, &ADummyOldGenHeader, objPtr, newValPtr)


/* newValPtr contains the new value. It must be unmovable and tracable by the
   gc (eg. in stack). The modified object must be tracable by garbage
   collector. */
#define AModifyObject_M(t, header, objPtr, newValPtr, result) \
    do {                                                          \
        AValue newVal_ = *(newValPtr);                            \
                                                                  \
        if (!AIsShortInt(newVal_) && !AIsNewGenBlock(header)) {   \
            AValue *ptr_ = AValueToPtr(newVal_);                  \
                                                                  \
            if ((!AIsFloat(newVal_) && AIsNewGenBlock(ptr_))      \
                || (AIsFloat(newVal_) && AIsInNursery(ptr_))) {   \
                if ((t)->newRefPtr != (t)->newRefEnd) {           \
                    *(t)->newRefPtr++ = (objPtr);                 \
                    *(objPtr) = newVal_;                          \
                    (result) = TRUE;                              \
                } else {                                          \
                    if (AAdvanceNewRefList(t)) {                  \
                        *(t)->newRefPtr++ = (objPtr);             \
                        *(objPtr) = *(newValPtr);                 \
                        (result) = TRUE;                          \
                    } else                                        \
                        (result) = FALSE;                         \
                }                                                 \
            } else if (AGCState == A_GC_MARK && !AIsMarked(ptr_)) { \
                if (AVerifyUntracedList(t)) {                     \
                    *t->untracedPtr++ = newVal_;                  \
                    *(objPtr) = newVal_;                          \
                    (result) = TRUE;                              \
                } else                                            \
                    (result) = FALSE;                             \
            } else {                                              \
                *(objPtr) = newVal_;                              \
                (result) = TRUE;                                  \
            }                                                     \
        } else {                                                  \
            *(objPtr) = newVal_;                                  \
            (result) = TRUE;                                      \
        }                                                         \
    } while (0)


/* Return value is true if non-zero and false otherwise. */
AMarkBitmapInt AIsMarked(void *ptr);
ABool AIsSwept(void *ptr);


extern int AGCState;

extern ABool AIsNewGenGCActive;


extern const AValue ADummyOldGenHeader;


/* Different states of incremental gc */
enum {
    A_GC_NONE,        /* Incremental gc not active */
    A_GC_MARK,        /* Mark active */
    A_GC_MARK_EXE,    /* Mark currently executing */
    A_GC_SWEEP        /* Sweep active */
};


extern AInstance *AOldGenFinalizeInst;
extern AInstance *ANewGenFinalizeInst;


typedef struct {
    long long allocCount;  /* Number of bytes allocated during execution */
    long long retireCount; /* Number of bytes retired from newgen to old */
    unsigned long heapSize;     /* Size of heap, without bitfields */
    unsigned long nurserySize;  /* Size of nursery, without bitfields */
    unsigned long oldGenSize;   /* Data bytes in old generation */
    unsigned long lastLiveSize; /* Live data size after last full gc */

    unsigned long newGenCollectCount; /* Number of new gen collections */
    unsigned long fullCollectCount;   /* Number of full collections */
    unsigned long forcedCollectCount; /* Number of full forced collections */
    unsigned long fullIncrementCount; /* Number of full collection
                                         increments */
} AGCStatistics;


extern AGCStatistics AGCStat;


#endif
