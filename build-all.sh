#!/bin/bash
# Build script for rebuilding everything
set echo on

echo "Building everything..."


# pushd engine
# source build.sh
# popd
make -f Makefile.engine.linux.mak all

ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# pushd testbed
# source build.sh
# popd

make -f Makefile.testbed.linux.mak all
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

make -f Makefile.tests.linux.mak all
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "All assemblies built successfully."