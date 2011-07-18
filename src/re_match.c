/* re_match.c - __re module (regular expression matching)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "re_internal.h"
#include "str_internal.h"
#include "internal.h"
#include "str.h"
#include "gc.h"


/* Uncomment this to display debugging output */
/*#define DEBUG_REGEXP*/


#define STACK_INITIAL_SIZE 128
#define DEFAULT_NUM_SUBEXPS 16


#define PAREN_MIN2 (-(1 << 30))
#define PAREN_MIN (-(1 << 29))
#define PAREN (-(1 << 28))


typedef struct {
    int repeat;
    AReOpcode *ip;
    void *str;
} StackEntry;


typedef struct {
    AReOpcode *ip; /* Pointer to the regexp matching opcodes; unmovable! */
    StackEntry *stack;
    StackEntry *stackTop;
    StackEntry *stackBase;
    AValue *strValue;
    int strLen;
    void *strEnd;
    void *strBeg;
    int *subExpBeg;
    int flags;
} MatchInfo;


static int Try(MatchInfo *info, unsigned char *str);
static int TryWide(MatchInfo *info, AWideChar *str);
static StackEntry *GrowStack(MatchInfo *info, StackEntry *st);
static void FindSubExpressions(MatchInfo *info, AReRange *subExp,
                               int numSubExps, ABool isNarrow);


static AReOpcode EmptyOpcode = A_EMPTY;


#ifdef DEBUG_REGEXP
#define TRACE(a) do { printf a; printf("\n"); } while (0)
#else
#define TRACE(a)
#endif


#define GetStrPointers(info) \
    do { \
        if (AIsNarrowStr(*(info)->strValue)                                \
            || AIsNarrowSubStr(*(info)->strValue)) {                       \
            (info)->strBeg = ANarrowStrItems(*(info)->strValue);           \
            (info)->strEnd = (char *)(info)->strBeg + (info)->strLen;      \
        } else {                                                           \
            (info)->strBeg = AWideStrItems(*(info)->strValue);             \
            (info)->strEnd = (AWideChar *)(info)->strBeg + (info)->strLen; \
        } \
    } while (0)


/* Match a regular expression against a string. Return 1 if matched, 0 if
   did not match and -1 if out of memory. */
