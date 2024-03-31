/**
 * @file vulkan_renderer_plugin_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Hosts creation and destruction methods for the renderer backend.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include <renderer/renderer_types.h>

/**
 * @brief Creates a new renderer plugin of the given type.
 * 
 * @param out_renderer_backend A pointer to hold the newly-created renderer plugin.
 * @return True if successful; otherwise false.
 */
KAPI b8 plugin_create(renderer_plugin* out_plugin);

/**
 * @brief Destroys the given renderer backend.
 * 
 * @param renderer_backend A pointer to the plugin to be destroyed.
 */
KAPI void plugin_destroy(renderer_plugin* plugin);
