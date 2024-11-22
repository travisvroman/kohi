/**
 * @file material_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The material system is responsible for managing materials in the
 * engine, including reference counting and auto-unloading.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"

#include <defines.h>
#include <strings/kname.h>

#define MATERIAL_DEFAULT_NAME_STANDARD "Material.DefaultStandard"
#define MATERIAL_DEFAULT_NAME_WATER "Material.DefaultWater"
#define MATERIAL_DEFAULT_NAME_BLENDED "Material.DefaultBlended"

struct material_system_state;

/** @brief The configuration for the material system. */
typedef struct material_system_config {
    /** @brief The maximum number of loaded materials. */
    u32 max_material_count;

    /** @brief The maximum number of material instances. */
    u32 max_instance_count;
} material_system_config;

typedef enum material_texture_param {
    // Albedo for PBR, sometimes known as a "diffuse" colour. Specifies per-pixel colour.
    MATERIAL_TEXTURE_PARAM_ALBEDO = 0,
    // Texture specifying per-pixel normal vector.
    MATERIAL_TEXTURE_PARAM_NORMAL = 1,
    // Texture specifying per-pixel metallic value.
    MATERIAL_TEXTURE_PARAM_METALLIC = 2,
    // Texture specifying per-pixel roughness value.
    MATERIAL_TEXTURE_PARAM_ROUGHNESS = 3,
    // Texture specifying per-pixel ambient occlusion value.
    MATERIAL_TEXTURE_PARAM_AMBIENT_OCCLUSION = 4,
    // Texture specifying per-pixel emissive value.
    MATERIAL_TEXTURE_PARAM_EMISSIVE = 5,
    // Texture specifying per-pixel refraction strength.
    MATERIAL_TEXTURE_INPUT_REFRACTION = 6,
    // Texture holding per-pixel metallic (r), roughness (g) and ambient occlusion (b) value.
    MATERIAL_TEXTURE_MRA = 7,
    // The size of the material_texture_param enumeration.
    MATERIAL_TEXTURE_COUNT
} material_texture_param;

/**
 * @brief A material instance, which contains handles to both
 * the base material as well as the instance itself. Every time
 * an instance is "acquired", one of these is created, and the instance
 * should be referenced using this going from that point.
 */
typedef struct material_instance {
    // Handle to the base material.
    khandle material;
    // Handle to the instance.
    khandle instance;
} material_instance;

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
b8 material_system_initialize(u64* memory_requirement, struct material_system_state* state, const material_system_config* config);

/**
 * @brief Shuts down the material system.
 *
 * @param state The state block of memory.
 */
void material_system_shutdown(struct material_system_state* state);

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
KAPI b8 material_system_get_handle(struct material_system_state* state, kname name, khandle* out_material_handle);

KAPI const kresource_texture* material_texture_get(struct material_system_state* state, khandle material, material_texture_param tex_param);
KAPI void material_texture_set(struct material_system_state* state, khandle material, material_texture_param tex_param, const kresource_texture* texture);

KAPI texture_channel material_metallic_texture_channel_get(struct material_system_state* state, khandle material);
KAPI void material_metallic_texture_channel_set(struct material_system_state* state, khandle material, texture_channel value);

KAPI texture_channel material_roughness_texture_channel_get(struct material_system_state* state, khandle material);
KAPI void material_roughness_texture_channel_set(struct material_system_state* state, khandle material, texture_channel value);

KAPI texture_channel material_ao_texture_channel_get(struct material_system_state* state, khandle material);
KAPI void material_ao_texture_channel_set(struct material_system_state* state, khandle material, texture_channel value);

KAPI material_texture_filter material_texture_filter_get(struct material_system_state* state, khandle material);
KAPI void material_texture_filter_set(struct material_system_state* state, khandle material, material_texture_filter value);

KAPI material_texture_mode material_texture_mode_get(struct material_system_state* state, khandle material);
KAPI void material_texture_mode_set(struct material_system_state* state, khandle material, material_texture_mode value);

