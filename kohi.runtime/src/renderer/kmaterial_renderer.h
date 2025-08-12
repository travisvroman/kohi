/**
 * LEFTOFF:instance
 * - Remove multiple view/view_positions and per-draw view_index because they are no longer required due to
 *   the way scene renders are always done within a pass of their own, and never more than once.
 * - Move over all shader data (UBO structs, uniform locations, etc.)
 * - simplify API:
 *   - Maintain state at global level only, that persists across frames.
 *   - Material base creation registers material with mat renderer, unregisters when destroyed
 *     - Update() looks at base properties every frame, updates when needed. Use Generation?
 *   - Material instances register with mat renderer, unregisters when destroyed.
 *     - Update() looks at material instance-level properties every frame and updates uniforms. Use Generation?
 *   - Set properties instead of uniforms where it makes sense (i.e. proj/view matrix, skybox data, etc)
 *   - Move all directional light properties to global instead of group in shaders.
 *   - Directional light should be set once "per frame".
 *   - Point lights should be set per-draw (and based on the closest lights in the scene, max 10)
 * - kmaterial_system_prepare_frame() -> kmaterial_renderer_apply_global()
 * - kmaterial_system_apply() -> kmaterial_renderer_apply_base()
 * - kmaterial_system_apply_instance -> kmaterial_renderer_apply_instance()
 */
#pragma once

#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "systems/kmaterial_system.h"
#include "systems/light_system.h"

// Max number of point lights that can exist in the renderer at once.
#define KMATERIAL_MAX_GLOBAL_POINT_LIGHTS 64
// Max number of point lights that can be bound in a single draw.
#define KMATERIAL_MAX_BOUND_POINT_LIGHTS 8

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
    mat4 projection;                                              // 64 bytes
    mat4 view;                                                    // 64 bytes
    kdirectional_light_uniform_data dir_light;                    // 48 bytes
    vec4 view_position;                                           // 16 bytes
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
    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;
    f32 padding;
} kmaterial_water_instance_uniform_data;

/** @brief State for the material renderer. */
typedef struct kmaterial_renderer {

    // per-frame ("global") material data - applied to _all_ material types.
    kmaterial_global_uniform_data global_data;

    ktexture shadow_map_texture;
    u8 ibl_cubemap_texture_count;
    ktexture* ibl_cubemap_textures;

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
KAPI b8 kmaterial_renderer_shutdown(kmaterial_renderer* state);

KAPI b8 kmaterial_renderer_update(kmaterial_renderer* state);

KAPI void kmaterial_renderer_register_base(kmaterial_renderer* state, kmaterial base);
KAPI void kmaterial_renderer_unregister_base(kmaterial_renderer* state, kmaterial base);

KAPI void kmaterial_renderer_register_instance(kmaterial_renderer* state, kmaterial_instance instance);
KAPI void kmaterial_renderer_unregister_instance(kmaterial_renderer* state, kmaterial_instance instance);

KAPI void kmaterial_renderer_set_render_mode(kmaterial_renderer* state, renderer_debug_view_mode renderer_mode);

KAPI void kmaterial_renderer_set_pcf_enabled(kmaterial_renderer* state, b8 pcf_enabled);

KAPI void kmaterial_renderer_set_shadow_bias(kmaterial_renderer* state, f32 shadow_bias);

KAPI void kmaterial_renderer_set_delta_game_times(kmaterial_renderer* state, f32 delta_time, f32 game_time);

KAPI void kmaterial_renderer_set_directional_light(kmaterial_renderer* state, const directional_light* dir_light);

// Sets global point light data for the entire scene.
// NOTE: count exceeding KMATERIAL_MAX_GLOBAL_POINT_LIGHTS will be ignored
KAPI void kmaterial_renderer_set_point_lights(kmaterial_renderer* state, u8 point_light_count, point_light* point_lights);

void kmaterial_renderer_set_matrices(kmaterial_renderer* state, mat4 projection, mat4 view);

KAPI void kmaterial_renderer_apply_globals(kmaterial_renderer* state);

// Updates and binds base material.
KAPI void kmaterial_renderer_bind_base(kmaterial_renderer* state, kmaterial base);

// Updates and binds material instance using the provided lighting information.
// NOTE: Up to KMATERIAL_MAX_BOUND_POINT_LIGHTS. Anything past this will be ignored.
KAPI void kmaterial_renderer_bind_instance(kmaterial_renderer* state, kmaterial_instance instance, mat4 model, vec4 clipping_plane, u8 point_light_count, const u8* point_light_indices);
