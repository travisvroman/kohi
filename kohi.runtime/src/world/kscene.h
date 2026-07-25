#pragma once

#include <core/frame_data.h>
#include <core_resource_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <strings/kstring_id.h>

#include "renderer/kforward_renderer.h"
#include "systems/kcamera_system.h"
#include "systems/kmodel_system.h"
#include "systems/light_system.h"
#include "utils/kcolour.h"
#include "world/heightfield_terrain.h"
#include "world/world_types.h"

struct kscene;
struct frame_data;
struct ray;
struct raycast_result;
struct avatar;

typedef enum kscene_state {
	KSCENE_STATE_UNINITIALIZED,
	KSCENE_STATE_PARSING_CONFIG,
	KSCENE_STATE_LOADING,
	KSCENE_STATE_PRE_LOADED,
	KSCENE_STATE_LOADED,
} kscene_state;

typedef enum kscene_render_data_flag {
	KSCENE_RENDER_DATA_FLAG_NONE = 0,
	// Only get transparent geometry. Don't set this flag if opaque geometry is needed.
	KSCENE_RENDER_DATA_FLAG_TRANSPARENT_BIT = 1 << 0,

	KSCENE_RENDER_INCLUDE_BVH_DEBUG_BIT = 1 << 1,
} kscene_render_data_flag;

typedef u32 kscene_render_data_flag_bits;

typedef enum kscene_flag_bits {
	KSCENE_FLAG_NONE = 0,
	KSCENE_FLAG_RENDER_SKYBOX_BIT = 1 << 1,
	KSCENE_FLAG_RENDER_STATIC_MODELS_BIT = 1 << 2,
	KSCENE_FLAG_RENDER_ANIMATED_MODELS_BIT = 1 << 3,
	KSCENE_FLAG_RENDER_HF_TERRAIN_BIT = 1 << 4,
	KSCENE_FLAG_RENDER_WATER_PLANES_BIT = 1 << 5,
	KSCENE_FLAG_RENDER_FOG_BIT = 1 << 6,
	KSCENE_FLAG_DEBUG_ENABLED_BIT = 1 << 29,
	KSCENE_FLAG_DEBUG_GRID_ENABLED_BIT = 1 << 30,
	KSCENE_FLAG_DEBUG_BVH_ENABLED_BIT = 1 << 31,
} kscene_flag_bits;

typedef u32 kscene_flags;

typedef struct hf_terrain_material_data {
	kstring_id name;
	kname albedo_asset_name;
	kname albedo_asset_package_name;
	kname normal_asset_name;
	kname normal_asset_package_name;
	kname mra_asset_name;
	kname mra_asset_package_name;
} hf_terrain_material_data;

typedef void (*PFN_scene_loaded)(struct kscene *scene, void *context);

// Creates the scene and kicks off the loading process.
KAPI struct kscene *kscene_create (kname kscene_asset_name, const char *config, PFN_scene_loaded loaded_callback, void *load_context, b8 is_editor);
KAPI void kscene_destroy (struct kscene *scene);

KAPI void kscene_on_window_resize (struct kscene *scene, const struct kwindow *window);

KAPI b8 kscene_update (struct kscene *scene, struct frame_data *p_frame_data);
KAPI b8 kscene_frame_prepare (struct kscene *scene, struct frame_data *p_frame_data, u32 render_mode, kcamera current_camera);

KAPI kscene_state kscene_state_get (const struct kscene *scene);

KAPI kscene_flags kscene_get_flags (const struct kscene *scene);
KAPI b8 kscene_get_flag (const struct kscene *scene, kscene_flag_bits flag);
KAPI void kscene_set_flags (struct kscene *scene, kscene_flags flags);
KAPI void kscene_set_flag (struct kscene *scene, kscene_flags flag, b8 enabled);

