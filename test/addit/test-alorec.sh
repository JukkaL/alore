#!/bin/sh

# Test alorec. Test both the version at the build directory and the installed
# version.
#
# Preconditions:
#  - This must be run in the main Alore directory which contains configure etc.
#  - You must have done "make clean; ./configure; make; make install"

# FIX test return value from compiled program
# FIX test command line arguments in a compiled program

TMPDIR=/tmp

if test ! -f ./alore; then
  echo 'ERROR: ./alore missing. Did you run make?'
  exit 1
fi

if test -f test/test; then
  echo 'ERROR: test/test exists. Did you run "make clean"?'
  exit 1
fi

which alorec > /dev/null
if test $? != 0; then
  echo ERROR: alorec not in PATH. Did you run "make install"?
  exit 1
fi

cd test

#export ALOREPATH=../check

## Compile unit tests.
#echo Compiling unit tests...
#../alorec test.alo
#if test $? != 0; then
#  echo ERROR: ../alorec test.alo failed
#  exit 1
#fi

## Run a selection of unit tests (currently only a subset of all test cases).
#echo Running unit tests...
#./test "testI*"
#if test $? != 0; then
#  echo ERROR: Compiled test.alo failed
#  exit 1
#fi
#rm test

# Create a test application in a separate directory and compile it using
# the alorec available at PATH.
DIR=$TMPDIR/xxxaloretemp
if test -d $DIR; then
  echo ERROR: $DIR exists
  exit 1
fi
mkdir $DIR
FILEPATH=$DIR/hello.alo
echo 'import time, socket, re, encodings, base64' > $FILEPATH
echo 'def Main()' >> $FILEPATH
echo '  WriteLn("hello, world")' >> $FILEPATH
echo 'end' >> $FILEPATH
(cd $DIR; alorec hello.alo; ./hello >output)
if test "`cat $DIR/output`" != "hello, world"; then
  echo ERROR: Unexpected output from hello.alo
  exit 1
fi
rm -rf $DIR

echo
echo All done.
