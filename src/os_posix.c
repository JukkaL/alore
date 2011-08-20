/* os_posix.c - Unix/posix specific implementation details of the os module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"

#if defined(A_HAVE_OS_MODULE) && !defined(A_HAVE_WINDOWS)

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>
#include <pwd.h>
#include "alore.h"
#include "os_module.h"
#include "io_module.h"
#include "str.h"
#include "array.h"
#include "internal.h"
#include "str_internal.h"


#define MKDIR_MODE 0777 /* FIX: what is the correct mode? symbolic? */


/* Evaluate a blocking expression so that the current thread can be
   suspended. */
#define BLOCKING(expr) \
    (AAllowBlocking(), (expr), AEndBlocking())


static AValue MakeDateTime(AThread *t, AValue *frame, time_t t1);


/* Process the status return value of a C lib operation. If status is negative,
   raise an IoError with path as the argument. Otherwise, return nil. */
static AValue CheckStatus(AThread *t, int status, const char *path)
{
    if (status < 0)
        return ARaiseErrnoIoError(t, path);
    else
        return ANil;
}


/* os::Remove(path)
   Remove a file, a directory or another name in the file system. */
AValue AOsRemove(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;
    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = remove(path));
    return CheckStatus(t, status, path);
}


/* os::Rename(source, target)
   Rename a file or a directory. */
AValue AOsRename(AThread *t, AValue *frame)
{
    char source[A_MAX_PATH_LEN], target[A_MAX_PATH_LEN];
    int status;
    
    /* IDEA: Maybe return a special error if cannot rename because destination
       on a different file system. */

    /* IDEA: Probably create the directories beneath the destination directory
       as needed. */

    AGetStr(t, frame[0], source, sizeof(source));
    AGetStr(t, frame[1], target, sizeof(target));
    BLOCKING(status = rename(source, target));

    return CheckStatus(t, status, NULL);
}


/* os::MakeDir(path)
   Create a directory. */
AValue AOsMakeDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;
    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = mkdir(path, MKDIR_MODE));
    return CheckStatus(t, status, path);
}


/* os::ChangeDir(path)
   Change the current working directory. */
AValue AOsChangeDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;
    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = chdir(path));
    return CheckStatus(t, status, path);
}


/* os::CurrentDir()
   Return the current working directory. */
AValue AOsCurrentDir(AThread *t, AValue *frame)
{
    char buf[A_MAX_PATH_LEN];
    
    if (getcwd(buf, A_MAX_PATH_LEN) == NULL)
        return ARaiseErrnoIoError(t, NULL);
    else
        return AMakeStr(t, buf);
}


/* os::MakeSymLink(target, path)
   Create a symbolic link that points to target. */
AValue AOsMakeSymLink(AThread *t, AValue *frame)
{
    char target[A_MAX_PATH_LEN], path[A_MAX_PATH_LEN];
    int status;
    
    AGetStr(t, frame[0], target, sizeof(target));
    AGetStr(t, frame[1], path, sizeof(path));
    
    BLOCKING(status = symlink(target, path));
    return CheckStatus(t, status, NULL);
}


/* os::ReadSymLink(path)
   Read the target of a symbolic link. */
AValue AOsReadSymLink(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    char buf[A_MAX_PATH_LEN];
    ssize_t status;
    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = readlink(path, buf, A_MAX_PATH_LEN - 1));
    if (status < 0)
        return ARaiseErrnoIoError(t, path);
    else {
        buf[status] = '\0';
        return AMakeStr(t, buf);
    }
}


/* os::Sleep(time)
   Suspend the execution of the current thread for a certain period time (in
   seconds). */
AValue AOsSleep(AThread *t, AValue *frame)
{
    double seconds = AGetFloat(t, frame[0]);
     /* FIX: Rounding? Overflow? */
    BLOCKING(usleep((int)(seconds * 1000000)));
    return ANil;
}


/* os::System(cmd)
   Execute an executable or a command using the command interpreter. */
AValue AOsSystem(AThread *t, AValue *frame)
{
    /* IDEA: Can we analyze the return code and raise an exception or
       something -- but perhaps we'd better not. */
    char cmd[1024]; /* IDEA: Use a constant for the size */
    int error;
    AGetStr(t, frame[0], cmd, sizeof(cmd));
    BLOCKING(error = system(cmd));
    return AMakeInt(t, error);
}


