@echo off

echo "Compiling shaders..."

PUSHD bin
tools.exe buildshaders ^
..\assets\shaders\Builtin.MaterialShader.vert.glsl ^
..\assets\shaders\Builtin.MaterialShader.frag.glsl ^
..\assets\shaders\Builtin.UIShader.vert.glsl ^
..\assets\shaders\Builtin.UIShader.frag.glsl ^
..\assets\shaders\Builtin.UIShader.vert.glsl ^
..\assets\shaders\Builtin.UIShader.frag.glsl ^
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

POPD


echo "assets/shaders/Builtin.SkyboxShader.vert.glsl -> assets/shaders/Builtin.SkyboxShader.vert.spv"
%VULKAN_SDK%\bin\glslc.exe -fshader-stage=vert assets/shaders/Builtin.SkyboxShader.vert.glsl -o assets/shaders/Builtin.SkyboxShader.vert.spv
IF %ERRORLEVEL% NEQ 0 (echo Error: %ERRORLEVEL% && exit)

echo "assets/shaders/Builtin.SkyboxShader.frag.glsl -> assets/shaders/Builtin.SkyboxShader.frag.spv"
%VULKAN_SDK%\bin\glslc.exe -fshader-stage=frag assets/shaders/Builtin.SkyboxShader.frag.glsl -o assets/shaders/Builtin.SkyboxShader.frag.spv
IF %ERRORLEVEL% NEQ 0 (echo Error: %ERRORLEVEL% && exit)


echo "Done."