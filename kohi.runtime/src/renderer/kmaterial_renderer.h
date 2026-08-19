#pragma once

#include <core_render_types.h>
#include <math/math_types.h>

#include "renderer/renderer_types.h"
#include "systems/kmaterial_system.h"

#define KMATERIAL_UBO_MAX_SHADOW_CASCADES 4

#define KRENDERBUFFER_NAME_MATERIALS_GLOBAL "Kohi.StorageBuffer.MaterialsGlobal"

/**
 * The uniform data for a light. 32 bytes.
 * Can be used for point or directional lights.
 */
typedef struct klight_shader_data {
	// Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear
	vec4 colour;
	// Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
	vec4 position;
} klight_shader_data;

typedef struct base_material_shader_data {
	u32 metallic_texture_channel;
	u32 roughness_texture_channel;
	u32 ao_texture_channel;
	/** @brief The material lighting model. */
	u32 lighting_model;

	// Base set of flags for the material. Copied to the material instance when created.
	u32 flags;
	// Texture use flags
	u32 tex_flags;
	f32 refraction_scale;
	u32 material_type;

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
} base_material_shader_data;

typedef struct kmaterial_settings_ubo {
	mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes

	// Light space for shadow mapping. Per cascade
	vec4 cascade_splits; // 16 bytes

	f32 delta_time;
	f32 game_time;
	u32 render_mode;
	u32 use_pcf;

	// Shadow settings
	f32 shadow_bias;
	f32 shadow_distance;
	f32 shadow_fade_distance;
	f32 shadow_split_mult;

	// Fog settings
	colour4 fog_colour;
	f32 fog_start;
	f32 fog_end;
	vec2 padding; // TODO: available

	// Offsets into the matrix SSBO per type.
	u32 mat_ssbo_view_offset;
	u32 mat_ssbo_proj_offset;
	u32 mat_ssbo_tran_offset;
	u32 mat_ssbo_genc_offset;
} kmaterial_settings_ubo;

typedef enum kmaterial_data_index {
	KMATERIAL_DATA_INDEX_VIEW = 0,
	KMATERIAL_DATA_INDEX_PROJECTION = 1
} kmaterial_data_index;

typedef enum kmaterial_data_index2 {
	KMATERIAL_DATA_INDEX2_ANIMATION = 0,
	KMATERIAL_DATA_INDEX2_BASE_MATERIAL = 1
} kmaterial_data_index2;

typedef struct kmaterial_render_immediate_data {
	// bytes 0-15
	u32 view_index;
	u32 projection_index;
	u32 animation_index;
	u32 base_material_index;

	// bytes 16-31
	// Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
	uvec2 packed_point_light_indices; // 8 bytes
	u32 num_p_lights;
	// Index into global irradiance cubemap texture array
	u32 irradiance_cubemap_index;

	// bytes 32-47
	vec4 clipping_plane;

	// bytes 48-63
	u32 dir_light_index; // probably zero
	f32 tiling;			 // only used for water materials
	f32 wave_strength;	 // only used for water materials
	f32 wave_speed;		 // only used for water materials

	// bytes 64-79
	u32 transform_index;
	u32 geo_type;
	f32 near_clip;
	f32 far_clip;
	// 80-128 available
} kmaterial_render_immediate_data;

/** @brief State for the material renderer. */
typedef struct kmaterial_renderer {
	// Global storage buffer used for rendering materials.
	krenderbuffer material_global_ssbo;

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

	kshader material_standard_skinned_shader;
	u32 material_standard_skinned_shader_bs_0_instance_id;
	// FIXME: implement this
	kshader material_blended_shader;

	// Keep a pointer to various system states for quick access.
	struct renderer_system_state *renderer;
	struct texture_system_state *texture_system;
	struct kmaterial_system_state *material_state;

	u32 max_material_count;

	// Renderer state settings.
	kmaterial_settings_ubo settings;

	// Runtime package name pre-hashed and kept here for convenience.
	kname runtime_package_name;

	b8 current_uses_animated;
} kmaterial_renderer;

KAPI b8 kmaterial_renderer_initialize (kmaterial_renderer *out_state, u32 max_material_count, u32 max_material_instance_count);
KAPI void kmaterial_renderer_shutdown (kmaterial_renderer *state);

KAPI void kmaterial_renderer_update (kmaterial_renderer *state);

KAPI void kmaterial_renderer_set_fog_colour (kmaterial_renderer *state, colour4 colour);
KAPI void kmaterial_renderer_set_fog_near_far (kmaterial_renderer *state, f32 near, f32 far);

// Sets material_data->group_id;
KAPI void kmaterial_renderer_register_base (kmaterial_renderer *state, kmaterial_data *material_data);
KAPI void kmaterial_renderer_unregister_base (kmaterial_renderer *state, kmaterial_data *material_data);

KAPI void kmaterial_renderer_set_irradiance_cubemap_textures (kmaterial_renderer *state, u8 count, ktexture *irradiance_cubemap_textures);

KAPI void kmaterial_renderer_apply_globals (kmaterial_renderer *state);

// Updates and binds base material.
KAPI void kmaterial_renderer_bind_base (kmaterial_renderer *state, kmaterial base);
KAPI void kmaterial_renderer_set_animated (kmaterial_renderer *state, b8 is_animated);

// Updates material instance immediates using the provided data.
KAPI void kmaterial_renderer_apply_immediates (kmaterial_renderer *state, kmaterial_instance instance, const kmaterial_render_immediate_data *immediates);
