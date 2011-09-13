#!/bin/sh

# Get script directory.
DIR="$( cd "$( dirname "$0" )" && pwd )"

# Cd to script directory, since test cases may assume this.
cd "$DIR"

# Include type checker modules in the module search path.
export ALOREPATH=../check

# Run test cases.
../alore test.alo "$@"
