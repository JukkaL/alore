/* error.c - Error messages and message generation helpers

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "error.h"
#include "ident.h"
#include "parse.h"
#include "class.h"
#include "cutil.h"
#include "util.h"
#include "memberid.h"
#include "std_module.h"

#include <stdarg.h>


ACompileError *AFirstError;
ACompileError *ALastError;


ABool AIsOutOfMemoryError;
ABool AIsEofError;


const char ErrOutOfMemory[] = "Out of memory during compilation";
const char ErrInvalidCast[] = "Invalid cast";
const char ErrInvalidLvalue[] = "Invalid lvalue in assignment";
const char ErrBreakOutsideLoop[] = "Break statement outside loop";
const char ErrInvalidSuperClass[] = "Invalid superclass";
const char ErrInvalidAnnotation[] = "Invalid annotation";
const char ErrCreateMustBeMethod[] = "\"create\" must be a method";
const char ErrUnexpectedEndOfFile[] = "Unexpected end of file";
const char ErrInvalidModuleHeader[] = "Invalid module header";
const char ErrMissingModuleHeader[] = "Module header missing";
const char ErrInvalidUtf8Sequence[] = "Invalid UTF-8 sequence";
const char ErrInvalidExceptionType[] = "Invalid exception type";
const char ErrReturnOutsideFunction[] = "Return statement outside function";
const char ErrInvalidNumericLiteral[] = "Invalid numeric literal";
const char ErrUnrecognizedCharacter[] = "Unrecognized character";
const char ErrUnbalancedParentheses[] = "Unbalanced parentheses";
const char ErrBindWithSuperinterface[] =
    "Bind used in an interface that has a superinterface";
const char ErrIncompatibleAssignment[] = "Incompatible assignment";
const char ErrUnterminatedStringLiteral[] = "Unterminated string literal";
const char ErrNonAsciiCharacterInString[] = "Non-ASCII character in string";
const char ErrCycleInSupertypeHierarchy[] = "Cycle in supertype hierarchy";
const char ErrNonAsciiCharacterInComment[] = "Non-ASCII character in comment";
const char ErrUtf8EncodingDeclarationExpected[] =
    "UTF-8 encoding declaration expected";
const char ErrInterfaceCannotHavePrivateMembers[] =
    "Interface cannot have private members";
const char ErrInterfaceCannotHaveMemberVariables[] =
    "Interface cannot have member variables";
const char ErrRequiredArgumentAfterOptionalArgument[] =
    "Non-optional argument after optional argument";

const char ErrUndefined[] = "%t undefined";
const char ErrRedefined[] = "%t multiply defined";
const char ErrAlreadyDefined[] = "%t already defined";
const char ErrAmbiguous[] = "%t ambiguous";
const char ErrWriteOnly[] = "%t write-only";
const char ErrInternalError[] = "Internal error";
const char ErrWrongVisibility[] = "%t has wrong visibility";
const char ErrParseErrorBefore[] = "Parse error before %t";
const char ErrInternalOverflow[] = "Internal overflow #%d";
const char ErrErrorReadingFile[] = "%f: Error reading file";
const char ErrInvalidInterface[] = "Invalid interface \"%i\"";
const char ErrInvalidBindTarget[] = "Invalid bind target \"%i\"";
const char ErrModuleNotImported[] = "Module \"%m\" not imported";
const char ErrWrongNumberOfArgs[] = "Wrong number of arguments for \"%i\"";
const char ErrUndefinedInSuperClass[] = "%t undefined in superclass";
const char ErrModuleCouldNotBeImported[] =
               "Module \"%m\" could not be imported";
const char ErrSelfUsedInNonMemberFunction[] =
               "\"self\" used in non-member function";
const char ErrSuperUsedInNonMemberFunction[] =
               "\"super\" used in non-member function";
const char ErrWrongNumberOfArgsForMain[] =
               "Wrong number of arguments for \"Main\"";
const char ErrLocalVariableShadowsArgument[] =
               "Local variable %t shadows an argument";
const char ErrIncompatibleWithSuperClass[] =
               "%t incompatible with superclass";
const char ErrIncompatibleWithDefinitionIn[] =
               "%t incompatible with definition in \"%i\"";
const char ErrCannotAccessCreate[] = "Cannot access \"create\"";

static const char StringLiteral[]  = "string literal";
static const char NumericLiteral[] = "numeric literal";
static const char EndOfLine[]       = "end of line";

static const char ErrFrom[]        = "          imported in %f, line %d%s";
static const char ErrInFunction[]  = "%f: In function \"%i\":";
static const char ErrInClass[]     = "%f: In class \"%i\":";
static const char ErrInInterface[] = "%f: In interface \"%i\":";
static const char ErrAtTopLevel[]  = "%f: At top level:";
static const char ErrAtLine[]      = "%f, line %d: %s";
static const char ErrInMethodOfClass[] =
                                   "%f: In member \"%M\" of class \"%i\":";
static const char ErrInFileImportedFrom[] =
                                   "In module imported in %f, line %d%s";


/* NOTE: You may need to update these when modifying ATokenType. */ 
static const char OperatorId[TT_COLON - TT_PLUS + 1][3] = {
    "+",    /* TT_PLUS */
    "-",    /* TT_MINUS */
    "==",   /* TT_EQ */
    "!=",   /* TT_NEQ */
    "<",    /* TT_LT */
    ">=",   /* TT_GTE */
    ">",    /* TT_GT */
    "<=",   /* TT_LTE */
    "",     /* IN */
    "",     /* filler */
    "",     /* IS */
    "",     /* filler */
    "*",    /* TT_ASTERISK */
    "/",    /* TT_DIV */
    "",     /* TT_IDIV */
    "",     /* TT_MOD */
    "**",   /* TT_POW */
    ":"     /* TT_COLON */
};

