REM Builds only the testbed library

SET INC_CORE_RT=-Ikohi.core\src -Ikohi.runtime\src
SET LNK_CORE_RT=-lkohi.core -lkohi.runtime

REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.klib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -Ikohi.plugin.ui.standard\src -Ikohi.plugin.audio.openal\src" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)
