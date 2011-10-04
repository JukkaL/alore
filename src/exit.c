/* exit.c - Managing exit handlers that are run before program exit

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "runtime.h"
#include "gc.h"


#define EXIT_BLOCK_SIZE_INCREMENT 8


AValue *AExitHandlers;
int AExitBlockInd;
static int ExitBlockSize;


void AInitializeExitHandlers(void)
{
    AExitHandlers = NULL;
    ExitBlockSize = 0;
    AExitBlockInd = 0;
}


/* FIX: add a lock */
AValue AAddExitHandler(AThread *t, AValue *args)
{
    /* FIX: finish gc.. */
    /* FIX: perhaps use a global variable to hold this. a much cleaner
            approach.. */
    
    if (AExitBlockInd == ExitBlockSize) {
        AValue *newBuf = AGrowStatic(
            AExitHandlers,
            (ExitBlockSize + EXIT_BLOCK_SIZE_INCREMENT) * sizeof(AValue));
        if (newBuf == NULL)
            return ARaiseMemoryErrorND(t);
        
        AExitHandlers = newBuf;
        ExitBlockSize += EXIT_BLOCK_SIZE_INCREMENT;
    }
    
    AExitHandlers[AExitBlockInd++] = args[0];
    
    return ANil;
}


/* Call all registered exit handlers. Return FALSE if any of them raised an
   exception.

   FIX: Add a lock?
   
   IDEA: Exceptions could be handled better. Currently only minimal
         information is given to the user. */
ABool AExecuteExitHandlers(AThread *t)
{
    int i;
    ABool status = TRUE;

    for (i = AExitBlockInd - 1; i >= 0; i--) {
        /* FIX: thread->tempStack maybe cannot be used in this context,
                 since CallValue may modify it. */

        if (ATry(t))
            status = FALSE;
            
        if (ACallValue(t, AExitHandlers[i], 0, t->tempStack) == AError)
            status = FALSE;
        
        AEndTry(t);
    }

    AFreeStatic(AExitHandlers);

    return status;
}
