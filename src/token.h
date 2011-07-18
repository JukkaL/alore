/* token.h - Lexical analyzer related definitions

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef TOKEN_H_INCL
#define TOKEN_H_INCL


/* NOTE: Be careful when modifying these. Check error.c and the macros
         at the end of this file. */
typedef enum {
    TT_EOB,             /* End of token block */
    TT_BOM,             /* Byte order mark (in a UTF-8 source file) */
    TT_ID,              /* Identifier */
    TT_ID_EXPOSED,      /* Identifier (exposed variable in a definition) */
    TT_LITERAL_INT,
    TT_LITERAL_FLOAT,
    TT_LITERAL_STRING,

    TT_FIRST_OPERATOR,

    /* NOTE: The order of operators must be the same as in "operator.h" and
             "opcode.h" and "error.c" (OperatorId). */
    TT_PLUS = TT_FIRST_OPERATOR, /* The first binary operator */
    TT_MINUS,
    TT_EQ,
    TT_NEQ,
    TT_LT,
    TT_GTE,
    TT_GT,
    TT_LTE,
    TT_IN,
    TT_FILLER_5,        /* "not in" (has no token) */
    TT_IS,
    TT_FILLER_6,        /* "not is" (has no token) */
    TT_ASTERISK,
    TT_DIV,
    TT_IDIV,
    TT_MOD,
    TT_POW,
    TT_COLON,
    TT_TO,
    TT_AND,
    TT_OR,              /* The last binary operator */
    TT_NOT,

    /* NOTE: If you change these, also update PunctuatorId in error.c. */
    TT_COMMA,
    TT_LPAREN,
    TT_RPAREN,
    TT_LBRACKET,
    TT_RBRACKET,
    TT_ASSIGN,
    TT_ASSIGN_ADD,
    TT_ASSIGN_SUB,
    TT_ASSIGN_MUL,
    TT_ASSIGN_DIV,
    TT_ASSIGN_POW,
    TT_DOT,
    TT_SCOPEOP,
    
    TT_NEWLINE,
    TT_EOF,             /* End of file, always preceded by newline token */

    /* Reserved words; if you update these, also update symtable.c!  */
    TT_IF,
    TT_ELSE,
    TT_ELIF,
    TT_END,
    TT_WHILE,
    TT_REPEAT,
    TT_UNTIL,
    TT_FOR,
    TT_SWITCH,
    TT_CASE,
    TT_SUB,
    TT_DEF,
    TT_VAR,
    TT_CONST,
    TT_RETURN,
    TT_BREAK,
    TT_PRIVATE,
    TT_CLASS,
    TT_TRY,
    TT_EXCEPT,
    TT_FINALLY,
    TT_RAISE,
    TT_SELF,
    TT_MODULE,
    TT_IMPORT,
    TT_NIL,
    TT_SUPER,
    TT_ENCODING,
    TT_INTERFACE,
    TT_IMPLEMENTS,
    TT_AS,
    TT_DYNAMIC,
    TT_BIND,

    TT_LAST_RESERVED = TT_BIND,

    TT_ANNOTATION,                    /* Part of an (ignored) annotation */
    
    TT_ERR_STRING_UNTERMINATED,
    TT_ERR_INVALID_NUMERIC,
    TT_ERR_UNRECOGNIZED_CHAR,         /* Invalid character outside a comment or
                                         a string literal */
    TT_ERR_NON_ASCII_STRING_CHAR,     /* Non-ascii character code within a
                                         string literal in ascii file */
    TT_ERR_NON_ASCII_COMMENT_CHAR,    /* Non-ascii character code within a
                                         comment in ascii file */
    TT_ERR_INVALID_UTF8_SEQUENCE,     /* Invalid UTF-8 character sequence in
                                         a comment or a string literal*/
    TT_ERR_PARSE,
    
    TT_LAST_TOKEN,
    TT_EMPTY
} ATokenType;


typedef enum {
    ID_GLOBAL_CONST = TT_LAST_TOKEN,
    ID_GLOBAL_DEF,
    ID_GLOBAL_CLASS,
    ID_GLOBAL_INTERFACE,
    ID_GLOBAL,
    ID_GLOBAL_MODULE_SUB_EMPTY,
    ID_GLOBAL_MODULE_SUB,
    ID_GLOBAL_MODULE_EMPTY,
    ID_GLOBAL_MODULE,
    ID_GLOBAL_MODULE_MAIN,
    ID_MEMBER,
    ID_LOCAL_CONST,
    ID_LOCAL_CONST_EXPOSED,
    ID_LOCAL_EXPOSED,
    ID_LOCAL,
    ID_ERR_PARSE,
    ID_ERR_UNDEFINED
} AIdType;


#define AIsIdTokenType(type) ((type) == TT_ID || (type) == TT_ID_EXPOSED)
#define AIsBinaryOperator(type) ((type) >= TT_PLUS && (type) <= TT_OR)
#define AIsAlphaOperator(type) ((type) == TT_IDIV || (type) == TT_MOD \
                               || (type) == TT_IS || (type) == TT_IN \
                               || ((type) >= TT_TO && (type) <= TT_NOT))
#define AIsReservedWord(type) ((type) >= TT_IF && (type) <= TT_LAST_RESERVED)
#define AIsOperator(type) ((type) >= TT_PLUS && (type) <= TT_NOT)
#define AIsPunctuator(type) ((type) >= TT_COMMA && (type) <= TT_SCOPEOP)
#define AIsOperatorAssignment(type) ((type) >= TT_ASSIGN_ADD \
                                     && (type) <= TT_ASSIGN_POW)
#define AIsDefToken(type) ((type) == TT_SUB || (type) == TT_DEF)
#define AIsErrorToken(type) \
    ((type) == TT_ERR_STRING_UNTERMINATED \
     || (type) == TT_ERR_INVALID_NUMERIC \
     || (type) == TT_ERR_UNRECOGNIZED_CHAR \
     || (type) == TT_ERR_NON_ASCII_STRING_CHAR \
     || (type) == TT_ERR_NON_ASCII_COMMENT_CHAR \
     || (type) == TT_ERR_INVALID_UTF8_SEQUENCE \
     || (type) == TT_ERR_PARSE)


#define AIsId(type) ((type) >= ID_GLOBAL_CONST)
#define AIsModuleId(type) ((type) >= ID_GLOBAL_MODULE_SUB_EMPTY && \
                          (type) <= ID_GLOBAL_MODULE)
#define AIsRootModuleId(type) ((type) >= ID_GLOBAL_MODULE_EMPTY && \
                              (type) <= ID_GLOBAL_MODULE)
#define AIsSubModuleId(type) ((type) >= ID_GLOBAL_MODULE_SUB_EMPTY && \
                             (type) <= ID_GLOBAL_MODULE_SUB)
#define AIsLocalId(type) ((type) >= ID_LOCAL_CONST)
#define AIsExposedLocalId(type) ((type) == ID_LOCAL_EXPOSED || \
                                 (type) == ID_LOCAL_CONST_EXPOSED)
#define AIsGlobalId(type) ((type) >= ID_GLOBAL_CONST && (type) <= ID_GLOBAL)


#endif