KAPI const char *kscene_get_name (const struct kscene *scene);
KAPI void kscene_set_name (struct kscene *scene, const char *name);
KAPI colour4 kscene_get_fog_colour (const struct kscene *scene);
KAPI void kscene_set_fog_colour (struct kscene *scene, colour4 colour);
KAPI f32 kscene_get_fog_near (const struct kscene *scene);
KAPI void kscene_set_fog_near (struct kscene *scene, f32 near);
KAPI f32 kscene_get_fog_far (const struct kscene *scene);
KAPI void kscene_set_fog_far (struct kscene *scene, f32 far);

KAPI void kscene_get_shadow_properties (struct kscene *scene, kcamera current_camera, f32 *out_shadow_dist, f32 *out_shadow_fade_distance, f32 *out_shadow_split_mult, f32 *out_shadow_bias);

KAPI b8 kscene_raycast (struct kscene *scene, const struct ray *r, struct raycast_result *out_result);
KAPI b8 kscene_hf_terrain_raycast (struct kscene *scene, const ray *r, b8 use_editor_aabb, hf_block *out_block, hf_chunk *out_chunk, vec3 *out_pos, vec3 *out_normal);
KAPI hf_terrain *kscene_hf_terrain_get (struct kscene *scene);

/**
 * Attempts to retrieve the terrain height at the given location. A false result means there is no terrain at that
 * location, which can mean _either_ out-of-bounds or that there is a hole in the terrain at that location.
 *
 */
KAPI b8 kscene_hf_terrain_get_height_at (struct kscene *scene, f32 world_x, f32 world_z, vec3 *out_pos, vec3 *out_normal);
KAPI b8 kscene_hf_terrain_get_height_at_fast (struct kscene *scene, f32 world_x, f32 world_z, f32 *out_height);

KAPI kentity kscene_get_entity_by_name (struct kscene *scene, kname name);

KAPI kentity_flags kscene_get_entity_flags (struct kscene *scene, kentity entity);
KAPI void kscene_set_entity_flags (struct kscene *scene, kentity entity, kentity_flags flags);
KAPI void kscene_set_entity_flag (struct kscene *scene, kentity entity, kentity_flag_bits flag, b8 enabled);
KAPI kname kscene_get_entity_name (struct kscene *scene, kentity entity);
KAPI void kscene_set_entity_name (struct kscene *scene, kentity entity, kname name);
KAPI kentity_type kscene_get_entity_type (struct kscene *scene, kentity entity);
KAPI kentity *kscene_get_entity_children (struct kscene *scene, kentity entity, u16 *out_count);
KAPI kentity kscene_get_entity_parent (struct kscene *scene, kentity entity);
KAPI ktransform kscene_get_entity_transform (struct kscene *scene, kentity entity);
KAPI extents_3d kscene_get_aabb (struct kscene *scene, kentity entity);

KAPI vec3 kscene_get_entity_position (struct kscene *scene, kentity entity);
KAPI void kscene_set_entity_position (struct kscene *scene, kentity entity, vec3 position);
KAPI quat kscene_get_entity_rotation (struct kscene *scene, kentity entity);
KAPI void kscene_set_entity_rotation (struct kscene *scene, kentity entity, quat rotation);
KAPI vec3 kscene_get_entity_scale (struct kscene *scene, kentity entity);
KAPI void kscene_set_entity_scale (struct kscene *scene, kentity entity, vec3 scale);

KAPI void kscene_remove_entity (struct kscene *scene, kentity *entity);

KAPI kentity kscene_add_entity (struct kscene *scene, kname name, ktransform transform, kentity parent);

typedef void (*PFN_model_loaded)(kentity entity, kmodel_instance inst, void *context);
KAPI kentity kscene_add_model_pos_rot_scale (struct kscene *scene, kname name, kentity parent, kname asset_name, kname package_name, vec3 pos, quat rot, vec3 scale);
KAPI kentity kscene_add_model (struct kscene *scene, kname name, ktransform transform, kentity parent, kname asset_name, kname package_name, PFN_model_loaded on_loaded_callback, void *load_context);

/**
 * @brief Creates and adds a new point light entity to the scene.
 *
 * @param colour The light colour.
 * @param linear Reduces light intensity linearly.
 * @param quadratic Makes the light fall off slower at longer distances.
 */
