#!/bin/bash
# Build script for cleaning and/or building everything
PLATFORM="$1"
ACTION="$2"
TARGET="$3"

set echo off

txtgrn=$(echo -e '\e[0;32m')
txtred=$(echo -e '\e[0;31m')
txtrst=$(echo -e '\e[0m')

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
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Engine
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=engine VER_MAJOR=0 VER_MINOR=5 DO_VERSION=$DO_VERSION
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Vulkan Renderer Lib
if [ $PLATFORM = 'macos' ]
then
   VULKAN_SDK=/usr/local/
fi
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=vulkan_renderer VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-I./engine/src -I$VULKAN_SDK/include" ADDL_LINK_FLAGS="-lengine -lvulkan -lshaderc_shared -L$VULKAN_SDK/lib"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Standard UI Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=standard_ui VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-I./engine/src" ADDL_LINK_FLAGS="-lengine"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# OpenAL Audio Plugin lib
if [ $PLATFORM = 'macos' ]
then
    OPENAL_INC=-I/opt/homebrew/opt/openal-soft/include/
    OPENAL_LIB=-L/opt/homebrew/opt/openal-soft/lib/
fi
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=plugin_audio_openal VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-I./engine/src $OPENAL_INC" ADDL_LINK_FLAGS="-lengine -lopenal $OPENAL_LIB"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Testbed Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed_lib VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-I./engine/src -I./standard_ui/src -I./plugin_audio_openal/src" ADDL_LINK_FLAGS="-lengine -lstandard_ui -lplugin_audio_openal"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Testbed
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed ADDL_INC_FLAGS="-I./engine/src" ADDL_LINK_FLAGS="-lengine -lstandard_ui -lplugin_audio_openal"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# # Tests
# make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=tests ADDL_INC_FLAGS="-I./engine/src" ADDL_LINK_FLAGS="-lengine"
# ERRORLEVEL=$?
# if [ $ERRORLEVEL -ne 0 ]
# then
# echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
# fi

# Tools
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=tools ADDL_INC_FLAGS="-I./engine/src" ADDL_LINK_FLAGS="-lengine"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

echo "All assemblies $ACTION_STR_PAST successfully on $PLATFORM ($TARGET)." | sed -e "s/successfully/${txtgrn}successfully${txtrst}/g"

