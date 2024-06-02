/**
 * @file vulkan_renderer_plugin_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Hosts creation and destruction methods for the renderer backend.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include <plugins/plugin_types.h>

/**
 * @brief Creates a new runtime plugin of the renderer type.
 *
 * @param out_plugin A pointer to hold the newly-created renderer plugin.
 * @return True if successful; otherwise false.
 */
KAPI b8 kplugin_create(kruntime_plugin* out_plugin);

/**
 * @brief Destroys the given plugin.
 *
 * @param plugin A pointer to the runtime plugin to be destroyed.
 */
KAPI void kplugin_destroy(kruntime_plugin* plugin);
