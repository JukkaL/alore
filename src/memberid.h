/* memberid.h - Numeric ids of predefined member names such as "_add"

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef MEMBERID_H_INCL
#define MEMBERID_H_INCL


/* Numeric ids of predefined member names, including all "special" method
   names.
   NOTE: If these are updated, update symtable.c as well! */
enum {
    AM_NONE,                    /* Reserved for use as "no member" */
    AM_INITIALIZER,             /* Initializer method for C classes */
    AM_FINALIZER,               /* Finalizer method for C classes */
    AM_CREATE,
    AM_CALL,
    AM_ADD,
    AM_SUB,
    AM_MUL,
    AM_DIV,
    AM_IDIV,
    AM_MOD,
    AM_POW,
    AM_EQ,
    AM_LT,
    AM_GT,
    AM_GET_ITEM,
    AM_SET_ITEM,
    AM_NEGATE,
    AM__WRITE,
    AM__READ,
    AM_FLUSH,
    AM_EOF,
    AM_READLN,
    AM_CLOSE,
    AM_ITERATOR,
    AM_HAS_NEXT,
    AM_NEXT,
    AM_LENGTH,
    AM_IN,
    AM__STR,
    AM__REPR,
    AM__INT,
    AM__FLOAT,
    AM__HASH,
    AM__DUMMY_ANON,             /* Dummy member, the name is not special in
                                   any way. This is only used for creating a
                                   ASymbolInfo * that can be used to stand for
                                   anonymous functions. Yes, it'san ugly
                                   hack, but it might save a few kB of memory
                                   since we do not need to add an additional
                                   flag to AFunction... */

    AM_FIRST_USER_MEMBER        /* Id of first non-predefined member */
};


#endif
