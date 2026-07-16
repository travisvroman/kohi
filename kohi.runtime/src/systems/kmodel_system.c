#include "kmodel_system.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core/engine.h"
#include "core/event.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/allocators/pool_allocator.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "strings/kname.h"
#include "systems/asset_system.h"
#include "systems/kmaterial_system.h"

static void animator_update (kmodel_system_state *state, kmodel_animator *animator, f32 delta_time);
static void ensure_arrays_allocated (kmodel_system_state *state, u32 new_count);
static void ensure_instance_arrays_allocated (kmodel_base *base, u32 new_count);
static b8 get_base_id (struct kmodel_system_state *state, kname asset_name, kname package_name, u16 *out_id);
static u16 get_new_instance_id (struct kmodel_system_state *state, u16 base_id);
static void acquire_material_instances (struct kmodel_system_state *state, u16 base_id, u16 instance_id);

static void animator_set_animation (kmodel_system_state *state, kmodel_animator *animator, kname name);

b8 kmodel_system_initialize (u64 *memory_requirement, kmodel_system_state *memory, const kmodel_system_config *config) {
	*memory_requirement = sizeof(kmodel_system_state);

	if (!memory) {
		return true;
	}

	u32 max_instance_count = config->max_instance_count ? config->max_instance_count : 100;

	kmodel_system_state *state = memory;

	state->default_application_package_name = config->default_application_package_name;
	state->max_instance_count = max_instance_count;

	state->global_time_scale = 1.0f;

	// Global lighting storage buffer
	u64 buffer_size = sizeof(kmodel_animation_shader_data) * state->max_instance_count;
	state->global_animation_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_ANIMATIONS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT | RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT);
	KASSERT(state->global_animation_ssbo != KRENDERBUFFER_INVALID);
	KDEBUG("Created kanimation global storage buffer.");

	// The free states of instances here are managed by a pool allocator.
	state->shader_data_pool = pool_allocator_create(sizeof(kmodel_animation_shader_data), state->max_instance_count);
	state->shader_data = (kmodel_animation_shader_data *)state->shader_data_pool.memory;

	state->instance_queue = darray_create(kmodel_instance_queue_entry);

	return true;
}

void kmodel_system_shutdown (kmodel_system_state *state) {
	darray_destroy(state->instance_queue);
	for (u32 b = 0; b < state->max_mesh_count; ++b) {
		u32 instance_count = state->models[b].instance_count;

		for (u32 i = 0; i < instance_count; ++i) {
			kmodel_instance inst = {
				.base_mesh = b,
				.instance = i};
			kmodel_instance_release(state, &inst);
		}
	}

	if (state) {
		renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, state->global_animation_ssbo);
	}
}

void kmodel_system_update (kmodel_system_state *state, f32 delta_time, frame_data *p_frame_data) {
	// Iterate all mesh instances and update thier final_bone_matrices.
	for (u32 b = 0; b < state->max_mesh_count; ++b) {
		u32 instance_count = state->models[b].instance_count;

		for (u32 i = 0; i < instance_count; ++i) {
			animator_update(state, &state->models[b].instances[i].animator, delta_time);
		}
	}
}

void kmodel_system_frame_prepare (kmodel_system_state *state, frame_data *p_frame_data) {
	// Upload all of the mesh instance final_bone_matrices to the SSBO.
	void *memory = renderer_renderbuffer_get_mapped_memory(engine_systems_get()->renderer_system, state->global_animation_ssbo);

	kcopy_memory(memory, state->shader_data, sizeof(kmodel_animation_shader_data) * state->max_instance_count);
}

void kmodel_system_time_scale (kmodel_system_state *state, f32 time_scale) {
	KASSERT_DEBUG(state);
	state->global_time_scale = time_scale;
}

kmodel_instance kmodel_instance_acquire (struct kmodel_system_state *state, kname asset_name, PFN_animated_mesh_loaded callback, void *context) {
	return kmodel_instance_acquire_from_package(state, asset_name, state->default_application_package_name, callback, context);
}

typedef struct animated_mesh_asset_request_listener {
	kmodel_system_state *state;
	u16 base_id;
} animated_mesh_asset_request_listener;