/* os::Exists(path)
   Does a path point to any directory entry? */
AValue AOsExists(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = stat(path, &buf));

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }
    
    return ATrue;
}


/* os::IsFile(path)
   Does a path point to a file? */
AValue AOsIsFile(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = stat(path, &buf));

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }
    
    return S_ISREG(buf.st_mode) ? ATrue : AFalse;
}


/* os::IsDir(path)
   Does a path point to a directory? */
AValue AOsIsDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = stat(path, &buf));

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }

    return S_ISDIR(buf.st_mode) ? ATrue : AFalse;
}


/* os::IsLink(path)
   Does a path point to a link? */
AValue AOsIsLink(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    BLOCKING(status = lstat(path, &buf));

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }

    return S_ISLNK(buf.st_mode) ? ATrue : AFalse;
}


/* os::Stat slot ids */
enum {
    STAT_PATH,
    STAT_UID,
    STAT_PERM,
    STAT_SIZE,
    STAT_IS_FILE,
    STAT_IS_DIR,
    STAT_IS_LINK,
    STAT_IS_SPECIAL
};


/* Stat create(path) */
AValue AStatCreate(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    ABool isLink = FALSE;
    int status;

    AGetStr(t, frame[1], path, A_MAX_PATH_LEN);
    
    BLOCKING(status = lstat(path, &buf));
    
    if (status < 0)
        return ARaiseErrnoIoError(t, path);

    if (S_ISLNK(buf.st_mode)) {
        isLink = TRUE;
        if (stat(path, &buf) < 0)
            return ARaiseErrnoIoError(t, path);
    }

    ASetMemberDirect(t, frame[0], STAT_PATH, frame[1]);
    ASetMemberDirect(t, frame[0], STAT_UID, AIntToValue(buf.st_uid));
    ASetMemberDirect(t, frame[0], STAT_PERM, AIntToValue(buf.st_mode));
    /* We must support file sizes larger than 4GB. */
    frame[2] = AMakeInt64(t, buf.st_size);
    ASetMemberDirect(t, frame[0], STAT_SIZE, frame[2]);
    ASetMemberDirect(t, frame[0], STAT_IS_FILE, S_ISREG(buf.st_mode)
                    ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_DIR, S_ISDIR(buf.st_mode)
                    ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_LINK, isLink ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_SPECIAL, S_ISCHR(buf.st_mode) ||
                    S_ISBLK(buf.st_mode) || S_ISFIFO(buf.st_mode) ||
                    S_ISSOCK(buf.st_mode) ? ATrue : AFalse);

    *(time_t *)ADataPtr(frame[0], 0) = buf.st_mtime;
    *(time_t *)ADataPtr(frame[0], sizeof(time_t)) = buf.st_atime;
    
    return frame[0];
}


/* os::Stat modificationTime (getter) */
AValue AStatModificationTime(AThread *t, AValue *frame)
{
    time_t t1 = *(time_t *)ADataPtr(frame[0], 0);
    return MakeDateTime(t, frame, t1);
}


/* os::Stat accessTime (getter) */
AValue AStatAccessTime(AThread *t, AValue *frame)
{
    time_t t1 = *(time_t *)ADataPtr(frame[0], sizeof(time_t));
    return MakeDateTime(t, frame, t1);
}


/* Convert a time_t value to a string of form "YYYY-MM-DD hh:mm:ss.ss" (in
   local time). */
static AValue MakeDateTime(AThread *t, AValue *frame, time_t t1)
{
    struct tm t2;
    char buf[30];

    localtime_r(&t1, &t2);
    
    sprintf(buf, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
            t2.tm_year + 1900,
            t2.tm_mon + 1,
            t2.tm_mday,
            t2.tm_hour,
            t2.tm_min,
            AMin(59, t2.tm_sec));

    return AMakeStr(t, buf);
}


/* os::Stat isReadable (getter) */
AValue AStatIsReadable(AThread *t, AValue *frame)
{
    AValue pathVal = AMemberDirect(frame[0], STAT_PATH);
    char path[A_MAX_PATH_LEN];
    int status;
    
    AGetStr(t, pathVal, path, A_MAX_PATH_LEN);

    BLOCKING(status = access(path, R_OK));
    
    return status == 0 ? ATrue : AFalse;
}


