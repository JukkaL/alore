#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(alore, (development version))
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])

AH_TOP([
#ifndef CONFIG_H_INCL
#define CONFIG_H_INCL
])

AH_BOTTOM([
#endif
])

# Checks for programs.
AC_PROG_CC
#AC_PROG_INSTALL
AC_PROG_RANLIB

# Check if building from a git working copy.
AC_MSG_CHECKING([whether we are building from a git working copy])
if { test -d .git; }; then
  is_git_wc=yes
else
  is_git_wc=no
fi
AC_MSG_RESULT([$is_git_wc])

# Check if src/build.h should be generated based on git working copy version.
AC_SUBST(VERSIONDEP)
VERSIONDEP=""
if test "$is_git_wc" == yes; then
  VERSIONDEP=buildversion
fi

# Check operating system type

AC_SUBST(PLATFORM)
AC_MSG_CHECKING([for platform])
os_type=`uname`
case $os_type in
Linux)    PLATFORM='linux';;
FreeBSD)  PLATFORM='freebsd';;
SunOS)    PLATFORM='solaris';;
MINGW*)   PLATFORM='windows';;
Darwin)   PLATFORM='osx';;
*)        PLATFORM='unknown';;
esac
AC_MSG_RESULT([$PLATFORM])

AC_DEFINE_UNQUOTED([ALORE_PLATFORM], ["$PLATFORM"],
          [Define to Alore platform identifier string.])

AC_MSG_CHECKING([for platform variant])
PLATFORM_VARIANT="$PLATFORM"
case $os_type in
SunOS)  PLATFORM_VARIANT="$PLATFORM-`uname -p`";;
*)      ;;
esac
AC_MSG_RESULT([$PLATFORM_VARIANT])

AC_DEFINE_UNQUOTED([ALORE_PLATFORM_VARIANT], ["$PLATFORM_VARIANT"],
          [Define to Alore platform variant identifier string.])

# Setup install directories. Mingw uses a special directory scheme where
# Alore is assumed to be installed in a separate directory tree that is not
# shared by other programs.

AC_SUBST(alorelibdir)
AC_SUBST(aloreincdir)
AC_SUBST(aloredatadir)
alorelibdir="${libdir}/alore"
aloreincdir="${includedir}/alore"
aloredatadir="${datadir}/alore"

AC_MSG_CHECKING([for install prefix])
# The mingw default prefix is c:/Alore instead of /usr/local.
if { test "$PLATFORM" == windows && test "$prefix" == NONE; }; then
  prefix="c:/Alore"
fi
if test "$prefix" == NONE; then
  AC_MSG_RESULT([default])
else
  AC_MSG_RESULT([$prefix])
fi

# Mingw install directories are different from other platforms.
if test "$PLATFORM" == windows; then
  bindir="${prefix}"
  alorelibdir="${libdir}"
  aloreincdir="${includedir}"
fi

# Set compiler and linker options. These are heavily platform and compiler
# dependent.

if test "$GCC" == yes; then
  CFLAGS="$CFLAGS -Wall"
fi

# Check if pthreads support must be activated with special options.
case $os_type in
Linux)   CC="$CC -pthread";;
FreeBSD) CC="$CC -pthread";;
*)       ;;
esac

# Some compilers require additional options to export symbols. This is needed
# for the main alore executable, since dynamic modules require exported
# symbols.
AC_SUBST(LD_EXPORT_FLAGS)
AC_MSG_CHECKING([for LD_EXPORT_FLAGS])
case $os_type in
Linux)   LD_EXPORT_FLAGS="-export-dynamic";;
FreeBSD) LD_EXPORT_FLAGS="-export-dynamic";;
SunOS)   LD_EXPORT_FLAGS="";;
MINGW*)  LD_EXPORT_FLAGS="-Wl,--out-implib,libalore.a";;
Darwin)  LD_EXPORT_FLAGS="";;
*)       LD_EXPORT_FLAGS="-export-dynamic";;
esac
AC_MSG_RESULT([$LD_EXPORT_FLAGS])

