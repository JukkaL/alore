/* version.c - Alore version information

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "version.h"
#include "build.h"


#ifdef A_VERSION

char AVersion[] = A_VERSION;

#elif defined(A_SNAPSHOT)

char AVersion[] = "development snapshot " A_SNAPSHOT " (build " A_BUILD ")";

#else

char AVersion[] = "development version (build " A_BUILD ")";

#endif
