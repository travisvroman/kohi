/**
 * @file utils_plugin_defines.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Definitions for the utils plugin.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <defines.h>

#define PACKAGE_NAME_PLUGIN_UTILS "Kohi.Plugin.Utils"

#define SHADER_NAME_PLUGIN_UTILS_EDITOR_GIZMO "Shader.EditorGizmo"

#if KOHI_DEBUG
#	define KOHI_EDITOR 1
#endif
