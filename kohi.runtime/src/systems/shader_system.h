/**
 * @file shader_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system to manage shaders. Respondible for working with the
 * renderer to create, destroy, bind/unbind and set shader properties
 * such as uniforms.
 * @version 1.0
 * @date 2022-03-09
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "core_render_types.h"
#include "defines.h"
#include "kresources/kresource_types.h"

/** @brief Configuration for the shader system. */
typedef struct shader_system_config {
    /** @brief The maximum number of shaders held in the system. NOTE: Should be at least 512. */
    u16 max_shader_count;
    /** @brief The maximum number of uniforms allowed in a single shader. */
    u8 max_uniform_count;
} shader_system_config;

/**
 * @brief Initializes the shader system using the supplied configuration.
 * NOTE: Call this twice, once to obtain memory requirement (memory = 0) and a second time
 * including allocated memory.
 *
 * @param memory_requirement A pointer to hold the memory requirement of this system in bytes.
 * @param memory A memory block to be used to hold the state of this system. Pass 0 on the first call to get memory requirement.
 * @param config The configuration (shader_system_config) to be used when initializing the system.
 * @return b8 True on success; otherwise false.
 */
b8 shader_system_initialize(u64* memory_requirement, void* memory, void* config);

/**
 * @brief Shuts down the shader system.
 *
 * @param state A pointer to the system state.
 */
void shader_system_shutdown(void* state);

/**
 * @brief Reloads the given shader.
 *
 * @param shader A handle to the shader to reload.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_reload(khandle shader);

/**
 * @brief Returns a handle to a shader with the given name.
 * Attempts to load the shader if not already loaded.
 *
 * @param shader_name The kname to search for.
 * @return A handle to a shader, if found/loaded; otherwise an invalid handle.
 */
KAPI khandle shader_system_get(kname name);

/**
 * @brief Returns a handle to a shader with the given name based on the provided config source.
 * Attempts to load the shader if not already loaded.
 *
 * @param shader_name The name of the new shader.
 * @param shader_config_source A string containing the shader's configuration source as if it were loaded from an asset.
 * @return A handle to a shader, if loaded; otherwise an invalid handle.
 */
KAPI khandle shader_system_get_from_source(kname name, const char* shader_config_source);

/**
 * @brief Attempts to destroy the shader with the given handle. Handle will be invalidated.
 *
 * @param shader_name A pointer to a handle to the shader to destroy. Handle will be invalidated.
 */
KAPI void shader_system_destroy(khandle* shader);

/**
 * @brief Attempts to set wireframe mode on the given shader. If the renderer backend, or the shader
 * does not support this , it will fail when attempting to enable. Disabling will always succeed.
 *
 * @param shader A handle to the shader to set wireframe mode for.
 * @param wireframe_enabled Indicates if wireframe mode should be enabled.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_set_wireframe(khandle shader, b8 wireframe_enabled);

/**
 * @brief Uses the shader with the given handle.
 *
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_use(khandle shader);

/**
 * @brief Returns the uniform location for a uniform with the given name, if found.
 *
 * @param shader A handle to the shader to obtain the location from.
 * @param uniform_name The name of the uniform to search for.
 * @return The uniform location, if found; otherwise INVALID_ID_U16.
 */
KAPI u16 shader_system_uniform_location(khandle shader, kname uniform_name);