/* os::Stat isWritable (getter) */
AValue AStatIsWritable(AThread *t, AValue *frame)
{
    AValue pathVal = AMemberDirect(frame[0], STAT_PATH);
    char path[A_MAX_PATH_LEN];
    int status;
    
    AGetStr(t, pathVal, path, A_MAX_PATH_LEN);

    BLOCKING(status = access(path, W_OK));
    
    return status == 0 ? ATrue : AFalse;
}


/* Convert a numeric userid to a string value. */
static AValue UidToStr(AThread *t, int uid)
{
    /* IDEA: Perhaps there should some way of caching these results, since the
             getpwuid_r() call might be slow. */
    
    struct passwd pwbuf;
    char buf[2048];

#ifdef HAVE_GETPWUID_R_POSIX
    int status;
    struct passwd *pwbufptr;
    status = getpwuid_r(uid, &pwbuf, buf, sizeof(buf), &pwbufptr);
    if (status != 0) {
        /* getpwuid_r returns an error number but does not modify errno
           according to Linux manpages. */
        errno = status;
        return ARaiseErrnoIoError(t, NULL);
    }
#else
    /* Solaris getpwuid_r is slightly different... */
    if (getpwuid_r(uid, &pwbuf, buf, sizeof(buf)) == NULL)
        return ARaiseErrnoIoError(t, NULL);
#endif
    
    return AMakeStr(t, pwbuf.pw_name);
}


/* os::Stat owner (getter) */
AValue AStatOwner(AThread *t, AValue *frame)
{
    int uid = AValueToInt(AMemberDirect(frame[0], STAT_UID));
    return UidToStr(t, uid);
}


/* Make a permission string containing some of r/w/x to represent permissions,
   based on lower 3 bits of perm. */
static AValue MakePermStr(AThread *t, int perm)
{
    char buf[4];
    int i = 0;

    if (perm & 4)
        buf[i++] = 'r';
    if (perm & 2)
        buf[i++] = 'w';
    if (perm & 1)
        buf[i++] = 'x';
    buf[i] = '\0';
    return AMakeStr(t, buf);
}


/* os::Stat ownerPerm (getter) */
AValue AStatOwnerPerm(AThread *t, AValue *frame)
{
    int perm = AValueToInt(AMemberDirect(frame[0], STAT_PERM));
    return MakePermStr(t, (perm >> 6) & 7);
}


/* os::Stat otherPerm (getter) */
AValue AStatOtherPerm(AThread *t, AValue *frame)
{
    int perm = AValueToInt(AMemberDirect(frame[0], STAT_PERM));
    return MakePermStr(t, perm & 7);
}


/* os::Stat groupPerm (getter) */
AValue AStatGroupPerm(AThread *t, AValue *frame)
{
    int perm = AValueToInt(AMemberDirect(frame[0], STAT_PERM));
    return MakePermStr(t, (perm >> 3) & 7);
}


/* Match a file name with a mask that may contain ? and * wildcards. Return
   TRUE if the mask matches the name. */
static ABool MatchName(const char *str, const char *mask)
{
    for (; *mask != '\0'; mask++) {
        if (*mask == '*') {
            const char *s = str;
            while (*s != '\0')
                s++;
            for (; s >= str; s--) {
                if (MatchName(s, mask + 1))
                    return TRUE;
            }
            return FALSE;
        } else if (*mask == '?') {
            if (*str == '\0')
                return FALSE;
            str++;
        } else {
#ifndef HAVE_CASE_INSENSITIVE_FILE_NAMES
            /* Case sensitive matching. */
            if (*mask != *str)
                return FALSE;
#else
            /* Case insensitive matching. */
            /* IDEA: What about case insensitive matching for character codes 
               over 127? */
            int cm = *mask;
            int cs = *str;
            if (cm < 128 && cs < 128) {
                /* Case insensitive matching for ASCII characters. */
                if (ALower(cm) != ALower(cs))
                    return FALSE;
            } else {
                if (cm != cs)
                    return FALSE;
            }
#endif
            str++;
        }
    }

    return *str == '\0';
}