KAPI b8 material_has_transparency_get(struct material_system_state* state, khandle material);
KAPI void material_has_transparency_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_double_sided_get(struct material_system_state* state, khandle material);
KAPI void material_double_sided_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_recieves_shadow_get(struct material_system_state* state, khandle material);
KAPI void material_recieves_shadow_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_casts_shadow_get(struct material_system_state* state, khandle material);
KAPI void material_casts_shadow_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_normal_enabled_get(struct material_system_state* state, khandle material);
KAPI void material_normal_enabled_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_ao_enabled_get(struct material_system_state* state, khandle material);
KAPI void material_ao_enabled_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_emissive_enabled_get(struct material_system_state* state, khandle material);
KAPI void material_emissive_enabled_set(struct material_system_state* state, khandle material, b8 value);

KAPI b8 material_refraction_enabled_get(struct material_system_state* state, khandle material);
KAPI void material_refraction_enabled_set(struct material_system_state* state, khandle material, b8 value);

KAPI f32 material_refraction_scale_get(struct material_system_state* state, khandle material);
KAPI void material_refraction_scale_set(struct material_system_state* state, khandle material, f32 value);

KAPI b8 material_use_vertex_colour_as_albedo_get(struct material_system_state* state, khandle material);
KAPI void material_use_vertex_colour_as_albedo_set(struct material_system_state* state, khandle material, b8 value);

/**
 * @brief Sets the given material flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param material_flag_bits The flag to set.
 * @param value The value of the flag.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 material_flag_set(struct material_system_state* state, khandle material, material_flag_bits flag, b8 value);

/**
 * @brief Gets value of the given material flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param material_flag_bits The flag whose value to get.
 * @returns True if the flag is set; otherwise false.
 */
KAPI b8 material_flag_get(struct material_system_state* state, khandle material, material_flag_bits flag);

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
KAPI b8 material_system_acquire(struct material_system_state* state, kname name, material_instance* out_instance);

/**
 * @brief Releases the given material instance.
 *
 * @param state A pointer to the material system state.
 * @param instance A pointer to the material instance to unload. Handles are invalidated. Required.
 */
KAPI void material_system_release(struct material_system_state* state, material_instance* instance);

/**
 * @brief Sets the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param instance The the material instance.
 * @param material_flag_bits The flag to set.
 * @param value The value of the flag.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 material_instance_flag_set(struct material_system_state* state, material_instance instance, material_flag_bits flag, b8 value);

/**
 * @brief Gets value of the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param material_flag_bits The flag whose value to get.
 * @returns True if the flag is set; otherwise false.
 */
KAPI b8 material_instance_flag_get(struct material_system_state* state, material_instance instance, material_flag_bits flag);

/**
 * @brief Gets the value of the material instance-specific base colour.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 material_instance_base_colour_get(struct material_system_state* state, material_instance instance, vec4* out_value);

/**
 * @brief Sets the value of the material instance-specific base colour.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 material_instance_base_colour_set(struct material_system_state* state, material_instance instance, vec4 value);

/**
 * @brief Gets the value of the material instance-specific UV offset. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 material_instance_uv_offset_get(struct material_system_state* state, material_instance instance, vec3* out_value);

/**
 * @brief Sets the value of the material instance-specific UV offset. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 material_instance_uv_offset_set(struct material_system_state* state, material_instance instance, vec3 value);

/**
 * @brief Gets the value of the material instance-specific UV scale. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param out_value A pointer to hold the value. Required.
 * @returns True if value was gotten successfully; otherwise false.
 */
KAPI b8 material_instance_uv_scale_get(struct material_system_state* state, material_instance instance, vec3* out_value);

/**
 * @brief Sets the value of the material instance-specific UV scale. Can be used for animating the position of materials.
 *
 * @param state A pointer to the material system state.
 * @param instance The material instance.
 * @param value The value to be set.
 * @returns True if value was set successfully; otherwise false.
 */
KAPI b8 material_instance_uv_scale_set(struct material_system_state* state, material_instance instance, vec3 value);

/**
 * @brief Gets an instance of the default standard material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI material_instance material_system_get_default_standard(struct material_system_state* state);

/**
 * @brief Gets an instance of the default water material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI material_instance material_system_get_default_water(struct material_system_state* state);

/**
 * @brief Gets an instance of the default blended material.
 *
 * @param state A pointer to the material system state.
 * @returns A material instance with handles to the material and instance of it.
 */
KAPI material_instance material_system_get_default_blended(struct material_system_state* state);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 *
 * @param state A pointer to the material system state.
 */
KAPI void material_system_dump(struct material_system_state* state);