/**
 * @brief Sets the value of a uniform with the given name to the supplied value.
 *
 * @param shader A handle to the shader to update.
 * @param uniform_name The name of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set(khandle shader, kname uniform_name, const void* value);

/**
 * @brief Sets the value of an arrayed uniform with the given name to the supplied value.
 *
 * @param shader A handle to the shader to update.
 * @param uniform_name The name of the uniform to be set.
 * @param array_index The index into the uniform array, if the uniform is in fact an array. Otherwise ignored.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set_arrayed(khandle shader, kname uniform_name, u32 array_index, const void* value);

/**
 * @brief Sets the texture uniform with the given name to the supplied texture.
 *
 * @param shader A handle to the shader to update.
 * @param uniform_name The name of the uniform to be set.
 * @param t A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_texture_set(khandle shader, kname sampler_name, const kresource_texture* t);

/**
 * @brief Sets the arrayed texture uniform with the given name to the supplied texture at the given index.
 *
 * @param shader A handle to the shader to update.
 * @param uniform_name The name of the uniform to be set.
 * @param array_index The index into the uniform array, if the uniform is in fact an array. Otherwise ignored.
 * @param t A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_texture_set_arrayed(khandle shader, kname sampler_name, u32 array_index, const kresource_texture* t);

/**
 * @brief Sets a uniform value by location.
 *
 * @param shader A handle to the shader to update.
 * @param index The location of the uniform.
 * @param value The value of the uniform.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set_by_location(khandle shader, u16 location, const void* value);

/**
 * @brief Sets a uniform value by location.
 *
 * @param shader A handle to the shader to update.
 * @param location The location of the uniform.
 * @param array_index The index into the uniform array, if the uniform is in fact an array. Otherwise ignored.
 * @param value The value of the uniform.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_uniform_set_by_location_arrayed(khandle shader, u16 location, u32 array_index, const void* value);

/**
 * @brief Sets a sampler value by location.
 *
 * @param shader A handle to the shader to update.
 * @param location The location of the uniform.
 * @param value A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_texture_set_by_location(khandle shader, u16 location, const struct kresource_texture* t);

/**
 * @brief Sets a sampler value by location.
 *
 * @param shader A handle to the shader to update.
 * @param index The location of the uniform.
 * @param array_index The index into the uniform array, if the uniform is in fact an array. Otherwise ignored.
 * @param value A pointer to the texture to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_sampler_set_by_location_arrayed(khandle shader, u16 location, u32 array_index, const struct kresource_texture* t);

/**
 * @brief Binds the shader at per-frame frequency for use. Must be done before setting
 * frame-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param instance_id The identifier of the instance to bind.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_bind_frame(khandle shader);
/**
 * @brief Binds the instance with the given id for use. Must be done before setting
 * instance-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param instance_id The identifier of the instance to bind.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_bind_group(khandle shader, u32 instance_id);

/**
 * @brief Binds the local with the given id for use. Must be done before setting
 * local-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param local_id The identifier of the local to bind.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_bind_draw_id(khandle shader, u32 local_id);

/**
 * @brief Applies global-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param generation The data generation for this frequency.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_apply_per_frame(khandle shader, u16 generation);

/**
 * @brief Applies instance-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param generation The data generation for this frequency.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_apply_per_group(khandle shader, u16 generation);

/**
 * @brief Applies local-scoped uniforms.
 *
 * @param shader A handle to the shader to update.
 * @param generation The data generation for this frequency.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_apply_per_draw(khandle shader, u16 generation);

/**
 * @brief Attempts to acquire new group resources from the given shader using the
 * collection of maps passed.
 *
 * @param shader A handle to the shader to acquire group resources for.
 * @param out_group_id A pointer to hold the group id once resources are acquired.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_shader_group_acquire(khandle shader, u32* out_group_id);

/**
 * @brief Releases group resources and texture map resources from the provided shader.
 *
 * @param shader A handle to the shader to release group resources for.
 * @param instance_id The identifier of the group to release.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_shader_group_release(khandle shader, u32 instance_id);

/**
 * @brief Attempts to acquire new per-draw resources from the given shader using the
 * collection of maps passed.
 *
 * @param shader A handle to the shader to acquire per-draw resources for.
 * @param out_per_draw_id A pointer to hold the per-draw id once resources are acquired.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_shader_per_draw_acquire(khandle shader, u32* out_per_draw_id);

/**
 * @brief Releases per-draw resources and texture map resources from the provided shader.
 *
 * @param shader A handle to the shader to release per-draw resources for.
 * @param per_draw_id The identifier of the per-draw to release.
 * @return True on success; otherwise false.
 */
KAPI b8 shader_system_shader_per_draw_release(khandle shader, u32 per_draw_id);
