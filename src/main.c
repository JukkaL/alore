/* main.c - The main function of the Alore interpreter "alore"

   Copyright (c) 2010-2011 Jukka Lehtosalo

   Alore is licensed under the terms of the MIT license.  See the file
   LICENSE.txt in the distribution.
*/

#include "alore.h"
#include "compile.h"
#include "debug_runtime.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>


/* Usage string shown at the start of cmd line argument help message. */
#define USAGE "Usage: alore [option] ... programfile [arg] ..."


/* Representation of command line option values */
typedef struct {
    /* Maximum size of the Alore heap */
    Asize_t maxHeap;
    /* Perform type check and exit? */
    ABool typeCheckAndExit;
    /* Perform type before running program? */
    ABool typeCheckAndRun;

    /* Debugging-related option values */
    ABool displayCode;
} AOptions;


static void ParseOptions(int *argcp, char ***argvp, AOptions *options);
static void InitializeOptions(AOptions *options);
static void InvalidOption(const char *opt);
static void InvalidOptionValue(const char *opt, const char *value);
static void ShowHelp(void);
static void ShowVersion(void);
static void DisplayCode(void);
static int PerformTypeCheck(void);
static void GetCheckerPath(char *path);
static void SetDebugInstructionCounter(void);
static ABool ParseSize(const char *s, Asize_t *size);


/* The main function of the Alore interpreter. */
int main(int argc, char **argv)
{
    int num;
    int returnValue;
    AThread *t;
    char *file;
    char *interp;
    AOptions options;

    /* Store and skip interpreter program path (argument 0). */
    interp = argv[0];
    argv++;
    argc--;

    /* Parse command line options. */
    ParseOptions(&argc, &argv, &options);
    
    if (argc >= 1) {
        /* Get the name of the program to run. */
        file = argv[0];
        argv++;
        argc--;
    } else {
        /* Program name missing. Display error and exit. */
        InvalidOption(NULL);
        /* Get rid of warning (never executed). */
        file = NULL;
    }
    
    returnValue = 0;

    /* Compile the program. The return value is the global num of the function
       that runs the program. */
    num = ALoadAloreProgram(&t, file, interp, "", FALSE, argc, argv, NULL,
                            options.maxHeap);

    /* Was compilation successful? */
    if (num >= 0) {
        /* Yes. */
        if (options.displayCode)
            DisplayCode();
        else {
            /* Do we need to type check the program? */
            if (options.typeCheckAndRun || options.typeCheckAndExit)
                returnValue = PerformTypeCheck();
            
            /* Run the program unless there was a type check error or the
               user requested to exit after type checking. */
            if (!options.typeCheckAndExit && returnValue == 0) {
                AValue val;
                SetDebugInstructionCounter();
            
                if (ATry(t)) {
                    /* An uncaught direct exception was raised somewhere in the
                       program. We must not call AEndTry. */
                    val = AError;
                } else {
                    /* Run the program. */
                    val = ACallValue(t, AGlobalByNum(num), 0, NULL);
                    AEndTry(t);
                }
            
                returnValue = AEndAloreProgram(t, val);
            }
        }
    } else {
        /* Compilation failed. Return error status. */
        returnValue = 1;
    }
    
    return returnValue;
}


/* Parse command line options. Update *argcp and *argvp to point to the first
   positional argument. Fully initialize and populate the options struct.

   Note that some options are only available in builds with extra debugging
   features. These may directly modify the ADebug* configuration variables.

   This function does not return if one of options is invalid or is completely
   processed by this function (e.g. -v). */
