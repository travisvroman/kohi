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
../assets/shaders/Builtin.UIPickShader.vert.glsl \
../assets/shaders/Builtin.UIPickShader.frag.glsl \
../assets/shaders/Builtin.WorldPickShader.vert.glsl \
../assets/shaders/Builtin.WorldPickShader.frag.glsl \
../assets/shaders/Builtin.TerrainPickShader.vert.glsl \
../assets/shaders/Builtin.TerrainPickShader.frag.glsl \
../assets/shaders/Builtin.TerrainShader.vert.glsl \
../assets/shaders/Builtin.TerrainShader.frag.glsl \
../assets/shaders/Builtin.Colour3DShader.vert.glsl \
../assets/shaders/Builtin.Colour3DShader.frag.glsl \
../assets/shaders/Builtin.WireframeShader.vert.glsl \
../assets/shaders/Builtin.WireframeShader.frag.glsl
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
echo "Error:"$ERRORLEVEL && exit
fi

popd

echo "Done."
