/* error.h - Error messages and message generation helpers

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef ERROR_H_INCL
#define ERROR_H_INCL


/* FIX: somehow reduce dependencies */
#include "lex.h"
#include "compile.h"
#include "parse.h" /* FIX?? */
#include <stdarg.h>


#define AError_MESSAGE_MAX_LEN 128
#define AError_STRING_MAX_LEN 256


typedef enum {
    A_ERR_WARNING,
    A_ERR_ERROR
} AErrorType;


/* A list of error messages and warnings generated during compilation. */
typedef struct ACompileError_ {
    struct ACompileError_ *next; /* NOTE: Has to be the first field. */
    int numFiles;         /* Number of active files when this error
                             occurred. */
    char *files[A_MAX_COMPILE_DEPTH]; /* The files that were active when this
                                         error occurred. */
    unsigned short lineNums[A_MAX_COMPILE_DEPTH];
                          /* The number of the line at which the error occurred
                             relative to the beginning of the file. */
    ASymbolInfo *class_;  /* ASymbol representing the class in which this error
                             occurred or NULL if the error wasn't inside a
                             class definition. */
    ASymbolInfo *function;/* The function in which the error occurred or
                             NULL. */
    int member;           /* The member in which the error occurred or
                             M_NONE. */
    AErrorType type;      /* AType of the error (see AErrorType). */
    char *message;        /* The error message. */
} ACompileError;


extern const char ErrInvalidLvalue[];
extern const char ErrInvalidNumericLiteral[];
extern const char ErrUnterminatedStringLiteral[];
extern const char ErrUnrecognizedCharacter[];
extern const char ErrNonAsciiCharacterInString[];
extern const char ErrNonAsciiCharacterInComment[];
extern const char ErrInvalidUtf8Sequence[];
extern const char ErrUtf8EncodingDeclarationExpected[];
extern const char ErrUnexpectedEndOfFile[];
extern const char ErrInvalidModuleHeader[];
extern const char ErrMissingModuleHeader[];
extern const char ErrIncompatibleAssignment[];
extern const char ErrUnbalancedParentheses[];
extern const char ErrMainUndefined[];
extern const char ErrBreakOutsideLoop[];
extern const char ErrReturnOutsideFunction[];
extern const char ErrSelfUsedInNonMemberFunction[];
extern const char ErrSuperUsedInNonMemberFunction[];
extern const char ErrInvalidSuperClass[];
extern const char ErrInvalidInterface[];
extern const char ErrInvalidBindTarget[];
extern const char ErrInvalidExceptionType[];
extern const char ErrOutOfMemory[];
extern const char ErrRequiredArgumentAfterOptionalArgument[];
extern const char ErrInvalidAnnotation[];
extern const char ErrInterfaceCannotHavePrivateMembers[];
extern const char ErrInterfaceCannotHaveMemberVariables[];
extern const char ErrCreateMustBeMethod[];
extern const char ErrCycleInSupertypeHierarchy[];
extern const char ErrInvalidCast[];
extern const char ErrBindWithSuperinterface[];

extern const char ErrParseErrorBefore[];
extern const char ErrModuleCouldNotBeImported[];
extern const char ErrModuleNotImported[];
extern const char ErrUndefined[];
extern const char ErrRedefined[];
extern const char ErrAlreadyDefined[];
extern const char ErrWriteOnly[];
extern const char ErrAmbiguous[];
extern const char ErrWrongVisibility[];
extern const char ErrInternalOverflow[];
extern const char ErrInternalError[];
extern const char ErrErrorReadingFile[];
extern const char ErrUndefinedInSuperClass[];
extern const char ErrLocalVariableRedefined[];
extern const char ErrWrongNumberOfArgs[];
extern const char ErrIncompatibleWithSuperClass[];
extern const char ErrIncompatibleWithDefinitionIn[];
extern const char ErrCannotAccessCreate[];

extern const char ErrWrongNumberOfArgsForMain[];

extern const char ErrLocalVariableShadowsArgument[];


/* Internal overflow error numbers */
/* FIX: meaningful error messsage?? */
enum {
    IOF_STACK_FRAME_TOO_LARGE,
    IOF_COMPILE_DEPTH_TOO_LARGE,
    IOF_TOO_MANY_VARS_IN_MULTIPLE_ASSIGN,
    IOF_BLOCK_DEPTH_TOO_LARGE,
    IOF_EXPRESSION_DEPTH_TOO_LARGE,
    IOF_TOO_MANY_VARIABLES_IN_FOR,
    IOF_TOO_MANY_NESTED_SUBS
};


void AGenerateError(int line, const char *format, ...);
AToken *AGenerateErrorSkipNewline(AToken *tok, const char *message);

void AGenerateOutOfMemoryError(void);

AToken *AGenerateParseError(AToken *tok);
AToken *AGenerateParseErrorSkipNewline(AToken *tok);
AToken *AGenerateExpressionParseError(AToken *tok);

ABool ADisplayErrorMessages(ABool (*display)(const char *msg, void *data),
                          void *data);

ABool ADefDisplay(const char *msg, void *data);


Assize_t ACopyModName(char *buf, Assize_t i, Assize_t max, AModuleId *mod);


/* Generated error messages */
extern ACompileError *AFirstError;
extern ACompileError *ALastError;


extern ABool AIsOutOfMemoryError;
extern ABool AIsEofError;


#endif