static void kasset_model_loaded (void *listener, kasset_model *asset) {
	animated_mesh_asset_request_listener *typed_listener = (animated_mesh_asset_request_listener *)listener;
	KDEBUG("%s - model loaded", __FUNCTION__);

	kmodel_system_state *state = typed_listener->state;
	u16 base_id = typed_listener->base_id;

	// Base mesh setup.
	kmodel_base *base = &state->models[base_id];
	base->global_inverse_transform = asset->global_inverse_transform;

	// NOTE: All these copies are here because the asset and state types here might diverge at some point.
	// If this doesn't wind up happening, the types could be unified and this could be copied all at once instead.
	base->bone_count = asset->bone_count;
	if (base->bone_count) {
		base->bones = KALLOC_TYPE_CARRAY(kmodel_bone, base->bone_count);
		for (u32 i = 0; i < base->bone_count; ++i) {
			kasset_model_bone *source = &asset->bones[i];
			kmodel_bone *target = &base->bones[i];
			target->id = source->id;
			target->name = source->name;
			target->offset = source->offset;
		}
	}

	base->node_count = asset->node_count;
	if (base->node_count) {
		base->nodes = KALLOC_TYPE_CARRAY(kmodel_node, base->node_count);
		for (u32 i = 0; i < base->node_count; ++i) {
			kasset_model_node *source = &asset->nodes[i];
			kmodel_node *target = &base->nodes[i];

			target->name = source->name;
			/* KTRACE("node: '%k'", target->name); */
			target->parent_index = source->parent_index;
			target->local_transform = source->local_transform;
			target->child_count = source->child_count;
			if (target->child_count) {
				target->children = KALLOC_TYPE_CARRAY(u16, target->child_count);
				KCOPY_TYPE_CARRAY(target->children, source->children, u16, target->child_count);
			}
		}
	}

	base->animation_count = asset->animation_count;
	if (base->animation_count) {
		KTRACE("This model has the following animations available:");

		// NOTE: The presence of animations marks this as an animated model type.
		base->type = KMODEL_TYPE_ANIMATED;

		base->animations = KALLOC_TYPE_CARRAY(kmodel_animation, base->animation_count);
		for (u32 i = 0; i < base->animation_count; ++i) {
			kasset_model_animation *source = &asset->animations[i];
			kmodel_animation *target = &base->animations[i];

			target->name = source->name;
			KTRACE(kname_string_get(target->name));

			target->ticks_per_second = source->ticks_per_second;
			target->duration = source->duration;
			target->channel_count = source->channel_count;
			if (target->channel_count) {
				target->channels = KALLOC_TYPE_CARRAY(kmodel_channel, target->channel_count);

				for (u32 c = 0; c < target->channel_count; c++) {
					kasset_model_channel *sc = &source->channels[c];
					kmodel_channel *tc = &target->channels[c];

					tc->name = sc->name;
					tc->pos_count = sc->pos_count;
					if (tc->pos_count) {
						tc->positions = KALLOC_TYPE_CARRAY(anim_key_vec3, tc->pos_count);
						for (u32 k = 0; k < tc->pos_count; ++k) {
							tc->positions[k].time = sc->positions[k].time;
							tc->positions[k].value = sc->positions[k].value;
						}
					}
					tc->rot_count = sc->rot_count;
					if (tc->rot_count) {
						tc->rotations = KALLOC_TYPE_CARRAY(anim_key_quat, tc->rot_count);
						for (u32 k = 0; k < tc->rot_count; ++k) {
							tc->rotations[k].time = sc->rotations[k].time;
							tc->rotations[k].value = sc->rotations[k].value;
						}
					}
					tc->scale_count = sc->scale_count;
					if (tc->scale_count) {
						tc->scales = KALLOC_TYPE_CARRAY(anim_key_vec3, tc->scale_count);
						for (u32 k = 0; k < tc->scale_count; ++k) {
							tc->scales[k].time = sc->scales[k].time;
							tc->scales[k].value = sc->scales[k].value;
						}
					}
				}
			}
		}
	}

	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer standard_vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// Finally, the meshes
	base->submesh_count = asset->submesh_count;
	if (base->submesh_count) {
		b8 is_animated = base->type == KMODEL_TYPE_ANIMATED;

		base->meshes = KALLOC_TYPE_CARRAY(kmodel_submesh, base->submesh_count);
		for (u32 i = 0; i < base->submesh_count; ++i) {
			kmodel_submesh *target = &base->meshes[i];
			kasset_model_submesh_data *source = &asset->submeshes[i];

			target->name = source->name;
			target->material_name = source->material_name;
			KTRACE("Model submesh %u has a material_name of '%s'", i, kname_string_get(source->material_name));
			target->geo.name = source->name;
			target->geo.generation = INVALID_ID_U16;
			target->geo.type = is_animated ? KGEOMETRY_TYPE_3D_SKINNED : KGEOMETRY_TYPE_3D_STATIC;

			target->geo.vertex_element_size = is_animated ? sizeof(skinned_vertex_3d) : sizeof(vertex_3d);
			target->geo.vertex_count = source->vertex_count;
			target->geo.vertices = kallocate(target->geo.vertex_element_size * target->geo.vertex_count, MEMORY_TAG_ARRAY);

			kcopy_memory(target->geo.vertices, source->vertices, target->geo.vertex_element_size * target->geo.vertex_count);

			target->geo.index_element_size = sizeof(u32);
			target->geo.index_count = source->index_count;
			target->geo.indices = KALLOC_TYPE_CARRAY(u32, source->index_count);
			KCOPY_TYPE_CARRAY(target->geo.indices, source->indices, u32, source->index_count);

			// Extract the extents.
			vec3 min_pos = vec3_create(99999999.9f, 99999999.9f, 99999999.9f);
			vec3 max_pos = vec3_create(-99999999.9f, -99999999.9f, -99999999.9f);

			for (u32 v = 0; v < target->geo.vertex_count; ++v) {
				vec3 *position = (vec3 *)(((u8 *)source->vertices) + (target->geo.vertex_element_size * v));
				min_pos = vec3_min(min_pos, *position);
				max_pos = vec3_max(max_pos, *position);
			}

			target->geo.extents.min = min_pos;
			target->geo.extents.max = max_pos;
			target->geo.center = extents_3d_center(target->geo.extents);

			if (extents_3d_is_zero(target->geo.extents)) {
				KWARN("Zero extents detected. Overriding with one.");
				target->geo.extents = extents_3d_one();
			}

			// Upload the geometry.

			// Standard data first.
			u64 standard_vertex_size = target->geo.vertex_element_size * target->geo.vertex_count;
			u64 standard_vertex_offset = 0;
			if (!renderer_renderbuffer_allocate(renderer_system, standard_vertex_buffer, standard_vertex_size, &target->geo.vertex_buffer_offset)) {
				KERROR("Model system failed to allocate from the renderer's vertex buffer! Submesh geometry won't be uploaded (skipped)");
				continue;
			}
			// TODO: Passing false here produces a queue wait and should be offloaded to another queue.
			if (!renderer_renderbuffer_load_range(renderer_system, standard_vertex_buffer, target->geo.vertex_buffer_offset + standard_vertex_offset, standard_vertex_size, target->geo.vertices + standard_vertex_offset, false)) {
				KERROR("Model system failed to upload to the standard vertex buffer!");
				if (!renderer_renderbuffer_free(renderer_system, standard_vertex_buffer, standard_vertex_size, target->geo.vertex_buffer_offset)) {
					KERROR("Failed to recover from vertex write failure while freeing standard vertex buffer range.");
				}
				continue;
			}

			// Index data, if applicable
			u64 index_size = (u64)(sizeof(u32) * source->index_count);
			u64 index_offset = 0;
			if (index_size) {
				// Allocate space in the buffer.
				if (!renderer_renderbuffer_allocate(renderer_system, index_buffer, index_size, &target->geo.index_buffer_offset)) {
					KERROR("Model system failed to allocate from the renderer index buffer!");
					// Free vertex data
					if (!renderer_renderbuffer_free(renderer_system, standard_vertex_buffer, standard_vertex_size, target->geo.vertex_buffer_offset)) {
						KERROR("Failed to recover from index allocation failure while freeing vertex buffer range.");
					}
					continue;
				}

				// Load the data.
				// TODO: Passing false here produces a queue wait and should be offloaded to another queue.
				if (!renderer_renderbuffer_load_range(renderer_system, index_buffer, target->geo.index_buffer_offset + index_offset, index_size, target->geo.indices + index_offset, false)) {
					KERROR("Model system failed to upload to the renderer index buffer!");
					// Free vertex data
					if (!renderer_renderbuffer_free(renderer_system, standard_vertex_buffer, standard_vertex_size, target->geo.vertex_buffer_offset)) {
						KERROR("Failed to recover from index write failure while freeing vertex buffer range.");
					}
					// Free index data
					if (!renderer_renderbuffer_free(renderer_system, index_buffer, index_size, target->geo.index_buffer_offset)) {
						KERROR("Failed to recover from index write failure while freeing index buffer range.");
					}
					continue;
				}
			}

			target->geo.generation++;
		}
	}

	state->states[base_id] = KMODEL_STATE_LOADED;

	// Setup queued instances.
	for (u16 i = 0; i < darray_length(state->instance_queue);) {
		kmodel_instance_queue_entry *entry = &state->instance_queue[i];
		if (entry->base_mesh_id == typed_listener->base_id) {

			u16 instance_id = entry->instance_id;

			// Instance setup.
			kmodel_instance_data *instance = &state->models[base_id].instances[instance_id];

			acquire_material_instances(state, base_id, instance_id);

			// For animated models, alloc shader data from the animation SSBO.
			if (base->type == KMODEL_TYPE_ANIMATED) {
				kmodel_animator *animator = &instance->animator;
				animator->shader_data = pool_allocator_allocate(&state->shader_data_pool, &animator->shader_data_index);
				animator->time_scale = 1.0f; // Always default time scale to 1.0f
				animator->max_bones = base->bone_count;
				animator->time_in_ticks = 0.0f;

				// Auto-set the first animation and simulate a frame being updated so it shows up
				// properly.
				animator->current_animation = 0;
				animator->current_animation_name = base->animations[0].name;

				kmodel_animator_state prev_state = animator->state;
				animator->state = KMODEL_ANIMATOR_STATE_PLAYING;
				animator_update(state, animator, 0.0f);
				animator->state = prev_state;
			}

			if (entry->callback) {
				kmodel_instance inst = {
					.instance = entry->instance_id,
					.base_mesh = typed_listener->base_id};
				entry->callback(inst, entry->context);
			}

			darray_pop_at(state->instance_queue, i, 0);
		} else {
			++i;
		}
	}

	// After copying over all properties, release the asset.
	asset_system_release_model(engine_systems_get()->asset_state, asset);

	// Cleanup the listener.
	kfree(listener);
}

