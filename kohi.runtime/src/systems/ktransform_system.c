#include "ktransform_system.h"

#include <stdio.h>

#include "containers/darray.h"
#include "core/console.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "strings/kstring.h"
#include "systems/kmatrix_system.h"
#include "utils/ksort.h"

typedef enum ktransform_flags {
	KTRANSFORM_FLAG_NONE = 0,
	KTRANSFORM_FLAG_FREE = 1 << 0,
} ktransform_flags;

typedef u32 ktransform_flag_bits;

// Going with a SOA here so that like data is grouped together.
typedef struct ktransform_system_state {
	/** @brief The cached local matrices in the world, indexed by handle. */
	mat4 *local_matrices;

	/** @brief The cached world matrices in the world, indexed by handle. */
	mat4 *world_matrices;

	/** @brief The positions in the world, indexed by handle. */
	vec3 *positions;

	/** @brief The rotations in the world, indexed by handle. */
	quat *rotations;

	/** @brief The scales in the world, indexed by handle. */
	vec3 *scales;

	/** @brief The flags of the transforms, indexed by handle. */
	ktransform_flag_bits *flags;

	/** @brief User data, typically a handle or pointer to something. */
	u64 *user;

	/** @brief Parent transforms, indexed by handle. KTRANSFORM_INVALID means no parent. */
	ktransform *parents;

	/** @brief The depth of the transform in the hierarchy. Used for efficient recalculation of transforms. */
	u8 *depths;

	/** @brief Matrix ids from the matrix system, which facilitates SSBO upload. */
	kmatrix_id *matrix_ids;

	/** @brief A list of handle ids that represent dirty local ktransforms. */
	ktransform *local_dirty_handles;
	u32 local_dirty_count;

	/** The number of slots available (capacity) (NOT the allocated space in bytes!) */
	u32 capacity;

	/** The number of currently-used slots (NOT the allocated space in bytes!) */
	u32 allocated;
} ktransform_system_state;

/**
 * @brief Ensures the state has enough space for the provided slot count.
 * Reallocates as needed if not.
 * @param state A pointer to the state.
 * @param slot_count The number of slots to ensure exist.
 */
static void ensure_allocated (ktransform_system_state *state, u32 slot_count);
static void dirty_list_reset (ktransform_system_state *state);
static void dirty_list_add_r (ktransform_system_state *state, ktransform t);
static ktransform handle_create (ktransform_system_state *state);
static void handle_destroy (ktransform_system_state *state, ktransform *t);
// Validates the handle itself, as well as compares it against the ktransform at the handle's index position.
static b8 validate_handle (ktransform_system_state *state, ktransform handle);
static void recalculate_world_r (ktransform t);

static void on_transform_dump (console_command_context context) {
	ktransform_system_state *state = context.listener;

	KINFO("Transform system - allocated/capacity = %u/%u", state->allocated, state->capacity);
}

b8 ktransform_system_initialize (u64 *memory_requirement, void *state, void *config) {
	*memory_requirement = sizeof(ktransform_system_state);

	if (!state) {
		return true;
	}

	ktransform_system_config *typed_config = config;
	ktransform_system_state *typed_state = state;

	kzero_memory(state, sizeof(ktransform_system_state));

	if (typed_config->initial_slot_count == 0) {
		KERROR("ktransform_system_config->initial_slot_count must be greater than 0. Defaulting to 128 instead.");
		typed_config->initial_slot_count = 128;
	}

	ensure_allocated(state, typed_config->initial_slot_count);

	// Invalidate all entries after the first. The first is the "default" transform, and shouldn't be used.
	for (u32 i = 1; i < typed_config->initial_slot_count; ++i) {
		typed_state->flags[i] = FLAG_SET(typed_state->flags[i], KTRANSFORM_FLAG_FREE, true);
	}

	dirty_list_reset(state);

	/* // Global transform storage buffer
	u64 buffer_size = sizeof(mat4) * 16384; // TODO: configurable?
	typed_state->transform_global_ssbo = renderer_renderbuffer_create(engine_systems_get()->renderer_system, kname_create(KRENDERBUFFER_NAME_TRANSFORMS_GLOBAL), RENDERBUFFER_TYPE_STORAGE, buffer_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT | RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT);
	KASSERT(typed_state->transform_global_ssbo != KRENDERBUFFER_INVALID);
	KDEBUG("Created transforms global storage buffer."); */

	KASSERT(console_command_register("transform_system_dump", 0, 0, typed_state, on_transform_dump));

	return true;
}

