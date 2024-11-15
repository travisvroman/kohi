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
#include "resources/resource_types.h"
#include <defines.h>
#include <strings/kname.h>

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

typedef struct material_data {
    // A unique id used for handle validation.
    u64 unique_id;
    // Multiplied by albedo/diffuse texture. Default: 1,1,1,1 (white)
    vec4 albedo_colour;
    kresource_texture* albedo_diffuse_texture;

    kresource_texture* normal_texture;

    f32 metallic;
    kresource_texture* metallic_texture;
    texture_channel metallic_texture_channel;

    f32 roughness;
    kresource_texture* roughness_texture;
    texture_channel roughness_texture_channel;

    kresource_texture* ao_texture;
    texture_channel ao_texture_channel;

    kresource_texture* emissive_texture;
    f32 emissive_texture_intensity;

    kresource_texture* refraction_texture;
    f32 refraction_scale;

    /**
     * @brief This is a combined texture holding metallic/roughness/ambient occlusion all in one texture.
     * This is a more efficient replacement for using those textures individually. Metallic is sampled
     * from the Red channel, roughness from the Green channel, and ambient occlusion from the Blue channel.
     * Alpha is ignored.
     */
    kresource_texture* mra_texture;

    // Base set of flags for the material. Copied to the material instance when created.
    material_flags flags;

    // Texture mode used for all textures on the material.
    material_texture_mode texture_mode;

    // Texture filter used for all textures on the material.
    material_texture_filter texture_filter;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;

    // Shader group id for per-group uniforms.
    u32 group_id;

} material_data;

typedef struct material_instance_data {
    // A handle to the material to which this instance references.
    khandle material;
    // A unique id used for handle validation.
    u64 unique_id;
    // Shader draw id for per-draw uniforms.
    u32 per_draw_id;

    // Multiplied by albedo/diffuse texture. Overrides the value set in the base material.
    vec4 albedo_colour;

    // Overrides the flags set in the base material.
    material_flags flags;

    // Added to UV coords of vertex data.
    vec3 uv_offset;
    // Multiplied against uv coords of vertex data.
    vec3 uv_scale;
} material_instance_data;

typedef struct multimaterial_data {
    u8 submaterial_count;
    u16* material_ids;
} multimaterial_data;

typedef struct mulitmaterial_instance_data {
    khandle instance;
    u8 submaterial_count;
    material_instance_data* instance_datas;
} multimaterial_instance_data;

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
 * @param name The handle of the material to acquire an instance for.
 * @return A handle to a material instance the material with the given handle is found and valid; otherwise an invalid handle.
 */
KAPI khandle material_acquire_instance(struct material_system_state* state, khandle material);

/**
 * @brief Releases the given material instance.
 * Decrements internal owning material's reference count. If the reference count reaches 0,
 * internal resources for the owning material are released.
 *
 * @param state A pointer to the material system state.
 * @param material A handle to the owning material.
 * @param instance A handle to the material instance to unload.
 */
KAPI void material_release_instance(struct material_system_state* state, khandle material, khandle instance);

/**
 * @brief Sets the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param instance The identifier of the material instance.
 * @param material_flag_bits The flag to set.
 * @param value The value of the flag.
 * @returns True if successfully set; otherwise false.
 */
KAPI b8 material_instance_flag_set(struct material_system_state* state, khandle material, khandle instance, material_flag_bits flag, b8 value);

/**
 * @brief Gets value of the given material instance flag's state.
 *
 * @param state A pointer to the material system state.
 * @param material The identifier of the material.
 * @param instance The identifier of the material instance.
 * @param material_flag_bits The flag whose value to get.
 * @returns True if the flag is set; otherwise false.
 */
KAPI b8 material_instance_flag_get(struct material_system_state* state, khandle material, khandle instance, material_flag_bits flag);

KAPI vec4 material_instance_albedo_colour_get(struct material_system_state* state, khandle material, khandle instance);
KAPI void material_instance_albedo_colour_set(struct material_system_state* state, khandle material, khandle instance, vec4 value);

KAPI vec3 material_instance_uv_offset_get(struct material_system_state* state, khandle material, khandle instance);
KAPI void material_instance_uv_offset_set(struct material_system_state* state, khandle material, khandle instance, vec3 value);

KAPI vec3 material_instance_uv_scale_get(struct material_system_state* state, khandle material, khandle instance);
KAPI void material_instance_uv_scale_set(struct material_system_state* state, khandle material, khandle instance, vec3 value);

/**
 * @brief Gets an instance of the default material.
 */
KAPI khandle material_get_default(struct material_system_state* state);

/**
 * @brief Dumps all of the registered materials and their reference counts/handles.
 */
KAPI void material_system_dump(struct material_system_state* state);
