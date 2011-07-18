/* display.c - Function for displaying compiler and vm error messages

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "aconfig.h"
#include "error.h"
#include <stdio.h>


ABool ADefDisplay(const char *msg, void *data)
{
    fprintf(stderr, "%s\n", msg);
    return TRUE;
}