/* os::ListDir(path[, pattern]) */
AValue AOsListDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN], mask[A_MAX_PATH_LEN];
    char *mask_p;
    DIR *d;
    AValue *tmp;
    AValue ret;
    struct dirent *entry;

    AGetStr(t, frame[0], path, sizeof(path));
    if (!AIsDefault(frame[1])) {
        AGetStr(t, frame[1], mask, sizeof(mask));
        mask_p = mask;
    } else
        mask_p = NULL;

    tmp = AAllocTemps(t, 2);

    BLOCKING(d = opendir(path));
    
    if (d == NULL) {
        AFreeTemps(t, 2);
        return ARaiseErrnoIoError(t, path);
    }

    if (ATry(t)) {
        BLOCKING(closedir(d));
        return AError;
    }
    
    tmp[1] = AZero; /* IDEA: Is this superfluous? */
    tmp[0] = AMakeArray(t, 0);

    for (;;) {
        const char *n;

        BLOCKING(entry = readdir(d));
        
        if (entry == NULL)
            break;
        
        n = entry->d_name;
        
        if (!(n[0] == '.' && (strcmp(n, ".") == 0 || strcmp(n, "..") == 0))) {
            if (mask_p == NULL
                || MatchName(n, mask_p)) {
                tmp[1] = AMakeStr(t, n);
                AAppendArray(t, tmp[0], tmp[1]);
            }
        }
    }

    AEndTry(t);

    BLOCKING(closedir(d));

    ret = tmp[0];
    AFreeTemps(t, 2);
    return ret;
}


static AValue WalkDirRecursive(AThread *t, AValue *data, const char *base,
                               char *rel, const char *mask)
{
    DIR *d;
    struct dirent *entry;
    struct stat buf;
    char path[A_MAX_PATH_LEN];

    strcpy(path, base);
    strcat(path, A_DIR_SEPARATOR_STRING);
    strcat(path, rel);

    BLOCKING(d = opendir(path));
    
    if (d == NULL)
        return ARaiseErrnoIoError(t, path);
    
    if (ATry(t)) {
        BLOCKING(closedir(d));
        return AError;
    }

    for (;;) {
        const char *n;
        int status;

        BLOCKING(entry = readdir(d));
        
        if (entry == NULL)
            break;
        
        n = entry->d_name;
        
        if (n[0] == '.' && (strcmp(n, ".") == 0 || strcmp(n, "..") == 0))
            continue;

        strcpy(path, base);
        strcat(path, A_DIR_SEPARATOR_STRING);
        strcat(path, rel);
        strcat(path, A_DIR_SEPARATOR_STRING);
        strcat(path, n);

        BLOCKING(status = stat(path, &buf));
        
        if (status == -1) {
            BLOCKING(closedir(d));
            return ARaiseErrnoIoError(t, path);
        }

        /* Concatenate n to rel and store the result in path. */
        strcpy(path, rel);
        if (*path != '\0')
            strcat(path, A_DIR_SEPARATOR_STRING);
        strcat(path, n);
        
        if (mask == NULL || MatchName(n, mask)) {
            data[1] = AMakeStr(t, path);
            AAppendArray(t, data[0], data[1]);
        }
        
        /* Descent into non-link directories. */
        if (S_ISDIR(buf.st_mode) && !S_ISLNK(buf.st_mode)) {
            if (AIsError(WalkDirRecursive(t, data, base, path, mask))) {
                BLOCKING(closedir(d));
                return AError;
            }
        }
    }

    AEndTry(t);

    BLOCKING(closedir(d));
    
    return data[0];
}


/* os::WalkDir(path[, pattern]) */
AValue AOsWalkDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN], mask[A_MAX_PATH_LEN];
    char *mask_p;
    AValue *tmp;
    AValue ret;

    AGetStr(t, frame[0], path, sizeof(path));
    if (!AIsDefault(frame[1])) {
        AGetStr(t, frame[1], mask, sizeof(mask));
        mask_p = mask;
    } else
        mask_p = NULL;

    tmp = AAllocTemps(t, 2);
    tmp[1] = AZero; /* IDEA: Is this superfluous? */
    tmp[0] = AMakeArray(t, 0);

    ret = WalkDirRecursive(t, tmp, path, "", mask_p);

    AFreeTemps(t, 2);
    return ret;
}