/* NOTE: You may need to update these when modifying ATokenType. */ 
static const char PunctuatorId[TT_SCOPEOP - TT_COMMA + 1][4] = {
    ",", "(", ")", "[", "]", "=", "+=", "-=", "*=", "/=", "**=", ".", "::"
};


static void GenerateErrorVa(int line, const char *format, va_list args);
static int CopyId(char *msg, int i, int maxLen, ASymbolInfo *sym);
static int CopyFullyQualifiedId(char *msg, int i, int max, ASymbolInfo *sym);
static int CopyString(char *msg, int i, int maxLen, const char *str);
static int CopyToken(char *msg, int i, int maxLen, AToken *tok);
static char *TrimFileName(char *fnam);


/* An upper bound for the number of digits in an int value converted to a
   decimal string. */
#define MAX_INT_DIGITS 32


void AGenerateError(int line, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    GenerateErrorVa(line, format, args);
    va_end(args);
}


void GenerateErrorVa(int line, const char *format, va_list args)
{
    /* FIX: use AllocStatic instead? */
    ACompileError *err = ACAlloc(sizeof(ACompileError));
    char *msg = ACAlloc(AError_MESSAGE_MAX_LEN + 1);
    int i;

    if (err == NULL || msg == NULL) {
        ACFree(err, sizeof(ACompileError)); /* FIX: what if null? */
        ACFree(msg, AError_MESSAGE_MAX_LEN + 1); /* FIX: what if null? */
        AGenerateOutOfMemoryError();
        return;
    }

    if (line != -1) {
        for (i = 0; i < ANumActiveFiles; i++) {
            err->files[i] = ADupStr(AActiveFiles[i]);
            err->lineNums[i] = ALineNums[i];
            if (err->files[i] == NULL)
                break;
        }
        err->numFiles = i;
        err->lineNums[i - 1] = line;
    } else
        err->numFiles = 0;

    AFormatMessageVa(msg, AError_MESSAGE_MAX_LEN, format, args);

    err->next = NULL;
    err->class_ = ACurClass;
    err->function = ACurFunction;
    err->member = ACurMember;
    err->type = A_ERR_ERROR;
    err->message = msg;

    ALastError->next = err;
    ALastError = err;
}


