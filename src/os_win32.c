/* os_win32.c - Windows specific implementation details of the os module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"
#include "str_internal.h" /* for ALower */

#if defined(A_HAVE_OS_MODULE) && defined(A_HAVE_WINDOWS)

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <windows.h>
#include "alore.h"
#include "os_module.h"
#include "io_module.h"
#include "str.h"
#include "array.h"
#include "internal.h"
#include "runtime.h"


/* Maximum supported length for the value of an environment variable */
#define MAX_ENV_VALUE_LEN 4096


static AValue MakeDateTime(AThread *t, AValue *frame, time_t t1);


/* If a function is not supported in Windows, raise an exception with this
   message. */
static const char NotSupported[] = "Not supported in Windows";


AValue AOsRemove(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;

    AGetStr(t, frame[0], path, sizeof(path));

    AAllowBlocking();
    status = remove(path);
    AEndBlocking();
    
    if (status < 0) {
        if (errno == EACCES || errno == ENOENT) {
            AAllowBlocking();
            status = RemoveDirectory(path);
            AEndBlocking();
            
            if (!status) {
                errno = EACCES;
                return ARaiseErrnoIoError(t, path);
            } else
                return ANil;
        } else
            return ARaiseErrnoIoError(t, path);
    } else
        return ANil;
}


/* os::Rename(source, target) */
AValue AOsRename(AThread *t, AValue *frame)
{
    char source[A_MAX_PATH_LEN], target[A_MAX_PATH_LEN];
    int status;
    
    /* IDEA: Maybe return a special error if cannot rename because destination
       on a different file system. */

    /* IDEA: Maybe create the directories beneath the destination directory
       as needed. */

    AGetStr(t, frame[0], source, sizeof(source));
    AGetStr(t, frame[1], target, sizeof(target));

    AAllowBlocking();
    status = rename(source, target);
    AEndBlocking();
    
    if (status < 0)
        return ARaiseErrnoIoError(t, NULL);
    else
        return ANil;
}


/* os::MakeDir(path) */
AValue AOsMakeDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    
    /* IDEA: Create the directories beneath the destination directory as
       needed. */
    AAllowBlocking();
    status = mkdir(path);
    AEndBlocking();
    
    if (status < 0)
        return ARaiseErrnoIoError(t, path);
    else
        return ANil;
}


/* os::ChangeDir(path) */
AValue AOsChangeDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    
    AAllowBlocking();
    status = chdir(path);
    AEndBlocking();
    
    if (status < 0)
        return ARaiseErrnoIoError(t, path);
    else
        return ANil;
}


/* os::CurrentDir() */
AValue AOsCurrentDir(AThread *t, AValue *frame)
{
    char buf[A_MAX_PATH_LEN];

    if (getcwd(buf, A_MAX_PATH_LEN) == NULL)
        return ARaiseErrnoIoError(t, NULL);
    else
        return AMakeStr(t, buf);
}


/* os::MakeSymLink(target, path)
   Not supported in Windows. */
AValue AOsMakeSymLink(AThread *t, AValue *frame)
{
    return ARaiseRuntimeError(t, NotSupported);
}


/* os::ReadSymLink(path)
   Not supported in Windows. */
AValue AOsReadSymLink(AThread *t, AValue *frame)
{
    return ARaiseRuntimeError(t, NotSupported);
}


/* os::Sleep(timeInSeconds) */
AValue AOsSleep(AThread *t, AValue *frame)
{
    double seconds = AGetFloat(t, frame[0]);
    int ms = (int)(seconds * 1000 + 0.5);
    if (ms == 0 && seconds > 0.0)
        ms = 1;
    AAllowBlocking();
    Sleep(ms);
    AEndBlocking();
    return ANil;
}


/* os::System(cmd) */
AValue AOsSystem(AThread *t, AValue *frame)
{
    /* IDEA: Can we analyze the return code and raise an exception or
       something -- but perhaps we'd better not. */
    char cmd[1024]; /* IDEA: Use a constant for the size */
    int error;

    AGetStr(t, frame[0], cmd, sizeof(cmd));
    
    AAllowBlocking();
    error = system(cmd);
    AEndBlocking();
    
    return AIntToValue(error);
}