/* os::GetEnv(name[, default]) */
AValue AOsGetEnv(AThread *t, AValue *frame)
{
    /* IDEA: Use constant for buffer size. */
    char name[256];
    char *value;

    AGetStr(t, frame[0], name, sizeof(name));
    value = getenv(name);

    if (value != NULL)
        return AMakeStr(t, value);
    else if (!AIsDefault(frame[1]))
        return frame[1];
    else
        return ANil;
}


/* os::SetEnv(name, value) */
AValue AOsSetEnv(AThread *t, AValue *frame)
{
    /* IDEA: Use constants for buffer sizes or allocate them dynamically. */
    char name[256];
    AGetStr(t, frame[0], name, sizeof(name));
    if (!AIsNil(frame[1])) {
        char value[1024];
        AGetStr(t, frame[1], value, sizeof(value));
        if (setenv(name, value, TRUE) < 0)
            return ARaiseByNum(t, AErrorClassNum[EX_RESOURCE_ERROR], NULL);
        else
            return ANil;
    } else {
        unsetenv(name);
        return ANil;
    }
}


/* Parse a string that represents file permissions (it contains some of r/w/x
   or is empty), and convert it to a bitwise integer representation (lowest
   bit set => 'x', bit 2 set => 'w', bit 3 set => 'r'). */   
static int PermBits(AThread *t, const char *str)
{
    int bits = 0;
    for (; *str != '\0'; str++) {
        if (*str == 'r')
            bits |= 4;
        else if (*str == 'w')
            bits |= 2;
        else if (*str == 'x')
            bits |= 1;
        else
            ARaiseValueError(t, NULL);
    }
    return bits;
}


/* os::SetPerm(path, ownerPerm[, otherPerm[, groupPerm]]) */
AValue AOsSetPerm(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN], permStr[64];
    int userPerm, groupPerm, otherPerm;
    int mode;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    AGetStr(t, frame[1], permStr, sizeof(permStr));

    userPerm = PermBits(t, permStr);
    if (!AIsDefault(frame[2])) {
        AGetStr(t, frame[2], permStr, sizeof(permStr));
        otherPerm = PermBits(t, permStr);
    } else
        otherPerm = userPerm & ~2; /* Copy all but bits but the write bit. */
    if (!AIsDefault(frame[3])) {
        AGetStr(t, frame[3], permStr, sizeof(permStr));
        groupPerm = PermBits(t, permStr);
    } else
        groupPerm = otherPerm;
    
    mode = (userPerm << 6) | (groupPerm << 3) | otherPerm;

    BLOCKING(status = chmod(path, mode));
    
    if (status == -1)
        return ARaiseErrnoIoError(t, path);

    return ANil;
}


/* Helper function for setting a time attribute of a file. If isMod is TRUE,
   set modification time; otherwise set last access time. */
static AValue SetFileTime(AThread *t, AValue *frame, ABool isMod)
{
    char path[A_MAX_PATH_LEN];
    AValue v;
    struct tm tm;
    time_t tim;
    struct utimbuf utim;
    struct stat statbuf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));

    v = AMember(t, frame[1], "year");
    if (AIsError(v))
        return AError;
    tm.tm_year = AGetInt(t, v) - 1900;

    v = AMember(t, frame[1], "month");
    if (AIsError(v))
        return AError;
    tm.tm_mon = AGetInt(t, v) - 1;

    v = AMember(t, frame[1], "day");
    if (AIsError(v))
        return AError;
    tm.tm_mday = AGetInt(t, v);

    v = AMember(t, frame[1], "hour");
    if (AIsError(v))
        return AError;
    tm.tm_hour = AGetInt(t, v);

    v = AMember(t, frame[1], "minute");
    if (AIsError(v))
        return AError;
    tm.tm_min = AGetInt(t, v);

    v = AMember(t, frame[1], "second");
    if (AIsError(v))
        return AError;
    tm.tm_sec = AGetFloat(t, v);

    tm.tm_isdst = -1;

    tim = mktime(&tm);
    if (tim == (time_t)-1)
        return ARaiseValueError(t, NULL); /* IDEA: Message */

    /* Read the current access and modification times using stat. This needed
       since utime() changes both of them -- we must set the unchanged value
       to its current value. */
    BLOCKING(status = stat(path, &statbuf));
    if (status == -1)
        return ARaiseErrnoIoError(t, path);

    if (isMod) {
        utim.actime = statbuf.st_atime;
        utim.modtime = tim;
    } else {
        utim.actime = tim;
        utim.modtime = statbuf.st_mtime;
    }

    BLOCKING(status = utime(path, &utim));
    if (status == -1)
        return ARaiseErrnoIoError(t, path);

    return ANil;
}