static void ParseOptions(int *argcp, char ***argvp, AOptions *options)
{
    int argc = *argcp;
    char **argv = *argvp;
#ifdef A_DEBUG
    ABool checksDefined = FALSE;
    ABool checkFirstDefined = FALSE;
#endif

    InitializeOptions(options);

    /* Parse options; advance argc and argv. */
    while (argc > 0 && argv[0][0] == '-') {
        switch (argv[0][1]) {
#ifdef A_DEBUG
        case 'd':
            /* Display code and exit. */
            options->displayCode = TRUE;
            break;
#endif
        case 'v':
            ShowVersion();
            break;

        case 'c':
            /* Perform type checking and exit. */
            options->typeCheckAndExit = TRUE;
            break;

        case 't':
            /* Perform type checking before running the program. */
            options->typeCheckAndRun = TRUE;
            break;            

        case '-':
            /* Long argument with -- prefix. */
            if (strcmp(argv[0], "--version") == 0) {
                ShowVersion();
                break;
            } else if (strcmp(argv[0], "--max-heap") == 0 && argc > 1) {
                /* Set the maximum size of the heap. */
                ABool status = ParseSize(argv[1], &options->maxHeap);
                if (!status)
                    InvalidOptionValue(argv[0], argv[1]);
                argv++;
                argc--;
                break;
            } else
                InvalidOption(argv[0]);
            
#ifdef A_DEBUG
        case 'm':
            /* Memory dump (IDEA: explain) */
            ADebug_MemoryDump = 1;
            break;
            
        case 't':
            /* Memory trace (IDEA: explain) */
            ADebug_MemoryTrace = 1;
            break;
            
        case 'a': {
            /* Set up debug watch location (IDEA: explain) */
            unsigned long ptr;
            argv++;
            argc--;

            if (argc > 0 && sscanf(argv[0], "%lx", &ptr) == 1) {
                ADebug_WatchLocation = (void *)ptr;
                break;
            }
        }

        case 'D':
            /* Set periodic instruction checking options. */
            checksDefined = TRUE;
            switch (argv[0][2]) {
            case 'n':
                /* Debug every Nth instruction. */
                argv++;
                argc--;
                ADebugCheckEveryNth = atoi(argv[0]);
                if (ADebugCheckEveryNth < 1)
                    ADebugCheckEveryNth = 1;
                break;
            case 'f':
                /* Set first instruction to check. */
                argv++;
                argc--;
                ADebugCheckFirst = atoi(argv[0]);
                checkFirstDefined = TRUE;
                break;
            case 'l':
                /* Set last instruction to check. */
                argv++;
                argc--;
                ADebugCheckLast = atoi(argv[0]);
                break;
            default:
                InvalidOption(argv[0]);
            }
            break;
#endif
        default:
            InvalidOption(argv[0]);
        }
        
        argv++;
        argc--;
    }

#ifdef A_DEBUG
    /* If any periodic check related parameters are defined, also update
       ADebugCheckFirst (otherwise the parameters would have no effect). */
    if (checksDefined && !checkFirstDefined) {
#ifdef A_DEBUG_COMPILER
        ADebugCheckFirst = 0;
#else
        ADebugCheckFirst = 1;
#endif
    }
#endif

    /* Update caller's argument information. */
    *argcp = argc;
    *argvp = argv;
}


/* Initialize options struct to default values. */
static void InitializeOptions(AOptions *options)
{
    options->maxHeap = 0;
    options->typeCheckAndExit = FALSE;
    options->typeCheckAndRun = FALSE;
    options->displayCode = FALSE;
}


/* Dislay error message about invalid option. Also show general help
   message. */
static void InvalidOption(const char *opt)
{
    if (opt != NULL)
        fprintf(stderr, "alore: Invalid option %s\n\n", opt);
    ShowHelp();
}


/* Display error message about invalid value for an option. */
static void InvalidOptionValue(const char *opt, const char *value)
{
    if (opt != NULL)
        fprintf(stderr, "alore: Invalid value %s for option %s\n\n", value,
                opt);
    ShowHelp();
}


