/**
 * @file kmaterial_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The material system is responsible for managing materials in the
 * engine, including reference counting and auto-unloading.
 * @version 2.0
 * @date 2025-07-25
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2025
 *
 */

#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"

#include <defines.h>
#include <strings/kname.h>

#define KMATERIAL_DEFAULT_NAME_STANDARD "Material.DefaultStandard"
#define KMATERIAL_DEFAULT_NAME_WATER "Material.DefaultWater"
#define KMATERIAL_DEFAULT_NAME_BLENDED "Material.DefaultBlended"

#define KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT 4
#define KMATERIAL_MAX_SHADOW_CASCADES 4
#define KMATERIAL_MAX_POINT_LIGHTS 10
#define KMATERIAL_MAX_VIEWS 4

#define KMATERIAL_DEFAULT_BASE_COLOUR_VALUE (vec4){1.0f, 1.0f, 1.0f, 1.0f}
#define KMATERIAL_DEFAULT_NORMAL_VALUE (vec3){0.0f, 0.0f, 1.0f}
#define KMATERIAL_DEFAULT_NORMAL_ENABLED true
#define KMATERIAL_DEFAULT_METALLIC_VALUE 0.0f
#define KMATERIAL_DEFAULT_ROUGHNESS_VALUE 0.5f
#define KMATERIAL_DEFAULT_AO_VALUE 1.0f
#define KMATERIAL_DEFAULT_AO_ENABLED true
#define KMATERIAL_DEFAULT_MRA_VALUE (vec3){0.0f, 0.5f, 1.0f}
#define KMATERIAL_DEFAULT_MRA_ENABLED true
#define KMATERIAL_DEFAULT_HAS_TRANSPARENCY false
#define KMATERIAL_DEFAULT_DOUBLE_SIDED false
#define KMATERIAL_DEFAULT_RECIEVES_SHADOW true
#define KMATERIAL_DEFAULT_CASTS_SHADOW true
#define KMATERIAL_DEFAULT_USE_VERTEX_COLOUR_AS_BASE_COLOUR false

struct kmaterial_system_state;
struct frame_data;

/** @brief The configuration for the material system. */
typedef struct kmaterial_system_config {
    /** @brief The maximum number of loaded materials. */
    u32 max_material_count;

    /** @brief The maximum number of material instances. */
    u32 max_instance_count;
} kmaterial_system_config;

typedef enum kmaterial_texture_input {
    // Forms the base colour of a material. Albedo for PBR, sometimes known as a "diffuse" colour. Specifies per-pixel colour.
    KMATERIAL_TEXTURE_INPUT_BASE_COLOUR,
    // Texture specifying per-pixel normal vector.
    KMATERIAL_TEXTURE_INPUT_NORMAL,
    // Texture specifying per-pixel metallic value.
    KMATERIAL_TEXTURE_INPUT_METALLIC,
    // Texture specifying per-pixel roughness value.
    KMATERIAL_TEXTURE_INPUT_ROUGHNESS,
    // Texture specifying per-pixel ambient occlusion value.
    KMATERIAL_TEXTURE_INPUT_AMBIENT_OCCLUSION,
    // Texture specifying per-pixel emissive value.
    KMATERIAL_TEXTURE_INPUT_EMISSIVE,
    // Texture specifying the reflection (only used for water materials)
    KMATERIAL_TEXTURE_INPUT_REFLECTION,
    // Texture specifying per-pixel refraction strength.
    KMATERIAL_TEXTURE_INPUT_REFRACTION,
    // Texture specifying the reflection depth (only used for water materials)
    KMATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH,
    // Texture specifying the refraction depth.
    KMATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH,
    KMATERIAL_TEXTURE_INPUT_DUDV,
    // Texture holding per-pixel metallic (r), roughness (g) and ambient occlusion (b) value.
    KMATERIAL_TEXTURE_INPUT_MRA,
    // The size of the material_texture_input enumeration.
    KMATERIAL_TEXTURE_INPUT_COUNT
} kmaterial_texture_input;

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
b8 kmaterial_system_initialize(u64* memory_requirement, struct kmaterial_system_state* state, const kmaterial_system_config* config);

/**
 * @brief Shuts down the material system.
 *
 * @param state The state block of memory.
 */
void kmaterial_system_shutdown(struct kmaterial_system_state* state);

// -------------------------------------------------
// ---------------- MATERIAL -----------------------
// -------------------------------------------------

/**
 * @brief Attempts to get the identifier of a material with the given name. If it has not yet been loaded,
 * this triggers it to load. If the material is not found, a handle of the default material is returned.
 *
 * @param state A pointer to the material system state.
 * @param name The name of the material to get the identifier of.
 * @param out_material_handle A pointer to hold the material handle.
 * @return True if the material was found; otherwise false if the default material was returned.
 */
KAPI b8 kmaterial_system_get_handle(struct kmaterial_system_state* state, kname name, kmaterial* out_material);

KAPI b8 kmaterial_is_loaded_get(struct kmaterial_system_state* state, kmaterial material);

KAPI ktexture kmaterial_texture_get(struct kmaterial_system_state* state, kmaterial material, kmaterial_texture_input tex_input);
KAPI void kmaterial_texture_set(struct kmaterial_system_state* state, kmaterial material, kmaterial_texture_input tex_input, ktexture texture);

KAPI texture_channel kmaterial_metallic_texture_channel_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_metallic_texture_channel_set(struct kmaterial_system_state* state, kmaterial material, texture_channel value);

KAPI texture_channel kmaterial_roughness_texture_channel_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_roughness_texture_channel_set(struct kmaterial_system_state* state, kmaterial material, texture_channel value);

