/* parse.h - Alore parser and compiler

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#ifndef PARSE_H_INCL
#define PARSE_H_INCL

#include "common.h"
#include "compile.h"
#include "lex.h"
#include "operator.h"
#include "cutil.h"
#include "symtable.h"
#include "globals.h"


#define A_MAX_SUBEXPRESSION_DEPTH 128

#define A_MAX_BLOCK_DEPTH 32

#define A_MAX_QUICK_ARGS 8


/* Maximum number of destinations in multiple assignment. */
#define A_MAX_MULTI_ASSIGN 32


/* Expression types. Even types are lvalue types (modifiable), odd types are
   rvalue types (read-only). */
typedef enum {
    ET_LOCAL_LVALUE,            /* Local lvalue */
    ET_LOCAL,                   /* Local value */
    
    ET_GLOBAL_LVALUE,           /* Global lvalue */
    ET_GLOBAL,                  /* Global value */

    ET_MEMBER_LVALUE,           /* Member */
    ET_MEMBER,
    
    ET_MEMBER_FILLER,
    ET_MEMBER_FUNCTION,
    
    ET_LOCAL_LVALUE_EXPOSED,    /* Exposed local lvalue */
    ET_LOCAL_EXPOSED,           /* Exposed local value */
    
    ET_PARTIAL_LVALUE,          /* ?? */
    ET_PARTIAL,                 /* Partial local value */
    
    ET_ARRAY_LVALUE,            /* Array lvalue */
    ET_ARRAY,                   /* Array value */
    
    ET_ARRAY_LOCAL_LVALUE,      /* Array lvalue, all locals */
    ET_FILLER,

    ET_TUPLE_LVALUE,            /* Tuple lvalue */
    ET_TUPLE,                   /* Tuple value */

    ET_TUPLE_LOCAL_LVALUE,      /* Tuple lvalue, all locals */
    ET_INT,                     /* Integer */
    
    ET_ERROR,
    ET_LOGICAL                  /* Logical expression */
} AExpressionType;


#define AIsLocalExpression(type) ((type) <= ET_LOCAL)

#define AIsExposedLocalExpression(type) ((type) >= ET_LOCAL_LVALUE_EXPOSED && \
                                         (type) <= ET_LOCAL_EXPOSED)

#define AIsGlobalExpression(type) ((type) >= ET_GLOBAL_LVALUE && \
                                   (type) <= ET_GLOBAL)

#define AIsMemberExpression(type) ((type) >= ET_MEMBER_LVALUE && \
                                   (type) <= ET_MEMBER_FUNCTION)

#define AIsPartialExpression(type) ((type) >= ET_PARTIAL_LVALUE && \
                                    (type) <= ET_TUPLE_LOCAL_LVALUE)

#define AIsComplexExpression(type) ((type) >= ET_MEMBER_LVALUE && \
                                    (type) <= ET_TUPLE_LOCAL_LVALUE)

#define AIsCallableExpression(type) ((type) <= ET_MEMBER || \
                                     (type) == ET_MEMBER_FUNCTION)

#define AIsIndexableExpression(type) ((type) <= ET_GLOBAL)

#define AIsArrayExpression(type) \
    ((type) <= ET_ARRAY_LOCAL_LVALUE && (type) >= ET_ARRAY_LVALUE)

#define AIsTupleExpression(type) \
    ((type) <= ET_TUPLE_LOCAL_LVALUE && (type) >= ET_TUPLE_LVALUE)

#define AIsLvalueExpression(type) (!((type) & 1))


typedef AIntList ABranchList;


typedef struct {
    AExpressionType type;
    int num;
    ASymbolInfo *sym;
    ABranchList *branch[2];
    ABranchList *finalBranch;
} AExpression;


/* Node in a linked list that represents active break statements. */
typedef struct ABreakList_ {
    struct ABreakList_ *next;
    ABranchList *exits;
    int depth;
    ABool isSet;
    int opcodeIndex; /* Opcode that is used in finally statements. */
} ABreakList;


/* Node in a linked list that represents active return statements. */
typedef struct AReturnList_ {
    struct AReturnList_ *next;
    int index;
    ABool isSet;
} AReturnList;


/* Information about an exposed local variable */
typedef struct {
    ASymbol *sym;
    ABool isConst;
} AExposedInfo;


/* State of the parser within a function or method definition. */
typedef struct {
    int numLocals;
    int numLocalsActive;
    int blockDepth;
    int loopDepth;
    ASymbolInfo *locals; /* FIX this might not work properly */
    ABreakList *loopExits;
    AReturnList *returnBranches;
    int tryLocalVar;
    AIntList *finallyExits;
    ABool isRvalue;
    int numAccessedExposedVariables;
    AExposedInfo *accessedExposedVariables;
    unsigned assignPos[A_MAX_MULTI_ASSIGN];
} AParseState;