void ktransform_system_shutdown (void *state) {
	if (state) {
		ktransform_system_state *typed_state = state;

		/* renderer_renderbuffer_destroy(engine_systems_get()->renderer_system, typed_state->transform_global_ssbo); */

		if (typed_state->local_matrices) {
			kfree_aligned(typed_state->local_matrices);
			typed_state->local_matrices = KNULL;
		}
		if (typed_state->world_matrices) {
			kfree_aligned(typed_state->world_matrices);
			typed_state->world_matrices = KNULL;
		}
		if (typed_state->positions) {
			kfree_aligned(typed_state->positions);
			typed_state->positions = KNULL;
		}
		if (typed_state->rotations) {
			kfree_aligned(typed_state->rotations);
			typed_state->rotations = KNULL;
		}
		if (typed_state->scales) {
			kfree_aligned(typed_state->scales);
			typed_state->scales = KNULL;
		}
		if (typed_state->flags) {
			kfree_aligned(typed_state->flags);
			typed_state->flags = KNULL;
		}
		if (typed_state->user) {
			kfree_aligned(typed_state->user);
			typed_state->user = KNULL;
		}
		if (typed_state->parents) {
			kfree_aligned(typed_state->parents);
			typed_state->parents = KNULL;
		}
		if (typed_state->depths) {
			kfree_aligned(typed_state->depths);
			typed_state->depths = KNULL;
		}
		if (typed_state->matrix_ids) {
			kfree_aligned(typed_state->matrix_ids);
			typed_state->matrix_ids = KNULL;
		}
		if (typed_state->local_dirty_handles) {
			kfree_aligned(typed_state->local_dirty_handles);
			typed_state->local_dirty_handles = KNULL;
		}
	}
}

static i32 transform_depth_kquicksort_compare_internal (void *a, void *b, i32 mod) {
	ktransform *a_typed = a;
	ktransform *b_typed = b;

	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	u8 da = state->depths[*a_typed];
	u8 db = state->depths[*b_typed];
	if (da > db) {
		return -mod;
	} else if (da < db) {
		return mod;
	}
	return 0;
}

static i32 transform_depth_kquicksort_compare (void *a, void *b) {
	return transform_depth_kquicksort_compare_internal(a, b, 1);
}
static i32 transform_depth_kquicksort_compare_desc (void *a, void *b) {
	return transform_depth_kquicksort_compare_internal(a, b, -1);
}

b8 ktransform_system_update (ktransform_system_state *state, struct frame_data *p_frame_data) {
	struct kmatrix_system_state *matrix_system = engine_systems_get()->matrix_system;

	// Sort the dirty list by depth.
	kquick_sort(sizeof(ktransform), state->local_dirty_handles, 0, state->local_dirty_count - 1, transform_depth_kquicksort_compare);

	// Update dirty transforms top-down according to depth.
	for (u32 i = 0; i < state->local_dirty_count; ++i) {
		ktransform t = state->local_dirty_handles[i];
		recalculate_world_r(t);

		// Ensure the matrix system knows about the update.
		if (state->matrix_ids[t] != KMATRIX_INVALID) {
			kmatrix_system_update_by_id(matrix_system, state->matrix_ids[t], state->world_matrices[t]);
		}
	}

	// Clear the dirty list.
	dirty_list_reset(state);

	// Tell the matrix system about it.
	// FIXME: Updating this way won't work because the world_matrices array is indexed by transform id,
	// not matrix_id->index.
	/* kmatrix_system_bulk_update_transforms(matrix_system, state->world_matrices); */

	return true;
}