int AMatchRegExp(AValue *reValue, AValue *strValue, int len, int pos,
                int matchFlags, AReRange *subExp, int maxNumSubExps)
{
    MatchInfo info;
    int subExpBeg[DEFAULT_NUM_SUBEXPS];
    StackEntry stack[STACK_INITIAL_SIZE];
    ARegExp *re;
    int flags;
    int matchStart;
    int matchEnd;
    ABool isNarrow;

    re = AValueToPtr(*reValue);
    flags = re->flags;

#if 0
    /* FIX optimize when there is a must-literal */
    if (flags & A_ALRE_MUSTLITERAL) {
        tryEnd = alreSearch(re, uStr, len);
        if (tryEnd == NULL)
            return 0;
        if (re->flags & A_ALRE_LITERALONLY) {
            if (maxNumSubExps >= 0) {
                subExp[0].beg = tryEnd - uStr;
                subExp[0].end = tryEnd + re->mustStringLen - uStr;
            }
            return 1;
        }
    }
#endif

    /* Trivially reject if the string is too short. */
    if (len < re->minLen) {
        TRACE(("Failed: len %d < minLen %d", len, re->minLen));
        return 0;
    }

    if (re->numGroups < DEFAULT_NUM_SUBEXPS)
        info.subExpBeg = subExpBeg;
    else {
        info.subExpBeg = AAllocStatic(re->numGroups * sizeof(int));
        re = AValueToPtr(*reValue);
    }

    info.ip = APtrAdd(AValueToPtr(re->code), sizeof(AValue));
    info.strValue = strValue;
    info.strLen = len;

    info.stackBase = stack;
    info.stackTop = stack + STACK_INITIAL_SIZE - 1;

    info.flags = matchFlags;

    GetStrPointers(&info);

    stack[0].repeat = 0;
    stack[0].ip = &EmptyOpcode;

#if 0
    /* FIX use mustliteral */
    if ((flags & (A_ALRE_MUSTLITERAL | A_ALRE_ANCHORED)) ==
            A_ALRE_MUSTLITERAL) {
        AReOpcode *startChar = re->startChar;
        
        do {
            unsigned char *beg;
            
            if (tryEnd - info.strBeg <= re->mustStringBack)
                uStr = info.strBeg;
            else
                uStr = tryEnd - re->mustStringBack;

            do {
                if (AIsInSet(startChar, *uStr)
                        && (matchEnd = Try(&info, uStr)) != -1)
                    goto Match;
            } while (++uStr <= tryEnd);

            beg = tryEnd + re->mustStringLen;
            tryEnd = alreSearch(re, beg, info.strEnd - uStr);
        } while (tryEnd != NULL);
    } else
#endif

    isNarrow = AIsNarrowStr(*strValue) || AIsNarrowSubStr(*strValue);
        
    if (isNarrow) {
        unsigned char *str = (unsigned char *)info.strBeg + pos;
        AReOpcode *startChar = re->startChar;

        matchStart = pos;

        TRACE(("Narrow match"));

        if (!(flags & A_ALRE_ANCHORED) && !(matchFlags & A_RE_NOSEARCH)) {
            /* Try to match at every position. */

            for (; str < (unsigned char *)info.strEnd; str++, matchStart++) {
                TRACE(("Try at %d", str - (unsigned char *)info.strBeg));
                if (AReIsInSet(startChar, *str)) {
                    int index = str - (unsigned char *)info.strBeg;
                    if ((matchEnd = Try(&info, str)) != -1)
                        goto Match;
                    re = AValueToPtr(*reValue);
                    startChar = re->startChar;
                    str = (unsigned char *)info.strBeg + index;
                }
            }

            /* Some expressions may match at the end of the expression. */
            if (re->minLen == 0 && str == info.strEnd
                && (matchEnd = Try(&info, str)) != -1)
                goto Match;
        } else {
            /* Match only at a single location. Only one try necessary. */

            TRACE(("Try achored"));
            if (str <= (unsigned char *)info.strEnd
                && (matchEnd = Try(&info, str)) != -1)
                goto Match;
        }
    } else {
        AWideChar *str = AWideStrItems(*strValue) + pos;
        AReOpcode *startChar = re->startChar;

        matchStart = pos;

        TRACE(("Wide match"));

        if (!(flags & A_ALRE_ANCHORED) && !(matchFlags & A_RE_NOSEARCH)) {
            /* Try to match at every position. */

            for (; str < (AWideChar *)info.strEnd; str++, matchStart++) {
                TRACE(("Try at %d", str - (AWideChar *)info.strBeg));
                if (*str >= 256 || AReIsInSet(startChar, *str)) {
                    int index = str - (AWideChar *)info.strBeg;
                    if ((matchEnd = TryWide(&info, str)) != -1)
                        goto Match;
                    re = AValueToPtr(*reValue);
                    startChar = re->startChar;
                    str = (AWideChar *)info.strBeg + index;
                }
            }

            /* Some expressions may match at the end of the expression. */
            if (re->minLen == 0 && str == info.strEnd
                && (matchEnd = TryWide(&info, str)) != -1)
                goto Match;
        } else {
            /* Match only at a single location. Only one try necessary. */

            TRACE(("Try achored"));
            if (str <= (AWideChar *)info.strEnd
                && (matchEnd = TryWide(&info, str)) != -1)
                goto Match;
        }
    }
                
    /* Couldn't find a match. Free allocated data and return. */
       
    if (info.stackBase != stack)
        AFreeStatic(info.stackBase);
        
    if (info.subExpBeg != subExpBeg)
        AFreeStatic(info.subExpBeg);

    TRACE(("Failed: No matching position"));
    
    return 0;   

  Match:

    /* Possible match */

    /* Did the matching routine run out of memory? */
    if (matchEnd == -2) {
        TRACE(("Error: Out of memory"));
        return -1;
    }

    if (maxNumSubExps >= 0) {
        /* Mark the position of the match. */
        subExp[0].beg = matchStart;
        subExp[0].end = matchEnd;

        /* Try to find the positions of subexpressions. */
        if (maxNumSubExps > 0)
            FindSubExpressions(&info, subExp, maxNumSubExps, isNarrow);
    }
    
    if (info.stackBase != stack)
        AFreeStatic(info.stackBase);

    if (info.subExpBeg != subExpBeg)
        AFreeStatic(info.subExpBeg);
    
    TRACE(("Matched"));
        
    return 1;
}


/* Return a boolean indicating whether ch (which should be larger than 255)
   is included in a character set encoded at ip. */
