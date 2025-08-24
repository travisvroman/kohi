/**
 * LEFTOFF:instance
 * - simplify API:
 *   - Point lights should be set per-draw (and based on the closest lights in the scene, max 10)
 */
#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "systems/kmaterial_system.h"

// Option indices
typedef enum kmaterial_option {
    MAT_OPTION_IDX_RENDER_MODE = 0,
    MAT_OPTION_IDX_USE_PCF = 1,
    MAT_OPTION_IDX_UNUSED_0 = 2,
    MAT_OPTION_IDX_UNUSED_1 = 3,
} kmaterial_option;

// Param indices
typedef enum kmaterial_param {
    MAT_PARAM_IDX_SHADOW_BIAS = 0,
    MAT_PARAM_IDX_DELTA_TIME = 1,
    MAT_PARAM_IDX_GAME_TIME = 2,
    MAT_PARAM_IDX_UNUSED_0 = 3
} kmaterial_param;

typedef struct kdirectional_light_uniform_data {
    /** @brief The light colour, stored in rgb. The a component is ignored */
    vec4 colour;
    /** @brief The direction of the light, stored in .xyz. The w component is ignored.*/
    vec4 direction;

    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
    f32 padding;
} kdirectional_light_uniform_data;

/**
 * The uniform data for a point light. 32 bytes.
 */
typedef struct kpoint_light_uniform_data {
    /** @brief The light colour stored in .rgb as well as "linear" stored in .a. */
    vec4 colour;
    /** @brief The position of the light in the world, stored in .xyz. The w contains "quadratic".*/
    vec4 position;
} kpoint_light_uniform_data;

// Shader locations for all material shaders.
typedef struct kmaterial_shader_locations {
    // Per frame
    u16 material_frame_ubo;
    u16 shadow_texture;
    u16 irradiance_cube_textures;
    u16 shadow_sampler;
    u16 irradiance_sampler;

    // Per group
    u16 material_textures;
    u16 material_samplers;
    u16 material_group_ubo;

    // Per draw.
    u16 material_draw_ubo;
} kmaterial_shader_locations;

// Material Per-frame ("global") UBO data for ALL material types.
typedef struct kmaterial_global_uniform_data {
    // All available point lights in a scene. Indexed into during per-draw.
    kpoint_light_uniform_data global_point_lights[KMATERIAL_MAX_GLOBAL_POINT_LIGHTS]; // 2048 bytes
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[KMATERIAL_MAX_VIEWS];                              // 320 bytes
    vec4 view_positions[KMATERIAL_MAX_VIEWS];                     // 80 bytes
    mat4 projection;                                              // 64 bytes
    kdirectional_light_uniform_data dir_light;                    // 48 bytes
    vec4 cascade_splits;                                          // 16 bytes

    // [shadow_bias, delta_time, game_time, padding]
    vec4 params; // 16 bytes
    // [render_mode, use_pcf, padding, padding]
    uvec4 options; // 16 bytes
    vec4 padding;  // 16 bytes
} kmaterial_global_uniform_data;

// Standard Material Per-group UBO (i.e. per "base material")
typedef struct kmaterial_standard_base_uniform_data {
    // Packed texture channels for various maps requiring it.
    u32 texture_channels; // [metallic, roughness, ao, unused]
    /** @brief The material lighting model. */
    u32 lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    // Texture use flags
    u32 tex_flags;

    vec4 base_colour;
    vec4 emissive;
    vec3 normal;
    f32 metallic;
    vec3 mra;
    f32 roughness;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    f32 ao;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;
    f32 emissive_texture_intensity;

    f32 refraction_scale;
    vec3 padding;
} kmaterial_standard_base_uniform_data;

// Standard Material Per-draw UBO (i.e per "material instance")
typedef struct kmaterial_standard_instance_uniform_data {
    mat4 model;          // 64 bytes
    vec4 clipping_plane; // 16 bytes
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 u32s.
    uvec2 packed_point_light_indices; // 8 bytes
    u32 num_p_lights;
    u32 irradiance_cubemap_index;
    u32 view_index;
    uvec3 padding;
} kmaterial_standard_instance_uniform_data;

// Water Material Per-group UBO (i.e. per "base material")
typedef struct kmaterial_water_base_uniform_data {
    /** @brief The material lighting model. */
    u32 lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    vec2 padding;
} kmaterial_water_base_uniform_data;

// Water Material Per-draw UBO (i.e per "material instance")
typedef struct kmaterial_water_instance_uniform_data {
    mat4 model;
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 u32s.
    uvec2 packed_point_light_indices; // 8 bytes
    u32 num_p_lights;
    u32 irradiance_cubemap_index;
    u32 view_index;
    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;
} kmaterial_water_instance_uniform_data;