ktransform ktransform_create (u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = vec3_zero();
		state->rotations[handle] = quat_identity();
		state->scales[handle] = vec3_one();
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		state->parents[handle] = KTRANSFORM_INVALID;
		state->depths[handle] = 0;
		// NOTE: This is not added to the dirty list because the defualts form an identity matrix.
	} else {
		KERROR("Attempted to create a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

ktransform ktransform_clone (ktransform original, u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = state->positions[original];
		state->rotations[handle] = state->rotations[original];
		state->scales[handle] = state->scales[original];
		state->local_matrices[handle] = state->local_matrices[original];
		state->world_matrices[handle] = state->world_matrices[original];
		state->user[handle] = user;
		state->parents[handle] = state->parents[original];
		state->depths[handle] = state->depths[original];
		// NOTE: This is not added to the dirty list because the defualts form an identity matrix.
	} else {
		KERROR("Attempted to clone a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

void ktransform_mark_dirty (ktransform transform) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, transform)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		dirty_list_add_r(state, transform);
	}
}

ktransform ktransform_from_position (vec3 position, u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = position;
		state->rotations[handle] = quat_identity();
		state->scales[handle] = vec3_one();
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		state->parents[handle] = KTRANSFORM_INVALID;
		state->depths[handle] = 0;
		// Add to the dirty list.
		dirty_list_add_r(state, handle);
	} else {
		KERROR("Attempted to create a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

ktransform ktransform_from_rotation (quat rotation, u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = vec3_zero();
		state->rotations[handle] = rotation;
		state->scales[handle] = vec3_one();
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		state->parents[handle] = KTRANSFORM_INVALID;
		state->depths[handle] = 0;
		// Add to the dirty list.
		dirty_list_add_r(state, handle);
	} else {
		KERROR("Attempted to create a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

ktransform ktransform_from_position_rotation (vec3 position, quat rotation, u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = position;
		state->rotations[handle] = rotation;
		state->scales[handle] = vec3_one();
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		state->parents[handle] = KTRANSFORM_INVALID;
		state->depths[handle] = 0;
		// Add to the dirty list.
		dirty_list_add_r(state, handle);
	} else {
		KERROR("Attempted to create a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

ktransform ktransform_from_position_rotation_scale (vec3 position, quat rotation, vec3 scale, u64 user) {
	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = position;
		state->rotations[handle] = rotation;
		state->scales[handle] = scale;
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		state->parents[handle] = KTRANSFORM_INVALID;
		state->depths[handle] = 0;
		// Add to the dirty list.
		dirty_list_add_r(state, handle);
	} else {
		KERROR("Attempted to create a transform before the system was initialized.");
		handle = KTRANSFORM_INVALID;
	}
	return handle;
}

ktransform ktransform_from_matrix (mat4 m, u64 user) {
	// TODO: decompose matrix
	KASSERT_MSG(false, "Not implemented.");
	return KTRANSFORM_INVALID;
}

void ktransform_destroy (ktransform *t) {
	handle_destroy(engine_systems_get()->ktransform_system, t);
}

kmatrix_id ktransform_kmatrixid_get (ktransform t) {
	return engine_systems_get()->ktransform_system->matrix_ids[t];
}

b8 ktransform_is_identity (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		return false;
	}
	if (!vec3_compare(vec3_zero(), state->positions[t], K_FLOAT_EPSILON)) {
		return false;
	}
	if (!vec3_compare(vec3_one(), state->scales[t], K_FLOAT_EPSILON)) {
		return false;
	}
	if (!quat_is_identity(state->rotations[t])) {
		return false;
	}

	return true;
}

b8 ktransform_parent_set (ktransform t, ktransform parent) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		return false;
	}

	// Ensure no circular references.
	if (parent != KTRANSFORM_INVALID) {
		KASSERT(state->parents[parent] != t);
	}

	state->parents[t] = parent;
	// Update the depth too.
	state->depths[t] = parent == KTRANSFORM_INVALID ? 0 : state->depths[parent] + 1;

	dirty_list_add_r(state, t);

	return true;
}

ktransform ktransform_parent_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		return KTRANSFORM_INVALID;
	}

	return state->parents[t];
}

vec3 ktransform_position_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning zero vector as position.");
		return vec3_zero();
	}
	return state->positions[t];
}

vec3 ktransform_world_position_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning zero vector as position.");
		return vec3_zero();
	}
	return mat4_position(state->world_matrices[t]);
}

