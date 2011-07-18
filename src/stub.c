/* stub.c - Driver for running compiled Alore programs

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "compile.h"
#include "internal.h"
#include "module.h"
#include "debug_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


static void *CompOpenFile(char *path, void *param);
static ABool CompRead(void *file, unsigned char *buf, unsigned len,
                      Assize_t *actualLen, void *param);
static ABool CompCloseFile(void *file, void *param);
static void *CompOpenDir(const char *path);
static const char *CompReadDir(void *dir);
static void CompCloseDir(void *dir);
static int PathCmp(const char *p1, const char *p2, int n);


extern const char AloreModuleSearchPath[];
extern const int NumAloreSourceFiles;
extern const char *AloreSourceFiles[];
extern const unsigned AloreSourceFileOffsets[];
extern const char AloreSourceFileData[];


int main(int argc, char **argv)
{
    int num;
    int returnValue;
    AFileInterface iface;
    AThread *t;
    const char *file;
    char *program;

    program = argv[0];

    argv++;
    argc--;

    returnValue = 0;
        
    file = AloreSourceFiles[0];

    iface.initCompilation = ADefInitCompilation;
    iface.openFile = CompOpenFile;
    iface.read = CompRead;
    iface.closeFile = CompCloseFile;
    iface.findModule = ADefFindModule;
    iface.openDir = CompOpenDir;
    iface.readDir = CompReadDir;
    iface.closeDir = CompCloseDir;
    iface.mapModule = ADefMapModule;
    iface.deinitCompilation = ADefDeinitCompilation;

#ifdef A_DEBUG
    {
        char *v = getenv("ADEBUG_N");
        if (v)
            ADebugCheckEveryNth = atoi(v);
        v = getenv("ADEBUG_F");
        if (v)
            ADebugCheckFirst = atoi(v);
        v = getenv("ADEBUG_L");
        if (v)
            ADebugCheckLast = atoi(v);
    }
#endif

    num = ALoadAloreProgram(&t, file, program, AloreModuleSearchPath,
                            TRUE, argc, argv, &iface, 0);
    if (num >= 0) {
        AValue val;
            
#ifdef A_DEBUG_COMPILER
        ADebugInstructionCounter = 1000000;
#endif
        strcpy(ADefaultModuleSearchPath, "");
        
        if (AHandleException(t))
            val = AError;
        else {
            val = ACallValue(t, AGlobalByNum(num), 0, NULL);
            AEndTry(t);
        }

        ADebugVerifyMemory();
        
        returnValue = AEndAloreProgram(t, val);
    } else {
        /* Could not start program execution for an unknown reason. */
        returnValue = 1;
    }

    return returnValue;
}


static unsigned ReadOffset;
static unsigned EndOffset;


static void *CompOpenFile(char *path, void *param)
{
    int i;

    for (i = 0; i < NumAloreSourceFiles; i++) {
        if (PathCmp(AloreSourceFiles[i], path, -1) == 0) {
            ReadOffset = AloreSourceFileOffsets[i];
            EndOffset = AloreSourceFileOffsets[i + 1];
            return &ReadOffset;
        }
    }
    
    return NULL;
}


static ABool CompRead(void *file, unsigned char *buf, unsigned len,
                      Assize_t *actualLen, void *param)
{
    *actualLen = AMin(len, EndOffset - ReadOffset);
    memcpy(buf, AloreSourceFileData + ReadOffset, *actualLen);
    ReadOffset += *actualLen;
    return TRUE;
}


static ABool CompCloseFile(void *file, void *param)
{
    return TRUE;
}


static int DirPos;
static int DirLen;


static ABool HasDirSeparator(const char *path)
{
    for (; *path != '\0'; path++) {
        if (A_IS_DIR_SEPARATOR(*path))
            return TRUE;
    }
    return FALSE;
}


static int PathCmp(const char *p1, const char *p2, int n)
{
    int i;
    
    if (n == -1)
        n = AMax(strlen(p1), strlen(p2));

    for (i = 0; i < n; i++) {
        if (A_IS_DIR_SEPARATOR(p1[i]) || A_IS_DIR_SEPARATOR(p2[i])) {
            if (A_IS_DIR_SEPARATOR(p1[i]) != A_IS_DIR_SEPARATOR(p2[i]))
                return -1;
        } else {
#ifdef A_CASE_INSENSITIVE_FILE_NAMES
            if (tolower(p1[i]) != tolower(p2[i]))
                return p1[i] - p2[i];
#else
            if (p1[i] != p2[i])
                return p1[i] - p2[i];
#endif
        }
    }

    return 0;
}


void FindNextFile(const char *path)
{
    while (DirPos < NumAloreSourceFiles) {
        if (strlen(AloreSourceFiles[DirPos]) > DirLen
            && PathCmp(AloreSourceFiles[DirPos], path, DirLen) == 0
            && !HasDirSeparator(AloreSourceFiles[DirPos] + DirLen + 1))
            return;
        DirPos++;
    }
    DirPos = -1;
}


static void *CompOpenDir(const char *path)
{
    DirLen = strlen(path);
    DirPos = 0;
    FindNextFile(path);
    return DirPos >= 0 ? &DirPos : NULL;
}


static const char *CompReadDir(void *dir)
{
    const char *ret;
    const char *p;
    
    if (DirPos < 0)
        return NULL;

    ret = AloreSourceFiles[DirPos];
    DirPos++;
    FindNextFile(ret);

    /* Find the file name portion of the path. */
    for (p = ret; *p != '\0'; p++) {
        if (A_IS_DIR_SEPARATOR(*p))
            ret = p + 1;
    }
    
    return ret;
}


static void CompCloseDir(void *dir)
{
}
