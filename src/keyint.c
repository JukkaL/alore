/* keyint.c - Keyboard interrupt handling

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "thread_athread.h"
#include "internal.h"


#if defined(A_HAVE_POSIX) && !defined(A_NO_KEYBOARD_INTERRUPT)

#include <signal.h>

static void KeyboardInterruptHandler(int sig)
{
    AIsInterrupt = 1;
    AIsKeyboardInterrupt = 1;
}


ABool ASetKeyboardInterruptHandler(void)
{
    struct sigaction act;
    struct sigaction oldAct;

    /* Query the current SIGINT handler. */
    if (sigaction(SIGINT, NULL, &oldAct) < 0)
        return FALSE;

    /* If SIGINT is ignored, do not setup a new handler. */
    if (oldAct.sa_handler == SIG_IGN)
        return TRUE;

    /* Activate our SIGINT handler. */

    if (sigemptyset(&act.sa_mask) < 0)
        return FALSE;

    act.sa_handler = KeyboardInterruptHandler;
    act.sa_flags = 0;

    if (sigaction(SIGINT, &act, NULL) < 0)
        return FALSE;

    return TRUE;
}

#elif defined(A_HAVE_WINDOWS) && !defined(A_NO_KEYBOARD_INTERRUPT)

#include <windows.h>

BOOL WINAPI KeyboardInterruptHandler(DWORD type)
{
    if (type == CTRL_C_EVENT) {
        AIsInterrupt = 1;
        AIsKeyboardInterrupt = 1;
        return TRUE;
    } else
        return FALSE;
}

ABool ASetKeyboardInterruptHandler(void)
{
    if (!SetConsoleCtrlHandler(KeyboardInterruptHandler, TRUE))
        return FALSE;

    return TRUE;
}

#else

/* No keyboard interrupt handler. */
ABool ASetKeyboardInterruptHandler(void)
{
    return TRUE;
}

#endif
