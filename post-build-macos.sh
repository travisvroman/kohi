#!/bin/bash

# Run from root directory!
mkdir -p bin/assets
mkdir -p bin/assets/shaders

echo "Compiling shaders..."

echo "assets/shaders/Builtin.MaterialShader.vert.glsl -> bin/assets/shaders/Builtin.MaterialShader.vert.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=vert assets/shaders/Builtin.MaterialShader.vert.glsl -o bin/assets/shaders/Builtin.MaterialShader.vert.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.MaterialShader.frag.glsl -> bin/assets/shaders/Builtin.MaterialShader.frag.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=frag assets/shaders/Builtin.MaterialShader.frag.glsl -o bin/assets/shaders/Builtin.MaterialShader.frag.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.UIShader.vert.glsl -> bin/assets/shaders/Builtin.UIShader.vert.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=vert assets/shaders/Builtin.UIShader.vert.glsl -o bin/assets/shaders/Builtin.UIShader.vert.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.UIShader.frag.glsl -> bin/assets/shaders/Builtin.UIShader.frag.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=frag assets/shaders/Builtin.UIShader.frag.glsl -o bin/assets/shaders/Builtin.UIShader.frag.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.SkyboxShader.vert.glsl -> bin/assets/shaders/Builtin.SkyboxShader.vert.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=vert assets/shaders/Builtin.SkyboxShader.vert.glsl -o bin/assets/shaders/Builtin.SkyboxShader.vert.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.SkyboxShader.frag.glsl -> bin/assets/shaders/Builtin.SkyboxShader.frag.spv"
$VULKAN_SDK/macOS/bin/glslc -fshader-stage=frag assets/shaders/Builtin.SkyboxShader.frag.glsl -o bin/assets/shaders/Builtin.SkyboxShader.frag.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi


echo "Copying assets..."
echo cp -R "assets" "bin"
cp -R "assets" "bin"

echo "Done."