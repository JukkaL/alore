/* cutil.h - Miscellaneous utility functions used by the compiler

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef A_CUTIL_H
#define A_CUTIL_H


void AInitializeCAlloc(void);
void *ACAlloc(unsigned long size);
void ACFree(void *block, unsigned long size);


typedef struct AList_ {
    struct AList_ *next;
    void *data;
} AList;


/* IDEA: These should probably be implemented more intelligently. */

AList *AAddList(AList *list, void *data);
AList *AAddListEnd(AList *list, void *data);
AList *ARemoveList(AList *list);
void ADisposeList(AList *list);


typedef struct AIntList_ {
    struct AIntList_ *next;
    int data;
} AIntList;


#define AListSize AMax(sizeof(AList), sizeof(AIntList))


AIntList *AAddIntList(AIntList *list, int data);
AIntList *AAddIntListEnd(AIntList *list, int data);

/* Reuse some generic list operations for integer lists.
   IDEA: This is potentially dangerous and not very portable. Implement these
         operations separately for integer lists. */
#define ARemoveIntList(list) ((AIntList *)ARemoveList((AList *)(list)))
#define AFreeIntList(list) (ADisposeList((AList *)(list)))


#endif