/* Generate a parse error message that includes a description of the token tok.
   Return a pointer to the next TT_NEWLINE or TT_EOF token. */
AToken *AGenerateParseError(AToken *tok)
{
    if (tok->type == TT_EOF) {
        if (!AIsEofError) {
            AGenerateError(tok->lineNumber, ErrUnexpectedEndOfFile);
            AIsEofError = TRUE;
        }
    } else if (tok->type == TT_ERR_STRING_UNTERMINATED)
        AGenerateError(tok->lineNumber, ErrUnterminatedStringLiteral);
    else if (tok->type == TT_ERR_INVALID_NUMERIC)
        AGenerateError(tok->lineNumber, ErrInvalidNumericLiteral);
    else if (tok->type == TT_ERR_UNRECOGNIZED_CHAR)
        AGenerateError(tok->lineNumber, ErrUnrecognizedCharacter);
    else if (tok->type == TT_ERR_NON_ASCII_STRING_CHAR)
        AGenerateError(tok->lineNumber, ErrNonAsciiCharacterInString);
    else if (tok->type == TT_ERR_NON_ASCII_COMMENT_CHAR)
        AGenerateError(tok->lineNumber, ErrNonAsciiCharacterInComment);
    else if (tok->type == TT_ERR_INVALID_UTF8_SEQUENCE)
        AGenerateError(tok->lineNumber, ErrInvalidUtf8Sequence);
    else
        AGenerateError(tok->lineNumber, ErrParseErrorBefore, tok);
    
    while (tok->type != TT_NEWLINE && tok->type != TT_EOF)
        tok = AAdvanceTok(tok);

    return tok;
}


AToken *AGenerateParseErrorSkipNewline(AToken *tok)
{
    tok = AGenerateParseError(tok);
    if (tok->type == TT_NEWLINE)
        return AAdvanceTok(tok);
    else
        return tok;
}


AToken *AGenerateErrorSkipNewline(AToken *tok, const char *message)
{
    AGenerateError(tok->lineNumber, message);
    while (tok->type != TT_NEWLINE)
        tok = AAdvanceTok(tok);
    return AAdvanceTok(tok);
}


AToken *AGenerateExpressionParseError(AToken *tok)
{
    if (tok->type != TT_RPAREN)
        return AGenerateParseError(tok);

    AGenerateError(tok->lineNumber, ErrUnbalancedParentheses);

    do {
        tok = AAdvanceTok(tok);
    } while (tok->type != TT_NEWLINE);

    return tok;
}       


void AGenerateOutOfMemoryError(void)
{
    AIsOutOfMemoryError = TRUE;
}


int AFormatMessage(char *msg, int maxLen, const char *fmt, ...)
{
    va_list args;
    int n;
    
    va_start(args, fmt);
    n = AFormatMessageVa( msg, maxLen, fmt, args);
    va_end(args);

    return n;
}


/* Known format specifiers:
     %i identifier (SymbolInfo *)
     %q fully qualified identifier (SymbolInfo *) [but omit std:: prefix]
     %m module name (ModuleId *)
     %t token (Token *)
     %s string (char *)
     %d integer (int)
     %p pointer (void *)
     %f file name (char *)
     %F function name (Function *)
     %M member id (int)
     %T type of value (AValue)
     %% %
  */
