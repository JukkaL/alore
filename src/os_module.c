/* os_module.c - os module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* Implementation of the os module. Contains mostly miscellaneous file system
   related services.

   Platform-specific functionality is in separate source files: os_posix.c and
   os_win32.c. */

#include "aconfig.h"

#ifdef A_HAVE_OS_MODULE

#include <time.h>
#include <stdlib.h>
#include "alore.h"
#include "errmsg.h"
#include "util.h"
#include "os_module.h"
#include "internal.h"


/* IDEA: Add a function for extracting the drive portion of a Windows path? */


/* Global nums of os module constants. */
static int SeparatorNum;
static int AltSeparatorNum;
static int PathSeparatorNum;

/* Global nums of constants representing Alore functions implemented in the
   __os module. */
static int NormPathNum;
static int AbsPathNum;
static int ExpandUserNum;
static int MakeDirsNum;


/* os::DirName(path)
   Remove the last component from a path and return the remainder. */
static AValue DirName(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int i;

    i = AGetStr(t, frame[0], path, A_MAX_PATH_LEN);
    for (; i > 0 && !A_IS_DIR_SEPARATOR(path[i - 1])
             && !A_IS_DRIVE_SEPARATOR(path[i - 1]); i--);

    if (i > 0) {
        for (; A_IS_DIR_SEPARATOR(path[i - 1]); i--) {
            if (i == 1)
                break;
        }
    }

    if (i > 0 && A_IS_DRIVE_SEPARATOR(path[i - 1]) &&
        A_IS_DIR_SEPARATOR(path[i]))
        i++;

    return ASubStr(t, frame[0], 0, i);
}


/* os::BaseName(path)
   Return the last path component of a path. */
static AValue BaseName(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int len, i;

    len = AGetStr(t, frame[0], path, A_MAX_PATH_LEN);
    for (i = len; i > 0 && !A_IS_DIR_SEPARATOR(path[i - 1])
             && !A_IS_DRIVE_SEPARATOR(path[i - 1]); i--);

    return ASubStr(t, frame[0], i, len);
}


/* os::FileExt(path)
   Return the last file extension of a path. */
static AValue FileExt(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int len, i;

    len = AGetStr(t, frame[0], path, A_MAX_PATH_LEN);
    for (i = len - 1; i >= 0; i--) {
        if (A_IS_DIR_SEPARATOR(path[i])) {
            i = len;
            break;
        } else if (path[i] == '.')
            break;
    }

    if (i < 0)
        i = len;

    return ASubStr(t, frame[0], i, len);
}


/* os::FileBase(path)
   Return the path without the last file extension. */
static AValue FileBase(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int len, i;

    len = AGetStr(t, frame[0], path, A_MAX_PATH_LEN);
    for (i = len - 1; i >= 0; i--) {
        if (A_IS_DIR_SEPARATOR(path[i])) {
            i = len;
            break;
        } else if (path[i] == '.')
            break;
    }

    if (i < 0)
        i = len;

    return ASubStr(t, frame[0], 0, i);
}


/* os::Join(*components)
   Join path components. */
static AValue OsJoin(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int pathInd;
    int aLen;
    int i;

    pathInd = AGetStr(t, frame[0], path, A_MAX_PATH_LEN);

    aLen = AArrayLen(frame[1]);

    for (i = 0; i < aLen; i++) {
        int len;
        AValue item = AArrayItem(frame[1], i);
        AWideChar ch1;

        AExpectStr(t, item);
        len = AStrLen(item);
        if (len == 0) {
            if (pathInd > 0) {
                if (pathInd >= A_MAX_PATH_LEN - 1)
                    return ARaiseValueErrorND(t, NULL);
                /* Add separator if there isn't one unless if we have
                   just an MS-DOS drive specifier. */
                if (!A_IS_DIR_SEPARATOR(path[pathInd - 1])
                     && (!A_IS_DRIVE_PATH(path) || pathInd > 2)) {
                    path[pathInd++] = A_DIR_SEPARATOR;
                    path[pathInd] = '\0';
                }
            }
            continue;
        }

        if (pathInd > 0 && !A_IS_DIR_SEPARATOR(path[pathInd - 1])
            && (!A_IS_DRIVE_PATH(path) || pathInd > 2)) {
            if (pathInd == A_MAX_PATH_LEN)
                return ARaiseValueErrorND(t, NULL);
            path[pathInd++] = A_DIR_SEPARATOR;
        }

        ch1 = AStrItem(item, 0);
#ifndef A_HAVE_WINDOWS
        if (A_IS_DIR_SEPARATOR(ch1))
            pathInd = 0;
#else
        {
            AWideChar ch2 = AStrLen(item) > 1 ? AStrItem(item, 1) : ' ';
            if (A_IS_DIR_SEPARATOR(ch1) || (ch2 == ':' && AIsAlpha(ch1)))
                pathInd = 0;
        }
#endif
        pathInd += AGetStr(t, AArrayItem(frame[1], i), path + pathInd,
                           A_MAX_PATH_LEN - pathInd);
    }

    return AMakeStr(t, path);
}


/* is::IsAbs(path)
   Is a path absolute? */
static AValue IsAbs(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];

    AGetStr(t, frame[0], path, A_MAX_PATH_LEN);

    return A_IS_ABS(path) ? ATrue : AFalse;
}


