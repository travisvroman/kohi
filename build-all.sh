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

INC_CORE_RT="-I./kohi.core/src -I./kohi.runtime/src"
LNK_CORE_RT="-lkohi.core -lkohi.runtime"

echo "$ACTION_STR everything on $PLATFORM ($TARGET)..."

# Version Generator - Build this first so it can be used later in the build process.
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.tools.versiongen
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Kohi Core
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.core DO_VERSION=$DO_VERSION
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Tools NOTE: Building tools here since it's required below.
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.tools ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="-lkohi.core"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Kohi Runtime
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.runtime DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="-lkohi.core"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Kohi Utils plugin Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.utils DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT"
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
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.renderer.vulkan DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I../bin/ -I$VULKAN_SDK/include" ADDL_LINK_FLAGS="$LNK_CORE_RT -lshaderc_shared "
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Standard UI Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.ui.standard DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT"
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
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.plugin.audio.openal DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT $OPENAL_INC" ADDL_LINK_FLAGS="$LNK_CORE_RT -lopenal $OPENAL_LIB"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "error:"$errorlevel | sed -e "s/error/${txtred}error${txtrst}/g" && exit
fi

# Testbed Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.klib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src -I./kohi.plugin.utils/src" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Overdrive2069 Lib
#make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=overdrive2069.klib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src -I./kohi.plugin.utils/src" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils"
#ERRORLEVEL=$?
#if [ $ERRORLEVEL -ne 0 ]
#then
#echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
#fi

# Shadows of Illumina Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=soi.klib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="$INC_CORE_RT -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src -I./kohi.plugin.utils/src" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal -lkohi.plugin.utils"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# ---------------------------------------------------
# Executables
# ---------------------------------------------------

# Testbed
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.kapp ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Overdrive 2069 Game Executable
# make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=overdrive2069.kapp ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
# ERRORLEVEL=$?
# if [ $ERRORLEVEL -ne 0 ]
# then
# echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
# fi

# Shadows of Illumina Game Executable
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=soi.kapp ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi

# Tests
make -f Makefile.executable.mak $ACTION TARGET=$TARGET ASSEMBLY=kohi.core.tests ADDL_INC_FLAGS="$INC_CORE_RT" ADDL_LINK_FLAGS="$LNK_CORE_RT"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi


echo "All assemblies $ACTION_STR_PAST successfully on $PLATFORM ($TARGET)." | sed -e "s/successfully/${txtgrn}successfully${txtrst}/g"

