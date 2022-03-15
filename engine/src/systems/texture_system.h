/**
 * @file texture_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the texture system, which handles the acquisition
 * and releasing of textures. It also reference monitors textures, and can
 * auto-release them when they no longer have any references, if configured to
 * do so.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer/renderer_types.inl"

/** @brief The texture system configuration */
typedef struct texture_system_config {
    /** @brief The maximum number of textures that can be loaded at once. */
    u32 max_texture_count;
} texture_system_config;

/** @brief The default texture name. */
#define DEFAULT_TEXTURE_NAME "default"

/** @brief The default specular texture name. */
#define DEFAULT_SPECULAR_TEXTURE_NAME "default_SPEC"

/**
 * @brief Initializes the texture system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 * 
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration for this system.
 * @return True on success; otherwise false.
 */
b8 texture_system_initialize(u64* memory_requirement, void* state, texture_system_config config);

/**
 * @brief Shuts down the texture system.
 * 
 * @param state The state block of memory for this system.
 */
void texture_system_shutdown(void* state);

/**
 * @brief Attempts to acquire a texture with the given name. If it has not yet been loaded,
 * this triggers it to load. If the texture is not found, a pointer to the default texture
 * is returned. If the texture _is_ found and loaded, its reference counter is incremented.
 * 
 * @param name The name of the texture to find.
 * @param auto_release Indicates if the texture should auto-release when its reference count is 0.
 * Only takes effect the first time the texture is acquired.
 * @return A pointer to the loaded texture. Can be a pointer to the default texture if not found.
 */
texture* texture_system_acquire(const char* name, b8 auto_release);

/**
 * @brief Releases a texture with the given name. Ignores non-existant textures.
 * Decreases the reference counter by 1. If the reference counter reaches 0 and
 * auto_release was set to true, the texture is unloaded, releasing internal resources.
 * 
 * @param name The name of the texture to unload.
 */
void texture_system_release(const char* name);

/**
 * @brief Gets a pointer to the default texture. No reference counting is 
 * done for default textures.
 */
texture* texture_system_get_default_texture();

/**
 * @brief Gets a pointer to the default specular texture. No reference counting is 
 * done for default textures.
 */
texture* texture_system_get_default_specular_texture();
