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


if "%PLATFORM%" == "windows" (
    SET ENGINE_LINK=-luser32
) else (
    if "%PLATFORM%" == "linux" (
        SET ENGINE_LINK=
    ) else (
        if "%PLATFORM%" == "macos" (
            SET ENGINE_LINK=
        ) else (
            echo "Unknown platform %PLATFORM%. Aborting" && exit
        )
    )
)

REM del bin\*.pdb

ECHO "%ACTION_STR% everything on %PLATFORM% (%TARGET%)..."

REM Version Generator - Build this first so it can be used later in the build process.
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=versiongen
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=engine DO_VERSION=%DO_VERSION% ADDL_LINK_FLAGS="%ENGINE_LINK%"

IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Vulkan Renderer lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=vulkan_renderer DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Iengine\src -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="-lengine -lvulkan-1 -lshaderc_shared -L%VULKAN_SDK%\Lib"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM OpenAL plugin lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=plugin_audio_openal DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Iengine\src -I'%programfiles(x86)%\OpenAL 1.1 SDK\include'" ADDL_LINK_FLAGS="-lengine -lopenal32 -L'%programfiles(x86)%\OpenAL 1.1 SDK\libs\win64'"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Standard UI lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=standard_ui DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Iengine\src" ADDL_LINK_FLAGS="-lengine"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed_lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Iengine\src -Istandard_ui\src -Iplugin_audio_openal\src" ADDL_LINK_FLAGS="-lengine -lstandard_ui -lplugin_audio_openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Executables
@REM ---------------------------------------------------

REM Testbed
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed ADDL_INC_FLAGS="-Iengine\src " ADDL_LINK_FLAGS="-lengine"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tests
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tests ADDL_INC_FLAGS=-Iengine\src ADDL_LINK_FLAGS=-lengine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tools
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=tools ADDL_INC_FLAGS=-Iengine\src ADDL_LINK_FLAGS=-lengine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
