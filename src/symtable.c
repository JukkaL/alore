/* symtable.c - Alore symbol table

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "symtable.h"
#include "memberid.h"
#include "class.h"
#include "parse.h"
#include "cutil.h"
#include "mem.h"
#include "gc.h"
#include "internal.h"


#define SYMBOL_TABLE_INITIAL_SIZE 512


int ASymSize;
static int NumSyms;
ASymbol **ASym;

/* FIX: Should these be defined in this file? */
ASymbol *AMainSymbol;
ASymbol *AUtf8Symbol;
ASymbol *AAsciiSymbol;
ASymbol *ALatin1Symbol;
ASymbol *ACreateSymbol;
ASymbolInfo *ACreateMemberSymbol;

/* This refers to a dummy member (!) symbol that is used to flag some
   AFunction structures as anonymous functions... See also the comment for
   AM__DUMMY_ANON. It's ugly but it works and is unlikely to break anything. */
ASymbolInfo *AAnonFunctionDummySymbol;


static const unsigned char PredefinedWords[] =
    /* Reserved words (these must match ReservedWordType[]!) */
    "if "        "else "       "elif "     "end "      "while "    "repeat "
    "until "     "for "        "switch "   "case "     "sub "      "var "
    "const "     "return "     "break "    "private "  "class "    "try "
    "except "    "finally "    "raise "    "self "     "module "   "import "
    "div "       "mod "        "and "      "or "       "in "       "is "
    "not "       "nil "        "super "    "to "       "encoding " "def "
    "interface " "implements " "as "       "dynamic "  "bind "

    /* Special identifiers */
    "Main "     "ascii "    "latin1 "   "utf8 "

    /* Special method names (these match the symbols in memberid.h) */
    "create "   "_call "    "_add "     "_sub "     "_mul "     "_div "
    "_idiv "    "_mod "     "_pow "     "_eq "      "_lt "      "_gt "
    "_get "     "_set "     "_neg "     "_write "   "_read "    "flush "
    "eof "      "readLn "   "close "    "iterator " "hasNext "  "next "
    "length "   "_in "      "_str "     "_repr "    "_int "     "_float "
    "_hash "    "_dummy "   "\n";

static const unsigned char ReservedWordType[] = {
    TT_IF,       TT_ELSE,      TT_ELIF,    TT_END,     TT_WHILE,    TT_REPEAT,
    TT_UNTIL,    TT_FOR,       TT_SWITCH,  TT_CASE,    TT_SUB,      TT_VAR,
    TT_CONST,    TT_RETURN,    TT_BREAK,   TT_PRIVATE, TT_CLASS,    TT_TRY ,
    TT_EXCEPT,   TT_FINALLY,   TT_RAISE,   TT_SELF,    TT_MODULE,   TT_IMPORT,
    TT_IDIV,     TT_MOD,       TT_AND,     TT_OR,      TT_IN,       TT_IS,
    TT_NOT,      TT_NIL,       TT_SUPER,   TT_TO,      TT_ENCODING, TT_DEF,
    TT_INTERFACE,TT_IMPLEMENTS,TT_AS,      TT_DYNAMIC, TT_BIND,
};


#define NUM_RESERVED_WORDS \
    (sizeof(ReservedWordType) / sizeof(ReservedWordType[0]))


static ABool GrowSymbolTable(void);
static ASymbol **AllocSymbolTable(unsigned len);


ABool AInitializeSymbolTable(void)
{
    AToken *tok;
    AToken *prev;
    AToken *tokTmp;
    AToken *tokHead;
    int i;

    /* Allocate an empty symbol table. */
    ASym = AllocSymbolTable(SYMBOL_TABLE_INITIAL_SIZE);
    if (ASym == NULL)
        return FALSE;

    ASymSize = SYMBOL_TABLE_INITIAL_SIZE - 1;
    NumSyms = 0;

    /* Add all the predefined words into the symbol table by tokenizing a
       string containing all of them. */
    tok = NULL;
    if (!ATokenize(PredefinedWords, PredefinedWords + sizeof(PredefinedWords) -
                  1, &tok, &tokTmp, NULL))
        return FALSE;

    tokHead = tok;

    /* Mark reserved words in the symbol table. */
    for (i = 0; i < NUM_RESERVED_WORDS; i++) {
        tok->info.sym->type = ReservedWordType[i];
        tok = AAdvanceTok(tok);
    }

    /* Record the symbols representing "Main", "latin1" and "utf8" for later
       use. */
    AMainSymbol = tok->info.sym;
    tok = AAdvanceTok(tok);
    AAsciiSymbol = tok->info.sym;
    tok = AAdvanceTok(tok);
    ALatin1Symbol = tok->info.sym;
    tok = AAdvanceTok(tok);
    AUtf8Symbol = tok->info.sym;
    tok = AAdvanceTok(tok);

    /* Record the symbol representing "create" for later use. */
    ACreateSymbol = tok->info.sym;

    /* The first thee member id's are M_NONE, M_INITIALIZER and M_FINALIZER.
       They don't have symbols. */
    ANumMemberIds = 3;
    
    /* FIX: make sure that we can call GetMemberSymbol */
    ACreateMemberSymbol = AGetMemberSymbol(tok->info.sym);
    
    /* Initialize special member names. The -3 is because the first three
       builtin members don't have symbols. The 2 is because the first two
       non-special members are handled above as special cases. */
    for (i = 0; i < AM_FIRST_USER_MEMBER - 3; i++) {
        AGetMemberSymbol(tok->info.sym);
        prev = tok;
        tok = AAdvanceTok(tok);
    }
    
    /* Initialize the "dummy" that is used to flag AFunction structures that
       refer to anonymous functions. */
    AAnonFunctionDummySymbol = AGetMemberSymbol(prev->info.sym);
    
    AFreeTokens(tokHead);

    return TRUE;
}


