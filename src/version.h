/* version.h - Alore version information

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef VERSION_H_INCL
#define VERSION_H_INCL

#include "aconfig.h"


/* Define Alore version for non-development versions here. */
/* #define A_VERSION "x.y" */
/* #define A_SNAPSHOT "2007-05-05" */


/* Alore version. Official releases are of the form "x.y" or "x.y.z",
   optionally followed by (alpha|beta)[1-9] to denote an alpha or beta version.

   Development versions are of the form
     "development version (build x.y.z-dev-XXX-YYYYYYYY)"
   (where XXX is a number and YYY... is a git commit hash prefix).

   Development snapshots are of the form "development snapshot YYYY-MM-DD
   (build XXX)". */
A_APIVAR extern char AVersion[];


#endif
