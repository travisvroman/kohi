#pragma once

#include "systems/kmatrix_system.h"
#include "world/heightfield_terrain.h"
#include <core/frame_data.h>
#include <core_render_types.h>
#include <core_resource_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <renderer/renderer_types.h>
#include <systems/kmaterial_system.h>
#include <systems/light_system.h>
#include <utils/kcolour.h>

#define DEFAULT_SHADOW_BIAS 0.0005f
#define DEFAULT_SHADOW_DIST 100.0f
#define DEFAULT_SHADOW_FADE_DIST 5.0f
#define DEFAULT_SHADOW_SPLIT_MULT 0.75f

struct renderer_system_state;
struct standard_ui_renderable;

typedef struct kshadow_pass_data {
	// Static meshes
	kshader staticmesh_shader;
	u32 sm_set0_instance_id;
	u32 sm_set1_max_instances;
	u32 *sm_set1_instance_ids;
	// Used for opaque material rendering. Typically the first instance of the above list.
	u32 sm_default_instance_id;

	// Heightmap terrain
	kshader hmt_shader;
	u32 hmt_set0_instance_id;

	// Heightfield terrain
	kshader hft_shader;
	u32 hft_set0_instance_id;

	ktexture default_base_colour;

	u32 resolution;

	ktexture shadow_tex;
} kshadow_pass_data;

typedef struct kforward_pass_data {
	// Skybox shader
	kshader sb_shader;
	u32 sb_shader_set0_instance_id;

	ktexture default_cube_texture;

	kshader hf_terrain_shader;
	u32 shader_set0_instance_id;
} kforward_pass_data;

typedef struct kdepth_prepass_data {
	kshader depth_prepass_shader;
	u32 shader_set0_instance_id;
} kdepth_prepass_data;

#if KOHI_DEBUG

typedef struct kworld_debug_pass_data {
	kshader debug_shader;
	kshader colour_shader;
	u32 debug_set0_instance_id;
	u32 colour_set0_instance_id;
} kworld_debug_pass_data;

#endif

/**
 * @brief Represents the state of the Kohi Default Forward application renderer.
 */
typedef struct kforward_renderer {
	ktexture colour_buffer;
	ktexture depth_stencil_buffer;

	struct renderer_system_state *renderer_state;
	struct kmaterial_system_state *material_system;
	struct kmaterial_renderer *material_renderer;
	struct texture_system_state *texture_system;
	struct kmatrix_system_state *matrix_system;

	kdepth_prepass_data depth_prepass;
	kshadow_pass_data shadow_pass;
	kforward_pass_data forward_pass;
#if KOHI_DEBUG
	kworld_debug_pass_data world_debug_pass;
#endif

	krenderbuffer standard_vertex_buffer;
	krenderbuffer index_buffer;

} kforward_renderer;

typedef struct kskybox_render_data {
	b8 do_pass;
	u32 shader_set0_instance_id;
	ktexture skybox_texture;
	vec4 fog_colour;
	u32 sb_vertex_count;
	u64 sb_vertex_offset;
	u32 sb_index_count;
	u64 sb_index_offset;
} kskybox_render_data;

typedef enum kgeometry_render_data_flag_bits {
	KGEOMETRY_RENDER_DATA_FLAG_NONE = 0,
	KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT = 1 << 0,
} kgeometry_render_data_flag_bits;

typedef u32 kgeometry_render_data_flags;

// static mesh data for shadow pass.
typedef struct kgeometry_render_data {
	u64 vertex_offset;
	u64 extended_vertex_offset;
	u32 vertex_count;
	u64 index_offset;
	u32 index_count;
	kgeometry_render_data_flags flags;

	// The material instance for this geometry.
	u16 material_instance_id;
	ktransform transform;
	// Index into animation data SSBO. Ignored if INVALID_ID_U16.
	u16 animation_id;

	u8 bound_point_light_count;
	u8 bound_point_light_indices[KMATERIAL_MAX_BOUND_POINT_LIGHTS];
} kgeometry_render_data;

typedef struct kmaterial_render_data {
	// The base material used by all the geometries contained.
	kmaterial base_material;
	// The number of geometries.
	u32 geometry_count;
	// An array of geometries using the material.
	kgeometry_render_data *geometries;
} kmaterial_render_data;

typedef struct hm_terrain_chunk_render_data {
	u64 vertex_offset;
	u64 extended_vertex_offset;
	u64 vertex_count;
	u64 index_offset;
	u64 index_count;
} hm_terrain_chunk_render_data;

typedef struct hm_terrain_render_data {
	kmaterial_instance material_instance;
	ktransform transform;
	u32 chunk_count;
	hm_terrain_chunk_render_data *chunks;
} hm_terrain_render_data;

typedef struct kshadow_pass_cascade_render_data {
	mat4 view_projection;
} kshadow_pass_cascade_render_data;

