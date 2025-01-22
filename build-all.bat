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

SET INC_CORE_RT=-Ikohi.core\src -Ikohi.runtime\src
SET LNK_CORE_RT=-lkohi.core -lkohi.runtime

ECHO "%ACTION_STR% everything on %PLATFORM% (%TARGET%)..."

REM Version Generator - Build this first so it can be used later in the build process.
make -j -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools.versiongen
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine core lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.core DO_VERSION=%DO_VERSION% ADDL_LINK_FLAGS="-lgdi32 %ENGINE_LINK%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Engine runtime lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.runtime DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="-lkohi.core %ENGINE_LINK%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi Utils plugin lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.utils DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Vulkan Renderer plugin lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.renderer.vulkan DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -I%VULKAN_SDK%\include" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lshaderc_shared -L%VULKAN_SDK%\Lib"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM OpenAL plugin lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.audio.openal DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -I'%programfiles(x86)%\OpenAL 1.1 SDK\include'" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lopenal32 -L'%programfiles(x86)%\OpenAL 1.1 SDK\libs\win64'"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Standard UI lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.plugin.ui.standard DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Testbed lib
make -j -f "Makefile.library.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.klib DO_VERSION=%DO_VERSION% ADDL_INC_FLAGS="%INC_CORE_RT% -Ikohi.plugin.ui.standard\src -Ikohi.plugin.audio.openal\src" ADDL_LINK_FLAGS="%LNK_CORE_RT% -lkohi.plugin.ui.standard -lkohi.plugin.audio.openal"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

@REM ---------------------------------------------------
@REM Executables
@REM ---------------------------------------------------

REM Testbed
make -j -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=testbed.kapp ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tests
make -j -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.core.tests ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Tools
make -j -f "Makefile.executable.mak" %ACTION% TARGET=%TARGET% ASSEMBLY=kohi.tools ADDL_INC_FLAGS="%INC_CORE_RT%" ADDL_LINK_FLAGS="%LNK_CORE_RT%"
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO All assemblies %ACTION_STR_PAST% successfully on %PLATFORM% (%TARGET%).
