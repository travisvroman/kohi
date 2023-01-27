#!/bin/bash
# Build script for cleaning and/or building everything
PLATFORM="$1"
ACTION="$2"
TARGET="$3"

set echo off

if [ $ACTION = "all" ] || [ $ACTION = "build" ]
then
   ACTION="all"
   ACTION_STR="Building"
   ACTION_STR_PAST="built"
   DO_VERSION="yes"
elif [ $ACTION = "clean" ]
then
   ACTION="clean"
   ACTION_STR="Cleaning"
   ACTION_STR_PAST="cleaned"
   DO_VERSION="no"
else
   echo "Unknown action $ACTION. Aborting" && exit
fi

echo "$ACTION_STR everything on $PLATFORM ($TARGET)..."

# Version Generator
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=versiongen
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# Engine
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=engine VER_MAJOR=0 VER_MINOR=1 DO_VERSION=$DO_VERSION
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# Vulkan Renderer Lib
if [ $PLATFORM = 'macos' ]
then
   VULKAN_SDK=/usr/local/
fi
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=vulkan_renderer VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-Iengine/src -I$VULKAN_SDK/include" ADDL_LINK_FLAGS="-lengine -lvulkan -L$VULKAN_SDK/lib"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# Testbed
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed ADDL_INC_FLAGS="-Iengine/src -Ivulkan_renderer/src" ADDL_LINK_FLAGS="-lengine -lvulkan_renderer"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# Tests
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=tests ADDL_INC_FLAGS="-Iengine/src" ADDL_LINK_FLAGS="-lengine"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

# Tools
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=tools ADDL_INC_FLAGS=-Iengine/src ADDL_LINK_FLAGS=-lengine
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "All assemblies $ACTION_STR_PAST successfully on $PLATFORM ($TARGET)."