#!/bin/bash

# Generate and test a tar.gz source release file from a git working copy.
#
# TODO:
#  - generate also a zip file (for Windows users)
#  - detect errors during build etc. and bail out
#  - perform some tests before creating the build
#  - create a report of the release
#  - store a log of status messages
#  - test documentation generation

if [ ! $1 ]
then
  echo "Usage: make_release.sh VERSION"
  echo
  echo "Create an archive containing current source files in the working"
  echo "directory, test it and store it in the release/ directory with name"
  echo "alore-src-VERSION.tar.gz."
  echo
  echo "Version identifiers of development versions are of the form YYYYMMDD"
  echo "or YYYYMMDD-N (where N is an integer starting from 2)."
  echo
  echo "This script makes no changes to the files or the repository."
  exit 1
fi

if [ ! -f ./Makefile.in ] || [ ! -d .git ]
then
  echo "You must run this script in the root directory of the Alore git"
  echo "working copy!"
  exit 1
fi

if [ ! -f ./Makefile ]
then
  echo "Makefile not present. You must run ./configure!"
  exit 1
fi

echo
echo Generating dependencies...
make depend

# FIX: check that there are no uncommitted changes in the wc

# Run make to generate build.h and check that there are no errors.
echo
echo Performing build from the working copy...
make

DEST=~/TMPX/alore-$1

if [ -d ~/TMPX ]
then
  echo "Directory ~/TMPX exists! Aborting."
  exit 2
fi

echo
echo Copying files to $DEST...

mkdir ~/TMPX
mkdir $DEST

# Copy readme and license
cp LICENSE.txt README.txt CREDITS.txt CHANGELOG.txt $DEST

# Copy build files
cp Makefile.in Makefile.depend config.h.in configure configure.ac $DEST
cp install-sh $DEST

# Copy the src directory contents.
mkdir $DEST/src
cp src/*.[chS] $DEST/src

# Copy the lib directory contents.
# FIX: Only copy finished libraries, not works-in-progress
mkdir $DEST/lib
for dir in lib/*
do
  if [ -d $dir ]
  then
    mkdir $DEST/$dir
    cp $dir/*.alo $DEST/$dir
  fi
done

# Copy the util directory contents.
mkdir $DEST/util
cp util/*.alo $DEST/util
mkdir $DEST/util/compiler
cp util/compiler/*.alo $DEST/util/compiler

# Copy the test directory contents.
for dir in `find test -xtype d -a -regex '[a-z0-9/]*'`
do
  mkdir $DEST/$dir
  for ext in .alo .txt .html .sh
  do
    if [[ `find $dir -maxdepth 1 -name "*$ext"` ]]
    then
      cp $dir/*$ext $DEST/$dir
    fi
  done
done

# Copy the doc directory contents. (NOTE: Currently mostly skipped)
mkdir $DEST/doc
cp doc/*.man $DEST/doc
##mkdir $DEST/doc/html
##cp doc/{*.html,conv.alo,conv.sh,alore.css} $DEST/doc

# Create a tar.gz archive.
DIR=`pwd`
ARCHIVE=$DIR/release/alore-src-$1.tar.gz
echo
echo Creating $ARCHIVE...
cd ~/TMPX
tar czf $ARCHIVE alore-$1

# Create a zip archive.
ARCHIVE2=$DIR/release/alore-src-$1.zip
echo
echo Creating $ARCHIVE2...
zip -rq $ARCHIVE2 alore-$1

# Extract the archive to test that it has been created correctly.
echo
echo Extracting $ARCHIVE...
rm -rf $DEST
cd ~/TMPX
tar xzf $ARCHIVE

# Build and run tests.

cd $DEST

# Run configure.
echo
echo Running configure...
./configure

# Build.
echo
echo Building...
make -j2
if [ -f alore ]
then
  # Run tests.
  echo
  echo Running tests...
  make test
fi

# Remove temporary files.
echo
echo "Removing ~/TMPX..."
rm -rf ~/TMPX

echo
echo "Done. Created release/alore-src-$1.{tar.gz,zip}"
