/* globals.c - Global variables

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "runtime.h"
#include "compile.h"
#include "mem.h"
#include "gc.h"
#include "debug_runtime.h"


#define INITIAL_GLOBAL_LIST_LENGTH 128


AValue *AGlobalVars;
int AGlobalListLength;

int AFirstDynamicModule;

int ANumMainGlobals;
int AFirstMainGlobalVariable;

int AFreeGlobalBlock;


static int GlobalVarIndex;
static int GlobalConstIndex;


/* These variables point to non-movable blocks, and there are additional
   references to the relevant blocks, so the GC does not need to be aware of
   these variables. */
AValue ANil;
AValue ATrue;
AValue AFalse;
AValue ADefault;
AValue AError;


#define GetIndexInBucket(index) \
    ((index) & (A_GLOBAL_BUCKET_SIZE - 1))
#define IsOutOfBucket(index) \
    (GetIndexInBucket(index) == 0)
#define GetBucketBegin(index) \
    ((index) & ~(A_GLOBAL_BUCKET_SIZE - 1))


static ABool GrowGlobalVarList(void);
static int AllocGlobalBlock(void);
static void AddFreeGlobalBlock(int index);


/* Allocate the global variable array and initialize some global values. */
ABool AInitializeGlobals(void)
{
    int i;
    AString *s;

    /* NOTE: Errors don't have to checked, since we can assume that initially
             enough free memory is available in the heap for the required data
             structures. */
    
    AFirstDynamicModule = 0;

    AFreeGlobalBlock = -1;
    AGlobalVars = NULL;

    /* Allocate space for the built-in global variables. */
    for (i = 0; i < GL_NUM_BUILTIN_GLOBALS; i += A_GLOBAL_BUCKET_SIZE)
        AllocGlobalBlock();

    /* Initialize a few built-in global variables. */
    ASetConstGlobalValue(ACompilerThread, GL_NIL, ACreateConstant(NULL));
    ASetConstGlobalValue(ACompilerThread,
                         GL_DEFAULT_ARG, ACreateConstant(NULL));
    ASetConstGlobalValue(ACompilerThread, GL_ERROR, ACreateConstant(NULL));

    /* Initialize a few C level global variables that contain some commonly
       needed values. */
    ANil = AGlobalByNum(GL_NIL);
    ADefault = AGlobalByNum(GL_DEFAULT_ARG);
    AError = AGlobalByNum(GL_ERROR);
    /* ATrue and AFalse are initialized in std::Main. */
    
    /* FIX: perhaps use some function.. */
#ifdef A_NEWLINE_CHAR2
    s = AAlloc(ACompilerThread, sizeof(AValue) + 2);
    AInitNonPointerBlock(&s->header, 2);
    s->elem[1] = A_NEWLINE_CHAR2;
#else
    s = AAlloc(ACompilerThread, sizeof(AValue) + 1);
    AInitNonPointerBlock(&s->header, 1);
#endif
    s->elem[0] = A_NEWLINE_CHAR1;
    ASetConstGlobalValue(ACompilerThread, GL_NEWLINE, AStrToValue(s));
    
    AAllocateModuleGlobals(&AFirstMainGlobalVariable, &i);

    ANumMainGlobals = 0;

    return TRUE;
}


/* Allocate some global variables for a module. Set "GlobalVarIndex" and
   "GlobalConstIndex". */
ABool AAllocateModuleGlobals(int *firstVar, int *firstConst)
{
    int globalVar;
    int globalConst;
    
    globalVar = AllocGlobalBlock();
    if (globalVar == -1)
        return FALSE;
    
    globalConst = AllocGlobalBlock();
    if (globalConst == -1)
        return FALSE;

    *firstVar   = globalVar;
    *firstConst = globalConst;

    /* The first value in a block is a link to the next block. */
    GlobalVarIndex = globalVar + 1;

    /* Reserve space for the link, the module object and mark flag. */
    GlobalConstIndex = globalConst + 3;

    return TRUE;
}


void ARecordNumMainGlobals(void)
{
    if (AFreeGlobalBlock == -1)
        ANumMainGlobals = AGlobalListLength;
    else
        ANumMainGlobals = AFreeGlobalBlock;
}


/* Grow the global variable list. The current thread must be the only thread
   executing. */