static void acquire_material_instances (struct kmodel_system_state *state, u16 base_id, u16 instance_id) {
	if (state->states[base_id] != KMODEL_STATE_LOADED) {
		return;
	}

	kmodel_base *base = &state->models[base_id];
	kmodel_instance_data *instance = &base->instances[instance_id];

	// Only do this for acquired instances.
	if (instance->state == KMODEL_INSTANCE_STATE_ACQUIRED) {
		if (!instance->materials) {
			instance->materials = KALLOC_TYPE_CARRAY(kmaterial_instance, base->submesh_count);

			// Acquire material instances.
			for (u32 i = 0; i < base->submesh_count; ++i) {
				kmodel_submesh *mesh = &base->meshes[i];
				if (!kmaterial_system_acquire(engine_systems_get()->material_system, mesh->material_name, &instance->materials[i])) {
					KERROR("Failed to get material '%s' for model submesh '%s'.", kname_string_get(mesh->material_name), kname_string_get(mesh->name));
					// TODO: Should this just use the default material instead?
				}
			}
		}
	}
}

kmodel_instance kmodel_instance_acquire_from_package (struct kmodel_system_state *state, kname asset_name, kname package_name, PFN_animated_mesh_loaded callback, void *context) {
	KASSERT_MSG(state, "State is required, ya dingus");

	// Obtain a unique id for lookup into the resource arrays.
	u16 base_id = INVALID_ID_U16;
	b8 exists = get_base_id(state, asset_name, package_name, &base_id);

	// Always get a new instance.
	u16 instance_id = get_new_instance_id(state, base_id);

	// If the base didn't exist, will need to kick off an asset load.
	if (!exists) {
		KTRACE("Base mesh for '%k' does NOT already exist. Loading...", asset_name);
		animated_mesh_asset_request_listener *listener = KALLOC_TYPE(animated_mesh_asset_request_listener, MEMORY_TAG_ASSET);
		listener->state = state;
		listener->base_id = base_id;

		// Queue this so that we can make the callback when it loads.
		kmodel_instance_queue_entry new_entry = {
			.base_mesh_id = base_id,
			.instance_id = instance_id,
			.callback = callback,
			.context = context};
		darray_push(state->instance_queue, new_entry);

		// Kick off async asset load via the asset system.
		kasset_model *asset = asset_system_request_model_from_package(
			engine_systems_get()->asset_state,
			kname_string_get(package_name),
			kname_string_get(asset_name),
			listener,
			kasset_model_loaded);
		KASSERT(asset);
	} else {
		KTRACE("Base mesh for '%k' already exists (%u). Getting new instance.", asset_name, base_id);
		// Base mesh already exists, just need to get material instances.
		kmodel_base *base = &state->models[base_id];
		kmodel_instance_data *instance = &state->models[base_id].instances[instance_id];

		acquire_material_instances(state, base_id, instance_id);

		// For animated models, alloc shader data from the animation SSBO.
		if (base->type == KMODEL_TYPE_ANIMATED) {
			kmodel_animator *animator = &instance->animator;
			animator->shader_data = pool_allocator_allocate(&state->shader_data_pool, &animator->shader_data_index);
			animator->time_scale = 1.0f; // Always default time scale to 1.0f
			animator->max_bones = base->bone_count;
			animator->time_in_ticks = 0.0f;

			// Auto-set the first animation and simulate a frame being updated so it shows up
			// properly.
			animator->current_animation = 0;
			animator->current_animation_name = base->animations[0].name;

			kmodel_animator_state prev_state = animator->state;
			animator->state = KMODEL_ANIMATOR_STATE_PLAYING;
			animator_update(state, animator, 0.0f);
			animator->state = prev_state;
		}

		if (state->states[base_id] == KMODEL_STATE_LOADED) {
			// Make the callback immediately if loaded.
			if (callback) {
				callback((kmodel_instance){.base_mesh = base_id, .instance = instance_id}, context);
			}
		} else {
			// Queue this so that we can make the callback when it loads.
			kmodel_instance_queue_entry new_entry = {
				.base_mesh_id = base_id,
				.instance_id = instance_id,
				.callback = callback,
				.context = context};
			darray_push(state->instance_queue, new_entry);
		}
	}

	return (kmodel_instance){
		.base_mesh = base_id,
		.instance = instance_id};
}

