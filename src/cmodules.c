/* cmodules.c - List of C modules built into the interpreter

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "cmodules.h"


extern AModuleDef AthreadModuleDef[];
extern AModuleDef AloaderModuleDef[];
extern AModuleDef AreflectModuleDef[];
extern AModuleDef A__stringModuleDef[];
extern AModuleDef AmathModuleDef[];
extern AModuleDef AsysModuleDef[];
extern AModuleDef A__reModuleDef[];
extern AModuleDef ArandomModuleDef[];
extern AModuleDef A__timeModuleDef[];
extern AModuleDef AbitopModuleDef[];
extern AModuleDef AunicodeModuleDef[];
extern AModuleDef A__packModuleDef[];
extern AModuleDef AosModuleDef[];
extern AModuleDef AsetModuleDef[];
extern AModuleDef A__asmModuleDef[];


/* NOTE: std, io and encodings modules are omitted. They have special handling
         in compile.c */
AModuleDef *AAllModules[] = {
    AthreadModuleDef,
    AloaderModuleDef,
    AreflectModuleDef,
    A__stringModuleDef,
    A__reModuleDef,
    AmathModuleDef,
    AsysModuleDef,
    ArandomModuleDef,
    AbitopModuleDef,
    AsetModuleDef,
    A__timeModuleDef,
    A__packModuleDef,
#ifdef A_HAVE_OS_MODULE
    AosModuleDef,
#endif
#ifdef HAVE_JIT_COMPILER
    A__asmModuleDef,
#endif
    NULL
};
