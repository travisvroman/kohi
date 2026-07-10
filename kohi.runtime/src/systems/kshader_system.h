/**
 * @file shader_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system to manage shaders. Respondible for working with the
 * renderer to create, destroy, bind/unbind and set shader properties
 * such as uniforms.
 * @version 1.0
 * @date 2026-01-06
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <core_render_types.h>
#include <defines.h>

#include "renderer/renderer_types.h"

/** @brief Configuration for the shader system. */
typedef struct kshader_system_config {
	/** @brief The maximum number of shaders held in the system. NOTE: Should be at least 512. */
	u16 max_shader_count;
	/** @brief The maximum number of uniforms allowed in a single shader. */
	u8 max_uniform_count;
} kshader_system_config;

/**
 * @brief Initializes the shader system using the supplied configuration.
 * NOTE: Call this twice, once to obtain memory requirement (memory = 0) and a second time
 * including allocated memory.
 *
 * @param memory_requirement A pointer to hold the memory requirement of this system in bytes.
 * @param memory A memory block to be used to hold the state of this system. Pass 0 on the first call to get memory requirement.
 * @param config The configuration (kshader_system_config) to be used when initializing the system.
 * @return b8 True on success; otherwise false.
 */
b8 kshader_system_initialize (u64 *memory_requirement, void *memory, void *config);

/**
 * @brief Shuts down the shader system.
 *
 * @param state A pointer to the system state.
 */
void kshader_system_shutdown (void *state);

/**
 * @brief Returns a handle to a shader with the given name.
 * Attempts to load the shader if not already loaded.
 *
 * @param shader_name The kname to search for.
 * @param package_name The package to get the shader from if not already loaded. Pass INVALID_KNAME to search all packages.
 * @return A handle to a shader, if found/loaded; otherwise KSHADER_INVALID.
 */
KAPI kshader kshader_system_get (kname name, kname package_name);

/**
 * @brief Returns a handle to a shader with the given name based on the provided config source.
 * Attempts to load the shader if not already loaded.
 *
 * @param shader_name The name of the new shader.
 * @param shader_config_source A string containing the shader's configuration source as if it were loaded from an asset.
 * @return A handle to a shader, if loaded; otherwise KSHADER_INVALID.
 */
KAPI kshader kshader_system_get_from_source (kname name, const char *shader_config_source);

/**
 * @brief Attempts to destroy the shader with the given handle. Handle will be invalidated.
 *
 * @param shader_name A pointer to a handle to the shader to destroy. Handle will be invalidated.
 */
KAPI void kshader_system_destroy (kshader *shader);

/**
 * @brief Attempts to set wireframe mode on the given shader. If the renderer backend, or the shader
 * does not support this , it will fail when attempting to enable. Disabling will always succeed.
 *
 * @param shader A handle to the shader to set wireframe mode for.
 * @param wireframe_enabled Indicates if wireframe mode should be enabled.
 * @return True on success; otherwise false.
 */
KAPI b8 kshader_system_set_wireframe (kshader shader, b8 wireframe_enabled);

/**
 * @brief Uses the shader with the given handle and the shader's default topology.
 *
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 kshader_system_use (kshader shader, u8 vertex_layout_index);

/**
 * @brief Uses the shader with the given handle and the provided topology.
 *
 * @param shader A handle to the shader to be used.
 * @param topology The topology type to use.
 * @return True on success; otherwise false.
 */
KAPI b8 kshader_system_use_with_topology (kshader shader, primitive_topology_type topology, u8 vertex_layout_index);

KAPI void kshader_set_immediate_data (kshader shader, const void *data, u8 size);
KAPI void kshader_set_binding_data (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size);
KAPI void kshader_set_binding_texture (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture texture);
KAPI void kshader_set_binding_sampler (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler);
KAPI u32 kshader_acquire_binding_set_instance (kshader shader, u8 binding_set);
KAPI void kshader_release_binding_set_instance (kshader shader, u8 binding_set, u32 instance_id);
KAPI u32 kshader_binding_set_instance_count_get (kshader shader, u8 binding_set);
KAPI b8 kshader_apply_binding_set (kshader shader, u8 binding_set, u32 instance_id);
