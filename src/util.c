/* util.c - Miscellaneous helper functions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "util.h"
#include "str.h"

#include <ctype.h>


int AEndsWith(const char *str, const char *suffix)
{
    int sl = strlen(str);
    int pl = strlen(suffix);

    return sl >= pl && memcmp(str + sl - pl, suffix, pl) == 0;
}


int AStartsWith(const char *str, const char *prefix)
{
    int sl = strlen(str);
    int pl = strlen(prefix);

    return sl >= pl && memcmp(str, prefix, pl) == 0;
}


#ifdef A_HAVE_WINDOWS

int APathEndsWith(const char *str, const char *suffix)
{
    int sl = strlen(str);
    int pl = strlen(suffix);
    int i;

    if (sl < pl)
        return 0;

    for (i = 1; i <= pl; i++) {
        char c1 = str[sl - i];
        char c2 = suffix[pl - i];
        if (tolower(c1) != tolower(c2) &&
            (c2 != '/' || !A_IS_DIR_SEPARATOR(c1)))
            return 0;
    }

    return 1;
}

#endif
