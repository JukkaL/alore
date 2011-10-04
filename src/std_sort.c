/* std_sort.c - std::Sort function

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "std_module.h"


static ABool QSort(AThread *t, AValue *frame, int b, int e);


AValue AStdSort(AThread *t, AValue *frame)
{
    int len;
    int i;

    len = ALen(t, frame[0]);
    if (len < 0)
        return AError;

    /* Copy the items in the source sequence to an Array object. */
    frame[2] = AMakeArray(t, len);
    for (i = 0; i < len; i++) {
        AValue item = AGetItemAt(t, frame[0], i);
        if (AIsError(item))
            return AError;
        ASetArrayItem(t, frame[2], i, item);
    }

    if (!QSort(t, frame, 0, len - 1))
        return AError;
    else
        return frame[2];
}
   

static ABool QSort(AThread *t, AValue *frame, int b, int e)
{
    while (b < e) {
        int l = b + 1;
        int r = e;
        frame[3] = AArrayItem(frame[2], b);

        if (AIsDefault(frame[1])) {
            /* Use the default comparison operations. */
            while (l <= r) {
                int res = AIsLte(t, AArrayItem(frame[2], l), frame[3]);
                if (res > 0)
                    l++;
                else if (res == 0) {
                    frame[4] = AArrayItem(frame[2], r);
                    ASetArrayItem(t, frame[2], r, AArrayItem(frame[2], l));
                    ASetArrayItem(t, frame[2], l, frame[4]);
                    r--;
                } else
                    return FALSE;
            }
        } else {
            /* Use the provided comparison function. */
            while (l <= r) {
                AValue res;

                frame[4] = frame[3];
                frame[5] = AArrayItem(frame[2], l);
                res = ACallValue(t, frame[1], 2, frame + 4);
                if (AIsFalse(res))
                    l++;
                else if (AIsTrue(res)) {
                    frame[4] = AArrayItem(frame[2], r);
                    ASetArrayItem(t, frame[2], r, AArrayItem(frame[2], l));
                    ASetArrayItem(t, frame[2], l, frame[4]);
                    r--;
                } else {
                    if (!AIsError(res))
                        ARaiseValueError(t, NULL);
                    return FALSE;
                }
            }
        }
        l--;

        frame[3] = AArrayItem(frame[2], l);
        ASetArrayItem(t, frame[2], l, AArrayItem(frame[2], b));
        ASetArrayItem(t, frame[2], b, frame[3]);

        if (!QSort(t, frame, b, l - 1))
            return FALSE;
        
        b = l + 1;
        
        /* The next while iteration logically corresponds to recursive call
           QSort(frame, l + 1, e). */
    }

    return TRUE;
}
