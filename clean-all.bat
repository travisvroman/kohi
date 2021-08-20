@ECHO OFF
REM Clean Everything

ECHO "Cleaning everything..."

REM Engine
make -f "Makefile.engine.windows.mak" clean
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed
make -f "Makefile.testbed.windows.mak" clean
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO "All assemblies cleaned successfully."