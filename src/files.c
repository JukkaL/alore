/* files.c - Maintain a collection of paths of compiled Alore source files

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This module implements an interface that associates source file names to
   integer ids.

   - InitializeFileList() initializes the data structures.
   - BeginNewModule(mod) notifies that a new module is being compiled (all file
     names of a module are stored in the same location).
   - AddNewFile(path) stores a file name and returns the associated id.
   - EndModule() notifies that all file names in the current module have been
     processed.

   - GetFilePath(buf, len, fileId) returns the file name associated to an id. */

#include "files.h"
#include "symtable.h"
#include "mem.h"
#include "gc.h"
#include "compile.h" /* FIX: not needed? */
#include "ident.h" /* FIX: not needed? */
#include "internal.h"


#define INITIAL_PATH_BUFFER_SIZE 128


/* PathBuffer stores path names for a single module. */
static char *PathBuffer;
static unsigned PathBufferSize;
static unsigned PathBufferIndex;

/* List of all files names. */
static AFileListBlock *AList;


static int FirstIndex;
static int CurIndex;
static int LastIndex;
static ASymbolInfo *Module;
static ABool Status;


static AFileListBlock Sentinel = {
    NULL, NULL, -1, 1, ""
};


static void FindNewSpot(void);
static void AddString(char *str, int len);
static void Finalize(void);


ABool AInitializeFileList(void)
{
    PathBufferSize = INITIAL_PATH_BUFFER_SIZE;
    PathBuffer = AAllocStatic(PathBufferSize);
    if (PathBuffer == NULL)
        return FALSE;
    
    AList = &Sentinel;
    
    return TRUE;
}


void ABeginNewModule(AModuleId *mod)
{
    Module = mod->numParts > 0 ? mod->id[mod->numParts - 1] : NULL;
    FindNewSpot();
    PathBufferIndex = 0;
    Status = TRUE;
}


unsigned AAddNewFile(char *name)
{
    char *start;
    unsigned len;

    len = strlen(name);
    
    if (len > 0) {
        /* FIX: path separator */
        for (start = name + len - 1;
             start > name && !A_IS_DIR_SEPARATOR(start[-1]); start--);
    } else
        start = name;

    if (PathBufferIndex == 0)
        AddString(name, start - name);

    AddString(start, len - (start - name));

    if (CurIndex == LastIndex) {
        Finalize();
        FindNewSpot();
    }
    
    return CurIndex++;
}


ABool AEndModule(void)
{
    Finalize();
    return Status;
}


/* Put the buffer into the linked list. */
static void Finalize(void)
{
    AFileListBlock *newBlock;
    AFileListBlock *block;

    newBlock = AAllocStatic(sizeof(AFileListBlock) + PathBufferIndex - 1);
    if (newBlock == NULL) {
        Status = FALSE;
        return;
    }

    newBlock->module = Module;

    newBlock->firstFileIndex = FirstIndex;
    newBlock->numFiles = CurIndex - FirstIndex;

    ACopyMem(newBlock->str, PathBuffer, PathBufferIndex);

    if (CurIndex > AList->firstFileIndex) {
        newBlock->next = AList;
        AList = newBlock;
    } else {
        block = AList;
        while (CurIndex <= block->next->firstFileIndex)
            block = block->next;
    
        newBlock->next = block->next;
        block->next = newBlock;
    }

    PathBufferIndex = 0;
}


static void AddString(char *str, int len)
{
    while (PathBufferIndex + len + 1 > PathBufferSize) {
        char *newBuf = AGrowStatic(PathBuffer, 2 * PathBufferSize);
        
        if (newBuf == NULL) {
            Status = FALSE;
            return;
        }

        PathBuffer = newBuf;
        PathBufferSize *= 2;
    }

    ACopyMem(PathBuffer + PathBufferIndex, str, len);
    PathBufferIndex += len + 1;
    PathBuffer[PathBufferIndex - 1] = '\0';
}


/* Find a free place in the file list and do something else. */
/* FIX: This is SLOW. */
static void FindNewSpot(void)
{
    AFileListBlock *block;

    block = AList;

    FirstIndex = block->firstFileIndex + block->numFiles;
    LastIndex = 1 << 30;

    while (block != &Sentinel) {
        if (block->firstFileIndex != block->next->firstFileIndex +
            block->next->numFiles) {
            FirstIndex = block->next->firstFileIndex + block->next->numFiles;
            LastIndex = block->firstFileIndex;
        }
        block = block->next;
    }

    CurIndex = FirstIndex;
}


void AGetFilePath(char *buf, int bufLen, int fileNum)
{
    AFileListBlock *block;
    int baseLen;
    int nameLen;
    char *s;
    int i;
    
    block = AList;
    while (block->firstFileIndex > fileNum)
        block = block->next;

    baseLen = strlen(block->str);
    s = block->str + baseLen + 1;

    baseLen = AMin(baseLen, bufLen - 1);
    ACopyMem(buf, block->str, baseLen);

    for (i = 0; i < fileNum - block->firstFileIndex; i++)
        s += strlen(s) + 1;

    nameLen = AMin(strlen(s), bufLen - baseLen - 1);
    ACopyMem(buf + baseLen, s, nameLen);
    buf[baseLen + nameLen] = '\0';
}


/* FIX: put somewhere else? */
int ACreateModulePath(char *path, int max, AModuleId *mod)
{
    int i;
    int partInd;

    i = 0;
    for (partInd = 0; partInd < mod->numParts; partInd++) {
        char *id = (char *)AGetSymbolName(mod->id[partInd]);
        int len = strlen(id);

        if (i + len + 1 >= max)
            return -1;

        while (*id != '\0')
            path[i++] = *id++;
        
        if (partInd < mod->numParts - 1)
            path[i++] = A_DIR_SEPARATOR;
    }

    path[i] = '\0';
    
    return i;
}
