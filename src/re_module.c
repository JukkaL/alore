/* re_module.c - __re module (the C part of the re module)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "re.h"
#include "debug_params.h"


/* The re module implementation is divided between modules re (implemented in
   Alore) and __re (this module). */


static AValue BuildMatchObject(AThread *t, AValue *frame, int numGroups,
                              AReRange *subExp);
static ABool GetArguments(AThread *t, AValue *frame, int *pos);


/* Global nums of definitions */
static int MatchResultNum; /* __re::MatchResult */
static int IgnoreCaseNum;  /* __re::IgnoreCase */
static int RegExpNum;      /* __re::RegExp */
int ARegExpErrorNum;       /* re::RegExpError */


#ifdef A_DEBUG
/* __re::__Dump(regexp)
   Debugging helper that dumps the internal structure of a regular expression
   to stdout. */
static AValue ReDump(AThread *t, AValue *frame)
{
    AValue reVal;
    ARegExp *re;
    
    if (!AIsStr(frame[0]))
        return ARaiseTypeErrorND(t, NULL);

    reVal = ACompileRegExp(t, frame[0], 0);
    if (AIsError(reVal))
        return reVal;

    re = AValueToPtr(reVal);
    ADisplayRegExp(re);
    
    return ANil;
}
#endif


/* __re::Match(regexp, str[, index])
   Visible to use code as re::Match. */
static AValue ReMatch(AThread *t, AValue *frame)
{
    ARegExp *re;
    AReRange subExp[11];
    int status;
    int pos;

    if (!GetArguments(t, frame, &pos))
        return AError;

    status = AMatchRegExp(frame + 2, frame + 1, AStrLen(frame[1]), pos,
                          A_RE_NOSEARCH, subExp, 10);
    if (status > 0) {
        re = AValueToPtr(frame[2]);
        return BuildMatchObject(t, frame, re->numGroups, subExp);
    } else if (status == 0)
        return ANil;
    else
        return ARaiseMemoryErrorND(t);
}


/* __re::Search(regexp, std[, index])
   Visible to user code as re::Search. */
static AValue ReSearch(AThread *t, AValue *frame)
{
    ARegExp *re;
    AReRange subExp[11];
    int status;
    int pos;

    if (!GetArguments(t, frame, &pos))
        return AError;

    status = AMatchRegExp(frame + 2, frame + 1, AStrLen(frame[1]), pos, 0,
                         subExp, 10);
    if (status > 0) {
        re = AValueToPtr(frame[2]);
        return BuildMatchObject(t, frame, re->numGroups, subExp);
    } else if (status == 0)
        return ANil;
    else
        return ARaiseMemoryErrorND(t);
}


/* Process the arguments to ReMatch or ReSearch. */
static ABool GetArguments(AThread *t, AValue *frame, int *pos)
{
    AExpectStr(t, frame[1]);

    if (!AIsDefault(frame[2])) {
        *pos = AGetInt(t, frame[2]);
        if (*pos < 0) {
            ARaiseValueError(t, "Invalid position");
            return FALSE;
        }
    } else
        *pos = 0;

    if (AIsValue(t, frame[0], AGlobalByNum(RegExpNum))) {
        frame[2] = AMemberDirect(frame[0], 0);
        return TRUE;
    }

    AExpectStr(t, frame[0]);
    
    frame[2] = ACompileRegExp(t, frame[0], 0);
    if (AIsError(frame[2]))
        return FALSE;

    return TRUE;
}


/* MatchResult group(n) */
static AValue MatchResultGroup(AThread *t, AValue *frame)
{
    AValue a;
    int ind;
    int i1, i2;

    ind = AGetInt(t, frame[1]);
    a = AMemberDirect(frame[0], 0);

    if (ind > (AArrayLen(a) >> 1) - 1)
        return ARaiseValueErrorND(t, NULL);
    
    i1 = AValueToInt(AArrayItem(a, 2 * ind));
    i2 = AValueToInt(AArrayItem(a, 2 * ind + 1));

    if (i1 < 0)
        return ANil;
    else
        return ASubStr(t, AMemberDirect(frame[0], 1), i1, i2);
}


/* MatchResult start(n) */
static AValue MatchResultStart(AThread *t, AValue *frame)
{
    AValue a;
    int ind;

    ind = AGetInt(t, frame[1]);
    a = AMemberDirect(frame[0], 0);

    if (ind > (AArrayLen(a) >> 1) - 1)
        return ARaiseValueErrorND(t, NULL);

    return AArrayItem(a, 2 * ind);
}