/** @brief State for the material renderer. */
typedef struct kmaterial_renderer {

    // per-frame ("global") material data - applied to _all_ material types.
    kmaterial_global_uniform_data global_ubo_data;

    ktexture shadow_map_texture;
    u8 ibl_cubemap_texture_count;
    ktexture ibl_cubemap_textures[KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];

    // Pointer to use for material texture inputs _not_ using a texture map (because something has to be bound).
    ktexture default_texture;
    ktexture default_base_colour_texture;
    ktexture default_spec_texture;
    ktexture default_normal_texture;
    // Pointer to a default cubemap to fall back on if no IBL cubemaps are present.
    ktexture default_ibl_cubemap;
    ktexture default_mra_texture;
    ktexture default_water_normal_texture;
    ktexture default_water_dudv_texture;

    kshader material_standard_shader;
    kmaterial_shader_locations material_standard_locations;
    kshader material_standard_skinned_shader;
    kmaterial_shader_locations material_standard_skinned_locations;
    kshader material_water_shader;
    kmaterial_shader_locations material_water_locations;
    // FIXME: implement this
    kshader material_blended_shader;

    // Keep a pointer to various system states for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;

    // Runtime package name pre-hashed and kept here for convenience.
    kname runtime_package_name;
} kmaterial_renderer;

KAPI b8 kmaterial_renderer_initialize(kmaterial_renderer* out_state, u32 max_material_count, u32 max_material_instance_count);
KAPI void kmaterial_renderer_shutdown(kmaterial_renderer* state);

KAPI void kmaterial_renderer_update(kmaterial_renderer* state);

// Sets material_data->group_id;
KAPI void kmaterial_renderer_register_base(kmaterial_renderer* state, kmaterial_data* material_data);
KAPI void kmaterial_renderer_unregister_base(kmaterial_renderer* state, kmaterial_data* material_data);

KAPI void kmaterial_renderer_register_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance);
KAPI void kmaterial_renderer_unregister_instance(kmaterial_renderer* state, kmaterial_data* base_material, kmaterial_instance_data* instance);

#define kmaterial_renderer_set_render_mode(state, renderer_mode)                             \
    {                                                                                        \
        state->global_ubo_data.options.elements[MAT_OPTION_IDX_RENDER_MODE] = renderer_mode; \
    }

#define kmaterial_renderer_set_pcf_enabled(state, pcf_enabled)                         \
    {                                                                                  \
        state->global_ubo_data.options.elements[MAT_OPTION_IDX_USE_PCF] = pcf_enabled; \
    }

#define kmaterial_renderer_set_shadow_bias(state, shadow_bias)                           \
    {                                                                                    \
        state->global_ubo_data.params.elements[MAT_PARAM_IDX_SHADOW_BIAS] = shadow_bias; \
    }

#define kmaterial_renderer_set_delta_game_times(state, delta_time, game_time)          \
    {                                                                                  \
        state->global_ubo_data.params.elements[MAT_PARAM_IDX_DELTA_TIME] = delta_time; \
        state->global_ubo_data.params.elements[MAT_PARAM_IDX_GAME_TIME] = game_time;   \
    }

#define kmaterial_renderer_set_directional_light(state, p_dir_light)                               \
    {                                                                                              \
        state->global_ubo_data.dir_light.colour = vec4_from_vec3(p_dir_light->colour, 0.0f);       \
        state->global_ubo_data.dir_light.direction = vec4_from_vec3(p_dir_light->direction, 0.0f); \
        state->global_ubo_data.dir_light.shadow_distance = p_dir_light->shadow_distance;           \
        state->global_ubo_data.dir_light.shadow_fade_distance = p_dir_light->shadow_fade_distance; \
        state->global_ubo_data.dir_light.shadow_split_mult = p_dir_light->shadow_split_mult;       \
    }

KINLINE void kmaterial_renderer_set_point_lights(kmaterial_renderer* state, u8 count, kpoint_light_render_data* lights) {
    u8 num_p_lights = KMIN(count, KMATERIAL_MAX_GLOBAL_POINT_LIGHTS);
    for (u8 i = 0; i < num_p_lights; ++i) {
        kpoint_light_render_data* pls = &lights[i];
        kpoint_light_uniform_data* plt = &state->global_ubo_data.global_point_lights[i];
        plt->colour = vec4_from_vec3(pls->colour, pls->linear);
        plt->position = vec4_from_vec3(pls->position, pls->quadratic);
    }
}

KAPI void kmaterial_renderer_set_irradiance_cubemap_textures(kmaterial_renderer* state, u8 count, ktexture* irradiance_cubemap_textures);

KAPI void kmaterial_renderer_apply_globals(kmaterial_renderer* state);

// Updates and binds base material.
KAPI void kmaterial_renderer_bind_base(kmaterial_renderer* state, kmaterial base);

// Updates and binds material instance using the provided lighting information.
// NOTE: Up to KMATERIAL_MAX_BOUND_POINT_LIGHTS. Anything past this will be ignored.
KAPI void kmaterial_renderer_bind_instance(kmaterial_renderer* state, kmaterial_instance instance, u32 view_index, mat4 model, vec4 clipping_plane, u8 point_light_count, const u8* point_light_indices);
