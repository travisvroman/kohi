@echo off

echo "Compiling shaders..."

PUSHD bin
tools.exe buildshaders ^
..\assets\shaders\Builtin.MaterialShader.vert.glsl ^
..\assets\shaders\Builtin.MaterialShader.frag.glsl ^
..\assets\shaders\Builtin.UIShader.vert.glsl ^
..\assets\shaders\Builtin.UIShader.frag.glsl ^
..\assets\shaders\Builtin.SkyboxShader.vert.glsl ^
..\assets\shaders\Builtin.SkyboxShader.frag.glsl ^
..\assets\shaders\Builtin.UIPickShader.vert.glsl ^
..\assets\shaders\Builtin.UIPickShader.frag.glsl ^
..\assets\shaders\Builtin.WorldPickShader.vert.glsl ^
..\assets\shaders\Builtin.WorldPickShader.frag.glsl ^
..\assets\shaders\Builtin.TerrainPickShader.vert.glsl ^
..\assets\shaders\Builtin.TerrainPickShader.frag.glsl ^
..\assets\shaders\Builtin.TerrainShader.vert.glsl ^
..\assets\shaders\Builtin.TerrainShader.frag.glsl ^

IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

POPD


echo "Done."