static u16 get_active_instance_count (struct kmodel_system_state *state, u16 base_id) {
	kmodel_base *base = &state->models[base_id];

	u16 count = 0;
	for (u32 i = 0; i < base->instance_count; ++i) {
		if (base->instances[i].state != KMODEL_INSTANCE_STATE_UNINITIALIZED) {
			count++;
		}
	}
	return count;
}

// NOTE: Also releases held material instances.
void kmodel_instance_release (struct kmodel_system_state *state, kmodel_instance *instance) {
	if (instance->instance == INVALID_ID_U16 || instance->base_mesh == INVALID_ID_U16) {
		KERROR("Tried to release a kmodel instance with an invalid base/instance id.");
		return;
	}
	kmodel_base *base = &state->models[instance->base_mesh];
	kmodel_instance_data *inst = &base->instances[instance->instance];

	u16 submesh_count = base->submesh_count;
	if (inst->materials) {
		for (u16 i = 0; i < submesh_count; ++i) {
			kmaterial_system_release(engine_systems_get()->material_system, &inst->materials[i]);
		}
	}

	kmodel_animator *animator = &inst->animator;

	if (animator->shader_data) {
		pool_allocator_free(&state->shader_data_pool, animator->shader_data);
		animator->shader_data = KNULL;
	}

	kzero_memory(animator, sizeof(kmodel_animator));
	animator->base = INVALID_ID_U16;
	animator->current_animation = INVALID_ID_U16;
	animator->current_animation_name = INVALID_KNAME;

	inst->state = KMODEL_INSTANCE_STATE_UNINITIALIZED;
	if (inst->materials) {
		kfree(inst->materials);
		inst->materials = KNULL;
	}

	u16 active_count = get_active_instance_count(state, instance->base_mesh);
	if (!active_count) {
		KDEBUG("There are no longer any instances of model '%s' active, releasing entire model.", kname_string_get(state->models[instance->base_mesh].asset_name));

		// Clear the instance array for the particular base mesh
		kfree(base->instances);
		base->instances = KNULL;
		base->instance_count = 0;

		base->asset_name = INVALID_KNAME;
		base->package_name = INVALID_KNAME;

		struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
		krenderbuffer standard_vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
		krenderbuffer index_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

		// Unload submeshes from GPU.
		if (base->meshes && base->submesh_count) {
			for (u32 i = 0; i < base->submesh_count; ++i) {
				kmodel_submesh *m = &base->meshes[i];

				u64 standard_vert_buf_size = m->geo.vertex_element_size * m->geo.vertex_count;
				if (!renderer_renderbuffer_free(renderer_system, standard_vertex_buffer, standard_vert_buf_size, m->geo.vertex_buffer_offset)) {
					KWARN("Failed to release standard vertex data for animated mesh. See logs for details.");
				}

				u64 index_buf_size = m->geo.index_element_size * m->geo.index_count;
				if (!renderer_renderbuffer_free(renderer_system, index_buffer, index_buf_size, m->geo.index_buffer_offset)) {
					KWARN("Failed to release index data for animated mesh. See logs for details.");
				}

				kfree(m->geo.vertices);
				kfree(m->geo.indices);
			}

			kfree(base->meshes);
			base->meshes = KNULL;
			base->submesh_count = 0;
		}

		// Cleanup animations.
		if (base->animation_count && base->animations) {
			for (u32 i = 0; i < base->animation_count; ++i) {
				kmodel_animation *anim = &base->animations[i];

				if (anim->channels && anim->channel_count) {
					for (u32 c = 0; c < anim->channel_count; c++) {
						kmodel_channel *ch = &anim->channels[c];

						if (ch->pos_count && ch->positions) {
							kfree(ch->positions);
						}

						if (ch->scale_count && ch->scales) {
							kfree(ch->scales);
						}

						if (ch->rot_count && ch->rotations) {
							kfree(ch->rotations);
						}
					}

					kfree(anim->channels);
				}
			}

			kfree(base->animations);
			base->animations = KNULL;
			base->animation_count = 0;
		}

		if (base->node_count && base->nodes) {
			for (u32 i = 0; i < base->node_count; ++i) {
				kmodel_node *node = &base->nodes[i];

				if (node->child_count && node->children) {
					kfree(node->children);
				}
			}

			kfree(base->nodes);
			base->nodes = KNULL;
			base->node_count = 0;
		}

		if (base->bone_count && base->bones) {
			kfree(base->bones);
			base->bones = KNULL;
			base->node_count = 0;
		}
	} else {
		KDEBUG("Released instance, but there are %u remaining active instances of model '%s' active.", active_count, kname_string_get(state->models[instance->base_mesh].asset_name));
	}

	instance->base_mesh = INVALID_ID_U16;
	instance->instance = INVALID_ID_U16;
}

