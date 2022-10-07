@ECHO OFF
REM Build script for cleaning and/or building everything

SET PLATFORM=%1
SET ACTION=%2
SET TARGET=%3


if "%ACTION%" == "build" (
    SET ACTION=all
    SET ACTION_STR=Building
    SET ACTION_STR_PAST=built
    SET DO_VERSION=yes
) else (
    if "%ACTION%" == "clean" (
        SET ACTION=clean
        SET ACTION_STR=Cleaning
        SET ACTION_STR_PAST=cleaned
        SET DO_VERSION=no
    ) else (
        echo "Unknown action %ACTION%. Aborting" && exit
    )
)

ECHO "%ACTION_STR% everything on %PLATFORM% (%TARGET%)..."

REM Version Generator
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=versiongen
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine
make -f "Makefile.engine.mak" %ACTION% TARGET=%TARGET% VER_MAJOR=0 VER_MINOR=1 DO_VERSION=%DO_VERSION%
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tests
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tests
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tools
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tools
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