KAPI kentity kscene_add_point_light (struct kscene *scene, kname name, ktransform transform, kentity parent, vec3 colour, f32 linear, f32 quadratic);

KAPI kentity kscene_add_spawn_point (struct kscene *scene, kname name, ktransform transform, kentity parent, f32 radius);

KAPI kentity kscene_add_volume (
	struct kscene *scene,
	kname name,
	ktransform transform,
	kentity parent,
	kscene_volume_type type,
	kcollision_shape shape,
	u8 hit_shape_tag_count,
	kstring_id *hit_shape_tags,
	const char *on_enter_command,
	const char *on_leave_command,
	const char *on_tick_command);

KAPI kentity kscene_add_hit_shape (
	struct kscene *scene,
	kname name,
	ktransform transform,
	kentity parent,
	kcollision_shape shape,
	u8 tag_count,
	kstring_id *tags);

KAPI kentity kscene_add_water_plane (
	struct kscene *scene,
	kname name,
	ktransform transform,
	kentity parent,
	f32 size);

KAPI kentity kscene_add_audio_emitter (
	struct kscene *scene,
	kname name,
	ktransform transform,
	kentity parent,
	f32 inner_radius,
	f32 outer_radius,
	f32 volume,
	f32 falloff,
	b8 is_looping,
	b8 is_streaming,
	kname asset_name,
	kname package_name);

KAPI kentity kscene_add_spawn_point (struct kscene *scene, kname name, ktransform transform, kentity parent, f32 radius);

KAPI kmodel_instance kscene_model_entity_get_instance (struct kscene *scene, kentity entity);

KAPI kdirectional_light_data kscene_get_directional_light_data (struct kscene *scene);

KAPI kskybox_render_data kscene_get_skybox_render_data (struct kscene *scene);

// Gets static mesh render data, organized by material.
KAPI kmaterial_render_data *kscene_get_static_model_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	kscene_render_data_flag_bits flags,
	u16 *out_material_count);

KAPI kmaterial_render_data *kscene_get_animated_model_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	kscene_render_data_flag_bits flags,
	u16 *out_material_count);

// Gets terrain chunk render data.
KAPI hm_terrain_render_data *kscene_get_hm_terrain_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	u32 flags,
	u16 *out_terrain_count);

KAPI hf_terrain_render_data kscene_get_hf_terrain_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	u32 flags);

// NOTE: Caller should free the dynamically-allocated array.
KAPI hf_terrain_material_data *kscene_get_hf_terrain_materials (struct kscene *scene, u8 *out_count);

#if KOHI_DEBUG
KAPI kdebug_geometry_render_data *kscene_get_debug_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	u32 flags,
	u16 *out_geometry_count);

KAPI kdebug_geometry_render_data kscene_get_editor_gizmo_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	u32 flags);
#endif

KAPI kwater_plane_render_data *kscene_get_water_plane_render_data (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	kfrustum *frustum,
	u32 flags,
	u16 *out_water_plane_count);

KAPI kentity *kscene_get_spawn_points (
	struct kscene *scene,
	u32 flags,
	u16 *out_spawn_point_count);

KAPI klight_render_data *kscene_get_all_point_lights (
	struct kscene *scene,
	struct frame_data *p_frame_data,
	u32 flags,
	u16 *out_point_light_count);

KAPI const char *kscene_serialize (const struct kscene *scene);
KAPI b8 kscene_hf_terrain_save (const struct kscene *scene);

KAPI void kscene_dump_hierarchy (const struct kscene *scene);

typedef struct kscene_hierarchy_node {
	kentity entity;
	u32 child_count;
	struct kscene_hierarchy_node *children;
} kscene_hierarchy_node;

KAPI kscene_hierarchy_node *kscene_get_hierarchy (const struct kscene *scene, u32 *out_count);
KAPI void kscene_cleanup_hierarchy (kscene_hierarchy_node *nodes, u32 count);