int AFormatMessageVa(char *msg, int maxLen, const char *fmt, va_list args)
{
    int i = 0;

    /* Make sure that there will be enough room for the null terminator. */
    maxLen--;
    
    while (*fmt != '\0' && i < maxLen) {
        if (*fmt != '%')
            msg[i++] = *fmt++;
        else {
            fmt++;
            switch (*fmt++) {
            case 'i':
                i = CopyId(msg, i, maxLen, va_arg(args, ASymbolInfo *));
                break;

            case 'q':
                i = CopyFullyQualifiedId(msg, i, maxLen,
                                         va_arg(args, ASymbolInfo *));
                break;
                
            case 'm':
                i = ACopyModName(msg, i, maxLen, va_arg(args, AModuleId *));
                break;
                
            case 't':
                i = CopyToken(msg, i, maxLen, va_arg(args, AToken *));
                break;

            case 's':
                i = CopyString(msg, i, maxLen, va_arg(args, char *));
                break;

            case 'f':
                i = CopyString(msg, i, maxLen,
                               TrimFileName(va_arg(args, char *)));
                break;

            case 'F': {
                AFunction *func = va_arg(args, AFunction *);
                if (func->sym == NULL)
                    i += AFormatMessage(msg + i, maxLen - i, "at main level");
                else if (func->sym == AAnonFunctionDummySymbol)
                    i += AFormatMessage(msg + i, maxLen - i,
                                        "anonymous function");
                /* FIX use CopyString */
                else if (func->sym->type == ID_GLOBAL_CLASS) {
                    char method[128];
                    int createNum;
                    int memberInitializerNum;
                    
                    createNum = AValueToType(
                        AGlobalByNum(func->sym->num))->create;
                    memberInitializerNum = AValueToType(
                        AGlobalByNum(func->sym->num))->memberInitializer;
                    
                    /* Is the current function the constructor of the class? */
                    if (func == AValueToFunction(AGlobalByNum(createNum)) ||
                        func == AValueToFunction(
                            AGlobalByNum(memberInitializerNum)))
                        i += AFormatMessage(msg + i, maxLen - 1, "class %q",
                                            func->sym);
                    else {
                        AFormatMethodSymbol(method, 128, func);
                        i += AFormatMessage(msg + i, maxLen - 1,
                                            "%s of %q",
                                           method, ARealTypeSymbol(func->sym));
                    }
                } else
                    i += AFormatMessage(msg + i, maxLen - i, "%q",
                                        func->sym);
                break;
            }

            case 'M': {
                int key = va_arg(args, int);
                
                if (key == AM_NONE)
                    i = CopyString(msg, i, maxLen, "(none)");
                else if (key == AM_INITIALIZER)
                    i = CopyString(msg, i, maxLen, "#i");
                else if (key == AM_FINALIZER)
                    i = CopyString(msg, i, maxLen, "#f");
                else
                    i += AFormatMessage(msg + i, maxLen - i, "%i",
                                       AGetMemberSymbolByKey(key));
                break;
            }

            case 'd': {
                /* IDEA: This is brain-dead. Use sprintf instead. */
                
                char str[MAX_INT_DIGITS];
                int num = va_arg(args, int);
                int strInd = MAX_INT_DIGITS;
                ABool sign = num < 0;

                /* IDEA: This does not work with the largest negative
                         integer. */
                if (num < 0)
                    num = -num;

                str[--strInd] = '\0';

                do {
                    str[--strInd] = num % 10 + '0';
                    num /= 10;
                } while (num != 0);

                if (sign)
                    str[--strInd] = '-';

                i = CopyString(msg, i, maxLen, str + strInd);
                
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                char s[128];
                sprintf(s, "%p", ptr);
                i = CopyString(msg, i, maxLen, s);
                break;                
            }

            case 'T': {
                AValue val = va_arg(args, AValue);
                char type[256];
                
                if (AIsInt(val))
                    strcpy(type, "Int");
                else if (AIsFloat(val))
                    strcpy(type, "Float");
                else if (AIsStr(val))
                    strcpy(type, "Str");
                else if (AIsArray(val))
                    strcpy(type, "Array");
                else if (AIsTuple(val))
                    strcpy(type, "Tuple");
                else if (AIsRange(val))
                    strcpy(type, "Range");
                else if (AIsPair(val))
                    strcpy(type, "Pair");
                else if (AIsNil(val))
                    strcpy(type, "nil");
                else if (AIsTrue(val))
                    strcpy(type, "True");
                else if (AIsFalse(val))
                    strcpy(type, "False");
                else if (AIsConstant(val))
                    strcpy(type, "Constant");
                else if (AIsNonSpecialType(val))
                    strcpy(type, "Type");
                else if (AIsGlobalFunction(val) || AIsAnonFunc(val))
                    strcpy(type, "Function");
                else if (AIsMethod(val))
                    strcpy(type, "bound method"); /* IDEA: Show the name of the
                                                           method. */
                else if (AIsInstance(val)) {
                    ASymbolInfo *sym =
                        AGetInstanceType(AValueToInstance(val))->sym;

                    if (sym == AConstantClass->sym && AMemberDirect(val, 0) ==
                        ANil)
                        strcpy(type, "nil");
                    else
                        AFormatMessage(type, sizeof(type), "%q",
                                       ARealTypeSymbol(sym));
                } else
                    strcpy(type, "Object");
                
                i = CopyString(msg, i, maxLen, type);
                break;
            }                
                
            case '%':
                msg[i++] = '%';
                break;

            default:
                fmt--;
                break;
            }
        }
    }
    msg[i] = '\0';
    return i;
}


