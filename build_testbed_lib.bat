REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed_lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Iengine\src -Istandard_ui\src -Iplugin_audio_openal\src" ADDL_LINK_FLAGS="-lengine -lstandard_ui -lplugin_audio_openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)
