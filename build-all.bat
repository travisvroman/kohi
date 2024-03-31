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
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools.versiongen
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.engine DO_VERSION=%DO_VERSION% ADDL_LINK_FLAGS="%ENGINE_LINK%"

IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Vulkan Renderer plugin lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.renderer.vulkan DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Ikohi.engine\src -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="-lkohi.engine -lvulkan-1 -lshaderc_shared -L%VULKAN_SDK%\Lib"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM OpenAL plugin lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.audio.openal DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Ikohi.engine\src -I'%programfiles(x86)%\OpenAL 1.1 SDK\include'" ADDL_LINK_FLAGS="-lkohi.engine -lopenal32 -L'%programfiles(x86)%\OpenAL 1.1 SDK\libs\win64'"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Standard UI lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.ui.standard DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Ikohi.engine\src" ADDL_LINK_FLAGS="-lkohi.engine"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed lib
make -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.lib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="-Ikohi.engine\src -Ikohi.plugin.ui.standard\src -Ikohi.plugin.audio.openal\src" ADDL_LINK_FLAGS="-lkohi.engine -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Executables
@REM ---------------------------------------------------

REM Testbed
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.app ADDL_INC_FLAGS=-Ikohi.engine\src ADDL_LINK_FLAGS=-lkohi.engine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tests
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.engine.tests ADDL_INC_FLAGS=-Ikohi.engine\src ADDL_LINK_FLAGS=-lkohi.engine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tools
make -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools ADDL_INC_FLAGS=-Ikohi.engine\src ADDL_LINK_FLAGS=-lkohi.engine
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
