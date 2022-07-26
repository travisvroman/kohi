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

echo "Done."