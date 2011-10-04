/* file.c - Compiler file operations (POSIX implementation)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* This file implements POSIX compatible file and directory management for the
   compiler. Some Windows specific tweaks are also included -- there is no
   Windows-specific implementation. */

#include "compile.h"
#include "error.h"
#include "cutil.h"
#include "files.h"
#include "gc.h"
#include "internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>


#define MAX_MODULE_PATH_LEN PATH_MAX
/* Alore source file extension */
#define EXTENSION ".alo"
#define MODULE_SEARCH_PATH_ENV_VAR "ALOREPATH"


static ABool HasExtension(const char *name, const char *ext);

static int GetDirLen(const char *path);


/* Initialize default module search path. It will contain the value of
   environment variable ALOREPATH, additPath and an OS-dependant base path. */
ABool AInitializeDefaultModuleSearchPath(const char *additPath)
{
    static char base[] = A_MODULE_SEARCH_PATH_BASE;
    int len;
    char *envPath;
    char *path;

    envPath = getenv(MODULE_SEARCH_PATH_ENV_VAR);
    if (envPath == NULL)
        envPath = "";

    len = strlen(base) + strlen(envPath) + strlen(additPath) + 3;

    path = AAllocStatic(len);
    if (path == NULL)
        return FALSE;

    strcpy(path, envPath);
    if (envPath[0] != '\0')
        strcat(path, A_PATH_SEPARATOR_STRING);
    strcat(path, additPath);
    if (additPath[0] != '\0')
        strcat(path, A_PATH_SEPARATOR_STRING);
    strcat(path, A_MODULE_SEARCH_PATH_BASE);

    ADefaultModuleSearchPath = path;

    return TRUE;
}


/* Default implementation of compiler initialization. Only initialize
   AModuleSearchPath. */
ABool ADefInitCompilation(const char *path, char *moduleSearchPath,
                          void *param)
{
    char *searchPath;
    int searchPathLen;
    int dirLen;

    if (moduleSearchPath == NULL)
        moduleSearchPath = "";

    /* Create new moduleSearchPath that contains the directory of the main
       source file, the moduleSearchPath variable given by the caller and the
       value of the ADefaultModuleSearchPath variable. */

    dirLen = GetDirLen(path);

    /* Calculate an upper bound of the length of the resulting string,
       including the \0 terminator. */
    searchPathLen = dirLen + strlen(moduleSearchPath) +
        strlen(ADefaultModuleSearchPath) + 3;
    if (!A_IS_ABS(path))
        searchPathLen += 2;

    searchPath = AAllocStatic(searchPathLen);
    if (searchPath == NULL)
        return FALSE;

    /* Get the path of the source file. */
    if (!A_IS_ABS(path)) {
        strcpy(searchPath, ".");
        if (dirLen > 0)
            strcat(searchPath, A_DIR_SEPARATOR_STRING);
    } else
        *searchPath = '\0';
    strncat(searchPath, path, dirLen);

    /* Get the path given by the caller. */
    if (*moduleSearchPath != '\0') {
        strcat(searchPath, A_PATH_SEPARATOR_STRING);
        strcat(searchPath, moduleSearchPath);
    }

    /* Get the path from the environment variable. */
    if (*ADefaultModuleSearchPath != '\0') {
        strcat(searchPath, A_PATH_SEPARATOR_STRING);
        strcat(searchPath, ADefaultModuleSearchPath);
    }

    AModuleSearchPath = searchPath;

    return TRUE;
}


/* Default implementation of opening a source file during compilation. */
void *ADefOpenFile(char *path, void *param)
{
    static int file;

#ifdef A_HAVE_WINDOWS
    file = open(path, O_RDONLY | O_BINARY);
#else
    file = open(path, O_RDONLY);
#endif
    if (file < 0)
        return NULL;
    else
        return &file;
}


/* Default implementation of reading a source file during compilation. */
ABool ADefRead(void *file, unsigned char *buf, unsigned len,
               Assize_t *actualLen, void *param)
{
    *actualLen = read(*(int *)file, buf, len);
    return *actualLen >= 0;
}


/* Default implementation of closing a source file during compilation. */
ABool ADefCloseFile(void *file, void *param)
{
    close(*(int *)file);
    return TRUE;
}


