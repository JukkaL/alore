/* re_match_inc.c - __re module (regexp matching, #included by re_match.c)

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

/* NOTE: This file is not a stand-alone source file! It is included by
         re_match.c TWICE, once for matching 8-bit strings and once for
         matching wide characer strings. */


/* The following defines are required by this file:

     CHAR: unsigned character type (i.e. unsigned char or unsigned short)
     TRY_FN: name of the Try function
     LOWER(ch): lower case transformation
     IS_IN_SET(ip, ch): set membership testing
     IS_WORD_CHAR(ch): word character classification */


#define SkipSet(ip) ((ip) + (ip)[A_SET_SIZE] * 2 + 2 + A_SET_SIZE)


static int TRY_FN(MatchInfo *info, CHAR *str)
{
    AReOpcode *ip;
    CHAR *strEnd;
    StackEntry *st;
    StackEntry *stTop;

    ip = info->ip;
    strEnd = info->strEnd;
    st = info->stackBase;
    stTop = info->stackTop;

    for (;;) {
        CHAR *repeatBeg;

#ifdef DEBUG_REGEXP
        {
            char ch[20];

            if (str == info->strEnd)
                sprintf(ch, "<end-of-str>");
            else if (str > info->strEnd || str < info->strBeg)
                sprintf(ch, "!!out-of-bounds");
            else if (*str >= 32)
                sprintf(ch, "'%c'", *str);
            else if (*str == '\n')
                sprintf(ch, "'\\n'");
            else
                sprintf(ch, "'\\x%.2x'", *str);

            TRACE(("%d: opcode %d - char %s (depth %d)", ip - info->ip, *ip,
                   ch, st - info->stackBase));
        }
#endif

        switch (*ip++) {
        case A_BOW:
            /* Beginning of word */

            if ((str != info->strBeg && IS_WORD_CHAR(str[-1]))
                || str == strEnd || !IS_WORD_CHAR(str[0]))
                goto Fail;

            break;

        case A_EOW:
            /* End of word */

            if (str == info->strBeg || !IS_WORD_CHAR(str[-1])
                || (str != strEnd && IS_WORD_CHAR(str[0])))
                goto Fail;

            break;

        case A_BOL:
            /* Beginning of line (single line) */

            if (str != info->strBeg || (info->flags & A_RE_NOBOL))
                goto Fail;

            break;

        case A_BOL_MULTI:
            /* Beginning of line (multiline) */

            if ((str != info->strBeg || (info->flags & A_RE_NOBOL))
                     && str[-1] != '\n')
                goto Fail;

            break;

        case A_EOL:
            /* End of line (single line) */

            if (str != strEnd || (info->flags & A_RE_NOEOL))
                goto Fail;

            break;

        case A_EOL_MULTI:
            /* End of line (multiline) */

            if ((str != strEnd || (info->flags & A_RE_NOEOL)) && *str != '\n')
                goto Fail;

            break;

        case A_LPAREN: {
            int backRef = *ip++;

            st++;
            st->repeat = backRef + PAREN_MIN2;
            st->str = (CHAR *)info->strBeg + info->subExpBeg[backRef];
            st->ip = NULL;

            info->subExpBeg[backRef] = str - (CHAR *)info->strBeg;
            goto Check;
        }

        case A_RPAREN_SKIP:
            if (info->subExpBeg[*ip] == str - (CHAR *)info->strBeg) {
                ip += 3;
                break;
            }

            /* Fall through */

        case A_RPAREN: {
            int backRef = *ip++;

            st++;
            st->repeat = backRef + PAREN_MIN;
            st->str = (CHAR *)info->strBeg + info->subExpBeg[backRef];
            st->ip = (AReOpcode *)str;

            goto Check;
        }

        case A_BACKREF:
        case A_BACKREF_I:
        case A_BACKREF_SKIP:
        case A_BACKREF_I_SKIP: {
            StackEntry *ptr;
            int backRef;
            CHAR *ref;
            CHAR *refEnd;
            CHAR *refBeg;

            ptr = st;
            backRef = *ip++;

            /* Try to find the position of the subexpression from the stack. */
            while (ptr->ip != &EmptyOpcode
                   && ptr->repeat != backRef + PAREN_MIN)
                ptr--;

            if (ptr->ip == &EmptyOpcode)
                goto Fail;

            refBeg = ref = ptr->str;
            refEnd = (CHAR *)ptr->ip;

            /* Fail if the subexpression hasn't been matched or the sub-
               expression is longer than the rest of the string. */
            if (ptr->repeat == 0 || str + (refEnd - ref) > strEnd)
                goto Fail;

            /* Try to match the back reference. */
            if (ip[-2] == A_BACKREF_I || ip[-2] == A_BACKREF_I_SKIP) {
                while (ref != refEnd) {
                    if (LOWER(*str) != LOWER(*ref))
                        goto Fail;
                    str++;
                    ref++;
                }
            } else {
                while (ref != refEnd) {
                    if (*str != *ref)
                        goto Fail;
                    str++;
                    ref++;
                }
            }

            /* Matched ok. */

            /* If the subexpression is zero-length and we're in a loop, break
               out of the loop to avoid an infinite loop. */
            if (refBeg == refEnd && ip[-1] >= A_BACKREF_SKIP)
                ip += 2;

            break;
        }

            /* Literal character opcodes */

        case A_LITERAL:
            if (str == strEnd || *str != *ip)
                goto Fail;

            ip++;
            str++;

            break;

        case A_LITERAL_REPEAT:
        case A_LITERAL_REPEAT_MIN: {
            int min = ip[0];
            int opt = ip[1];

            CHAR ch;
            CHAR *repeatEnd;

            if (str + min > strEnd)
                goto Fail;

            ch = ip[2];

            for (; min > 0; min--)
                if (*str++ != ch)
                    goto Fail;

            ip += 3;

            /* FIX: Does this work with very long strings? */
            repeatEnd = AMin(str + opt, strEnd);

            if (str == repeatEnd || *str != ch)
                break;

            repeatBeg = str;
            do {
                str++;
            } while (str != repeatEnd && *str == ch);

            if (ip[-4] == A_LITERAL_REPEAT)
                goto Repeat;
            else
                goto RepeatMin;
        }

        case A_LITERAL_STRING: {
            int litLen = *ip++;
            CHAR *litEnd = str + litLen;

            if (litEnd > strEnd)
                goto Fail;

            do {
                if (*str++ != *ip++)
                    goto Fail;
            } while (str < litEnd);

            break;
        }

            /* Literal character opcodes (ignoring case) */
        case A_LITERAL_I:
            if (str == strEnd || LOWER(*str) != *ip)
                goto Fail;

            ip++;
            str++;

            break;

        case A_LITERAL_I_REPEAT:
        case A_LITERAL_I_REPEAT_MIN: {
            int min = ip[0];
            int opt = ip[1];

            CHAR ch;
            CHAR *repeatEnd;

            if (str + min > strEnd)
                goto Fail;

            ch = ip[2];

            for (; min > 0; min--)
                if (LOWER(*str++) != ch)
                    goto Fail;

            ip += 3;

            repeatEnd = AMin(str + opt, strEnd);

            if (str == repeatEnd || LOWER(*str) != ch)
                break;

            repeatBeg = str;
            do {
                str++;
            } while (str != repeatEnd && LOWER(*str) == ch);

            if (ip[-4] == A_LITERAL_I_REPEAT)
                goto Repeat;
            else
                goto RepeatMin;
        }

        case A_LITERAL_I_STRING: {
            int litLen = *ip++;
            CHAR *litEnd = str + litLen;

            if (litEnd > strEnd)
                goto Fail;

            do {
                if (LOWER(*str) != *ip)
                    goto Fail;
                str++;
                ip++;
            } while (str < litEnd);

            break;
        }

            /* Character set opcodes */

        case A_SET:
            if (str == strEnd || !IS_IN_SET(ip, *str))
                goto Fail;

            ip = SkipSet(ip);
            str++;

            break;

        case A_SET_REPEAT:
        case A_SET_REPEAT_MIN: {
            int min = ip[0];
            int opt = ip[1];

            CHAR *repeatEnd;

            if (str + min > strEnd)
                goto Fail;

            ip += 2;

            for (; min > 0; min--) {
                if (!IS_IN_SET(ip, *str))
                    goto Fail;
                str++;
            }

            repeatEnd = AMin(str + opt, strEnd);

            if (str == repeatEnd || !IS_IN_SET(ip, *str)) {
                ip = SkipSet(ip);
                break;
            }

            repeatBeg = str;
            do {
                str++;
            } while (str != repeatEnd && IS_IN_SET(ip, *str));

            if (ip[-3] == A_SET_REPEAT) {
                ip = SkipSet(ip);
                goto Repeat;
            } else {
                ip = SkipSet(ip);
                goto RepeatMin;
            }
        }

            /* Any character opcodes */

        case A_ANY:
            if (str == strEnd)
                goto Fail;

            str++;

            break;

        case A_ANY_REPEAT:
        case A_ANY_REPEAT_MIN:
        case A_ANY_ALL_REPEAT:
        case A_ANY_ALL_REPEAT_MIN: {
            int min = ip[0];
            int opt = ip[1];

            CHAR *repeatEnd;

            if (str + min > strEnd)
                goto Fail;

            str += min;

            ip += 2;

            repeatEnd = AMin(str + opt, strEnd);

            if (str == repeatEnd)
                break;

            repeatBeg = str;
            do {
                str++;
            } while (str != repeatEnd);

            if ((ip[-3] & 1) == (A_ANY_REPEAT & 1))
                goto Repeat;
            else
                goto RepeatMin;
        }

        case A_BRANCH_BEFORE:
            /* First try to branch, then continue normally if no match. */

            st++;

            st->str = str;
            st->repeat = 0;
            st->ip = ip + 1;

            ip += (ASignedRegExpOpcode)*ip;

            goto Check;

        case A_BRANCH_AFTER:
            /* First try to continue normally, then branch if no match. */

            st++;

            st->str = str;
            st->repeat = 0;
            st->ip = ip + (ASignedRegExpOpcode)*ip;

            ip++;

            goto Check;

        case A_BRANCH_ALWAYS:
            ip += (ASignedRegExpOpcode)*ip;
            break;

        case A_MATCH:
            /* Success! */
            info->stack = st;
            return str - (CHAR *)info->strBeg;

#ifdef DEBUG_REGEXP
        case A_EMPTY:
            TRACE(("Fail opcode"));
            return -1;
#endif

        default:
            /* Failure! */
            TRACE(("Invalid opcode"));
            return -1;
        }

        continue;

      RepeatMin:

        st++;
        st->str = str;
        st->repeat = repeatBeg - str;
        st->ip = ip;

        str = repeatBeg;

        goto Check;

      Repeat:

        st++;
        st->str = repeatBeg;
        st->repeat = str - repeatBeg;
        st->ip = ip;

      Check:

        if (st == stTop) {
            int index = str - (CHAR *)info->strBeg;
            st = GrowStack(info, st);
            if (st == NULL)
                return -2;
            /* Update stTop, strEnd and str since they may have been changed
               by GrowStack (garbage collection). */
            stTop = info->stackTop;
            strEnd = info->strEnd;
            str = (CHAR *)info->strBeg + index;
        }

        continue;

      Fail:

        if (st->repeat == 0) {
            /* Simple failure. */
            ip = st->ip;
            str = st->str;
            st--;
        } else if (st->repeat > 0) {
            /* Repeat. */
            st->repeat--;
            ip = st->ip;
            str = (CHAR *)st->str + st->repeat;
        } else if (st->repeat < PAREN) {
            /* Pop a subexpression marker: If it is a start marker, restore
               previous subExpBeg value. Otherwise, simply ignore it. */
            if (st->repeat < PAREN_MIN) {
                int index = st->repeat - PAREN_MIN2;
                info->subExpBeg[index] =
                    (CHAR *)st->str - (CHAR *)info->strBeg;
            }
            st--;
            goto Fail;
        } else {
            /* Repeat backwards. */
            st->repeat++;
            ip = st->ip;
            str = (CHAR *)st->str + st->repeat;
        }
    }
}