ABranchList *AAddBranch(ABranchList *branch);
void AMergeBranches(ABranchList *branch, ABranchList *append);
void AToggleBranches(ABranchList *branch);
void AAdjustBranchPos(ABranchList *branch, int indexDif);
void ASetBranches(ABranchList *branch, int codeIndex);
ABranchList *ACreateBranchList(void);

void AConvertToLogicalExpression(AExpression *expr);
void AConvertToValueExpression(AExpression *expr);
void AConvertToLocalExpression(AExpression *expr);
void ACreateOpcode(AExpression *expr);
int AGetLocalVariable(void);

ASymbolInfo *AAddLocalVariableSymbol(ASymbol *sym, AIdType type, unsigned num);
void ALeaveBlock(void);

AToken *AParseExpression(AToken *tok, int *localNum, ABool allowCast);
AToken *AParseAssignExpression(AToken *tok, ABool allowCast);
AToken *AParseSingleAssignExpression(AToken *tok, ABool *isErr);
AToken *AParseMultiExpression(AToken *tok, int num, ABool allowCast);
AToken *AParseLogicalExpression(AToken *tok, ABool cond, ABranchList **branch);
AToken *AParseAssignmentOrExpression(AToken *tok);
AToken *AParseCaseExpression(AToken *tok, int num, int *skipIndex);

AToken *AParseAnonymousFunction(AToken *tok);

AToken *AParseTypeAnnotation(AToken *tok);
AToken *AClearTypeAnnotation(AToken *tok);

AToken *AParseTypeAnnotationUntilSeparator(AToken *tok);
AToken *AClearTypeAnnotationUntilSeparator(AToken *tok);
AToken *ASkipTypeAnnotationUntilSeparator(AToken *tok);

AToken *AParseGenericAnnotation(AToken *tok);
AToken *AClearGenericAnnotation(AToken *tok);

AToken *AParseBracketedTypeAnnotation(AToken *tok);
AToken *AClearBracketedTypeAnnotation(AToken *tok);


/* FIX: where should this be? */
ASymbolInfo *AAddGlobalVariable(ASymbol *sym, ASymbolInfo *module,
                                AIdType type, ABool isPrivate);
AToken *AParseGlobalVariable(AToken *tok, ASymbolInfo **var, ABool override,
                             ABool isSilent);
AToken *AParseQuotedGlobalVariable(AToken *tok, ASymbolInfo **var,
                                   ABool isSilent);
AToken *AParseAnyGlobalVariable(AToken *tok, ASymbolInfo **var,
                                ABool isSilent);

AToken *AFindModule(AToken *tok, AModuleId *mod, ABool *isCompiled);
ABool ARealizeModule(ASymbolInfo *sym);
ATypeInfo *AGetResolveSupertype(ATypeInfo *type);


#define A_MEMBER_PRIVATE (1U << 31) /* FIX: assumes 32-bit ints */
#define A_MEMBER_DIRECT_METHOD (1U << 30)
#define A_MEMBER_DIRECT_VARIABLE (1U << 29)
#define A_MEMBER_CONSTANT (1U << 28)

#define AGetMemberValue(memberValue) \
    ((memberValue) & ~(A_MEMBER_PRIVATE | A_MEMBER_DIRECT_METHOD | \
                       A_MEMBER_DIRECT_VARIABLE | A_MEMBER_CONSTANT))


#define ASuperType(type) \
    ((type)->isSuperValid ? (type)->super : AGetResolveSupertype(type))


void AResolveTypeInterfaces(ATypeInfo *type);


int AGetOperatorMethod(ATokenType token, ABool isReverse);


void ACheckMethodDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                           struct AToken_ *tok);
void ACheckMemberVariableDefinition(ATypeInfo *type, unsigned key,
                                   ABool isPrivate, struct AToken_ *tok);
void ACheckGetterDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                           struct AToken_ *tok);
void ACheckSetterDefinition(ATypeInfo *type, unsigned key, ABool isPrivate,
                           struct AToken_ *tok);


extern struct AList_ *AImportedModules;

extern ASymbolInfo *ACurModule;
extern ASymbolInfo *ACurClass;
extern ASymbolInfo *ACurFunction;
extern int ACurMember;

extern ATypeInfo *AType;

extern ASymbolInfo *AAccessorMember;

extern int ANumLocals;
extern int ANumLocalsActive;
extern int ABlockDepth;

/* Sub nesting depth (global function / method = 0, first anon function = 1,
   etc.) */
extern int AFunDepth;

extern struct AIntList_ *AInitFunctions;


extern int ANumMemberIds;


extern ABool AIsMainDefined;
extern int AMainFunctionNum;

extern ABool AIsRvalue;


extern unsigned AAssignPos[A_MAX_MULTI_ASSIGN];


extern int ANumAccessedExposedVariables;
extern int AAccessedExposedVariablesSize;
extern AExposedInfo *AAccessedExposedVariables;


#endif
