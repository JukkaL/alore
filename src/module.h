/* module.h - Managing modules

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef CMODULE2_H_INCL
#define CMODULE2_H_INCL

#include "value.h"


typedef struct {
    unsigned short type;
    unsigned short num1;
    const char *str;
    unsigned short num2;
    unsigned short num3;
    ACFunction func;
    int *numPtr;
} AModuleDef;


enum {
    MD_MODULE_NAME,
    MD_IMPORT,
    MD_END_MODULE,
    MD_VAR,
    MD_CONST,
    MD_EMPTY_CONST,
    MD_DEF,
    MD_CLASS,
    MD_INTERFACE,
    MD_END_TYPE,
    MD_INHERIT,
    MD_IMPLEMENT,
    MD_METHOD,
    MD_GETTER,
    MD_SETTER,
    MD_BINARY_DATA,
    MD_EXTERNAL_DATA
};

#ifdef __cplusplus
#define A_EXTERN_C_MOD(def) extern "C" AModuleDef def[];
#else
#define A_EXTERN_C_MOD(def)
#endif


/* NOTE: name must be no longer than MAX_MODULE_NAME_LENGTH characters. */
#define A_MODULE(name, str) \
    A_EXTERN_C_MOD(A##name##ModuleDef) \
    A_EXPORT AModuleDef A##name##ModuleDef[] = \
        { { MD_MODULE_NAME, 0, str, 0, 0, NULL, NULL },

#define A_END_MODULE() \
    { MD_END_MODULE, 0, NULL, 0, 0, NULL, NULL } };

#define A_IMPORT(module) \
    { MD_IMPORT, 0, module, 0, 0, NULL, NULL },

#define A_CLASS_PRIV_P(name, numPrivate, ptr) \
    { MD_CLASS, numPrivate, name, 0, 0, NULL, ptr },
#define A_CLASS_PRIV(name, numPrivate) \
    A_CLASS_PRIV_P(name, numPrivate, NULL)
#define A_CLASS_P(name, ptr) \
    A_CLASS_PRIV_P(name, 0, ptr)
#define A_CLASS(name) \
    A_CLASS_PRIV(name, 0)

#define A_INHERIT(name) \
    { MD_INHERIT, 0, name, 0, 0, NULL, NULL },

#define A_IMPLEMENT(name) \
    { MD_IMPLEMENT, 0, name, 0, 0, NULL, NULL },

#define A_BINARY_DATA_P(size, ptr) \
    { MD_BINARY_DATA, size, "", 0, 0, NULL, ptr },
#define A_BINARY_DATA(size) \
    A_BINARY_DATA_P(size, NULL)

#define A_EXTERNAL_DATA() \
    { MD_EXTERNAL_DATA, 0, "", 0, 0, NULL, NULL },

#define A_END_CLASS() \
    { MD_END_TYPE, 0, NULL, 0, 0, NULL, NULL },

#define A_INTERFACE(name) \
    { MD_INTERFACE, 0, name, 0, 0, NULL, NULL },

#define A_END_INTERFACE() A_END_CLASS()

#define A_DEF_OPT_P(name, minArgs, maxArgs, numLocals, cfunc, ptr) \
    { MD_DEF, minArgs, name, maxArgs, numLocals + maxArgs, cfunc, ptr },
#define A_DEF_OPT(name, minArgs, maxArgs, numLocals, cfunc) \
    A_DEF_OPT_P(name, minArgs, maxArgs, numLocals, cfunc, NULL)
#define A_DEF(name, numArgs, numLocals, cfunc) \
    A_DEF_OPT(name, numArgs, numArgs, numLocals, cfunc)
#define A_DEF_P(name, numArgs, numLocals, cfunc, ptr) \
    A_DEF_OPT_P(name, numArgs, numArgs, numLocals, cfunc, ptr)
#define A_DEF_VARARG(name, minArgs, maxArgs, numLocals, cfunc) \
    { MD_DEF, minArgs, name, (maxArgs + 1) | A_VAR_ARG_FLAG, \
       numLocals + maxArgs + 1, cfunc, NULL },
#define A_SUB_VARARG_P(name, minArgs, maxArgs, numLocals, cfunc, ptr) \
    { MD_DEF, minArgs, name, (maxArgs + 1) | A_VAR_ARG_FLAG, \
       numLocals + maxArgs + 1, cfunc, ptr },

#define A_METHOD_OPT(name, minArgs, maxArgs, numLocals, cfunc) \
    { MD_METHOD, minArgs + 1, name, maxArgs + 1, numLocals + maxArgs + 1, \
      cfunc, NULL },
#define A_METHOD(name, numArgs, numLocals, cfunc) \
    A_METHOD_OPT(name, numArgs, numArgs, numLocals, cfunc)
#define A_METHOD_VARARG(name, minArgs, maxArgs, numLocals, cfunc) \
    { MD_METHOD, minArgs + 1, name, (maxArgs + 2) | A_VAR_ARG_FLAG, \
       numLocals + maxArgs + 2, cfunc, NULL },

#define A_PRIVATE(name) ("-" name)

#define A_VAR(name) \
    { MD_VAR, 0, name, 0, 0, NULL, NULL },
#define A_VAR_P(name, ptr) \
    { MD_VAR, 0, name, 0, 0, NULL, ptr },
#define A_SYMBOLIC_CONST(name) \
    { MD_CONST, 0, name, 0, 0, NULL, NULL },
#define A_SYMBOLIC_CONST_P(name, ptr) \
    { MD_CONST, 0, name, 0, 0, NULL, ptr },
#define A_EMPTY_CONST(name) \
    { MD_EMPTY_CONST, 0, name, 0, 0, NULL, NULL },
#define A_EMPTY_CONST_P(name, ptr) \
    { MD_EMPTY_CONST, 0, name, 0, 0, NULL, ptr },

#define A_GETTER(name, numLocals, cfunc) \
    { MD_GETTER, 1, name, 1, numLocals + 1, cfunc, NULL },

#define A_SETTER(name, numLocals, cfunc) \
    { MD_SETTER, 2, name, 2, numLocals + 2, cfunc, NULL },


ABool AInitializeCModules(void);
ABool ACreateModule(AModuleDef *module, ABool existsAlready);
ABool AInitializeModule(char *name, ABool isAutoImport);
ABool ACreateAndInitializeModule(char *name, AModuleDef *module);
struct AToken_ *AImportModule(struct AToken_ *tok, ABool isFromC);

AValue AEmptyCreate(struct AThread_ *t, AValue *args);
AValue AEmptyMain(struct AThread_ *t, AValue *args);


A_APIFUNC ABool ALoadCModule(const char *path, AModuleDef **def);


#endif