/* Wrapper around stat() that behaves closer to Posix stat(). */
static int MyStat(const char *path, struct stat *buf)
{
	char path2[A_MAX_PATH_LEN];
	int status;	
	int fixed;
	int len = strlen(path);
	ABool trailing = FALSE;
	
	if (len >= A_MAX_PATH_LEN)
		return -1;
	
	strcpy(path2, path);
	
	/* Strip trailing dir separators, as otherwise the stat call may
           fail. */
	fixed =  A_IS_DRIVE_PATH(path2) ? 3 : 1;
	while (len > fixed && A_IS_DIR_SEPARATOR(path2[len - 1])) {
		len--;
		trailing = TRUE;
	}
	path2[len] = '\0';
	
    AAllowBlocking();
    status = stat(path2, buf);
    AEndBlocking();
    
    /* If there was a trailing separator but the name does not refer to a
       directory, we must return an error. */
    if (status >= 0 && trailing && !S_ISDIR(buf->st_mode)) {
	    errno = ENOTDIR;
	    return -1;
    }
    
    return status;
}


/* os::Exists(path) */
AValue AOsExists(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    status = MyStat(path, &buf);

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }
    
    return ATrue;
}


/* os::IsFile(path) */
AValue AOsIsFile(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    status = MyStat(path, &buf);

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }
    
    return S_ISREG(buf.st_mode) ? ATrue : AFalse;
}


/* os::IsDir(path) */
AValue AOsIsDir(AThread *t, AValue *frame)
{
    char path[A_MAX_PATH_LEN];
    struct stat buf;
    int status;

    AGetStr(t, frame[0], path, sizeof(path));
    status = MyStat(path, &buf);

    if (status < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return AFalse;
        return ARaiseErrnoIoError(t, path);
    }

    return S_ISDIR(buf.st_mode) ? ATrue : AFalse;
}


/* os::IsLink(path) */
AValue AOsIsLink(AThread *t, AValue *frame)
{
    /* IDEA: Detect shortcuts? Probably not, since they behave differently
             from symbolic links. */
    return AFalse;
}


/* Stat slot ids */
enum {
    STAT_PATH,
    STAT_FILLER1, /* Posix-only */
    STAT_FILLER2, /* Posix-only */
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

    status = MyStat(path, &buf);
    if (status < 0)
        return ARaiseErrnoIoError(t, path);

    /* IDEA: Detect shortcuts? */
    isLink = FALSE;

    ASetMemberDirect(t, frame[0], STAT_PATH, frame[1]);
    /* We must support file sizes larger than 4GB. */
    frame[2] = AMakeInt64(t, buf.st_size);
    ASetMemberDirect(t, frame[0], STAT_SIZE, frame[2]);
    ASetMemberDirect(t, frame[0], STAT_IS_FILE, S_ISREG(buf.st_mode)
                    ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_DIR, S_ISDIR(buf.st_mode)
                    ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_LINK, isLink ? ATrue : AFalse);
    ASetMemberDirect(t, frame[0], STAT_IS_SPECIAL, S_ISCHR(buf.st_mode) ||
                    S_ISBLK(buf.st_mode) || S_ISFIFO(buf.st_mode)
                    ? ATrue : AFalse);

    *(time_t *)ADataPtr(frame[0], 0) = buf.st_mtime;
    *(time_t *)ADataPtr(frame[0], sizeof(time_t)) = buf.st_atime;
    
    return frame[0];
}


/* Stat modificationTime (getter) */
AValue AStatModificationTime(AThread *t, AValue *frame)
{
    time_t t1 = *(time_t *)ADataPtr(frame[0], 0);
    return MakeDateTime(t, frame, t1);
}


/* Stat accessTime (getter) */
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

    t2 = *localtime(&t1);
    
    sprintf(buf, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
            t2.tm_year + 1900,
            t2.tm_mon + 1,
            t2.tm_mday,
            t2.tm_hour,
            t2.tm_min,
            AMin(59, t2.tm_sec));

    return AMakeStr(t, buf);
}


/* Stat isReadable (getter) */
AValue AStatIsReadable(AThread *t, AValue *frame)
{
    AValue pathVal = AMemberDirect(frame[0], 0);
    char path[A_MAX_PATH_LEN];
    int status;
    
    AGetStr(t, pathVal, path, A_MAX_PATH_LEN);

    AAllowBlocking();
    status = access(path, R_OK);
    AEndBlocking();
        
    return status == 0 ? ATrue : AFalse;
}


/* Stat isWritable (getter) */
AValue AStatIsWritable(AThread *t, AValue *frame)
{
    AValue pathVal = AMemberDirect(frame[0], 0);
    char path[A_MAX_PATH_LEN];
    int status;
    
    AGetStr(t, pathVal, path, A_MAX_PATH_LEN);

    AAllowBlocking();
    status = access(path, W_OK);
    AEndBlocking();
    
    return status == 0 ? ATrue : AFalse;
}


/* Stat owner (getter) */
AValue AStatOwner(AThread *t, AValue *frame)
{
    /* IDEA: Implement */
    return AMakeStr(t, "");
}