/* Show command line option help and exit with code 1. */
static void ShowHelp(void)
{
    fprintf(stderr, "%s\n", USAGE);
    fprintf(stderr, "Options and arguments:\n"
            "  -c             type check program and exit\n"
            "  -t             type check program before running it\n"
            "  -v, --version  show version information and exit\n"
#ifdef A_DEBUG
            "  -d             DEBUG: dump bytecode and exit\n"
            "  -m             DEBUG: memory dump\n"
            "  -t             DEBUG: memory trace\n"
            "  -Dn N          DEBUG: check every Nth instruction\n"
            "  -Df N          DEBUG: specify first checked instruction\n"
            "  -Dl N          DEBUG: specify last checked instruction\n"
#endif
            "  arg ...        arguments passed to Main (also in sys::Args)\n");
    fprintf(stderr, "Environment variables:\n"
            "  ALOREPATH      directories prefixed to the default module search path,\n"
            "                 separated by '%c'\n", A_PATH_SEPARATOR);
    exit(1);
}


/* Show version number and exit with code 0. */
static void ShowVersion(void)
{
    printf("Alore %s\n"
           "Copyright (c) 2010-2011 Jukka Lehtosalo\n", AVersion);
    exit(0);
}


/* Dump all compiled code and exit. Only supported in debug builds. */
static void DisplayCode(void)
{
#ifdef A_DEBUG
    ADisplayCode();
#else
    AEpicInternalFailure("Request to display code on non-debug build");
#endif
}


/* Type checking the target program. Assume that the program has been
   loaded already. Return 0 if successful and a positive value if there were
   errors or if failed to run the type checker.

   This function may display and error message and halt the program if
   something unexpected happens. */
static int PerformTypeCheck(void)
{
    char checkerPath[A_MAX_PATH_LEN];
    char cmd[A_MAX_PATH_LEN * 3 + 10];
    int status;

    GetCheckerPath(checkerPath);

    /* FIX: Support spaces in path names properly. */

    /* Build the command line for running the checker. */
    sprintf(cmd, "%s %s %s", AInterpreterPath, checkerPath, AProgramPath);
    
    status = system(cmd);

    /* If the command did not return successfully, something unexpected
       happened; perhaps somebody killed the type checker. In these
       cases display a generic error message and exit. */
    if (!WIFEXITED(status)) {
        fprintf(stderr, "alore: Running type checker failed");
        exit(2);
    }

    /* Return the exit status of the type checker. */
    return WEXITSTATUS(status);
}


/* Determine the path to the type checker. */
static void GetCheckerPath(char *path)
{
    /* FIX: Cross-platform support */
    /* FIX: Length checks */
    /* FIX: It should be possible to run this when installed. */
    int i;
    
    strcpy(path, AInterpreterPath);
    for (i = strlen(path); i > 1 && !A_IS_DIR_SEPARATOR(path[i - 1]); i--)
        ;

    path[i] = '\0';
    strcat(path, "check/check.alo");
}


/* Initialize ADebugInstructionCounter in debug builds. Otherwise, do
   nothing. */
static void SetDebugInstructionCounter(void)
{
#ifdef A_DEBUG_COMPILER
    ADebugInstructionCounter = 1000000;
#endif
}


/* Parse sizes such as 4M or 123k and store them in *size.

   NOTE: Does not check overflow on 64-bit systems. */
static ABool ParseSize(const char *s, Asize_t *size)
{
    char *end;
    long sval = strtol(s, &end, 10);
    Asize_t val;

    if (end == s || sval < 0) {
        return FALSE;
    }

    val = sval;
    if (*end == 'k') {
        if (SIZEOF_LONG == 4 && val >= 4096 * 1024)
            return FALSE;
        val *= 1024;
        end++;
    } else if (*end == 'M') {
        if (SIZEOF_LONG == 4 && val >= 4096)
            return FALSE;
        val *= 1024 * 1024;
        end++;
    } else if (*end == 'G') {
        /* Handle 4G as a special case, as otherwise it would overflow. */
        if (SIZEOF_LONG == 4 && val == 4)
            val = 0xffffffffUL;
        else if (SIZEOF_LONG == 4 && val > 4)
            return FALSE;
        else
            val *= 1024 * 1024 * 1024;
        end++;
    }

    if (*end != '\0')
        return FALSE;

    *size = val;
    return TRUE;
}