b8 kmodel_ray_intersects (struct kmodel_system_state *state, kmodel_instance instance, const ray *r, mat4 world, raycast_hit *out_hit) {
	if (!state || instance.base_mesh == INVALID_ID_U16) {
		return false;
	}
	u32 count = state->models[instance.base_mesh].submesh_count;
	if (!count) {
		return false;
	}

	mat4 world_inv = mat4_inverse(world);

	for (u32 i = 0; i < count; ++i) {
		kmodel_submesh *mesh = &state->models[instance.base_mesh].meshes[i];
		triangle picked;
		vec3 pos;
		vec3 normal;

		// Transform ray by inverted world transform
		ray rt = ray_transformed(r, world_inv);

		if (ray_pick_triangle(&rt, false, mesh->geo.vertex_count, mesh->geo.vertex_element_size, mesh->geo.vertices, mesh->geo.index_count, mesh->geo.indices, &picked, &pos, &normal)) {
			if (out_hit) {
				out_hit->type = RAYCAST_HIT_TYPE_SURFACE;
				// FIXME: Should this be the inverse world? To get the world position?
				// Transform position.
				pos = vec3_transform(pos, 1.0f, world);
				// Transform normal too.
				normal = vec3_transform(normal, 0.0f, world);

				out_hit->distance = vec3_distance(r->origin, pos);
				out_hit->position = pos;
				out_hit->normal = normal;
			}

			return true;
		}
	}

	return false;
}