KAPI texture_channel kmaterial_ao_texture_channel_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_ao_texture_channel_set(struct kmaterial_system_state* state, kmaterial material, texture_channel value);

KAPI texture_filter kmaterial_texture_filter_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_texture_filter_set(struct kmaterial_system_state* state, kmaterial material, texture_filter value);

KAPI texture_repeat kmaterial_texture_mode_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_texture_mode_set(struct kmaterial_system_state* state, kmaterial material, texture_repeat value);

KAPI b8 kmaterial_has_transparency_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_has_transparency_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_double_sided_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_double_sided_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_recieves_shadow_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_recieves_shadow_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_casts_shadow_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_casts_shadow_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_normal_enabled_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_normal_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_ao_enabled_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_ao_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_emissive_enabled_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_emissive_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI b8 kmaterial_refraction_enabled_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_refraction_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

KAPI f32 kmaterial_refraction_scale_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_refraction_scale_set(struct kmaterial_system_state* state, kmaterial material, f32 value);

KAPI b8 kmaterial_use_vertex_colour_as_base_colour_get(struct kmaterial_system_state* state, kmaterial material);
KAPI void kmaterial_use_vertex_colour_as_base_colour_set(struct kmaterial_system_state* state, kmaterial material, b8 value);

/**
 * @brief Sets the given material flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param material_flag_bits The flag to set.
 * @param value The value of the flag.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 kmaterial_flag_set(struct kmaterial_system_state* state, kmaterial material, kmaterial_flag_bits flag, b8 value);

/**
 * @brief Gets value of the given material flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param material_flag_bits The flag whose value to get.
 * @returns True if the flag is set; otherwise false.
 */
KAPI b8 kmaterial_flag_get(struct kmaterial_system_state* state, kmaterial material, kmaterial_flag_bits flag);

// -------------------------------------------------
// ------------- MATERIAL INSTANCE -----------------
// -------------------------------------------------

/**
 * @brief Attempts to acquire an instance of the material with the given handle.
 * Increases internal reference count.
 *
 * @param state A pointer to the material system state.
 * @param name The name of the material to acquire an instance for.
 * @param out_instance A pointer to hold the acquired material instance. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 kmaterial_system_acquire(struct kmaterial_system_state* state, kname name, kmaterial_instance* out_instance);

/**
 * @brief Releases the given material instance.
 *
 * @param state A pointer to the material system state.
 * @param instance A pointer to the material instance to unload. Handles are invalidated. Required.
 */
KAPI void kmaterial_system_release(struct kmaterial_system_state* state, kmaterial_instance* instance);

/**
 * Holds internal state for per-frame data (i.e across all standard materials);
 */
typedef struct kmaterial_frame_data {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES];
    mat4 projection;
    mat4 views[KMATERIAL_MAX_VIEWS];
    vec4 view_positions[KMATERIAL_MAX_VIEWS];
    u32 render_mode;
    f32 cascade_splits[KMATERIAL_MAX_SHADOW_CASCADES];
    f32 shadow_bias;
    f32 delta_time;
    f32 game_time;

    ktexture shadow_map_texture;
    ktexture irradiance_cubemap_textures[KMATERIAL_MAX_SHADOW_CASCADES];
} kmaterial_frame_data;
KAPI b8 kmaterial_system_prepare_frame(struct kmaterial_system_state* state, kmaterial_frame_data mat_frame_data, struct frame_data* p_frame_data);

KAPI b8 kmaterial_system_apply(struct kmaterial_system_state* state, kmaterial material, struct frame_data* p_frame_data);

typedef struct kmaterial_instance_draw_data {
    mat4 model;
    vec4 clipping_plane;
    u32 irradiance_cubemap_index;
    u32 view_index;
} kmaterial_instance_draw_data;
KAPI b8 kmaterial_system_apply_instance(struct kmaterial_system_state* state, const kmaterial_instance* instance, struct kmaterial_instance_draw_data draw_data, struct frame_data* p_frame_data);

/**
 * @brief Sets the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param instance The the material instance.
 * @param material_flag_bits The flag to set.
 * @param value The value of the flag.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 kmaterial_instance_flag_set(struct kmaterial_system_state* state, kmaterial_instance instance, kmaterial_flag_bits flag, b8 value);

/**
 * @brief Gets value of the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param material_flag_bits The flag whose value to get.
 * @returns True if the flag is set; otherwise false.
 */
KAPI b8 kmaterial_instance_flag_get(struct kmaterial_system_state* state, kmaterial_instance instance, kmaterial_flag_bits flag);

/**
 * @brief Gets the value of the material instance-specific base colour.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_base_colour_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec4* out_value);

/**
 * @brief Sets the value of the material instance-specific base colour.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_base_colour_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec4 value);

/**
 * @brief Gets the value of the material instance-specific UV offset. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_uv_offset_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec3* out_value);

/**
 * @brief Sets the value of the material instance-specific UV offset. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_uv_offset_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec3 value);

/**
 * @brief Gets the value of the material instance-specific UV scale. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_uv_scale_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec3* out_value);

/**
 * @brief Sets the value of the material instance-specific UV scale. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 kmaterial_instance_uv_scale_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec3 value);

/**
 * @brief Gets an instance of the default standard material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI kmaterial_instance kmaterial_system_get_default_standard(struct kmaterial_system_state* state);

/**
 * @brief Gets an instance of the default water material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI kmaterial_instance kmaterial_system_get_default_water(struct kmaterial_system_state* state);

/**
 * @brief Gets an instance of the default blended material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI kmaterial_instance kmaterial_system_get_default_blended(struct kmaterial_system_state* state);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 *
 * @param state A pointer to the material system state.
 */
KAPI void kmaterial_system_dump(struct kmaterial_system_state* state);