static int CopyId(char *msg, int i, int max, ASymbolInfo *sym)
{
    int result;
    
    ALockInterpreter();
    result = CopyString(msg, i, max, (char *)AGetSymbolName(sym));
    AUnlockInterpreter();
    
    return result;
}


int ACopyModName(char *msg, int i, int max, AModuleId *mod)
{
    int partInd;

    for (partInd = 0; partInd < mod->numParts; partInd++) {
        i = CopyId(msg, i, max, mod->id[partInd]);
        if (partInd < mod->numParts - 1 && i + 1 < max) {
            msg[i++] = ':';
            msg[i++] = ':';
        }
    }

    return i;
}


static int CopyFullyQualifiedId(char *msg, int i, int max, ASymbolInfo *sym)
{
    if (sym->sym != NULL && sym->sym->type != ID_GLOBAL_MODULE_MAIN) {
        int j = CopyFullyQualifiedId(msg, i, max, sym->sym);
        /* Omit std:: prefix. */
        if (j - i != 3 || strncmp(msg + i, "std", 3) != 0) {
            i = j;
            if (i + 1 < max) {
                msg[i++] = ':';
                msg[i++] = ':';
            }
        }
    }

    return CopyId(msg, i, max, sym);
}


static int CopyToken(char *msg, int i, int max, AToken *tok)
{
    ATokenType type = tok->type;
    
    if (type == TT_ID || type == TT_ID_EXPOSED
        || AIsAlphaOperator(type) || AIsReservedWord(type)) {
        if (i < max)
            msg[i++] = '\"';
        i = CopyId(msg, i, max, (ASymbolInfo *)tok->info.sym);
        if (i < max)
            msg[i++] = '\"';
    } else if (AIsOperator(type))
        i = CopyString(msg, i, max, OperatorId[type - TT_PLUS]);
    else if (AIsPunctuator(type))
        i = CopyString(msg, i, max, PunctuatorId[type - TT_COMMA]);
    else {
        switch (type) {
        case TT_NEWLINE:
            i = CopyString(msg, i, max, EndOfLine);
            break;
            
        case TT_LITERAL_INT:
        case TT_LITERAL_FLOAT:
        case TT_ERR_INVALID_NUMERIC:
            i = CopyString(msg, i, max, NumericLiteral);
            break;
            
        case TT_LITERAL_STRING:
        case TT_ERR_STRING_UNTERMINATED:
            i = CopyString(msg, i, max, StringLiteral);
            break;

        case TT_EOF:
            i = CopyString(msg, i, max, "end of file");
            break;

        case TT_ANNOTATION:
            /* We should not get here normally. */
            i = CopyString(msg, i, max, "<annotation>");
            break;

        default:
            /* Unsupported token. We should never get here except if using
               ATrace() or similar for debugging. */

            /* During parsing if we see an unrecognized character, it should
               have been reported earlier and should not end up here. */
            
            i = CopyString(msg, i, max, "<unknown>");            
            break;
        }
    }

    return i;       
}

    
static int CopyString(char *msg, int i, int max, const char *str)
{
    while (*str != '\0' && i < max)
        msg[i++] = *str++;

    return i;
}