b8 kmodel_submesh_count_get (struct kmodel_system_state *state, u16 base_mesh_id, u16 *out_count) {
	if (!state || base_mesh_id == INVALID_ID_U16) {
		return false;
	}
	*out_count = state->models[base_mesh_id].submesh_count;
	return true;
}

b8 kmodel_is_loaded (struct kmodel_system_state *state, u16 base_mesh_id) {
	return state->states[base_mesh_id] == KMODEL_STATE_LOADED;
}

const kgeometry *kmodel_submesh_geometry_get_at (struct kmodel_system_state *state, u16 base_mesh_id, u16 index) {
	return &state->models[base_mesh_id].meshes[index].geo;
}

const kmaterial_instance *kmodel_submesh_material_instance_get_at (struct kmodel_system_state *state, kmodel_instance instance, u16 index) {
	return &state->models[instance.base_mesh].instances[instance.instance].materials[index];
}

// NOTE: Returns dynamic array, needs to be freed by caller.
kname *kmodel_query_animations (struct kmodel_system_state *state, u16 base_mesh, u32 *out_count) {
	u32 count = state->models[base_mesh].animation_count;
	if (!count) {
		return KNULL;
	}

	kname *anim_names = KALLOC_TYPE_CARRAY(kname, count);
	for (u32 i = 0; i < count; ++i) {
		anim_names[i] = state->models[base_mesh].animations[i].name;
	}

	*out_count = count;
	return anim_names;
}

void kmodel_instance_animation_set (struct kmodel_system_state *state, kmodel_instance instance, kname animation_name) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;

	animator_set_animation(state, animator, animation_name);

	if (animator->current_animation == INVALID_ID_U16) {
		KWARN("Animation '%s' not found on base mesh '%s'.", kname_string_get(animation_name), kname_string_get(base->asset_name));
		if (base->animation_count > 0) {
			animator->current_animation = 0;
			animator->current_animation_name = base->animations[0].name;
			KWARN("Set animation to default of the first entry, '%s'.", kname_string_get(base->animations[0].name));
		} else {
			KWARN("No animations exist, thus there is nothing to set.");
		}
	}
}

u32 kmodel_instance_animation_id_get (struct kmodel_system_state *state, kmodel_instance instance) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;

	return animator->shader_data_index;
}

void kmodel_instance_time_scale_set (kmodel_system_state *state, kmodel_instance instance, f32 time_scale) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	animator->time_scale = time_scale;
}

void kmodel_instance_loop_set (struct kmodel_system_state *state, kmodel_instance instance, b8 loop) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	animator->loop = loop;
}
void kmodel_instance_play (struct kmodel_system_state *state, kmodel_instance instance) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	if (animator->current_animation != INVALID_ID_U16) {
		animator->state = KMODEL_ANIMATOR_STATE_PLAYING;
	} else {
		KWARN("%s - No current animation assigned, state will default to stopped.", __FUNCTION__);
		animator->state = KMODEL_ANIMATOR_STATE_STOPPED;
	}
}
void kmodel_instance_pause (struct kmodel_system_state *state, kmodel_instance instance) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	if (animator->current_animation != INVALID_ID_U16) {
		animator->state = KMODEL_ANIMATOR_STATE_PAUSED;
	} else {
		KWARN("%s - No current animation assigned, state will default to stopped.", __FUNCTION__);
		animator->state = KMODEL_ANIMATOR_STATE_STOPPED;
	}
}
void kmodel_instance_stop (struct kmodel_system_state *state, kmodel_instance instance) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	animator->state = KMODEL_ANIMATOR_STATE_STOPPED;
	animator->time_in_ticks = 0.0f;
}
void kmodel_instance_seek (struct kmodel_system_state *state, kmodel_instance instance, f32 time) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	kmodel_animation *current = &base->animations[animator->current_animation];
	f32 ticks_per_second = current->ticks_per_second;

	// Wrap around.
	f32 duration = current->duration;
	if (duration > 0.0f) {
		animator->time_in_ticks = ticks_per_second * kmod(time, duration);
		if (animator->time_in_ticks < 0.0f) {
			animator->time_in_ticks += duration;
		}
	}
}

void kmodel_instance_seek_percent (struct kmodel_system_state *state, kmodel_instance instance, f32 percent) {
	kmodel_base *base = &state->models[instance.base_mesh];
	kmodel_instance_data *inst = &base->instances[instance.instance];
	kmodel_animator *animator = &inst->animator;
	kmodel_animation *current = &base->animations[animator->current_animation];
	f32 clamped_pct = KCLAMP(percent, 0, 1.0f);
	f32 time = current->duration * clamped_pct;

	kmodel_instance_seek(state, instance, time);
}

static kmodel_channel *kanimation_find_channel (kmodel_animation *animation, kname node_name) {
	for (u32 i = 0; i < animation->channel_count; ++i) {
		if (animation->channels[i].name == node_name) {
			return &animation->channels[i];
		}
	}
	return 0;
}

static u32 base_find_node_index (kmodel_base *base, kname name) {
	for (u32 i = 0; i < base->node_count; ++i) {
		if (base->nodes[i].name == name) {
			return i;
		}
	}

	return INVALID_ID;
}

