/**
 * @file kcamera_system.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "kcamera_system.h"

#include "core/engine.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "systems/kmatrix_system.h"
#include "utils/render_type_utils.h"

typedef enum kcamera_flags {
	KCAMERA_FLAG_NONE_BIT = 0,
	KCAMERA_FLAG_IS_FREE_BIT = 1 << 0,
	KCAMERA_FLAG_TRANSFORM_DIRTY_BIT = 1 << 1,
	KCAMERA_FLAG_PROJECTION_DIRTY_BIT = 1 << 2,
} kcamera_flags;

typedef u32 kcamera_flag_bits;

typedef struct kcamera_data {
	mat4 view_matrix;
	kmatrix_id view_matrix_id;
	mat4 transform;
	kmatrix_id transform_matrix_id;
	mat4 projection;
	kmatrix_id projection_matrix_id;
	kfrustum frustum;
	rect_2di vp_rect;
	vec3 position;
	// Euler angles, but stored in radians
	vec3 euler_rotation;
	vec3 target;
	vec3 target_forward;
	f32 follow_distance;
	f32 vertical_offset;

	kcamera_type type;
	kcamera_mode mode;
	f32 fov;
	f32 near_clip;
	f32 far_clip;
	kcamera_flag_bits flags;
} kcamera_data;

typedef struct kcamera_system_state {
	u8 max_camera_count;
	kcamera_data *cameras;
} kcamera_system_state;

static kcamera_system_state *state_ptr;

static void process_camera (kcamera_data *data);
static void regenerate_matrices (kcamera_data *data);

static kcamera get_new_camera (kcamera_system_state *state) {
	for (u8 i = 0; i < state->max_camera_count; ++i) {
		if (FLAG_GET(state->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT)) {
			// Unflag it as being free
			state->cameras[i].flags = FLAG_SET(state->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT, false);
			return i;
		}
	}

	KERROR("KCamera system: The internal array is full (max_camera_count=%u). Expand this in configuration. Returning defualt camera instead.", state->max_camera_count);
	return INVALID_KCAMERA;
}

// In range and not default
static b8 kcamera_is_valid (kcamera camera) {
	KASSERT(state_ptr);
	if (camera == DEFAULT_KCAMERA || camera == INVALID_KCAMERA) {
		return false;
	}

	if (camera >= state_ptr->max_camera_count) {
		return false;
	}

	return true;
}

b8 kcamera_system_initialize (u64 *memory_requirement, void *state, void *config) {
	kcamera_system_config *typed_config = (kcamera_system_config *)config;
	if (typed_config->max_camera_count == 0) {
		KFATAL("camera_system_initialize - config.max_camera_count must be > 0.");
		return false;
	}

	// Block of memory will contain state structure, then block for array.
	u64 struct_requirement = sizeof(kcamera_system_state);
	u64 array_requirement = sizeof(kcamera_data) * typed_config->max_camera_count;
	*memory_requirement = struct_requirement + array_requirement;

	if (!state) {
		return true;
	}

	state_ptr = (kcamera_system_state *)state;
	state_ptr->max_camera_count = typed_config->max_camera_count;

	// The array block is after the state. Already allocated, so just set the pointer.
	void *array_block = state + struct_requirement;
	state_ptr->cameras = array_block;

	// Mark all 'slots' as free
	for (u32 i = 0; i < state_ptr->max_camera_count; ++i) {
		state_ptr->cameras[i].flags = FLAG_SET(state_ptr->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT, true);
	}

	// Setup default camera.
	kcamera default_camera = kcamera_create(KCAMERA_TYPE_3D, (rect_2di){0, 0, 1280, 720}, vec3_zero(), vec3_zero(), deg_to_rad(45.0f), 0.1f, 1000.0f);

	return default_camera == 0;
}

void kcamera_system_shutdown (void *state) {
	if (state) {
		// NOTE: Nothing in the system needs shutting down as there are no
		// dynamic allocations done, or resources held by this system.
	}

	state_ptr = 0;
}

kcamera kcamera_create (kcamera_type type, rect_2di vp_rect, vec3 position, vec3 euler_rotation, f32 fov_radians, f32 near_clip, f32 far_clip) {
	if (!state_ptr) {
		KERROR("%s: called before system was initialized.", __FUNCTION__);
		return DEFAULT_KCAMERA;
	}

	struct kmatrix_system_state *matrix_system = engine_systems_get()->matrix_system;

	kcamera new_cam = get_new_camera(state_ptr);
	if (kcamera_is_valid(new_cam) || new_cam == 0) {
		kcamera_data *data = &state_ptr->cameras[new_cam];

		data->type = type;
		data->position = position;
		data->euler_rotation = euler_rotation;
		data->fov = fov_radians;
		data->near_clip = near_clip;
		data->far_clip = far_clip;
		data->vp_rect = vp_rect;
		// Some reasonable defaults.
		data->mode = KCAMERA_MODE_FREE;
		data->target = vec3_zero();
		data->target_forward = vec3_forward();
		data->vertical_offset = 10;
		data->follow_distance = 10;

		// Mark transform as dirty so it gets recalculated on the next pass.
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
		// Also mark projection as dirty so it gets recalculated as well.
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);

		// Register with the matrix system so they are obtainable in shaders.
		data->projection_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_PROJECTION, mat4_identity());
		data->view_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_VIEW, mat4_identity());
		// NOTE: might be better to store this in KMATRIX_TYPE_GENERIC?
		data->transform_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_TRANSFORM, mat4_identity());

		regenerate_matrices(data);

		return new_cam;
	}

	KWARN("Error creating new camera, returning default camera.");
	return DEFAULT_KCAMERA;
}

kcamera kcamera_clone (kcamera camera) {
	// Take a clone of the default camera if an invalid one is passed.
	if (!kcamera_is_valid(camera)) {
		return kcamera_clone(DEFAULT_KCAMERA);
	}

	struct kmatrix_system_state *matrix_system = engine_systems_get()->matrix_system;

	kcamera new_cam = get_new_camera(state_ptr);
	if (kcamera_is_valid(new_cam)) {
		kcamera_data *old_data = &state_ptr->cameras[camera];
		kcamera_data *new_data = &state_ptr->cameras[new_cam];

		new_data->type = old_data->type;
		new_data->position = old_data->position;
		new_data->euler_rotation = old_data->euler_rotation;
		new_data->fov = old_data->fov;
		new_data->near_clip = old_data->near_clip;
		new_data->far_clip = old_data->far_clip;
		new_data->vp_rect = old_data->vp_rect;
		new_data->mode = old_data->mode;
		new_data->target = old_data->target;
		new_data->target_forward = old_data->target_forward;
		new_data->follow_distance = old_data->follow_distance;
		new_data->vertical_offset = old_data->vertical_offset;

		// Mark transform as dirty so it gets recalculated on the next pass.
		new_data->flags = FLAG_SET(new_data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
		// Also mark projection as dirty so it gets recalculated as well.
		new_data->flags = FLAG_SET(new_data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);

		// Register with the matrix system so they are obtainable in shaders.
		new_data->projection_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_PROJECTION, mat4_identity());
		new_data->view_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_VIEW, mat4_identity());
		// NOTE: might be better to store this in KMATRIX_TYPE_GENERIC?
		new_data->transform_matrix_id = kmatrix_system_add(matrix_system, KMATRIX_TYPE_TRANSFORM, mat4_identity());

		regenerate_matrices(new_data);

		return new_cam;
	}

	KWARN("Error cloning camera, returning default camera.");
	return DEFAULT_KCAMERA;
}

void kcamera_destroy (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];

		// Unregister matrices
		struct kmatrix_system_state *matrix_system = engine_systems_get()->matrix_system;
		kmatrix_system_remove(matrix_system, &data->projection_matrix_id);
		kmatrix_system_remove(matrix_system, &data->view_matrix_id);
		kmatrix_system_remove(matrix_system, &data->transform_matrix_id);

		kzero_memory(data, sizeof(kcamera_data));

		// Invalidate the matrix ids.
		data->projection_matrix_id = KMATRIX_INVALID;
		data->view_matrix_id = KMATRIX_INVALID;
		data->transform_matrix_id = KMATRIX_INVALID;

		// Mark it as free.
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_IS_FREE_BIT, true);
	}
}

kcamera kcamera_system_get_default (void) {
	return DEFAULT_KCAMERA; // 0 is the defualt
}

kcamera_mode kcamera_get_mode (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].mode;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return KCAMERA_MODE_FREE;
}
void kcamera_set_mode (kcamera camera, kcamera_mode mode) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->mode = mode;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

vec3 kcamera_get_position (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].position;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_zero();
}
void kcamera_set_position (kcamera camera, vec3 position) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->position = position;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

vec3 kcamera_get_target (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].target;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_zero();
}
void kcamera_set_target (kcamera camera, vec3 target) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->target = target;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

vec3 kcamera_get_target_forward (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].target_forward;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_zero();
}
void kcamera_set_target_forward (kcamera camera, vec3 target_forward) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->target_forward = target_forward;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}
vec3 kcamera_get_euler_rotation (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].euler_rotation;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_zero();
}
void kcamera_set_euler_rotation (kcamera camera, vec3 euler_rotation) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->euler_rotation = (vec3){
			euler_rotation.x = deg_to_rad(euler_rotation.x),
			euler_rotation.y = deg_to_rad(euler_rotation.y),
			euler_rotation.z = deg_to_rad(euler_rotation.z),
		};
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}
void kcamera_set_euler_rotation_radians (kcamera camera, vec3 euler_rotation_radians) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->euler_rotation = euler_rotation_radians;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}
f32 kcamera_get_fov (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].fov;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return 0.0f;
}
void kcamera_set_fov (kcamera camera, f32 fov) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->fov = fov;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
	}
}
f32 kcamera_get_near_clip (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].near_clip;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return 0.0f;
}
void kcamera_set_near_clip (kcamera camera, f32 near_clip) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->near_clip = near_clip;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
	}
}
f32 kcamera_get_far_clip (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].far_clip;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return 0.0f;
}
void kcamera_set_far_clip (kcamera camera, f32 far_clip) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->far_clip = far_clip;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
	}
}
rect_2di kcamera_get_vp_rect (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return state_ptr->cameras[camera].vp_rect;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return (rect_2di){0, 0, 0, 0};
}
void kcamera_set_vp_rect (kcamera camera, rect_2di vp_rect) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		data->vp_rect = vp_rect;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
	}
}

kfrustum kcamera_get_frustum (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->frustum;
	}
	return state_ptr->cameras[DEFAULT_KCAMERA].frustum;
}

mat4 kcamera_get_view (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->view_matrix;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return mat4_identity();
}
mat4 kcamera_get_transform (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->transform;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return mat4_identity();
}
mat4 kcamera_get_projection (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->projection;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return mat4_identity();
}

kmatrix_id kcamera_get_view_id (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->view_matrix_id;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return KMATRIX_INVALID;
}
kmatrix_id kcamera_get_transform_id (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->transform_matrix_id;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return KMATRIX_INVALID;
}
kmatrix_id kcamera_get_projection_id (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		regenerate_matrices(data);
		return data->projection_matrix_id;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return KMATRIX_INVALID;
}

mat4 kcamera_get_projection_far_clipped (kcamera camera, f32 far) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];
		mat4 proj_near = generate_projection_matrix(data->vp_rect, data->fov, data->near_clip, far, PROJECTION_MATRIX_TYPE_PERSPECTIVE);
		return proj_near;
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return mat4_identity();
}

vec3 kcamera_forward (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_forward(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_forward();
}

vec3 kcamera_backward (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_backward(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_backward();
}

vec3 kcamera_left (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_left(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_left();
}

vec3 kcamera_right (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_right(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_right();
}

vec3 kcamera_up (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_up(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_up();
}

vec3 kcamera_down (kcamera camera) {
	if (kcamera_is_valid(camera)) {
		return mat4_down(state_ptr->cameras[camera].transform);
	}
	KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
	return vec3_down();
}

void kcamera_move_direction (kcamera camera, vec3 direction, b8 normalize_dir, f32 amount) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];

		if (normalize_dir) {
			vec3_normalize(&direction);
		}

		direction = vec3_mul_scalar(direction, amount);
		data->position = vec3_add(data->position, direction);
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

void kcamera_move_forward (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, mat4_forward(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_backward (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, mat4_backward(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_left (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, mat4_left(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_right (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, mat4_right(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_up (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, vec3_up(), false, amount);
}

void kcamera_move_down (kcamera camera, f32 amount) {
	kcamera_move_direction(camera, vec3_down(), false, amount);
}

void kcamera_yaw (kcamera camera, f32 amount) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];

		// Ignore input for modes that don't support it.
		if (data->mode == KCAMERA_MODE_ISOMETRIC) {
			return;
		}
		data->euler_rotation.y += amount;
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

void kcamera_pitch (kcamera camera, f32 amount) {
	if (kcamera_is_valid(camera)) {
		kcamera_data *data = &state_ptr->cameras[camera];

		// Ignore input for modes that don't support it.
		if (data->mode == KCAMERA_MODE_ISOMETRIC) {
			return;
		}

		data->euler_rotation.x += amount;

		// Clamp to avoid Gimball lock.
		static const f32 limit = 1.55334306f; // 89 degrees, or equivalent to deg_to_rad(89.0f);
		data->euler_rotation.x = KCLAMP(data->euler_rotation.x, -limit, limit);

		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
	}
}

void calculate_orbit_position (kcamera_data *data) {
	data->position.x = data->target.x + data->follow_distance * kcos(data->euler_rotation.x) * ksin(data->euler_rotation.y);
	data->position.y = data->target.y + data->follow_distance * ksin(-data->euler_rotation.x);
	data->position.z = data->target.z + data->follow_distance * kcos(data->euler_rotation.x) * kcos(data->euler_rotation.y);
}

void calculate_follow_upwardoffset (kcamera_data *data) {
	// Update follow camera to follow.
	vec3 backward_offset = vec3_mul_scalar(data->target_forward, -data->follow_distance);
	vec3 upward_offset = vec3_create(0.0f, data->vertical_offset, 0.0f);
	data->position = vec3_add(vec3_add(data->target, backward_offset), upward_offset);

	// Direction from point to focus object
	vec3 to_obj = vec3_normalized(vec3_sub(data->target, data->position));

	// pitch
	data->euler_rotation.x = kasin(to_obj.y);
	// yaw
	data->euler_rotation.y = katan2(-to_obj.x, -to_obj.z);
	// no roll
	data->euler_rotation.z = 0;
}

void process_camera (kcamera_data *data) {
	switch (data->mode) {
	case KCAMERA_MODE_FREE:
	case KCAMERA_MODE_MOUSELOOK:
		// No processing needed for these types.
		return;
	case KCAMERA_MODE_ORBIT:
		calculate_orbit_position(data);
		break;
	case KCAMERA_MODE_FOLLOW:
	case KCAMERA_MODE_ISOMETRIC:
		calculate_follow_upwardoffset(data);
		break;
	}
}

// Regenerate matrices, if needed.
void regenerate_matrices (kcamera_data *data) {

	b8 needs_frustum = false;
	struct kmatrix_system_state *matrix_system = engine_systems_get()->matrix_system;

	// Regenerate transform and view, if needed.
	if (FLAG_GET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT)) {
		// Depending on camera type, some calculations may need to be made before generating the transform.
		process_camera(data);

		// Recalculate transform
		mat4 rotation = mat4_euler_xyz(data->euler_rotation.x, data->euler_rotation.y, data->euler_rotation.z);
		mat4 translation = mat4_translation(data->position);
		data->transform = mat4_mul(rotation, translation);
		kmatrix_system_update_by_id(matrix_system, data->transform_matrix_id, data->transform);

		// View is just inverse transform.
		data->view_matrix = mat4_inverse(data->transform);
		kmatrix_system_update_by_id(matrix_system, data->view_matrix_id, data->view_matrix);

		// Make sure to unset the dirty flag.
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, false);
		needs_frustum = true;
	}

	// Recalculate the projection matrix.
	if (FLAG_GET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT)) {
		projection_matrix_type matrix_type;
		switch (data->type) {
		case KCAMERA_TYPE_2D:
			matrix_type = PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC;
			break;
		case KCAMERA_TYPE_3D:
		default:
			matrix_type = PROJECTION_MATRIX_TYPE_PERSPECTIVE;
			break;
		}
		data->projection = generate_projection_matrix(data->vp_rect, data->fov, data->near_clip, data->far_clip, matrix_type);
		kmatrix_system_update_by_id(matrix_system, data->projection_matrix_id, data->projection);

		// Make sure to unset the dirty flag.
		data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, false);
		needs_frustum = true;
	}

	// If any matrix required regeneration, so too does the frustum.
	if (needs_frustum) {
		vec3 forward = mat4_forward(data->transform);
		vec3 target = vec3_add(data->position, vec3_mul_scalar(forward, data->far_clip));
		data->frustum = kfrustum_create(
			data->position,
			target,
			vec3_up(),
			(f32)data->vp_rect.width / data->vp_rect.height,
			data->fov,
			data->near_clip,
			data->far_clip);
	}
}
