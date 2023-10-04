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

del bin\*.pdb

ECHO "%ACTION_STR% everything on %PLATFORM% (%TARGET%)..."

REM Version Generator
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=versiongen
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=engine VER_MAJOR=0 VER_MINOR=1 DO_VERSION=%DO_VERSION%
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Vulkan Renderer lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=vulkan_renderer VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-Iengine\src -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="-lengine -lvulkan-1 -L%VULKAN_SDK%\Lib"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM OpenAL plugin lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=plugin_audio_openal VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-Iengine\src -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="-lengine -lopenal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed_lib VER_MAJOR=0 VER_MINOR=1 DO_VERSION=no ADDL_INC_FLAGS="-Iengine\src" ADDL_LINK_FLAGS="-lengine"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed ADDL_INC_FLAGS="-Iengine\src" ADDL_LINK_FLAGS="-lengine"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tests
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tests ADDL_INC_FLAGS=-Iengine\src ADDL_LINK_FLAGS=-lengine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tools
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tools ADDL_INC_FLAGS=-Iengine\src ADDL_LINK_FLAGS=-lengine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
