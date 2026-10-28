/**
 * @file runtime_defines.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Definitions having to do with the Kohi Runtime.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

// The standard name of the runtime package.
#define PACKAGE_NAME_RUNTIME "Kohi.Runtime"

// Shader that takes position and colour in vertex data.
#define SHADER_NAME_RUNTIME_COLOUR_3D "Shader.Colour3D"
// Shader that takes position-only vertices, and defines colour in UBO data.
#define SHADER_NAME_RUNTIME_DEBUG_3D "Shader.Debug3D"

#define SHADER_NAME_RUNTIME_DEPTH_PREPASS "Shader.Depth_Prepass"

#define SHADER_NAME_RUNTIME_MATERIAL_STANDARD "Shader.MaterialStandard"
#define SHADER_NAME_RUNTIME_MATERIAL_WATER "Shader.MaterialWater"
#define SHADER_NAME_RUNTIME_MATERIAL_BLENDED "Shader.MaterialBlended"

#define SHADER_NAME_RUNTIME_SHADOW_MODEL "Shader.Shadow_Model"
#define SHADER_NAME_RUNTIME_SHADOW_TERRAIN "Shader.Shadow_Terrain"
#define SHADER_NAME_RUNTIME_SHADOW_HF_TERRAIN "Shader.Shadow_HFTerrain"

#define SHADER_NAME_RUNTIME_SKYBOX "Shader.Skybox"

#define SHADER_NAME_RUNTIME_HF_TERRAIN "Shader.HF_Terrain"