void ktransform_position_set (ktransform t, vec3 position) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->positions[t] = position;
		dirty_list_add_r(state, t);
	}
}

void ktransform_translate (ktransform t, vec3 translation) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->positions[t] = vec3_add(state->positions[t], translation);
		dirty_list_add_r(state, t);
	}
}

quat ktransform_rotation_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning identity quaternion as rotation.");
		return quat_identity();
	}
	return state->rotations[t];
}

quat ktransform_world_rotation_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning identity quaternion as rotation.");
		return quat_identity();
	}

	// Climb the tree until a root is reached, then multiply the rotations all the way down.
	// This ensures that scale data doesn't affect rotational data.
	quat *rotations = darray_reserve(quat, 16);
	ktransform parent = state->parents[t];
	while (parent != KTRANSFORM_INVALID) {
		darray_push(rotations, &state->rotations[parent]);
		parent = state->parents[parent];
	}

	quat world = quat_identity();
	u32 len = darray_length(rotations);
	for (i32 i = len - 1; i >= 0; --i) {
		world = quat_mul(world, rotations[i]);
	}

	darray_destroy(rotations);

	world = quat_mul(world, state->rotations[t]);
	world = quat_normalize(world);

	return world;
}

void ktransform_rotation_set (ktransform t, quat rotation) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->rotations[t] = rotation;
		dirty_list_add_r(state, t);
	}
}

void ktransform_rotate (ktransform t, quat rotation) {
	rotation = quat_normalize(rotation);
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->rotations[t] = quat_normalize(quat_mul(state->rotations[t], rotation));
		dirty_list_add_r(state, t);
	}
}

vec3 ktransform_scale_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning one vector as scale.");
		return vec3_zero();
	}
	return state->scales[t];
}

vec3 ktransform_world_scale_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, returning one vector as scale.");
		return vec3_one();
	}

	// Climb the tree until a root is reached, then multiply the scales all the way down.
	// This ensures that rotational data doesn't affect scale data.
	vec3 *scales = darray_reserve(vec3, 16);
	ktransform parent = state->parents[t];
	while (parent != KTRANSFORM_INVALID) {
		darray_push(scales, &state->scales[parent]);
		parent = state->parents[parent];
	}

	vec3 world = vec3_one();
	u32 len = darray_length(scales);
	for (i32 i = len - 1; i >= 0; --i) {
		world = vec3_mul(world, scales[i]);
	}

	world = vec3_mul(world, state->scales[t]);

	return world;
}

void ktransform_scale_set (ktransform t, vec3 scale) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->scales[t] = scale;
		dirty_list_add_r(state, t);
	}
}

void ktransform_scale (ktransform t, vec3 scale) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->scales[t] = vec3_mul(state->scales[t], scale);
		dirty_list_add_r(state, t);
	}
}

void ktransform_position_rotation_set (ktransform t, vec3 position, quat rotation) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->positions[t] = position;
		state->rotations[t] = rotation;
		dirty_list_add_r(state, t);
	}
}

void ktransform_position_rotation_scale_set (ktransform t, vec3 position, quat rotation, vec3 scale) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->positions[t] = position;
		state->rotations[t] = rotation;
		state->scales[t] = scale;
		dirty_list_add_r(state, t);
	}
}

void ktransform_translate_rotate (ktransform t, vec3 translation, quat rotation) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (!validate_handle(state, t)) {
		KWARN("Invalid handle passed, nothing was done.");
	} else {
		state->positions[t] = vec3_add(state->positions[t], translation);
		state->rotations[t] = quat_mul(state->rotations[t], rotation);
		dirty_list_add_r(state, t);
	}
}

void ktransform_calculate_local (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		u32 index = t;
// TODO: investigate mat4_from_translation_rotation_scale
#if 0
		state->local_matrices[index] = mat4_mul(quat_to_mat4(state->rotations[index]), mat4_translation(state->positions[index]));
		state->local_matrices[index] = mat4_mul(mat4_scale(state->scales[index]), state->local_matrices[index]);
#else
		state->local_matrices[index] = mat4_from_translation_rotation_scale(state->positions[index], state->rotations[index], state->scales[index]);
#endif
	}
}

