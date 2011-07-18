/* memapi.c - Alore C API (memory related functions)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Alore C API Memory management functions. Memory blocks are implemented as
   narrow Str objects and and buffers are implemented as instances of the
   private class "std::__Buf". These objects should never be visible to Alore
   code, i.e. they should only be used internally in C code to facilitate
   storing C data types in the garbage collected heap. */

#include "alore.h"
#include "std_module.h"
#include "gc.h"
#include "mem.h"
#include "internal.h"


/* Private member numbers of __Buf instances */
#define BUF_MEM 0
#define BUF_INDEX 1


#define MemSize(m) AGetNonPointerBlockDataLength((AValue *)AValueToPtr(m))

#define ABufSize(b) \
    MemSize(AMemberDirect((b), BUF_MEM))


AValue AAllocMem(AThread *t, Asize_t size)
{
    return AMakeEmptyStr(t, size);
}


AValue AAllocMemFixed(AThread *t, Asize_t size)
{
    AValue *block = AAllocLocked(t, sizeof(AValue) + size);
    if (block == NULL)
        return ARaiseMemoryError(t);
    AInitNonPointerBlock(block, size);
    return ANonPointerBlockToValue(block);
}


AValue AReallocMem(AThread *t, AValue mem, Asize_t newSize)
{
    /* IDEA: Try to grow or shrink the current block. This is not optimized. */
    
    unsigned long size = MemSize(mem);
    if (newSize <= size)
        return mem;
    else {
        AValue *temp = AAllocTemp(t, mem);
        AValue new;

        if (AIsInNursery(AValueToPtr(mem)))
            new = AAllocMem(t, newSize);
        else
            new = AAllocMemFixed(t, newSize);
        
        memcpy(AMemPtr(new), AMemPtr(*temp), size);
        AFreeTemp(t);
        return new;
    }
    
}


void AFreeMem(AThread *t, AValue mem)
{
    /* IDEA: Implement. Currently we rely on the garbage collector. */
}


void *AMemPtr(AValue mem)
{
    return AStrPtr(mem);
}


static AValue AAllocBufInternal(AThread *t, unsigned long initReserve,
                                ABool isFixed)
{
    AValue *temp = AAllocTemps(t, 1);
    AValue mem;
    AValue ret;
    
    *temp = AMakeUninitializedObject(t, AGlobalByNum(AStdBufNum));
    
    if (isFixed)
        mem = AAllocMemFixed(t, initReserve);
    else
        mem = AAllocMem(t, initReserve);
    
    ASetMemberDirect(t, *temp, BUF_MEM, mem);
    ASetMemberDirect(t, *temp, BUF_INDEX, AZero);
    
    ret = *temp;
    AFreeTemps(t, 1);

    return ret;
}


AValue AAllocBuf(AThread *t, Asize_t initReserve)
{
    return AAllocBufInternal(t, initReserve, FALSE);
}


AValue AAllocBufFixed(AThread *t, Asize_t initReserve)
{
    return AAllocBufInternal(t, initReserve, TRUE);
}


void AAppendBuf(AThread *t, AValue buf, const char *data, Asize_t len)
{
    AValue *temp = AAllocTemp(t, buf);
    void *ptr = AReserveBuf(t, *temp, len);
    memcpy(ptr, data, len);
    AFreeTemp(t);
}


void *AReserveBuf(AThread *t, AValue buf, Asize_t len)
{
    int size = ABufSize(buf);
    int index = AValueToInt(AMemberDirect(buf, BUF_INDEX));

    if (index + len > size) {
        AValue *temp = AAllocTemp(t, buf);
        AValue new;
        
        if (size <= 16)
            size = 32;
        else
            size = AMax(index + len, size * 2);
        
        new = AReallocMem(t, AMemberDirect(buf, BUF_MEM), size);
        ASetMemberDirect(t, *temp, BUF_MEM, new);
        buf = *temp;
        
        AFreeTemp(t);
    }

    ASetMemberDirect(t, buf, BUF_INDEX, AIntToValue(index + len));
    return APtrAdd(ABufPtr(buf), index);
}


void AFreeBuf(AThread *t, AValue buf)
{
    /* IDEA: Implement. Currently we rely on the garbage collector. */
}


void *ABufPtr(AValue buf)
{
    return AMemPtr(AMemberDirect(buf, BUF_MEM));
}