static u32 base_find_bone_index (kmodel_base *base, kname name) {
	for (u32 i = 0; i < base->bone_count; ++i) {
		if (base->bones[i].name == name) {
			return i;
		}
	}

	return INVALID_ID;
}

static vec3 interpolate_position (const kmodel_channel *channel, f32 time) {
	if (!channel->pos_count) {
		return vec3_zero();
	}
	if (channel->pos_count == 1) {
		return channel->positions[0].value;
	}

	u32 idx = 0;
	while (idx + 1 < channel->pos_count && time >= channel->positions[idx + 1].time) {
		idx++;
	}
	if (idx + 1 == channel->pos_count) {
		return channel->positions[channel->pos_count - 1].value;
	}

	f32 t0 = channel->positions[idx + 0].time;
	f32 t1 = channel->positions[idx + 1].time;
	f32 factor = (f32)((time - t0) / (t1 - t0));
	return vec3_lerp(channel->positions[idx].value, channel->positions[idx + 1].value, factor);
}

static quat interpolate_rotation (const kmodel_channel *channel, f32 time) {
	if (!channel->rot_count) {
		return quat_identity();
	}
	if (channel->rot_count == 1) {
		return channel->rotations[0].value;
	}

	u32 idx = 0;
	while (idx + 1 < channel->rot_count && time >= channel->rotations[idx + 1].time) {
		idx++;
	}
	if (idx + 1 == channel->rot_count) {
		return channel->rotations[channel->rot_count - 1].value;
	}

	f32 t0 = channel->rotations[idx + 0].time;
	f32 t1 = channel->rotations[idx + 1].time;
	f32 factor = (f32)((time - t0) / (t1 - t0));
	return quat_slerp(channel->rotations[idx].value, channel->rotations[idx + 1].value, factor);
}

static vec3 interpolate_scale (const kmodel_channel *channel, f32 time) {
	if (!channel->scale_count) {
		return vec3_zero();
	}
	if (channel->scale_count == 1) {
		return channel->scales[0].value;
	}

	u32 idx = 0;
	while (idx + 1 < channel->scale_count && time >= channel->scales[idx + 1].time) {
		idx++;
	}
	if (idx + 1 == channel->scale_count) {
		return channel->scales[channel->scale_count - 1].value;
	}

	f32 t0 = channel->scales[idx + 0].time;
	f32 t1 = channel->scales[idx + 1].time;
	f32 factor = (f32)((time - t0) / (t1 - t0));
	return vec3_lerp(channel->scales[idx].value, channel->scales[idx + 1].value, factor);
}

static void process_animator (
	kmodel_system_state *state,
	kmodel_animator *animator,
	kmodel_animation *animation,
	u32 node_index,
	const mat4 parent_transform) {

	kmodel_base *asset = &state->models[animator->base];
	kmodel_node *node = &asset->nodes[node_index];
	mat4 node_transform = node->local_transform;

	kmodel_channel *channel = kanimation_find_channel(animation, node->name);
	if (channel) {
		vec3 translation = interpolate_position(channel, animator->time_in_ticks);
		quat rotation = interpolate_rotation(channel, animator->time_in_ticks);
		vec3 scale = interpolate_scale(channel, animator->time_in_ticks);
		node_transform = mat4_from_translation_rotation_scale(translation, rotation, scale);
	}

	mat4 world_transform = mat4_mul(node_transform, parent_transform);

	u32 bone_index = base_find_bone_index(asset, node->name);
	mat4 final_matrix = world_transform;
	if (bone_index < animator->max_bones) {
		final_matrix = mat4_mul(asset->bones[bone_index].offset, final_matrix);
		animator->shader_data->final_bone_matrices[bone_index] = final_matrix;
	}

	// Recurse children.
	for (u32 i = 0; i < node->child_count; ++i) {
		u32 ci = node->children[i];
		process_animator(state, animator, animation, ci, world_transform);
	}
}

static void animator_create (kmodel_base *asset, kmodel_animator *out_animator) {
	out_animator->base = asset->id;
	out_animator->current_animation = (asset->animation_count > 0) ? 0 : INVALID_ID_U16;
	out_animator->current_animation_name = (asset->animation_count > 0) ? asset->animations[0].name : INVALID_KNAME;
	out_animator->time_in_ticks = 0.0f;
	out_animator->max_bones = asset->bone_count;
	for (u32 i = 0; i < KANIMATION_MAX_BONES; ++i) {
		out_animator->shader_data->final_bone_matrices[i] = mat4_identity();
	}
}

static void animator_set_animation (kmodel_system_state *state, kmodel_animator *animator, kname name) {
	if (animator->current_animation_name != name) {
		kmodel_base *base = &state->models[animator->base];
		u32 count = base->animation_count;
		for (u32 i = 0; i < count; ++i) {
			if (base->animations[i].name == name) {
				KTRACE("Animation '%s' now active on base mesh '%s'.", kname_string_get(base->animations[i].name), kname_string_get(base->asset_name));
				animator->current_animation_name = name;
				animator->current_animation = i;
				animator->time_in_ticks = 0.0f;
				break;
			}
		}
	}
}

