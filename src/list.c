/* list.c - Linked list (usable only in the compiler)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "common.h"
#include "cutil.h"
#include "error.h"


/* All operations generate an out-of-memory compile error and return NULL if
   out of memory. */


/* Add an item to the start of the list. */
AList *AAddList(AList *list, void *data)
{
    AList *node = ACAlloc(sizeof(AList));
    
    if (node == NULL) {
        ADisposeList(list);
        AGenerateOutOfMemoryError();
        return NULL;
    }

    node->next = list;
    node->data = data;

    return node;
}


/* Append an item to the end of a list. */
AList *AAddListEnd(AList *list, void *data)
{
    AList *node = ACAlloc(sizeof(AList));

    if (node == NULL) {
        ADisposeList(list);
        AGenerateOutOfMemoryError();
        return NULL;
    }

    node->next = NULL;
    node->data = data;
    
    if (list == NULL)
        return node;

    while (list->next != NULL)
        list = list->next;

    list->next = node;

    return list;
}


/* Remove the head of a list. */
AList *ARemoveList(AList *list)
{
    AList *newList = list->next;
    
    ACFree(list, sizeof(AList));

    return newList;
}


/* Free the items of a list. Do not process the data values. */
void ADisposeList(AList *list)
{
    AList *prev;

    while (list != NULL) {
        prev = list;
        list = list->next;
        
        ACFree(prev, sizeof(AList));
    }
}


/* Add an item to the start of an integer list. */
AIntList *AAddIntList(AIntList *list, int data)
{
    AIntList *node = ACAlloc(sizeof(AList));
    
    if (node == NULL) {
        AFreeIntList(list);
        AGenerateOutOfMemoryError();
        return NULL;
    }

    node->next = list;
    node->data = data;

    return node;
}


/* Append an item to an integer list. */
AIntList *AAddIntListEnd(AIntList *list, int data)
{
    AIntList *node = ACAlloc(sizeof(AIntList));
    AIntList *cur;

    if (node == NULL) {
        AFreeIntList(list);
        AGenerateOutOfMemoryError();
        return NULL;
    }

    node->next = NULL;
    node->data = data;
    
    if (list == NULL)
        return node;

    for (cur = list; cur->next != NULL; cur = cur->next);

    cur->next = node;

    return list;
}