mat4 ktransform_world_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		return state->world_matrices[t];
	}

	KWARN("Invalid handle passed to ktransform_world_get. Returning identity matrix.");
	return mat4_identity();
}

u64 ktransform_user_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		return state->user[t];
	}

	KWARN("Invalid handle passed to %s. Returning default of INVALID_ID_U64.", __FUNCTION__);
	return INVALID_ID_U64;
}

void ktransform_user_set (ktransform t, u64 user) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		state->user[t] = user;
		return;
	}

	KWARN("Invalid handle passed to %s. Nothing will be done.", __FUNCTION__);
}

mat4 ktransform_local_get (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		u32 index = t;
		return state->local_matrices[index];
	}

	KWARN("Invalid handle passed to ktransform_local_get. Returning identity matrix.");
	return mat4_identity();
}

const char *ktransform_to_string (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		u32 index = t;
		vec3 position = state->positions[index];
		vec3 scale = state->scales[index];
		quat rotation = state->rotations[index];

		return string_format(
			"%f %f %f %f %f %f %f %f %f %f",
			position.x,
			position.y,
			position.z,
			rotation.x,
			rotation.y,
			rotation.z,
			rotation.w,
			scale.x,
			scale.y,
			scale.z);
	}

	KERROR("Invalid handle passed to ktransform_to_string. Returning null.");
	return 0;
}

b8 ktransform_from_string (const char *str, u64 user, ktransform *out_ktransform) {
	if (!out_ktransform) {
		KERROR("string_to_scene_ktransform_config requires a valid pointer to out_ktransform.");
		return false;
	}

	b8 result = true;

	vec3 position = vec3_zero();
	quat rotation = quat_identity();
	vec3 scale = vec3_one();

	if (!str) {
		KWARN("Format error: invalid string provided. Identity transform will be used.");
		result = false;
	} else {
		f32 values[7] = {0};

		i32 count = sscanf(
			str,
			"%f %f %f %f %f %f %f %f %f %f",
			&position.x, &position.y, &position.z,
			&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6]);

		if (count == 10) {
			// Treat as quat, load directly.
			rotation.x = values[0];
			rotation.y = values[1];
			rotation.z = values[2];
			rotation.w = values[3];

			// Set scale
			scale.x = values[4];
			scale.y = values[5];
			scale.z = values[6];
		} else if (count == 9) {
			quat x_rot = quat_from_axis_angle((vec3){1.0f, 0, 0}, deg_to_rad(values[0]), true);
			quat y_rot = quat_from_axis_angle((vec3){0, 1.0f, 0}, deg_to_rad(values[1]), true);
			quat z_rot = quat_from_axis_angle((vec3){0, 0, 1.0f}, deg_to_rad(values[2]), true);
			rotation = quat_mul(x_rot, quat_mul(y_rot, z_rot));

			// Set scale
			scale.x = values[3];
			scale.y = values[4];
			scale.z = values[5];
		} else {
			KWARN("Format error: invalid ktransform provided. Identity transform will be used.");
			result = false;
		}
	}

	ktransform handle = {0};
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (state) {
		handle = handle_create(state);
		state->positions[handle] = position;
		state->rotations[handle] = rotation;
		state->scales[handle] = scale;
		state->local_matrices[handle] = mat4_identity();
		state->world_matrices[handle] = mat4_identity();
		state->user[handle] = user;
		// Add to the dirty list.
		dirty_list_add_r(state, handle);
	} else {
		KERROR("Attempted to create a ktransform before the system was initialized.");
		*out_ktransform = KTRANSFORM_INVALID;
		return false;
	}

	*out_ktransform = handle;
	return result;
}