/* Stat ownerPerm (getter) */
AValue AStatOwnerPerm(AThread *t, AValue *frame)
{
    /* IDEA: Implement */
    return ANil;
}

    
/* Stat otherPerm (getter) */
AValue AStatOtherPerm(AThread *t, AValue *frame)
{
    /* IDEA: Implement */
    return ANil;
}

    
/* Stat groupPerm (getter)
   Always return nil; this does not really make sense in Windows. */
AValue AStatGroupPerm(AThread *t, AValue *frame)
{
    /* IDEA: Raise an exception instead? */
    return ANil;
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

    AAllowBlocking();
    d = opendir(path);
    AEndBlocking();
    
    if (d == NULL) {
        AFreeTemps(t, 2);
        return ARaiseErrnoIoError(t, path);
    }
    
    if (ATry(t)) {
        AAllowBlocking();
        closedir(d);
        AEndBlocking();
        return AError;
    }
    
    tmp[1] = AZero; /* IDEA: Is this superfluous? */
    tmp[0] = AMakeArray(t, 0);

    for (;;) {
        const char *n;

        AAllowBlocking();
        entry = readdir(d);
        AEndBlocking();
        
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

    AAllowBlocking();
    closedir(d);
    AEndBlocking();

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

    /* IDEA: Use logical try/finally to avoid leaks.. */

    strcpy(path, base);
    strcat(path, A_DIR_SEPARATOR_STRING);
    strcat(path, rel);

    AAllowBlocking();
    d = opendir(path);
    AEndBlocking();
    
    if (d == NULL)
        return ARaiseErrnoIoError(t, path);
    
    if (ATry(t)) {
        AAllowBlocking();
        closedir(d);
        AEndBlocking();
        return AError;
    }

    for (;;) {
        const char *n;

        AAllowBlocking();
        entry = readdir(d);
        AEndBlocking();

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

        if (stat(path, &buf) == -1) {
            closedir(d);
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
        
        /* Descent into subdirectories. */
        if (S_ISDIR(buf.st_mode)) {
            if (AIsError(WalkDirRecursive(t, data, base, path, mask))) {
                AAllowBlocking();
                closedir(d);
                AEndBlocking();
                return AError;
            }
        }
    }

    AEndTry(t);

    AAllowBlocking();
    closedir(d);
    AEndBlocking();

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
    char name[256];
    char value[MAX_ENV_VALUE_LEN];
    DWORD ret;

    AGetStr(t, frame[0], name, sizeof(name));

    ret = GetEnvironmentVariable(name, value, sizeof(value));
    if (ret > sizeof(value))
        return ARaiseByNum(t, AErrorClassNum[EX_RESOURCE_ERROR], NULL);
    else if (ret == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            if (!AIsDefault(frame[1]))
                return frame[1];
            else
                return ANil;
        } else {
            /* Assume that the variable is empty. This might rely on 
               undocumented behaviour. */
            strcpy(value, "");
        }
    }
    
    return AMakeStr(t, value);
}


/* os::SetEnv(name, value) */
AValue AOsSetEnv(AThread *t, AValue *frame)
{
    char name[256];

    AGetStr(t, frame[0], name, sizeof(name));
    
    if (!AIsNil(frame[1])) {
        char value[MAX_ENV_VALUE_LEN];
        AGetStr(t, frame[1], value, sizeof(value));
        if (!SetEnvironmentVariable(name, value))
            return ARaiseByNum(t, AErrorClassNum[EX_RESOURCE_ERROR], NULL);
        else
            return ANil;
    } else {
        if (!SetEnvironmentVariable(name, NULL)) {
            /* It is not an error to unset a nonexistent environment
               variable. */
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
                return ANil;
            return ARaiseByNum(t, AErrorClassNum[EX_RESOURCE_ERROR], NULL);
        } else
            return ANil;
    }
}


/* os::SetPerm(path, ownerPerm[, otherPerm[, groupPerm]]) */
AValue AOsSetPerm(AThread *t, AValue *frame)
{
    /* IDEA: Implement */
    return ANil;
}


/* Helper function for setting a time attribute of a file. If isAccess is TRUE,
   set last access time; otherwise set modification time. */
static AValue ASetFileTime(AThread *t, AValue *frame, ABool isAccess)
{
    char path[A_MAX_PATH_LEN];
    AValue v;
    double s;
    SYSTEMTIME sysTime;
    FILETIME localFileTime;
    FILETIME fileTime;
    HANDLE handle;
    ABool status;

    AGetStr(t, frame[0], path, sizeof(path));

    v = AMember(t, frame[1], "year");
    if (AIsError(v))
        return AError;
    sysTime.wYear = AGetInt(t, v);

    v = AMember(t, frame[1], "month");
    if (AIsError(v))
        return AError;
    sysTime.wMonth = AGetInt(t, v);

    v = AMember(t, frame[1], "day");
    if (AIsError(v))
        return AError;
    sysTime.wDay = AGetInt(t, v);
    sysTime.wDayOfWeek = 0;

    v = AMember(t, frame[1], "hour");
    if (AIsError(v))
        return AError;
    sysTime.wHour = AGetInt(t, v);

    v = AMember(t, frame[1], "minute");
    if (AIsError(v))
        return AError;
    sysTime.wMinute = AGetInt(t, v);

    v = AMember(t, frame[1], "second");
    if (AIsError(v))
        return AError;
    s = AGetFloat(t, v);
    sysTime.wSecond = s;
    sysTime.wMilliseconds = (s - (int)s) * 1000;
    
    if (!SystemTimeToFileTime(&sysTime, &localFileTime))
        return ARaiseValueError(t, NULL);

    if (!LocalFileTimeToFileTime(&localFileTime, &fileTime))
        return ARaiseWin32Exception(t, AStdIoErrorNum);

    AAllowBlocking();
    handle = CreateFile(path, FILE_WRITE_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    AEndBlocking();
    
    if (handle == INVALID_HANDLE_VALUE)
        return ARaiseWin32Exception(t, AStdIoErrorNum);

    AAllowBlocking();
    if (isAccess)
        status = SetFileTime(handle, NULL, &fileTime, NULL);
    else
        status = SetFileTime(handle, NULL, NULL, &fileTime);
    AEndBlocking();

    if (!status)
        return ARaiseWin32Exception(t, AStdIoErrorNum);

    CloseHandle(handle);

    return ANil;
}


/* os::SetModificationTime(path, time) */
AValue ASetModificationTime(AThread *t, AValue *frame)
{
    return ASetFileTime(t, frame, FALSE);
}


/* os::SetAccessTime(path, time) */
AValue ASetAccessTime(AThread *t, AValue *frame)
{
    return ASetFileTime(t, frame, TRUE);
}


/* os::ListEnv() */
AValue AListEnv(AThread *t, AValue *frame)
{
    char *env = GetEnvironmentStrings();
    char *start = env;

    int i, j, k;
    int len;

    /* env points to a concatenated list of environment strings. Each of these
       is of the form "name=value". They are separated by 0 bytes and ended
       with two 0 bytes. */

    /* Count the number of environment variables. */
    len = 0;
    for (i = 0; env[i] != '\0' || env[i + 1] != '\0'; i++) {
        /* Only count environment variables whose name is not empty. */
        if ((i == 0 || env[i - 1] == '\0') && env[i] != '=' && env[i] != '\0')
            len++;
    }

    /* Create an array for storing the names of environment variables. */
    frame[0] = AMakeArray(t, len);

    /* Parse the environment and store variable names in the array. */
    j = 0;
    i = 0;
    while (i < len) {
        char *s = env + j;
        char var[1024];
        AValue ss;

        for (k = 0; k < 1023 && s[k] != '\0' && s[k] != '='; k++)
            var[k] = s[k];
        var[k] = '\0';

        if (k > 0) {
            ss = AMakeStr(t, var);
            ASetArrayItem(t, frame[0], i, ss);
            i++;
        }

        j += k;
        while (env[j] != '\0')
            j++;
        j++;
    }

    FreeEnvironmentStrings(start);

    return frame[0];
}


/* os::User() */
AValue AOsUser(AThread *t, AValue *frame)
{
    /* See also: NetWkstaGetInfo() (NT) */

    char buf[100];
    DWORD len;

    len = sizeof(buf);
    if (WNetGetUser(NULL, buf, &len) != NO_ERROR)
        return ARaiseIoError(t, NULL);
    
    return AMakeStr(t, buf);
}


/* os::__getpwnam(user)
   __getpwnam is an internal Posix function. Provide a dummy in Windows that 
   simply raises an exception. */
AValue AOsGetpwnam(AThread *t, AValue *frame)
{
    return ARaiseRuntimeError(t, NotSupported);
}


/* Get the absolute full version of a path name using Windows API. os::AbsPath 
   needs this to work in Windows. This is an internal function. */
AValue AOsGetFullPathName(AThread *t, AValue *frame)
{
	char file[A_MAX_PATH_LEN];
	char out[A_MAX_PATH_LEN];
	char *outp;
	DWORD status;
	
	AGetStr(t, frame[0], file, sizeof(file));
	
	status = GetFullPathName(file, sizeof(out), out, &outp);
	if (status < 0 || status >= sizeof(out))
		return ARaiseWin32Exception(t, AStdIoErrorNum);
		
	return AMakeStr(t, out);
}


#endif
