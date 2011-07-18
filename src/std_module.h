/* std_module.h - std module

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef STD_MODULE_H_INCL
#define STD_MODULE_H_INCL

#include "thread.h"


AValue AStdRepr(AThread *t, AValue *frame);
AValue AStdChr(AThread *t, AValue *frame);
AValue AStdExceptionCreate(AThread *t, AValue *frame);
AValue AStdHash(AThread *t, AValue *frame);
AValue AStdSort(AThread *t, AValue *frame);


#define A_NUM_EXCEPTION_MEMBER_VARS 2
enum {
    AError_TRACEBACK,
    AError_MESSAGE
};


extern int AStdObjectNum;
extern int AStdStrNum;
extern int AStdReprNum;
extern int AStdIntNum;
extern int AStdTypeNum;
extern int AStdFloatNum;
extern int AStdRangeNum;
extern int AStdFunctionNum;
extern int AStdPairNum;
extern int AStdConstantNum;
extern int AStdSymbolNum;
extern int AStdHashNum;

extern int AStdBufNum;
extern int ARangeIterNum;


extern ATypeInfo *AStrClass;
extern ATypeInfo *AIntClass;
extern ATypeInfo *AFloatClass;
extern ATypeInfo *ABooleanClass;
extern ATypeInfo *ARangeClass;
extern ATypeInfo *APairClass;
extern ATypeInfo *AConstantClass;
extern ATypeInfo *ASymbolClass;
extern ATypeInfo *AFunctionClass;
extern ATypeInfo *ATypeClass;


ABool AInitializeHashValueMapping(void);
AValue AGetIdHashValue(AThread *t, AValue *v);
void AMoveNewGenHashMappingsToOldGen(void);
void ASweepOldGenHashMappings(void);

void AFixInfAndNanStrings(char *buf);


/* Declarations for std::Map */

AValue AMapCreate(AThread *t, AValue *frame);
AValue AMapInitialize(AThread *t, AValue *frame);
AValue AMap_get(AThread *t, AValue *frame);
AValue AMap_set(AThread *t, AValue *frame);
AValue AMapGet(AThread *t, AValue *frame);
AValue AMapHasKey(AThread *t, AValue *frame);
AValue AMapLength(AThread *t, AValue *frame);
AValue AMapRemove(AThread *t, AValue *frame);
AValue AMapItems(AThread *t, AValue *frame);
AValue AMapKeys(AThread *t, AValue *frame);
AValue AMapValues(AThread *t, AValue *frame);
AValue AMap_str(AThread *t, AValue *frame);
AValue AMapIter(AThread *t, AValue *frame);

AValue AMapIterCreate(AThread *t, AValue *frame);
AValue AMapIterHasNext(AThread *t, AValue *frame);
AValue AMapIterNext(AThread *t, AValue *frame);

extern int AMapIterNum;
extern int AEmptyMarkerNum;
extern int ARemovedMarkerNum;


/* Declarations for std::Type */

AValue ATypeSupertype(AThread *t, AValue *frame);


#endif