ABool AAddSymbol(unsigned hashValue, const unsigned char *id, unsigned idLen,
                 ASymbol **symRet)
{
    ASymbol *sym;
    int i;

    if (NumSyms++ == ASymSize)
        if (!GrowSymbolTable())
            return FALSE;

    sym = (ASymbol *)&ASym[hashValue & ASymSize];
    while (sym->next != NULL)
        sym = sym->next;

    sym->next = ACAlloc(sizeof(ASymbol) + idLen);
    sym = sym->next;

    if (sym == NULL)
        return FALSE;
    
    sym->next = NULL;
    sym->type = TT_ID;
    sym->info = (ASymbolInfo *)sym;
    sym->len  = idLen;

    for (i = 0; i < idLen; i++)
        sym->str[i] = id[i];
    sym->str[i] = '\0';

    *symRet = sym;

    return TRUE;
}


/* Find the symbol with the given name. Create the symbol if it doesn't
   exist. */
ABool AGetSymbol(const char *id, unsigned idLen, ASymbol **symRet)
{
    ASymbol *sym;

    sym = AFindSymbol(id, idLen);
    if (sym != NULL) {
        *symRet = sym;
        return TRUE;
    } else {
        unsigned hashValue;
        int i;
        
        /* Calculate hash value. Inlined for efficiency. */
        hashValue = 0;
        for (i = 0; i < idLen; i++)
            hashValue += hashValue * 32 + id[i];

        return AAddSymbol(hashValue, (unsigned char *)id, idLen, symRet);
    }
}


/* Return the symbol with the given name. Return NULL if it doesn't exist. */
ASymbol *AFindSymbol(const char *id, unsigned idLen)
{
    unsigned hashValue;
    ASymbol *sym;
    int i;

    /* Calculate hash value. */
    hashValue = 0;
    for (i = 0; i < idLen; i++)
        hashValue += hashValue * 32 + id[i];

    /* Search the table. */
    sym = ASym[hashValue & ASymSize];
    while (sym != NULL) {
        if (sym->len == idLen) {
            for (i = 0;
                 i < idLen && sym->str[i] == id[i]; i++);

            if (i == idLen)
                return sym;
        }

        sym = sym->next;
    }

    return NULL;
}


/* Doubles the size of the symbol table and rehashes it. */
static ABool GrowSymbolTable(void)
{
    int i;
    ASymbol **table;
    int grow = ASymSize + 1;

    /* FIX: possibly gotta get rid of AllocSymbolTable.. */
    
    table = AllocSymbolTable(2 * grow);
    if (table == NULL)
        return FALSE;

    for (i = 0; i <= ASymSize; i++)
        table[i] = ASym[i];

    AFreeStatic(ASym);
    
    ASym = table;
    ASymSize += grow;

    for (i = 0; i < grow; i++) {
        ASymbol *oldSym = (ASymbol *)&table[i];
        ASymbol *newSym = (ASymbol *)&table[i + grow];

        newSym->next = NULL;

        while (oldSym->next) {
            unsigned hashValue = AGetSymbolHashValue(oldSym->next);

            if (hashValue & grow) {
                newSym->next = oldSym->next;
                newSym = newSym->next;
                oldSym->next = newSym->next;
                newSym->next = NULL;
            } else
                oldSym = oldSym->next;
        }
    }
    
    return TRUE;
}


static ASymbol **AllocSymbolTable(unsigned len)
{
    ASymbol **table;
    int i;
    
    /* Allocate the symbol table. */
    table = AAllocStatic(len * sizeof(ASymbol *));
    if (table == NULL)
        return NULL;

    for (i = 0; i < len; i++)
        table[i] = NULL;

    return table;
}
