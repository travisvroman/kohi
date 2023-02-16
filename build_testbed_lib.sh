#!/bin/bash
# Testbed Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed_lib VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-Iengine/src" ADDL_LINK_FLAGS="-lengine"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi