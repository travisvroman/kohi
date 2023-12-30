/**
 * @file material_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The material system is responsible for managing materials in the
 * engine, including reference counting and auto-unloading.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"
#include "resources/resource_types.h"

/** @brief The name of the default PBR material. */
#define DEFAULT_PBR_MATERIAL_NAME "default_pbr"

/** @brief The name of the default terrain material. */
#define DEFAULT_TERRAIN_MATERIAL_NAME "default_terrain"

/** @brief The configuration for the material system. */
typedef struct material_system_config {
    /** @brief The maximum number of loaded materials. */
    u32 max_material_count;
} material_system_config;

struct frame_data;

/**
 * @brief Initializes the material system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (material_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 material_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the material system.
 *
 * @param state The state block of memory.
 */
void material_system_shutdown(void* state);

/**
 * @brief Attempts to acquire a material with the given name. If it has not yet been loaded,
 * this triggers it to load. If the material is not found, a pointer to the default material
 * is returned. If the material _is_ found and loaded, its reference counter is incremented.
 *
 * @param name The name of the material to find.
 * @return A pointer to the loaded material. Can be a pointer to the default material if not found.
 */
KAPI material* material_system_acquire(const char* name);

/**
 * @brief Attempts to acquire a terrain material with the given name. If it has not yet been
 * loaded, this triggers it to be loaded from using the provided standard material names. If
 * the material is not able to be loaded, a pointer to the default terrain material is returned.
 * If the material _is_ found and loaded, its reference counter is incremented.
 *
 * @param name The name of the terrain material to find.
 * @param material_count The number of standard source material names.
 * @param material_names The names of the source materials to be used.
 * @return A pointer to the loaded terrain material. Can be a pointer to the defualt terrain material if not found.
 */
KAPI material* material_system_acquire_terrain_material(const char* material_name, u32 material_count, const char** material_names, b8 auto_release);

/**
 * @brief Attempts to acquire a material from the given configuration. If it has not yet been loaded,
 * this triggers it to load. If the material is not found, a pointer to the default material
 * is returned. If the material _is_ found and loaded, its reference counter is incremented.
 *
 * @param config The config of the material to load.
 * @return A pointer to the loaded material.
 */
KAPI material* material_system_acquire_from_config(material_config* config);

/**
 * @brief Releases a material with the given name. Ignores non-existant materials.
 * Decreases the reference counter by 1. If the reference counter reaches 0 and
 * auto_release was set to true, the material is unloaded, releasing internal resources.
 *
 * @param name The name of the material to unload.
 */
KAPI void material_system_release(const char* name);

/**
 * @brief Gets a pointer to the default material. Does not reference count.
 */
KAPI material* material_system_get_default(void);

/**
 * @brief Gets a pointer to the default PBR material. Does not reference count.
 */
KAPI material* material_system_get_default_pbr(void);

/**
 * @brief Gets a pointer to the default terrain material. Does not reference count.
 */
KAPI material* material_system_get_default_terrain(void);

/**
 * @brief Applies global-level data for the material shader id.
 *
 * @param shader_id The identifier of the shader to apply globals for.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @param projection A constant pointer to a projection matrix.
 * @param view A constant pointer to a view matrix.
 * @param ambient_colour The ambient colour of the scene.
 * @param view_position The camera position.
 * @param render_mode The render mode.
 * @return True on success; otherwise false.
 */
KAPI b8 material_system_apply_global(u32 shader_id, struct frame_data* p_frame_data, const mat4* projection, const mat4* view, const vec4* ambient_colour, const vec3* view_position, u32 render_mode);

/**
 * @brief Applies instance-level material data for the given material.
 *
 * @param m A pointer to the material to be applied.
 * @param p_frame_data A pointer to the current frame's data.
 * @param needs_update Indicates if material internals require updating, or if they should just be bound.
 * @return True on success; otherwise false.
 */
KAPI b8 material_system_apply_instance(material* m, struct frame_data* p_frame_data, b8 needs_update);

/**
 * @brief Applies local-level material data (typically just model matrix).
 *
 * @param m A pointer to the material to be applied.
 * @param model A constant pointer to the model matrix to be applied.
 * @return True on success; otherwise false.
 */
KAPI b8 material_system_apply_local(material* m, const mat4* model, struct frame_data* p_frame_data);

/**
 * @ brief Sets the provided shadowmap texture to be used for future binding/draw calls until changed.
 *
 * @param shadow_texture A pointer to the shadowmap texture to be used.
 * @returns True on success; otherwise false;
 */
KAPI b8 material_system_shadow_map_set(texture* shadow_texture, u8 index);
/**
 * @brief Sets the provided cubemap texture to be used for future binding/draw calls until changed.
 *  NOTE: Provided texture must be a cubemap texture or this function will fail.
 *
 * @param irradiance_cube_texture A pointer to the irradiance cubemap texture to be used. If null, system falls back on default cubemap texuture.
 * @returns True on success; otherwise false.
 */
KAPI b8 material_system_irradiance_set(texture* irradiance_cube_texture);

/**
 * @brief Sets the current directional light-space matrix to be used for future binding calls that require it.
 *
 * @param directional_light_space The directional light-space matrix.
 */
KAPI void material_system_directional_light_space_set(mat4 directional_light_space, u8 index);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 */
KAPI void material_system_dump(void);
