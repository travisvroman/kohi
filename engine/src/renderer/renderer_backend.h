/**
 * @file renderer_backend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Hosts creation and destruction methods for the renderer backend.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer_types.inl"

/**
 * @brief Creates a new renderer backend of the given type.
 * 
 * @param type The type of backend to create (e.g. Vulkan, OpenGL, DirectX)
 * @param out_renderer_backend A pointer to hold the newly-created renderer backend.
 * @return True if successful; otherwise false.
 */
b8 renderer_backend_create(renderer_backend_type type, renderer_backend* out_renderer_backend);

/**
 * @brief Destroys the given renderer backend.
 * 
 * @param renderer_backend A pointer to the backend to be destroyed.
 */
void renderer_backend_destroy(renderer_backend* renderer_backend);