/* os:::SetModificationTime(path, time) */
AValue ASetModificationTime(AThread *t, AValue *frame)
{
    return SetFileTime(t, frame, TRUE);
}


/* os:::SetAccessTime(path, time) */
AValue ASetAccessTime(AThread *t, AValue *frame)
{
    return SetFileTime(t, frame, FALSE);
}


/* For accessing environment directly */
extern char **environ;


/* os::ListEnv() */
AValue AListEnv(AThread *t, AValue *frame)
{
    /* Return a list of environment variables by parsing the environment. */
    int len;
    int i;

    /* Determine the number of env variables. */
    for (len = 0; environ[len] != NULL; len++);

    /* Construct an Array that can hold all the names. */
    frame[0] = AMakeArray(t, len);

    /* Parse the environment and store the variable names. */
    for (i = 0; i < len; i++) {
        char *s = environ[i];
        char var[1024];
        int j;
        AValue ss;

        j = 0;
        for (j = 0; j < 1023 && s[j] != '\0' && s[j] != '='; j++)
            var[j] = s[j];
        var[j] = '\0';

        ss = AMakeStr(t, var);
        ASetArrayItem(t, frame[0], i, ss);
    }

    return frame[0];
}


/* os::User() */
AValue AOsUser(AThread *t, AValue *frame)
{
    return UidToStr(t, getuid());
}


/* os::__getpwnam(user)
   Helper used by os::ExpandUser. Return the information related to an entry
   in the password database as a tuple (name, passwd, uid, gid, gecos, dir,
   shell). See the man page of getpwnam_r for details. */
AValue AOsGetpwnam(AThread *t, AValue *frame)
{
    char user[256];
    struct passwd pwd;
    struct passwd *result;
    char *buf;
    size_t bufsize;
    int s;

    AGetStr(t, frame[0], user, sizeof(user));
    
    bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1)
        bufsize = 16384;

    buf = malloc(bufsize);
    if (buf == NULL)
        return ARaiseMemoryError(t);

    s = getpwnam_r(user, &pwd, buf, bufsize, &result);
    if (result ==  NULL) {
        if (s == 0)
            return ARaiseKeyError(t, "%s: User not found", user);
        else
            return ARaiseErrnoIoError(t, NULL);
    }

    if (ATry(t)) {
        free(buf);
        return AError;
    }

    frame[1] = AMakeTuple(t, 7);
    
    frame[2] = AMakeStr(t, pwd.pw_name);
    AInitTupleItem(t, frame[1], 0, frame[2]);
    
    frame[2] = AMakeStr(t, pwd.pw_passwd);
    AInitTupleItem(t, frame[1], 1, frame[2]);
    
    frame[2] = AMakeInt(t, pwd.pw_uid);
    AInitTupleItem(t, frame[1], 2, frame[2]);
    
    frame[2] = AMakeInt(t, pwd.pw_gid);
    AInitTupleItem(t, frame[1], 3, frame[2]);
    
    frame[2] = AMakeStr(t, pwd.pw_gecos);
    AInitTupleItem(t, frame[1], 4, frame[2]);
    
    frame[2] = AMakeStr(t, pwd.pw_dir);
    AInitTupleItem(t, frame[1], 5, frame[2]);
    
    frame[2] = AMakeStr(t, pwd.pw_shell);
    AInitTupleItem(t, frame[1], 6, frame[2]);

    AEndTry(t);

    free(buf);

    return frame[1];
}


/* __GetFullPathName is only supported in Windows. This is an internal
   function. */
AValue AOsGetFullPathName(AThread *t, AValue *frame)
{
    return ARaiseRuntimeError(t, "Not supported");
}


#endif /* defined(A_HAVE_OS_MODULE) && !defined(A_HAVE_WINDOWS) */