typedef struct kshadow_pass_render_data {
	klight dir_light;

	u32 cascade_count;
	kshadow_pass_cascade_render_data *cascades;

	// The number of opaque geometries.
	u16 opaque_geometry_count;
	// An array of geometries whose materials are opaque and can thus be rendered with the defualt group.
	kgeometry_render_data *opaque_geometries;

	// Static mesh geo data organized by transparent material.
	u16 transparent_geometries_by_material_count;
	kmaterial_render_data *transparent_geometries_by_material;

	// The number of animated opaque geometries.
	u16 animated_opaque_geometry_count;
	// An array of animated geometries whose materials are opaque and can thus be rendered with the defualt group.
	kgeometry_render_data *animated_opaque_geometries;

	// Animated mesh geo data organized by transparent material.
	u16 animated_transparent_geometries_by_material_count;
	kmaterial_render_data *animated_transparent_geometries_by_material;

	// Terrain geo data
	u16 terrain_count;
	hm_terrain_render_data *terrains;

	hf_terrain_render_data hf_terrain_data;

	// Indicates if the pass should be done.
	b8 do_pass;
} kshadow_pass_render_data;

// Water plane render data used once for reflection and once for refraction.
typedef struct kscene_pass_render_data {
	kmatrix_id view_id;
	vec3 view_position;

	// Opaque static mesh geo data organized by material.
	u16 opaque_meshes_by_material_count;
	kmaterial_render_data *opaque_meshes_by_material;

	// Transparent static mesh geo data organized by material.
	u16 transparent_meshes_by_material_count;
	kmaterial_render_data *transparent_meshes_by_material;

	// Opaque animated mesh geo data organized by material.
	u16 animated_opaque_meshes_by_material_count;
	kmaterial_render_data *animated_opaque_meshes_by_material;

	// Transparent animated mesh geo data organized by material.
	u16 animated_transparent_meshes_by_material_count;
	kmaterial_render_data *animated_transparent_meshes_by_material;

	// Terrain geo data
	u16 terrain_count;
	hm_terrain_render_data *terrains;

	hf_terrain_render_data hf_terrain_data;
} kscene_pass_render_data;

typedef struct kwater_plane_render_data {
	// Water plane model matrix
	ktransform transform;
	u64 index_buffer_offset;
	u64 vertex_buffer_offset;

	// Instance of water material.
	kmaterial_instance material;

	u8 bound_point_light_count;
	u8 bound_point_light_indices[KMATERIAL_MAX_BOUND_POINT_LIGHTS];
} kwater_plane_render_data;

// Render data used per water plane,
typedef struct kforward_pass_water_plane_render_data {
	kwater_plane_render_data plane_render_data;

	// Data used for the water plane reflection pass.
	kscene_pass_render_data reflection_pass;
	// Data used for the water plane refraction pass.
	kscene_pass_render_data refraction_pass;

} kforward_pass_water_plane_render_data;

typedef struct kforward_pass_render_data {
	// View matrix/position used for the rendering of the water plane itself.
	kmatrix_id view_id;
	vec4 view_position;

	kmatrix_id projection_id;

	u32 render_mode;

	mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES];
	f32 cascade_splits[KMATERIAL_MAX_SHADOW_CASCADES];
	f32 shadow_bias; // NOTE: 0.0005f is a good value;

	u8 irradiance_cubemap_texture_count;
	ktexture irradiance_cubemap_textures[KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT];

	// Skybox data
	kskybox_render_data skybox;

	f32 shadow_distance;
	f32 shadow_fade_distance;
	f32 shadow_split_mult;

	f32 near_clip;
	f32 far_clip;

	colour4 fog_colour;
	f32 fog_near;
	f32 fog_far;

	kdirectional_light_data dir_light;

	// Water planes
	u16 water_plane_count;
	kforward_pass_water_plane_render_data *water_planes;

	// Data to be used after reflection/refraction passes.
	kscene_pass_render_data standard_pass;

	// Indicates if the pass should be done.
	b8 do_pass;
} kforward_pass_render_data;

typedef struct kdebug_geometry_render_data {
	kgeometry_render_data geo;
	mat4 model;
	colour4 colour;
} kdebug_geometry_render_data;

#if KOHI_DEBUG
typedef struct kworld_debug_pass_render_data {
	mat4 projection;
	mat4 view;

	// The number of geometries.
	u16 geometry_count;
	// An array of geometries.
	kdebug_geometry_render_data *geometries;

	b8 draw_grid;
	kdebug_geometry_render_data grid_geometry;

	b8 do_pass;
} kworld_debug_pass_render_data;
#endif

typedef struct kforward_renderer_render_data {

	// Data to render in the shadow pass.
	kshadow_pass_render_data shadow_data;

	// Data to render in the forward pass.
	kforward_pass_render_data forward_data;

#if KOHI_DEBUG
	// Data to render world debug geometry
	kworld_debug_pass_render_data world_debug_data;
#endif
} kforward_renderer_render_data;

KAPI b8 kforward_renderer_create (ktexture colour_buffer, ktexture depth_stencil_buffer, kforward_renderer *out_renderer);
KAPI void kforward_renderer_destroy (kforward_renderer *renderer);

KAPI b8 kforward_renderer_render_frame (kforward_renderer *renderer, frame_data *p_frame_data, kforward_renderer_render_data *render_data);
