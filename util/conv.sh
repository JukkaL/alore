#!/bin/sh

# Build HTML documentation.

cd doc

# Determine version string (either a git revision or release string).
VERSION=`egrep "^#define (A_VERSION|A_SNAPSHOT)" ../src/version.h | \
            sed 's/^.*"\(.*\)".*/\1/g'`
VERSION_STR="Release $VERSION"
if [ x$VERSION = x ]; then
  VERSION=`grep "^#define A_BUILD" ../build.h | \
              sed 's/^.*"\(.*\)".*/\1/g'`
  VERSION_STR="Build $VERSION"
fi

export ALOREPATH=../check

../alore ../util/conv.alo index.html html "$VERSION_STR"
