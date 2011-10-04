/* dynload.c - Dynamic C module loading

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "compile.h"


#ifdef A_HAVE_WINDOWS

#include <windows.h>

typedef HANDLE LibRef;
#define DLOPEN(path) LoadLibrary(path)
#define DLCLOSE(handle) FreeLibrary(handle)
#define DLSYM(handle, name) GetProcAddress(handle, name)

#else

#include <dlfcn.h>

typedef void *LibRef;
#define DLOPEN(path) dlopen(path, RTLD_LAZY)
#define DLCLOSE(handle) dlclose(handle)
#define DLSYM(handle, name) dlsym(handle, name)

#endif


ABool ALoadCModule(const char *path, AModuleDef **def)
{
    LibRef m;
    char name[PATH_MAX + 30];
    int i;

    m = DLOPEN(path);
    if (!m) {
        /* IDEA: Use dlerror() to somehow report an error message.
                 Uncomment the following line for debugging support. */
        /* fputs(dlerror(), stderr); */
        return FALSE;
    }

    for (i = strlen(path) - 1; i >= 0 && path[i] != A_DIR_SEPARATOR; i--);

    if (i < 0) {
        DLCLOSE(m);
        return FALSE;
    }

    strcpy(name, "A");
    strcpy(name + 1, path + i + 1);
    name[strlen(name) - strlen(DYNAMIC_C_MODULE_EXTENSION)] = '\0';
    strcat(name, "ModuleDef");

    *def = (AModuleDef *)DLSYM(m, name);
    if (*def == NULL) {
        DLCLOSE(m);
        return FALSE;
    }

    return TRUE;
}