static ABool GrowGlobalVarList(void)
{
    AValue *new;
    unsigned long newLen;
    int i;

    ADebugCompilerMsg(("Grow global variable list"));

    if (AGlobalVars == NULL)
        newLen = INITIAL_GLOBAL_LIST_LENGTH;
    else {
        /* Collect new generation, since there might pointers to the global
           constants in old gen -> newgen reference list, and this will clear
           the references. */
        /* FIX: This is somewhat ugly...  */
        if (!ACollectNewGen(FALSE))
            return FALSE;
        
        newLen = 2 * AGlobalListLength;
    }

    /* FIX: What if gc is on? We have to end the gc, presumably.. Though this
            might be impossible, since old gen gc will be disallowed anyway? */
    if (AGCState != A_GC_NONE)
        return FALSE /* FIX: End gc.. */;

    /* Now it should be safe to modify the global variable list. */

    new = AGrowStatic(AGlobalVars, newLen * sizeof(AValue));
    if (new == NULL)
        return FALSE;

    AGlobalVars = new;
    
    for (i = newLen - A_GLOBAL_BUCKET_SIZE;
         i >= AGlobalListLength; i -= A_GLOBAL_BUCKET_SIZE)
        AddFreeGlobalBlock(i);
    
    AGlobalListLength = newLen;

    ADebugCompilerMsg(("End grow global variable list"));
    
    return TRUE;
}


static void AddFreeGlobalBlock(int index)
{
    AGlobalVars[index] = AIntToValue(AFreeGlobalBlock);
    AFreeGlobalBlock = index;
}


void AFreeGlobals(int index)
{
    do {
        int next = AGetNextGlobalBucket(index);
        AddFreeGlobalBlock(index);
        index = next;
    } while (index != 0);
}


static int AllocGlobalBlock(void)
{
    int result;
    int i;
    
    if (AFreeGlobalBlock == -1) {
        if (!GrowGlobalVarList())
            return -1;
    }

    result = AFreeGlobalBlock;
    AFreeGlobalBlock = AValueToInt(AGlobalVars[result]);

    for (i = 0; i < A_GLOBAL_BUCKET_SIZE; i++)
        AGlobalVars[result + i] = AZero;
    
    return result;
}


/* Allocate a new global variable and initialize it to value "v". Return the
   number of the variable or -1 if failed.
   NOTE: The assigned object may be moved.
   NOTE: Compilation must be active. */
/* FIX: check that all callers take -1 gracefully */
int AAddGlobalValue(AValue v)
{
    int num;

    num = GlobalVarIndex;
    
    if (IsOutOfBucket(num)) {
        int newBlock;
        int curBucket;

        *ACompilerThread->tempStack = v;
        newBlock = AllocGlobalBlock();
        v = *ACompilerThread->tempStack;
        
        curBucket = GetBucketBegin(num - 1);
        
        if (newBlock == -1)
            return -1;

        AGlobalVars[curBucket] = AIntToValue(newBlock);

        num = newBlock + 1;
        GlobalVarIndex = num;
    }

    AGlobalVars[num] = v;
    
    GlobalVarIndex++;

    return num;
}


/* Allocate a new constant global variable and initialize it to value "v".
   Return the number of the variable or -1 if failed.
   NOTE: The assigned object may be moved.
   NOTE: Compilation must be active. */
int AAddConstGlobalValue(AValue v)
{
    int num;

    num = GlobalConstIndex;

    if (IsOutOfBucket(num)) {
        int newBlock;
        int curBucket;

        *ACompilerThread->tempStack = v;
        newBlock = AllocGlobalBlock();
        v = *ACompilerThread->tempStack;
        
        curBucket = GetBucketBegin(num - 1);

        if (newBlock == -1)
            return -1;

        AGlobalVars[curBucket] = AIntToValue(newBlock);

        num = newBlock + 1;
        GlobalConstIndex = num;
    }

    if (!ASetConstGlobalValue(ACompilerThread, num, v))
        return -1;

    GlobalConstIndex++;

    return num;
}


/* Modify a global value. Add it to untraced list.
   NOTE: The assigned object may be moved. */
ABool ASetConstGlobalValue(AThread *t, int n, AValue v)
{
    ABool result;

    *t->tempStack = v;
    result = AModifyOldGenObject(t, AGlobalVars + n, t->tempStack);
    *t->tempStack = AZero;

    return result;
}