/* Default implementation of finding module files during compilation. Call the
   caller-provided addFile function to add a file to the file list. The caller
   must initialize *files.

   IDEA: Perhaps we don't really need to parametrize AddFileFunc, i.e. it
         could be constant? */
ABool ADefFindModule(AFileInterface *iface, AModuleId *module,
                     AAddFileFunc addFile, AFileList **files)
{
    char relPath[MAX_MODULE_PATH_LEN + 1];
    int relPathLen;
    char fullPath[MAX_MODULE_PATH_LEN + 1];
    char *search;

    relPathLen = ACreateModulePath(relPath, MAX_MODULE_PATH_LEN, module);
    if (relPathLen == -1)
        return FALSE;

    search = AModuleSearchPath;

    /* Go through all the directories in the search path. */
    while (*search != '\0') {
        void *dir;
        int baseLen;
        int pathLen;

        for (baseLen = 0;
             search[baseLen] != '\0' && search[baseLen] != A_PATH_SEPARATOR;
             baseLen++);

        pathLen = baseLen + relPathLen + 1;
        if (pathLen >= MAX_MODULE_PATH_LEN)
            return FALSE;

        ACopyMem(fullPath, search, baseLen);
        fullPath[baseLen] = A_DIR_SEPARATOR;
        strcpy(fullPath + baseLen + 1, relPath);

        if (search[baseLen] == A_PATH_SEPARATOR)
            search++;
        search += baseLen;

        dir = iface->openDir(fullPath);
        if (dir == NULL) {
#ifdef A_HAVE_DYNAMIC_C_MODULES
            /* Try loading a dynamic C module. */
            int i;
            struct stat buf;

            /* Replace the directory separators with underscores in the module
               name based portion of the path. */
            for (i = baseLen + 1; fullPath[i] != '\0'; i++) {
                if (fullPath[i] == A_DIR_SEPARATOR)
                    fullPath[i] = '_';
            }
            strcat(fullPath, DYNAMIC_C_MODULE_EXTENSION);
            if (stat(fullPath, &buf) == 0) {
                /* Add a dynamic C module. */
                if (!addFile(files, fullPath)) {
                    iface->closeDir(dir);
                    return FALSE;
                } else
                    break;
            }
#endif
            continue;
        }

        fullPath[pathLen++] = A_DIR_SEPARATOR;

        for (;;) {
            const char *fileName;
            int fileNameLen;

            fileName = iface->readDir(dir);
            if (fileName == NULL)
                break;

            fileNameLen = strlen(fileName);

            if (!HasExtension(fileName, EXTENSION))
                continue;

            if (pathLen + fileNameLen >= MAX_MODULE_PATH_LEN) {
                iface->closeDir(dir);
                AFreeFileList(*files);
                return FALSE;
            }

            ACopyMem(fullPath + pathLen, fileName, fileNameLen + 1);

            /* Add an Alore source file. */
            if (!addFile(files, fullPath)) {
                iface->closeDir(dir);
                return FALSE;
            }
        }

        iface->closeDir(dir);
        break;
    }

    return *files != NULL;
}


ABool ADefMapModule(char *moduleName, char *newModuleName)
{
    strcpy(newModuleName, moduleName);
    return TRUE;
}


void ADefDeinitCompilation(void *param)
{
    AFreeStatic(AModuleSearchPath);
}


/* Does the path have the specified extension? */
static ABool HasExtension(const char *name, const char *ext)
{
    int nameLen = strlen(name);
    int extLen  = strlen(ext);
    int i;

    if (nameLen < extLen || *name == '.')
        return FALSE;

    for (i = 0; i < extLen; i++)
        if (name[nameLen - extLen + i] != ext[i])
            return FALSE;

    return TRUE;
}


static int GetDirLen(const char *path)
{
    int i;

    for (i = strlen(path); i > 0 && !A_IS_DIR_SEPARATOR(path[i - 1]); i--);

    return i > 1 ? i - 1 : i;
}


void *ADefOpenDir(const char *path)
{
    return opendir(path);
}


const char *ADefReadDir(void *dir)
{
    struct dirent *e = readdir(dir);
    if (e == NULL)
        return NULL;
    else
        return e->d_name;
}


void ADefCloseDir(void *dir)
{
    closedir(dir);
}