# LD_SHARED is the command for linking a shared library (used for linking
# dynamic modules).
AC_SUBST(LD_SHARED)
AC_MSG_CHECKING([for LD_SHARED])
case $os_type in
Linux)   LD_SHARED='$(CC) -shared';;
FreeBSD) LD_SHARED='$(CC) -shared';;
SunOS)   LD_SHARED='$(CC) -G';;
MINGW*)  LD_SHARED='$(CC) -shared';;
Darwin)  LD_SHARED='$(CC) -bundle -undefined dynamic_lookup';;
*)       LD_SHARED='$(CC) -shared';;
esac
AC_MSG_RESULT([$LD_SHARED])

# CC_SHARED_FLAGS are the additional options needed by the C compiler to
# compile objects that go into a shared library.
AC_SUBST(CC_SHARED_FLAGS)
AC_MSG_CHECKING([for CC_SHARED_FLAGS])
case $PLATFORM_VARIANT in
linux)          CC_SHARED_FLAGS="-fPIC";;
freebsd)        CC_SHARED_FLAGS="-fPIC";;
solaris-sparc)  CC_SHARED_FLAGS="-xcode=pic32";;
solaris-*)      CC_SHARED_FLAGS="-KPIC";;
windows)        CC_SHARED_FLAGS="";;
osx)            CC_SHARED_FLAGS="";;
*)              CC_SHARED_FLAGS="-fPIC";;
esac
AC_MSG_RESULT([$CC_SHARED_FLAGS])

AC_SUBST(MOD_LDFLAGS)
AC_MSG_CHECKING([for --enable-profiling])
AC_ARG_ENABLE(profiling,
	      AS_HELP_STRING([--enable-profiling],
                             [enable profiling with gprof]),
[case "${enableval}" in
 yes) profiling=yes ;;
 no)  profiling=no ;;
 *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profiling]) ;;
 esac],
[profiling=no])
AC_MSG_RESULT([$profiling])

if test "$profiling" == yes;
then
  CFLAGS="-pg $CFLAGS"
  LDFLAGS="-pg $LDFLAGS"
  MOD_LDFLAGS="-pg"
fi

# Mingw requires a special import library for building shared modules.
AC_SUBST(IMPLIB)
AC_MSG_CHECKING([for import library])
case $PLATFORM in
windows)       IMPLIB="libalore.a";;
*)	       IMPLIB="";;
esac
AC_MSG_RESULT([$IMPLIB])

# Determine shared library extension.
AC_SUBST(SOEXT)
AC_MSG_CHECKING([for suffix of shared libraries])
case $PLATFORM in
windows)       SOEXT=".dll";;
osx)           SOEXT=".so";;
*)	       SOEXT=".so";;
esac
AC_MSG_RESULT([$SO])

AC_DEFINE_UNQUOTED([DYNAMIC_C_MODULE_EXTENSION], ["$SOEXT"],
          [Define to dynamic C module file extension.])

# Determine file system case sensitivity.
AC_MSG_CHECKING(for case insensitive file names)
echo > confCaSe.x
if test -f confcAsE.X; then 
  AC_DEFINE(HAVE_CASE_INSENSITIVE_FILE_NAMES, 1,
            [Define to 1 if you have case insensitive file names.])
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi
rm -f confCaSe.x

## Check if we have a good enough x86 assembler that supports the C
## preprocessor. This is is needed by the JTI compiler support code.
#AC_MSG_CHECKING([for supported x86 assembler])
#AC_SUBST(JIT_SRC)
#JIT_SRC=
#cat >conftest.S <<_ACEOF
#/* comment */
##define REG %eax   
#        .globl Foo
#        .p2align 4,,15
#Foo:
#        pushl REG
#        movl %eax, %ebx
#        popl %eax
#        ret
#_ACEOF
#$CC -o conftest.o -c conftest.S 2>conftest.err
#rm -f conftest.S
#if { test -f conftest.o && test ! -s conftest.err; }; then
#  AC_DEFINE(HAVE_JIT_COMPILER, 1,
#            [Define to 1 if JIT compiler support is available.])
#  JIT_SRC="src/asm_module.c src/asm.c src/asm_ops.S"
#  AC_MSG_RESULT([yes])
#else
#  AC_MSG_RESULT([no])
#fi
#rm -f conftest.o conftest.err

