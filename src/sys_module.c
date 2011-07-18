/* sys_module.c - sys module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "compile.h"

#include <string.h>


static int ArgsNum;
static int ProgramPathNum;
static int InterpreterPathNum;
static int PlatformNum;
static int PlatformVariantNum;
static int IsStandaloneNum;


/* Precondition: option must be #defined */
#define CHECK_OPT(str, option) \
    do { \
        if (strcmp(str, #option) == 0) return ATrue; \
    } while (0)


static AValue SysMain(AThread *t, AValue *frame)
{
    ASetConstGlobalByNum(t, ArgsNum, AGlobalByNum(GL_COMMAND_LINE_ARGS));
    ASetConstGlobalByNum(t, ProgramPathNum, AMakeStr(t, AProgramPath));
    if (AInterpreterPath != NULL)
        ASetConstGlobalByNum(t, InterpreterPathNum,
                             AMakeStr(t, AInterpreterPath));
    ASetConstGlobalByNum(t, PlatformNum, AMakeStr(t, ALORE_PLATFORM));
    ASetConstGlobalByNum(t, PlatformVariantNum,
                         AMakeStr(t, ALORE_PLATFORM_VARIANT));
    ASetConstGlobalByNum(t, IsStandaloneNum, AIsStandalone() ? ATrue : AFalse);
    return ANil;
}


static AValue Sys__ConfigOpt(AThread *t, AValue *frame)
{
    char opt[128];

    AGetStr(t, frame[0], opt, sizeof(opt));

    /* Return True if opt is the name of a recognized config.h #define that
       is defined. Otherwise, return False. */
#ifdef HAVE_LIBDL
    CHECK_OPT(opt, HAVE_LIBDL);
#endif
#ifdef HAVE_LIBSOCKET
    CHECK_OPT(opt, HAVE_LIBSOCKET);
#endif
#ifdef HAVE_LIBNSL
    CHECK_OPT(opt, HAVE_LIBNSL);
#endif

    return AFalse;
}


A_MODULE(sys, "sys")
    A_DEF(A_PRIVATE("Main"), 0, 0, SysMain)
    A_EMPTY_CONST_P("Args", &ArgsNum)
    A_EMPTY_CONST_P("ProgramPath", &ProgramPathNum)
    A_EMPTY_CONST_P("InterpreterPath", &InterpreterPathNum)
    A_EMPTY_CONST_P("Platform", &PlatformNum)
    A_EMPTY_CONST_P("__PlatformVariant", &PlatformVariantNum)
    A_EMPTY_CONST_P("IsStandalone", &IsStandaloneNum)
    A_DEF("__ConfigOpt", 1, 0, Sys__ConfigOpt)
A_END_MODULE()
