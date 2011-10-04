/* common.c - Miscellaneous useful definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "common.h"
#include "runtime.h"
#include "gc.h"
#include "internal.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


char *ADupStr(const char *src)
{
    unsigned long len;
    char *new;

    len = strlen(src) + 1;
    new = AAllocStatic(len);

    if (new != NULL)
        ACopyMem(new, src, len);

    return new;
}


ABool AJoinPath(char *dst, const char *path, const char *add)
{
    int len = strlen(path);
    if (len + strlen(add) + 1 >= A_MAX_PATH_LEN)
        return FALSE;

    if (dst != path)
        strcpy(dst, path);
    if (len > 0 && !A_IS_DIR_SEPARATOR(dst[len - 1]))
        strcat(dst, A_DIR_SEPARATOR_STRING);
    strcat(dst, add);
    return TRUE;
}


ABool AIsFile(const char *path)
{
    struct stat buf;
    return stat(path, &buf) >= 0 && S_ISREG(buf.st_mode);
}
