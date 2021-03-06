#!/bin/sh

# Generate the list of exported symbols in libalorec.a
#
# NOTE: Some Windows-only symbols are missing from the list (athread*,
#   AGetWinsockErrorMessage).

OUTFILE='util/compiler/exports.alo'

echo '-- DO NOT EDIT; automatically generated by gen-exports.sh' > $OUTFILE
echo 'module compiler' >> $OUTFILE
echo >> $OUTFILE
echo 'private const Exports = [' >> $OUTFILE

# Find all A_APIFUNC declarations (assume that they follow a certain format).
grep "^A_APIFUNC" */*.h | \
    grep -v "athread" | \
    grep -v "AGetWinsockErrorMessage" | \
    sed 's/\([a-zA-Z0-9_]*\)(/##\1==/g' | \
    sed 's/.*##/  "/g' | sed 's/==.*/",/g' | sort >> $OUTFILE

echo >> $OUTFILE

# Find all A_APIVAR declarations.
grep -h "^A_APIVAR .*;" */*.h | \
    sed 's/^\([ a-zA-Z0-9_*]*[ *]\)\([a-zA-Z_][a-zA-Z0-9_]*\)[;\[].*/  "\2",/g' | \
    sort >> $OUTFILE

echo ']' >> $OUTFILE