/* os::Main() */
static AValue OSMain(AThread *t, AValue *frame)
{
    /* Initialize constants. */
    ASetConstGlobalByNum(t, SeparatorNum,
                         AMakeStr(t, A_DIR_SEPARATOR_STRING));
#ifdef A_ALT_DIR_SEPARATOR_STRING
    ASetConstGlobalByNum(t, AltSeparatorNum,
                         AMakeStr(t, A_ALT_DIR_SEPARATOR_STRING));
#endif
    ASetConstGlobalByNum(t, PathSeparatorNum,
                         AMakeStr(t, A_PATH_SEPARATOR_STRING));

    /* Some functions are defined in the __os module. Initialize aliases within
       the os module to refer to them. */
    ASetConstGlobalByNum(t, NormPathNum, AGlobal(t, "__os::NormPath"));
    ASetConstGlobalByNum(t, AbsPathNum, AGlobal(t, "__os::AbsPath"));
    ASetConstGlobalByNum(t, ExpandUserNum, AGlobal(t, "__os::ExpandUser"));
    ASetConstGlobalByNum(t, MakeDirsNum, AGlobal(t, "__os::MakeDirs"));

    return ANil;
}


A_MODULE(os, "os")
    A_IMPORT("__os")

    A_DEF(A_PRIVATE("Main"), 0, 5, OSMain)
    A_DEF("Remove", 1, 0, AOsRemove)
    A_DEF("Rename", 2, 0, AOsRename)
    A_DEF("MakeDir", 1, 0, AOsMakeDir)
    A_DEF("ChangeDir", 1, 0, AOsChangeDir)
    A_DEF("CurrentDir", 0, 0, AOsCurrentDir)
    A_DEF("MakeSymLink", 2, 0, AOsMakeSymLink)
    A_DEF("ReadSymLink", 1, 0, AOsReadSymLink)
    A_DEF("Sleep", 1, 0, AOsSleep)
    A_DEF("System", 1, 0, AOsSystem)
    A_DEF("Exists", 1, 0, AOsExists)
    A_DEF("IsDir", 1, 0, AOsIsDir)
    A_DEF("IsFile", 1, 0, AOsIsFile)
    A_DEF("IsLink", 1, 0, AOsIsLink)
    A_DEF_OPT("ListDir", 1, 2, 0, AOsListDir)
    A_DEF_OPT("WalkDir", 1, 2, 0, AOsWalkDir)
    A_DEF("IsAbs", 1, 0, IsAbs)
    A_DEF("DirName", 1, 0, DirName)
    A_DEF("BaseName", 1, 0, BaseName)
    A_DEF("FileExt", 1, 0, FileExt)
    A_DEF("FileBase", 1, 0, FileBase)
    A_DEF_VARARG("Join", 1, 1, 0, OsJoin)
    A_DEF("SetEnv", 2, 0, AOsSetEnv)
    A_DEF_OPT("GetEnv", 1, 2, 0, AOsGetEnv)
    A_DEF("ListEnv", 0, 1, AListEnv)
    A_DEF_OPT("SetPerm", 2, 4, 0, AOsSetPerm)
    A_DEF("SetModificationTime", 2, 0, ASetModificationTime)
    A_DEF("SetAccessTime", 2, 0, ASetAccessTime)
    A_DEF("User", 0, 0, AOsUser)

    A_DEF("__getpwnam", 1, 2, AOsGetpwnam)
    A_DEF("__GetFullPathName", 1, 0, AOsGetFullPathName)

    A_EMPTY_CONST_P("Separator", &SeparatorNum)
    A_EMPTY_CONST_P("AltSeparator", &AltSeparatorNum)
    A_EMPTY_CONST_P("PathSeparator", &PathSeparatorNum)

    /* Constants that are initialized to functions defined in __os. */
    A_EMPTY_CONST_P("NormPath", &NormPathNum)
    A_EMPTY_CONST_P("AbsPath", &AbsPathNum)
    A_EMPTY_CONST_P("ExpandUser", &ExpandUserNum)
    A_EMPTY_CONST_P("MakeDirs", &MakeDirsNum)

    A_CLASS_PRIV("Stat", 3)
        A_BINARY_DATA(sizeof(time_t) * 2)
        A_METHOD("create", 1, 1, AStatCreate)
        /* NOTE: StatImpl depends on the order of these variables! */
        A_EMPTY_CONST("size")
        A_EMPTY_CONST("isFile")
        A_EMPTY_CONST("isDir")
        A_EMPTY_CONST("isLink")
        A_EMPTY_CONST("isSpecial")
        A_GETTER("modificationTime", 7, AStatModificationTime)
        A_GETTER("accessTime", 7, AStatAccessTime)
        A_GETTER("isReadable", 0, AStatIsReadable)
        A_GETTER("isWritable", 0, AStatIsWritable)
        A_GETTER("owner", 0, AStatOwner)
        A_GETTER("ownerPerm", 0, AStatOwnerPerm)
        A_GETTER("otherPerm", 0, AStatOtherPerm)
        A_GETTER("groupPerm", 0, AStatGroupPerm)
    A_END_CLASS()
A_END_MODULE()


#endif /* HAVE_OS_MODULE */
