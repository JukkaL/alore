#!/bin/sh

# Test running a script using the installed Alore interpreter.
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

which alore > /dev/null
if test $? != 0; then
  echo ERROR: alore not in PATH. Did you run "make install"?
  exit 1
fi

DIR=$TMPDIR/xxyaloretemp
if test -d $DIR; then
  echo ERROR: $DIR exists
  exit 1
fi
mkdir $DIR
FILEPATH=$DIR/hello.alo
echo 'import time, socket, re, encodings, base64' > $FILEPATH
echo 'WriteLn("hello")' >> $FILEPATH
(cd $DIR; alore hello.alo >output)
if test "`cat $DIR/output`" != "hello"; then
  echo ERROR: Unexpected output from hello.alo
  cat $DIR/output
  exit 1
fi
rm -rf $DIR
echo "All done."