ABool ADisplayErrorMessages(ABool (*display)(const char *msg, void *data),
                          void *data)
{
    char buffer[AError_STRING_MAX_LEN];
    
    char *prevFile = "";
    ASymbolInfo *prevClass = NULL;
    ASymbolInfo *prevFunction = NULL;
    int prevMember = AM_NONE;
    
    ACompileError *curErr = AFirstError;
    ACompileError *lastErr = ALastError;

    if (curErr != NULL) {
        for (;;) {
            if (curErr->numFiles > 0) {
                char *file  = curErr->files[curErr->numFiles - 1];
                int lineNum = curErr->lineNums[curErr->numFiles - 1];
        
                if (strcmp(file, prevFile) != 0) {
                    ABool first = TRUE;
                    int i;

                    for (i = curErr->numFiles - 2; i >= 0; i--) {
                        AFormatMessage(buffer, AError_STRING_MAX_LEN,
                                      first ? ErrInFileImportedFrom : ErrFrom,
                                      curErr->files[i], curErr->lineNums[i],
                                      i == 0 ? ":" : ",");
                        if (!display(buffer, data))
                            return FALSE;
                        first = FALSE;
                    }
            
                    prevFile = file;
                }

                if (curErr->class_ != NULL) {
                    ATypeInfo *type = AValueToType(
                        AGlobalByNum(curErr->class_->num));
                    
                    if (curErr->member != AM_NONE && !type->isInterface) {
                        if (curErr->class_ != prevClass 
                            || curErr->member != prevMember) {
                            AFormatMessage(buffer, AError_STRING_MAX_LEN,
                                          ErrInMethodOfClass, file,
                                          curErr->member, curErr->class_);
                            if (!display(buffer, data))
                                return FALSE;
                        }
                    } else {
                        if (prevClass != curErr->class_
                            || prevFunction != NULL) {
                            const char *fmt = type->isInterface ?
                                ErrInInterface : ErrInClass;
                            AFormatMessage(buffer, AError_STRING_MAX_LEN,
                                           fmt, file, curErr->class_);
                            if (!display(buffer, data))
                                return FALSE;
                        }
                    }
                    
                    prevClass = curErr->class_;
                    prevFunction = curErr->function;
                    prevMember = curErr->member;
                } else {
                    if (curErr->function != prevFunction) {
                        /* Display the name of the function if available. */
                        if (curErr->function != NULL)
                            AFormatMessage(buffer, AError_STRING_MAX_LEN,
                                          ErrInFunction, file,
                                          curErr->function);
                        else
                            AFormatMessage(buffer, AError_STRING_MAX_LEN,
                                          ErrAtTopLevel, file);
                        if (!display(buffer, data))
                            return FALSE;
                
                        prevFunction = curErr->function;
                        prevClass = NULL;                       
                    }
                }

                /* Display the error message itself. */
                AFormatMessage(buffer, AError_STRING_MAX_LEN, ErrAtLine, file,
                              lineNum, curErr->message);
            } else
                AFormatMessage(buffer, AError_STRING_MAX_LEN, "%s",
                              curErr->message);

            if (!display(buffer, data))
                return FALSE;
            
            /* Advance to the next message and exit loop if none available. */ 
            if (curErr == lastErr)
                break;
            curErr = curErr->next;
        }
    }

    if (AIsOutOfMemoryError)
        return display(ErrOutOfMemory, data);

    return TRUE;
}


static char *TrimFileName(char *fnam)
{
    /* FIX: unix specific */
    
    while (fnam[0] == '.' && A_IS_DIR_SEPARATOR(fnam[1]))
        fnam += 2;

    return fnam;
}


void ATrace(const char *format, ...)
{
    char buf[1024];
    va_list args;

    va_start(args, format);    
    AFormatMessageVa(buf, sizeof(buf), format, args);
    va_end(args);

    fprintf(stderr, "%s", buf);
}