ABool AIsInWideSet2(AReOpcode *ip, AWideChar ch)
{
    int i;
    ABool complement;

    /* Skip the 8-bit character set data. */
    ip += A_SET_SIZE;

    /* Now *ip encodes the number of ranges included in the wide portion of
       the set and ip[1] encodes character set flags. The ranges follow,
       each of them being encoded as two integers (lower and higher bounds,
       inclusive). */
    
    /* Is this a completement set [^...]? */
    complement = (ip[1] & A_WS_COMPLEMENT) != 0;
    
    if (ip[1] & A_WS_IGNORE_CASE) {
        /* Case insensitive matching */
        AWideChar lower = ALower(ch);
        AWideChar upper = AUpper(ch);
        for (i = 0; i < *ip; i++) {
            if ((lower >= ip[2 + i * 2] && lower <= ip[3 + i * 2])
                || (upper >= ip[2 + i * 2] && upper <= ip[3 + i * 2]))
                return 1 ^ complement;
        }
    } else {
        /* Case sensitive matching */
        for (i = 0; i < *ip; i++) {
            if (ch >= ip[2 + i * 2] && ch <= ip[3 + i * 2])
                return 1 ^ complement;
        }
    }

    /* Check built-in character classes \w and \W if they are part of the
       character set. */
    if (ip[1] & (A_WS_WORD_CHAR | A_WS_NOT_WORD_CHAR)) {
        if (((ip[1] & A_WS_WORD_CHAR) && AIsWordChar(ch))
            || ((ip[1] & A_WS_NOT_WORD_CHAR) && !AIsWordChar(ch)))
            return 1 ^ complement;
    }
    
    return 0 ^ complement;
}


/* Include the actual matcher twice. The first copy matches 8-bit strings and
   the second copy matches 16-bit strings. */

#define CHAR unsigned char
#define TRY_FN Try
#define LOWER(ch) ALowerCase[ch]
#define IS_IN_SET AIsInSet
#define IS_WORD_CHAR(ch) AIsInSet(AWordCharSet, ch)

#include "re_match_inc.c"

#undef CHAR
#undef TRY_FN
#undef LOWER
#undef IS_IN_SET
#undef IS_WORD_CHAR

#define CHAR AWideChar
#define TRY_FN TryWide
#define LOWER(ch) ALower(ch)
#define IS_IN_SET AIsInWideSet
#define IS_WORD_CHAR(ch) AIsWordChar(ch)

#include "re_match_inc.c"


static StackEntry *GrowStack(MatchInfo *info, StackEntry *st)
{
    StackEntry *base = info->stackBase;
    int ind = st - base;
    void *oldStrBeg = info->strBeg;

    TRACE(("Growing match stack"));

    if (ind == STACK_INITIAL_SIZE - 1) {
        StackEntry *oldBase = base;
        base = AAllocStatic(sizeof(StackEntry) * 2 * (ind + 1));
        if (base == NULL)
            return NULL;
        memcpy(base, oldBase, sizeof(StackEntry) * (ind + 1));
    } else {
        base = AGrowStatic(base, sizeof(StackEntry) * 2 * (ind + 1));
        if (base == NULL)
            return NULL;
    }
    info->stackBase = base;

    GetStrPointers(info);

    if (info->strBeg != oldStrBeg) {
        /* The string to be matched has been moved by the garbage collector.
           Replace all the string pointers in the stack to point to the new
           location. */
        int i;
        TRACE(("Adjustment due to copying gc"));
        for (i = 0; i <= ind; i++)
            base[i].str = APtrAdd(info->strBeg, 
                                  APtrDiff(base[i].str, oldStrBeg));
    }

    info->stackTop = base + 2 * ind + 1;

    return base + ind;
}


static void FindSubExpressions(MatchInfo *info, AReRange *subExp,
                               int numSubExps, ABool isNarrow)
{
    StackEntry *st;
    StackEntry *stEnd;
    int i;

    for (i = 1; i <= numSubExps; i++)
        subExp[i].beg = subExp[i].end = -1;
    
    stEnd = info->stack;
    for (st = info->stackBase + 1; st <= stEnd; st++) {
        int backRef = st->repeat - PAREN_MIN;
        if (backRef > 0 && backRef <= numSubExps) {
            subExp[backRef].beg =
                (unsigned char *)st->str - (unsigned char *)info->strBeg;
            subExp[backRef].end =
                (unsigned char *)st->ip - (unsigned char *)info->strBeg;
            if (!isNarrow) {
                subExp[backRef].beg >>= 1;
                subExp[backRef].end >>= 1;
            }
        }
    }
}
