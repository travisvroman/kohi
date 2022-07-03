#!/bin/bash

echo "Compiling shaders..."

pushd bin
# The tools command to build shaders.
./tools buildshaders \
../assets/shaders/Builtin.MaterialShader.vert.glsl \
../assets/shaders/Builtin.MaterialShader.frag.glsl \
../assets/shaders/Builtin.UIShader.vert.glsl \
../assets/shaders/Builtin.UIShader.frag.glsl \
../assets/shaders/Builtin.SkyboxShader.vert.glsl \
../assets/shaders/Builtin.SkyboxShader.frag.glsl \

ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

popd

echo "assets/shaders/Builtin.SkyboxShader.vert.glsl -> assets/shaders/Builtin.SkyboxShader.vert.spv"
$VULKAN_SDK/bin/glslc -fshader-stage=vert assets/shaders/Builtin.SkyboxShader.vert.glsl -o assets/shaders/Builtin.SkyboxShader.vert.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "assets/shaders/Builtin.SkyboxShader.frag.glsl -> assets/shaders/Builtin.SkyboxShader.frag.spv"
$VULKAN_SDK/bin/glslc -fshader-stage=frag assets/shaders/Builtin.SkyboxShader.frag.glsl -o assets/shaders/Builtin.SkyboxShader.frag.spv
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

echo "Done."