# Checks for libraries.

AC_CHECK_LIB([m], [sin])

# At least Linux requires -ldl for dlopen.
AC_CHECK_LIB([dl], [dlopen])

# At least Solaris requires -lsocket and -lnsl for network code.
AC_CHECK_LIB([socket], socket)
AC_CHECK_LIB([nsl], t_open)

# Add additional libraries in Windows.
case $PLATFORM in
windows)       LIBS="$LIBS -lwsock32 -lmpr"
esac

# List of optional dynamically loaded C modules (those that have specific
# requirements that might not be met)
OPT_MODULES=
AC_SUBST(OPT_MODULES)

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h sys/socket.h sys/time.h unistd.h utime.h langinfo.h])

## Find PostgreSQL include files and libraries, if they exist.
#AC_PATH_PROGS(PG_CONFIG, pg_config)
#if ! test -z "$PG_CONFIG" && test -r "$PG_CONFIG"; then
#  AC_MSG_CHECKING([for PostgreSQL include directory])
#  pg_includedir=`$PG_CONFIG --includedir`
#  CFLAGS="$CFLAGS -I$pg_includedir"
#  AC_MSG_RESULT([$pg_includedir])
#  AC_MSG_CHECKING([for PostgreSQL linker options])
#  pg_libdir=`$PG_CONFIG --libdir`
#  PG_LDFLAGS="-L$pg_libdir -lpq"
#  AC_SUBST(PG_LDFLAGS)
#  AC_MSG_RESULT([$PG_LDFLAGS])
#
#  OPT_MODULES="__postgres $OPT_MODULES"
#fi

# Checks for typedefs, structures, and compiler characteristics.
#AC_TYPE_SIZE_T
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

AC_HEADER_SYS_WAIT

# Check for sizes of types
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(void *)
AC_CHECK_SIZEOF(double)
AC_CHECK_SIZEOF(size_t)
AC_CHECK_SIZEOF(ssize_t)

# Checks for library functions.
#AC_FUNC_CLOSEDIR_VOID
#AC_FUNC_MEMCMP
#AC_FUNC_MALLOC
#AC_FUNC_REALLOC
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK

# FIX This is buggy, hangs with gcc 4.3.. Probably not needed anyway.
#AC_FUNC_MKTIME

AC_FUNC_MMAP
AC_FUNC_STAT
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([getcwd gethostbyaddr gethostbyname gethostname inet_ntoa localtime_r mkdir munmap setenv socket utime])

# These C89 functions are not checked and are assumed to work:
#   floor memmove memset pow sqrt strchr
    
AC_CHECK_FUNCS([dlopen dlclose dlsym mremap gettimeofday getpwuid_r])

# The POSIX version of getpwuid_r takes 5 arguments, while the one in Solaris
# only takes 4 arguments.
#
# IDEA: Check if getpwuid_r was found. Otherwise, this can't work.
AC_MSG_CHECKING(for POSIX getpwuid_r)
AC_TRY_COMPILE([#include <sys/types.h>
                #include <pwd.h>
                #include <stdio.h>],
               [getpwuid_r((uid_t)0, NULL, NULL, (size_t)0, NULL);],
               [AC_DEFINE(HAVE_GETPWUID_R_POSIX, 1,
                          [Define to 1 if you have the POSIX `getpwuid_r'
                           function (5 arguments).])
                AC_MSG_RESULT(yes)],
               [AC_MSG_RESULT(no)])

# Check if POSIX threads are available.
AC_MSG_CHECKING(for POSIX threads)
AC_TRY_COMPILE([#include <pthread.h>
                #include <stdio.h>],
               [pthread_create(NULL, NULL, NULL, NULL);],
               [AC_DEFINE(HAVE_PTHREADS, 1,
                          [Define to 1 if you have POSIX threads.])
                AC_MSG_RESULT(yes)],
               [AC_MSG_RESULT(no)])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
