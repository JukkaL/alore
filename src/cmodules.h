/* cmodules.h - C module related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef CMODULES_H_INCL
#define CMODULES_H_INCL

#include "module.h"


extern AModuleDef AstdModuleDef[];
extern AModuleDef AioModuleDef[];
extern AModuleDef AencodingsModuleDef[];
extern AModuleDef AerrnoModuleDef[];
extern AModuleDef *AAllModules[];


ABool AInitializeStdTypes(void);
ABool AInitializeStdExceptions(struct AThread_ *t);


#endif