static void ensure_allocated (ktransform_system_state *state, u32 slot_count) {
	// FIXME: Something about this repeatedly asks for larger and larger size.
	KASSERT_MSG(slot_count % 8 == 0, "ensure_allocated requires new slot_count to be a multiple of 8.");

	if (state->capacity < slot_count) {
		// Setup the arrays of data, starting with the matrices. These should be 16-bit
		// aligned so that SIMD is an easy addition later on.
		mat4 *new_local_matrices = kallocate_aligned(sizeof(mat4) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->local_matrices) {
			kcopy_memory(new_local_matrices, state->local_matrices, sizeof(mat4) * state->capacity);
			kfree_aligned(state->local_matrices);
		}
		state->local_matrices = new_local_matrices;

		mat4 *new_world_matrices = kallocate_aligned(sizeof(mat4) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->world_matrices) {
			kcopy_memory(new_world_matrices, state->world_matrices, sizeof(mat4) * state->capacity);
			kfree_aligned(state->world_matrices);
		}
		state->world_matrices = new_world_matrices;

		// Also align positions, rotations and scales for future SIMD purposes.
		vec3 *new_positions = kallocate_aligned(sizeof(vec3) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->positions) {
			kcopy_memory(new_positions, state->positions, sizeof(vec3) * state->capacity);
			kfree_aligned(state->positions);
		}
		state->positions = new_positions;

		quat *new_rotations = kallocate_aligned(sizeof(quat) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->rotations) {
			kcopy_memory(new_rotations, state->rotations, sizeof(quat) * state->capacity);
			kfree_aligned(state->rotations);
		}
		state->rotations = new_rotations;

		vec3 *new_scales = kallocate_aligned(sizeof(vec3) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->scales) {
			kcopy_memory(new_scales, state->scales, sizeof(vec3) * state->capacity);
			kfree_aligned(state->scales);
		}
		state->scales = new_scales;

		// Identifiers don't *need* to be aligned, but do it anyways since everythingaelse is.
		ktransform_flag_bits *new_flags = kallocate_aligned(sizeof(ktransform_flag_bits) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->flags) {
			kcopy_memory(new_flags, state->flags, sizeof(ktransform_flag_bits) * state->capacity);
			kfree_aligned(state->flags);
		}
		state->flags = new_flags;
		// Mark all new slots as free.
		for (u32 i = state->capacity; i < slot_count; ++i) {
			state->flags[i] = FLAG_SET(state->flags[i], KTRANSFORM_FLAG_FREE, true);
		}

		// User data
		u64 *new_user = kallocate_aligned(sizeof(u64) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->user) {
			kcopy_memory(new_user, state->user, sizeof(u64) * state->capacity);
			kfree_aligned(state->user);
		}
		state->user = new_user;

		// Parent data
		ktransform *new_parent = kallocate_aligned(sizeof(ktransform) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->parents) {
			kcopy_memory(new_parent, state->parents, sizeof(ktransform) * state->capacity);
			kfree_aligned(state->parents);
		}
		state->parents = new_parent;
		// Invalidate new parent datas.
		for (u32 i = state->capacity; i < slot_count; ++i) {
			state->parents[i] = KTRANSFORM_INVALID;
		}

		// Depths
		u8 *new_depth = kallocate_aligned(sizeof(u8) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->depths) {
			kcopy_memory(new_depth, state->depths, sizeof(u8) * state->capacity);
			kfree_aligned(state->depths);
		}
		state->depths = new_depth;

		// Matrix ids
		kmatrix_id *new_matrix_ids = kallocate_aligned(sizeof(kmatrix_id) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->matrix_ids) {
			kcopy_memory(new_matrix_ids, state->matrix_ids, sizeof(kmatrix_id) * state->capacity);
			kfree_aligned(state->matrix_ids);
		}
		state->matrix_ids = new_matrix_ids;

		// Dirty handle list doesn't *need* to be aligned, but do it anyways since everything else is.
		u32 *new_dirty_handles = kallocate_aligned(sizeof(ktransform) * slot_count, 16, MEMORY_TAG_TRANSFORM);
		if (state->local_dirty_handles) {
			kcopy_memory(new_dirty_handles, state->local_dirty_handles, sizeof(ktransform) * state->capacity);
			kfree_aligned(state->local_dirty_handles);
		}
		state->local_dirty_handles = new_dirty_handles;

		// Make sure the allocated count is up to date.
		state->capacity = slot_count;
	}
}

static void dirty_list_reset (ktransform_system_state *state) {
	for (u32 i = 0; i < state->local_dirty_count; ++i) {
		state->local_dirty_handles[i] = INVALID_ID;
	}
	state->local_dirty_count = 0;
}