static void animator_update (kmodel_system_state *state, kmodel_animator *animator, f32 delta_time) {
	if (animator->current_animation == INVALID_ID_U16) {
		return;
	}
	// Skip updates for animators that are not currently in the playing state.
	if (animator->state != KMODEL_ANIMATOR_STATE_PLAYING) {
		return;
	}
	kmodel_base *base = &state->models[animator->base];
	kmodel_animation *current = &base->animations[animator->current_animation];
	f32 ticks_per_second = current->ticks_per_second;
	f32 time_scale = state->global_time_scale * animator->time_scale;
	f32 delta_ticks = delta_time * time_scale * ticks_per_second;
	f32 prev_time = animator->time_in_ticks;
	animator->time_in_ticks += delta_ticks;

	f32 duration = current->duration;
	if (duration > 0.0f) {
		// Wrap around.
		if (animator->loop) {
			animator->time_in_ticks = kmod(animator->time_in_ticks, duration);
			if (animator->time_in_ticks < 0.0f) {
				animator->time_in_ticks += duration;
			}
		} else {
			if (animator->time_in_ticks > duration) {
				animator->time_in_ticks = duration;

				// If just hitting the end of the animation, signal it's completion.
				if (!kfloat_compare(prev_time, duration)) {
					KTRACE("Animation complete: %k", current->name);
					event_context ctx = {
						.data.u64[0] = current->name};
					event_fire(EVENT_CODE_ANIMATION_COMPLETE, animator, ctx);
				}
			}
		}
	}

	// Process the hierarchy starting at the root.
	kmodel_base *asset = &state->models[animator->base];
	for (u32 i = 0; i < base->node_count; ++i) {
		if (base->nodes[i].parent_index == INVALID_ID_U16) {
			process_animator(state, animator, current, i, asset->global_inverse_transform);
		}
	}
}

static void animator_get_bone_transforms (kmodel_system_state *state, kmodel_animator *animator, u32 count, mat4 *out_transforms) {
	kmodel_base *base = &state->models[animator->base];
	u32 n = base->bone_count;
	if (count < n) {
		n = count;
	}

	for (u32 i = 0; i < n; ++i) {
		out_transforms[i] = animator->shader_data->final_bone_matrices[i];
	}
}

static void ensure_arrays_allocated (kmodel_system_state *state, u32 new_count) {
	KASSERT_DEBUG(state);
	KASSERT_DEBUG(new_count);

	KRESIZE_ARRAY(state->states, kmodel_state, state->max_mesh_count, new_count);
	KRESIZE_ARRAY(state->models, kmodel_base, state->max_mesh_count, new_count);
	state->max_mesh_count = new_count;
}

static void ensure_instance_arrays_allocated (kmodel_base *base, u32 new_count) {
	KASSERT_DEBUG(base);
	KASSERT_DEBUG(new_count);

	KRESIZE_ARRAY(base->instances, kmodel_instance_data, base->instance_count, new_count);
	base->instance_count = new_count;
}

// Returns true if already exists; otherwise false.
static b8 get_base_id (struct kmodel_system_state *state, kname asset_name, kname package_name, u16 *out_id) {
	// Search for currently loaded/existing assets for a match first.
	for (u32 i = 0; i < state->max_mesh_count; ++i) {
		kmodel_base *base = &state->models[i];
		if (base->asset_name == asset_name && base->package_name == package_name) {
			*out_id = (u16)i;
			return true;
		}
	}

	u16 id = INVALID_ID_U16;
	// If one does not exist, create a new one.
	// First look for an empty slot.
	for (u32 i = 0; i < state->max_mesh_count; ++i) {
		if (state->states[i] == KMODEL_STATE_UNINITIALIZED) {
			// Free slot found, use it.
			id = (u16)i;
			break;
		}
	}

	// If no empty slot, it's an error as there is no more room.
	if (id == INVALID_ID_U16) {
		id = (u16)state->max_mesh_count;
		ensure_arrays_allocated(state, state->max_mesh_count + 1); // TODO: optimize growth size.
	}

	state->states[id] = KMODEL_STATE_ACQUIRED;

	kmodel_base *new_base = &state->models[id];
	new_base->asset_name = asset_name;
	new_base->package_name = package_name;
	new_base->id = id;

	*out_id = id;
	return false;
}

static u16 get_new_instance_id (struct kmodel_system_state *state, u16 base_id) {
	kmodel_base *base = &state->models[base_id];

	u16 id = INVALID_ID_U16;
	for (u16 i = 0; i < base->instance_count; ++i) {
		if (base->instances[i].state == KMODEL_INSTANCE_STATE_UNINITIALIZED) {
			// Free slot found, use it.
			id = i;
			break;
		}
	}

	if (id == INVALID_ID_U16) {
		// A new one is needed.
		id = (u16)base->instance_count;
		ensure_instance_arrays_allocated(base, base->instance_count + 1); // TODO: optimize growth size.
	}

	kmodel_instance_data *inst = &base->instances[id];
	inst->state = KMODEL_INSTANCE_STATE_ACQUIRED;

	kmodel_animator *animator = &inst->animator;
	animator->base = base_id;
	animator->current_animation = INVALID_ID_U16;

	return id;
}