/* MatchResult stop(n) */
static AValue MatchResultStop(AThread *t, AValue *frame)
{
    AValue a;
    int ind;

    ind = AGetInt(t, frame[1]);
    a = AMemberDirect(frame[0], 0);

    if (ind > (AArrayLen(a) >> 1) - 1)
        return ARaiseValueErrorND(t, NULL);

    return AArrayItem(a, 2 * ind + 1);
}


/* MatchResult span(n) */
static AValue MatchResultSpan(AThread *t, AValue *frame)
{
    AValue a;
    int ind;
    AValue lo;

    ind = AGetInt(t, frame[1]);
    a = AMemberDirect(frame[0], 0);

    if (ind > (AArrayLen(a) >> 1) - 1)
        return ARaiseValueErrorND(t, NULL);

    lo = AArrayItem(a, 2 * ind);
    if ((ASignedValue)lo < 0)
        return ANil;
        
    return AMakePair(t, lo, AArrayItem(a, 2 * ind + 1));
}


/* Build a MatchResult object from match results.
   Frame points to the frame of ReMatch or ReSearch. The subExp argument
   should have numGroups items that specify the ranges matched by different
   subexpressions. */
static AValue BuildMatchObject(AThread *t, AValue *frame,
                              int numGroups, AReRange *subExp)
{
    AValue match;
    int i;

    /* Create an array containing the locations of the matched groups. */
    frame[3] = AMakeArray(t, numGroups * 2);
    for (i = 0; i < numGroups; i++) {
        int beg = subExp[i].beg;
        int end = subExp[i].end;

        ASetArrayItem(t, frame[3], 2 * i, AIntToValue(beg));
        ASetArrayItem(t, frame[3], 2 * i + 1, AIntToValue(end));
    }

    /* Create the MatchResult object. */
    match = ACallValue(t, AGlobalByNum(MatchResultNum), 0, frame + 4);
    if (AIsError(match))
        return AError;

    frame[4] = match;
    ASetMemberDirect(t, frame[4], 0, frame[3]);
    ASetMemberDirect(t, frame[4], 1, frame[1]);

    return frame[4];
}


/* RegExp create(str[, flag]) */
static AValue RegExpCreate(AThread *t, AValue *frame)
{
    int flags = 0;
    
    AExpectStr(t, frame[1]);

    if (!AIsDefault(frame[2])) {
        if (frame[2] == AGlobalByNum(IgnoreCaseNum))
            flags |= A_RE_NOCASE;
        else {
            ARaiseValueErrorND(t, NULL);
            return AError;
        }
    }

    frame[2] = ACompileRegExp(t, frame[1], flags);
    if (AIsError(frame[2]))
        return FALSE;

    ASetMemberDirect(t, frame[0], 0, frame[2]);

    return frame[0];
}


/* __re::Main() */
static AValue ReMain(AThread *t, AValue *frame)
{
    /* Initialize global constant. */
    ARegExpErrorNum = AGetGlobalNum(t, "re::RegExpError");
    return ANil;
}


A_MODULE(__re, "__re")
    A_IMPORT("re")

    A_DEF(A_PRIVATE("Main"), 0, 0, ReMain)
    
    A_DEF_OPT("Match", 2, 3, 2, ReMatch)
    A_DEF_OPT("Search", 2, 3, 2, ReSearch)

    A_SYMBOLIC_CONST_P("IgnoreCase", &IgnoreCaseNum)

    /* User code actually sees re::RegExp which is a subclass of this class
       (__re::RegExp). */
    A_CLASS_PRIV_P("RegExp", 1, &RegExpNum)
      A_METHOD_OPT("create", 1, 2, 0, RegExpCreate)
    A_END_CLASS()

    /* This is the implementation subtype of re::MatchResult. */
    A_CLASS_PRIV_P("MatchResult", 2, &MatchResultNum)
        A_INHERIT("re::MatchResult")
        /* FIX: Constructor? The type object is visible via TypeOf() */
        A_METHOD("group", 1, 0, MatchResultGroup)
        A_METHOD("start", 1, 0, MatchResultStart)
        A_METHOD("stop", 1, 0, MatchResultStop)
        A_METHOD("span", 1, 0, MatchResultSpan)
    A_END_CLASS()

#ifdef A_DEBUG
    A_DEF("__Dump", 1, 0, ReDump)
#endif
A_END_MODULE()
