#pragma once

#include "core/frame_data.h"
#include "core_render_types.h"
#include "renderer/renderer_types.h"

#include <defines.h>
#include <math/geometry.h>
#include <math/math_types.h>
#include <memory/allocators/pool_allocator.h>
#include <strings/kname.h>

#define KANIMATION_MAX_BONES 64
#define KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL "Kohi.StorageBuffer.AnimationsGlobal"

typedef enum kmodel_type {
	KMODEL_TYPE_STATIC,
	KMODEL_TYPE_ANIMATED
} kmodel_type;

typedef struct anim_key_vec3 {
	vec3 value;
	f32 time;
} anim_key_vec3;

typedef struct anim_key_quat {
	quat value;
	f32 time;
} anim_key_quat;

// Animation channel for a node.
typedef struct kmodel_channel {
	kname name;
	u32 pos_count;
	anim_key_vec3 *positions;
	u32 scale_count;
	anim_key_vec3 *scales;
	u32 rot_count;
	anim_key_quat *rotations;
} kmodel_channel;

// Animation that contains channels.
typedef struct kmodel_animation {
	kname name;
	f32 duration;
	f32 ticks_per_second;
	u32 channel_count;
	kmodel_channel *channels;
} kmodel_animation;

// Bone data
typedef struct kmodel_bone {
	kname name;
	// Transformation from mesh space to bone space.
	mat4 offset;
	// Index into bone array.
	u32 id;
} kmodel_bone;

typedef struct kmodel_node {
	kname name;
	mat4 local_transform;
	u16 parent_index; // INVALID_ID = root
	u16 child_count;
	u16 *children;
} kmodel_node;

typedef struct kmodel_submesh {
	kname name;
	kgeometry geo;
	kname material_name;
} kmodel_submesh;

typedef enum kmodel_state {
	// Slot is "free" for use.
	KMODEL_STATE_UNINITIALIZED,
	// Slot marked as taken, but loading has not yet begun.
	KMODEL_STATE_ACQUIRED,
	// Model is loading.
	KMODEL_STATE_LOADING,
	// Model is loaded and ready for use.
	KMODEL_STATE_LOADED
} kmodel_state;

typedef enum kmodel_instance_state {
	KMODEL_INSTANCE_STATE_UNINITIALIZED,
	KMODEL_INSTANCE_STATE_ACQUIRED
} kmodel_instance_state;

typedef struct kmodel_animation_shader_data {
	mat4 final_bone_matrices[KANIMATION_MAX_BONES];
} kmodel_animation_shader_data;

typedef enum kmodel_animator_state {
	KMODEL_ANIMATOR_STATE_STOPPED,
	KMODEL_ANIMATOR_STATE_PLAYING,
	KMODEL_ANIMATOR_STATE_PAUSED
} kmodel_animator_state;

// One animator = one animated mesh instance state
typedef struct kmodel_animator {
	kname name;
	// Index of the base mesh
	u16 base;
	kname current_animation_name;
	// Index into the animation array. INVALID_ID_U16 = no current animation.
	u16 current_animation;
	f32 time_in_ticks;
	f32 time_scale;
	b8 loop;
	kmodel_animator_state state;
	// Pointer to shader_data array where data is stored.
	u32 shader_data_index;
	kmodel_animation_shader_data *shader_data;
	u32 max_bones;
} kmodel_animator;

typedef struct kmodel_instance_data {
	kmodel_instance_state state;
	kmodel_animator animator;
	// NOTE: Size aligns with base mesh submesh count.
	kmaterial_instance *materials;
} kmodel_instance_data;

// This is the "base" model, queried by all animators/instances
typedef struct kmodel_base {
	u16 id;
	kmodel_type type;
	kname asset_name;
	kname package_name;

	u32 animation_count;
	kmodel_animation *animations;
	u32 bone_count;
	kmodel_bone *bones;
	u32 node_count;
	kmodel_node *nodes;
	mat4 global_inverse_transform;

	u32 submesh_count;
	kmodel_submesh *meshes;

	u32 instance_count;
	// The instances of this model.
	kmodel_instance_data *instances;
} kmodel_base;

