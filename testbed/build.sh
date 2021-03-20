#!/bin/bash
# Build script for testbed
set echo on

mkdir -p ../bin

# Get a list of all the .c files.
cFilenames=$(find . -type f -name "*.c")

# echo "Files:" $cFilenames

assembly="testbed"
compilerFlags="-g -fdeclspec -fPIC" 
# -fms-extensions 
# -Wall -Werror
includeFlags="-Isrc -I../engine/src/"
linkerFlags="-L../bin/ -lengine -Wl,-rpath,."
defines="-D_DEBUG -DKIMPORT"

echo "Building $assembly..."
echo clang $cFilenames $compilerFlags -o ../bin/$assembly $defines $includeFlags $linkerFlags
clang $cFilenames $compilerFlags -o ../bin/$assembly $defines $includeFlags $linkerFlags