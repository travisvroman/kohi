#!/bin/bash
# Testbed Lib
make -f Makefile.library.mak $ACTION TARGET=$TARGET ASSEMBLY=testbed.lib DO_VERSION=$DO_VERSION ADDL_INC_FLAGS="-I./kohi.engine/src -I./kohi.plugin.ui.standard/src -I./kohi.plugin.audio.openal/src" ADDL_LINK_FLAGS="-lkohi.engine -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL | sed -e "s/Error/${txtred}Error${txtrst}/g" && exit
fi