typedef struct kmodel_instance {
	u16 base_mesh;
	u16 instance;
} kmodel_instance;

typedef struct kmodel_system_config {
	kname default_application_package_name;
	// Max number of instances shared across all meshes.
	u16 max_instance_count;
} kmodel_system_config;

typedef void (*PFN_animated_mesh_loaded)(kmodel_instance instance, void *context);

typedef struct kmodel_instance_queue_entry {
	u16 base_mesh_id;
	u16 instance_id;
	PFN_animated_mesh_loaded callback;
	void *context;
} kmodel_instance_queue_entry;

typedef struct kmodel_system_state {
	kname default_application_package_name;
	// Max number of instances shared across all meshes.
	u16 max_instance_count;

	f32 global_time_scale;

	// darray Base meshes.
	u32 max_mesh_count;
	kmodel_base *models;
	kmodel_state *states;

	krenderbuffer global_animation_ssbo;

	// Queue of instances awaiting base asset load.
	kmodel_instance_queue_entry *instance_queue;

	// Element count = max_instance_count
	pool_allocator shader_data_pool;
	kmodel_animation_shader_data *shader_data;
} kmodel_system_state;

b8 kmodel_system_initialize (u64 *memory_requirement, kmodel_system_state *memory, const kmodel_system_config *config);
void kmodel_system_shutdown (kmodel_system_state *state);

void kmodel_system_update (kmodel_system_state *state, f32 delta_time, frame_data *p_frame_data);
void kmodel_system_frame_prepare (kmodel_system_state *state, frame_data *p_frame_data);

KAPI void kmodel_system_time_scale (kmodel_system_state *state, f32 time_scale); // 1.0 - normal

KAPI kmodel_instance kmodel_instance_acquire (struct kmodel_system_state *state, kname asset_name, PFN_animated_mesh_loaded callback, void *context);
KAPI kmodel_instance kmodel_instance_acquire_from_package (struct kmodel_system_state *state, kname asset_name, kname package_name, PFN_animated_mesh_loaded callback, void *context);
// NOTE: Also releases held material instances.
KAPI void kmodel_instance_release (struct kmodel_system_state *state, kmodel_instance *instance);

KAPI b8 kmodel_ray_intersects (struct kmodel_system_state *state, kmodel_instance instance, const ray *r, mat4 world, raycast_hit *out_hit);
KAPI b8 kmodel_submesh_count_get (struct kmodel_system_state *state, u16 base_mesh_id, u16 *out_count);
KAPI b8 kmodel_is_loaded (struct kmodel_system_state *state, u16 base_mesh_id);
KAPI const kgeometry *kmodel_submesh_geometry_get_at (struct kmodel_system_state *state, u16 base_mesh_id, u16 index);
KAPI const kmaterial_instance *kmodel_submesh_material_instance_get_at (struct kmodel_system_state *state, kmodel_instance instance, u16 index);

// NOTE: Returns dynamic array, needs to be freed by caller.
KAPI kname *kmodel_query_animations (struct kmodel_system_state *state, u16 base_mesh, u32 *out_count);

KAPI void kmodel_instance_animation_set (struct kmodel_system_state *state, kmodel_instance instance, kname animation_name);
KAPI u32 kmodel_instance_animation_id_get (struct kmodel_system_state *state, kmodel_instance instance);

KAPI void kmodel_instance_time_scale_set (kmodel_system_state *state, kmodel_instance instance, f32 time_scale); // 1.0 - normal
KAPI void kmodel_instance_loop_set (struct kmodel_system_state *state, kmodel_instance instance, b8 loop);
KAPI void kmodel_instance_play (struct kmodel_system_state *state, kmodel_instance instance);
KAPI void kmodel_instance_pause (struct kmodel_system_state *state, kmodel_instance instance);
KAPI void kmodel_instance_stop (struct kmodel_system_state *state, kmodel_instance instance);
KAPI void kmodel_instance_seek (struct kmodel_system_state *state, kmodel_instance instance, f32 time);			   // 0-total animation track time
KAPI void kmodel_instance_seek_percent (struct kmodel_system_state *state, kmodel_instance instance, f32 percent); // 0-1
