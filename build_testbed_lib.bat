REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Ikohi.engine\src -Ikohi.plugin.ui.standard\src -Ikohi.plugin.audio.openal\src" ADDL_LINK_FLAGS="-lkohi.engine -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)
