/* files.h - Maintain a collection of paths of compiled Alore source files

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef FILES_H_INCL
#define FILES_H_INCL


#include "common.h"


struct ASymbolInfo_;
struct AModuleId_;


typedef struct AFileListBlock_ {
    struct AFileListBlock_ *next;
    struct ASymbolInfo_ *module;
    int firstFileIndex;
    int numFiles;
    char str[1];
} AFileListBlock;


ABool AInitializeFileList(void);
void ABeginNewModule(struct AModuleId_ *mod);
unsigned AAddNewFile(char *path);
ABool AEndModule(void);
void ARemoveFiles(struct ASymbolInfo_ *module);

void AGetFilePath(char *buf, int bufLen, int fileNum);


int ACreateModulePath(char *path, int max, struct AModuleId_ *mod);


#endif