static void dirty_list_add_r (ktransform_system_state *state, ktransform t) {
	b8 do_add = true;
	for (u32 i = 0; i < state->local_dirty_count; ++i) {
		if (state->local_dirty_handles[i] == t) {
			// Already there, do nothing.
			do_add = false;
			break;
		}
	}

	if (state->local_dirty_handles) {
		if (do_add) {
			state->local_dirty_handles[state->local_dirty_count] = t;
			state->local_dirty_count++;
		}

		// Need to recurse all children and add them to the list as well.
		for (u32 i = 0; i < state->capacity; ++i) {
			if (state->parents[i] == t) {
				dirty_list_add_r(state, i);
			}
		}
	}
}

static ktransform handle_create (ktransform_system_state *state) {
	KASSERT_MSG(state, "ktransform_system state pointer accessed before initialized");

	ktransform handle = KTRANSFORM_INVALID;
	u32 ktransform_count = state->capacity;
	for (u32 i = 1; i < ktransform_count; ++i) {
		if (FLAG_GET(state->flags[i], KTRANSFORM_FLAG_FREE)) {
			// Found an entry.
			state->flags[i] = FLAG_SET(state->flags[i], KTRANSFORM_FLAG_FREE, false);
			// Ensure the parent is invalid.
			state->parents[i] = KTRANSFORM_INVALID;
			state->depths[i] = 0;
			state->matrix_ids[i] = kmatrix_system_add(engine_systems_get()->matrix_system, KMATRIX_TYPE_TRANSFORM, state->world_matrices[i]);
			state->allocated++;
			return i;
		}
	}

	// No open slots, expand array and use the first slot of the new memory.
	ensure_allocated(state, state->capacity * 2);
	handle = ktransform_count;
	state->flags[handle] = FLAG_SET(state->flags[handle], KTRANSFORM_FLAG_FREE, false);
	// Ensure the parent is invalid.
	state->parents[handle] = KTRANSFORM_INVALID;
	state->depths[handle] = 0;
	// Tell the matrix system about it.
	state->matrix_ids[handle] = kmatrix_system_add(engine_systems_get()->matrix_system, KMATRIX_TYPE_TRANSFORM, state->world_matrices[handle]);
	state->allocated++;
	return handle;
}

static void handle_destroy (ktransform_system_state *state, ktransform *t) {
	KASSERT_MSG(state, "ktransform_system state pointer accessed before initialized");

	if (*t != KTRANSFORM_INVALID) {
		/* KTRACE("Destroying transform handle %u", *t); */
		if (state->flags) {
			state->flags[*t] = 0;
			FLAG_SET(state->flags[*t], KTRANSFORM_FLAG_FREE, true);
		}
		// Ensure the parent is invalid.
		if (state->parents) {
			state->parents[*t] = KTRANSFORM_INVALID;
		}
		// Unregister from the matrix system.
		kmatrix_system_remove(engine_systems_get()->matrix_system, &state->matrix_ids[*t]);
		state->allocated--;
		*t = KTRANSFORM_INVALID;
	}
}

static b8 validate_handle (ktransform_system_state *state, ktransform handle) {
	if (handle == KTRANSFORM_INVALID) {
		KTRACE("Handle validation failed because the handle is invalid.");
		return false;
	}

	if (handle >= state->capacity) {
		KTRACE("Provided handle index is out of bounds: %u", handle);
		return false;
	}

	// Check for a match.
	return true;
}

static void recalculate_world_r (ktransform t) {
	ktransform_system_state *state = engine_systems_get()->ktransform_system;
	if (t != KTRANSFORM_INVALID) {
		mat4 child_world;
		ktransform_calculate_local(t);
		mat4 child_local = state->local_matrices[t];

		ktransform parent = state->parents[t];
		if (parent != KTRANSFORM_INVALID) {
			recalculate_world_r(parent);
			mat4 parent_world = state->world_matrices[parent];
			child_world = mat4_mul(child_local, parent_world);
		} else {
			child_world = child_local;
		}
		state->world_matrices[t] = child_world;
	}
}
