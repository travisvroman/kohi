#include "kcamera_system.h"

#include "core_render_types.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
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
    mat4 transform;
    mat4 projection;
    kfrustum frustum;
    rect_2di vp_rect;
    vec3 position;
    // Euler angles, but stored in radians
    vec3 euler_rotation;
    kcamera_type type;
    f32 fov;
    f32 near_clip;
    f32 far_clip;
    kcamera_flag_bits flags;
} kcamera_data;

typedef struct kcamera_system_state {
    u8 max_camera_count;
    kcamera_data* cameras;
} kcamera_system_state;

static kcamera_system_state* state_ptr;

static kcamera get_new_camera(kcamera_system_state* state) {
    for (u8 i = 0; i < state->max_camera_count; ++i) {
        if (FLAG_GET(state->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT)) {
            // Unflag it as being free
            state->cameras[i].flags = FLAG_SET(state->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT, false);
            return i;
        }
    }

    KERROR("KCamera system: The internal array is full (max_camera_count=%u). Expand this in configuration. Returning defualt camera instead.", state->max_camera_count);
    return 0;
}

// In range and not default
static b8 kcamera_is_valid(kcamera camera) {
    if (camera == DEFAULT_KCAMERA) {
        return false;
    }
    if (state_ptr) {
        if (camera >= state_ptr->max_camera_count) {
            KERROR("Tried to destroy a camera that is out of range, ya dingus!");
            return false;
        }

        return true;
    }

    return false;
}

b8 kcamera_system_initialize(u64* memory_requirement, void* state, void* config) {
    kcamera_system_config* typed_config = (kcamera_system_config*)config;
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

    state_ptr = (kcamera_system_state*)state;
    state_ptr->max_camera_count = typed_config->max_camera_count;

    // The array block is after the state. Already allocated, so just set the pointer.
    void* array_block = state + struct_requirement;
    state_ptr->cameras = array_block;

    // Mark all 'slots' as free
    for (u32 i = 0; i < state_ptr->max_camera_count; ++i) {
        state_ptr->cameras[i].flags = FLAG_SET(state_ptr->cameras[i].flags, KCAMERA_FLAG_IS_FREE_BIT, true);
    }

    // Setup default camera.
    kcamera default_camera = kcamera_create(KCAMERA_TYPE_3D, (rect_2di){0, 0, 1280, 720}, vec3_zero(), vec3_zero(), deg_to_rad(45.0f), 0.1f, 1000.0f);

    return default_camera == 0;
}

void kcamera_system_shutdown(void* state) {

    // NOTE: Nothing in the system needs shutting down as there are no
    // dynamic allocations done, or resources held by this system.
    state_ptr = 0;
}

kcamera kcamera_create(kcamera_type type, rect_2di vp_rect, vec3 position, vec3 euler_rotation, f32 fov_radians, f32 near_clip, f32 far_clip) {
    if (!state_ptr) {
        KERROR("%s: called before system was initialized.", __FUNCTION__);
        return DEFAULT_KCAMERA;
    }

    kcamera new_cam = get_new_camera(state_ptr);
    if (kcamera_is_valid(new_cam)) {
        kcamera_data* data = &state_ptr->cameras[new_cam];

        data->type = type;
        data->position = position;
        data->euler_rotation = euler_rotation;
        data->fov = fov_radians;
        data->near_clip = near_clip;
        data->far_clip = far_clip;
        data->vp_rect = vp_rect;

        // Mark transform as dirty so it gets recalculated on the next pass.
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
        // Also mark projection as dirty so it gets recalculated as well.
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
    }

    return new_cam;
}

void kcamera_destroy(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        // Nothing to destroy or release, just zero it out.
        kcamera_data* data = &state_ptr->cameras[camera];
        kzero_memory(data, sizeof(kcamera_data));

        // Mark it as free.
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_IS_FREE_BIT, true);
    }
}

kcamera kcamera_system_get_default(void) {
    return DEFAULT_KCAMERA; // 0 is the defualt
}

vec3 kcamera_get_position(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].position;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_zero();
}
void kcamera_set_position(kcamera camera, vec3 position) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->position = position;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}
vec3 kcamera_get_euler_rotation(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].euler_rotation;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_zero();
}
void kcamera_set_euler_rotation(kcamera camera, vec3 euler_rotation) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->euler_rotation = (vec3){
            euler_rotation.x = deg_to_rad(euler_rotation.x),
            euler_rotation.y = deg_to_rad(euler_rotation.y),
            euler_rotation.z = deg_to_rad(euler_rotation.z),
        };
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}
void kcamera_set_euler_rotation_radians(kcamera camera, vec3 euler_rotation_radians) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->euler_rotation = euler_rotation_radians;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}
f32 kcamera_get_fov(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].fov;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return 0.0f;
}
void kcamera_set_fov(kcamera camera, f32 fov) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->fov = fov;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
    }
}
f32 kcamera_get_near_clip(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].near_clip;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return 0.0f;
}
void kcamera_set_near_clip(kcamera camera, f32 near_clip) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->near_clip = near_clip;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
    }
}
f32 kcamera_get_far_clip(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].far_clip;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return 0.0f;
}
void kcamera_set_far_clip(kcamera camera, f32 far_clip) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->far_clip = far_clip;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
    }
}
rect_2di kcamera_get_vp_rect(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return state_ptr->cameras[camera].vp_rect;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return (rect_2di){0, 0, 0, 0};
}
void kcamera_set_vp_rect(kcamera camera, rect_2di vp_rect) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->vp_rect = vp_rect;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_PROJECTION_DIRTY_BIT, true);
    }
}

// Regenerate matrices, if needed.
static void regenerate_matrices(kcamera_data* data) {

    b8 needs_frustum = false;

    // Regenerate transform and view, if needed.
    if (FLAG_GET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT)) {
        // Recalculate transform
        mat4 rotation = mat4_euler_xyz(data->euler_rotation.x, data->euler_rotation.y, data->euler_rotation.z);
        mat4 translation = mat4_translation(data->position);
        data->transform = mat4_mul(rotation, translation);

        // View is just inverse transform.
        data->view_matrix = mat4_inverse(data->transform);

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

kfrustum kcamera_get_frustum(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        regenerate_matrices(data);
        return data->frustum;
    }
    return state_ptr->cameras[DEFAULT_KCAMERA].frustum;
}

mat4 kcamera_get_view(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        regenerate_matrices(data);
        return data->view_matrix;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return mat4_identity();
}
mat4 kcamera_get_transform(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        regenerate_matrices(data);
        return data->transform;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return mat4_identity();
}
mat4 kcamera_get_projection(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        regenerate_matrices(data);
        return data->projection;
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return mat4_identity();
}

vec3 kcamera_forward(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_forward(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_forward();
}

vec3 kcamera_backward(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_backward(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_backward();
}

vec3 kcamera_left(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_left(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_left();
}

vec3 kcamera_right(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_right(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_right();
}

vec3 kcamera_up(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_up(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_up();
}

vec3 kcamera_down(kcamera camera) {
    if (kcamera_is_valid(camera)) {
        return mat4_down(state_ptr->cameras[camera].transform);
    }
    KWARN("%s: invalid camera passed, returning default value", __FUNCTION__);
    return vec3_down();
}

void kcamera_move_direction(kcamera camera, vec3 direction, b8 normalize_dir, f32 amount) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];

        if (normalize_dir) {
            vec3_normalize(&direction);
        }

        direction = vec3_mul_scalar(direction, amount);
        data->position = vec3_add(data->position, direction);
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}

void kcamera_move_forward(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, mat4_forward(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_backward(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, mat4_backward(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_left(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, mat4_left(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_right(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, mat4_right(state_ptr->cameras[camera].transform), false, amount);
}

void kcamera_move_up(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, vec3_up(), false, amount);
}

void kcamera_move_down(kcamera camera, f32 amount) {
    kcamera_move_direction(camera, vec3_down(), false, amount);
}

void kcamera_yaw(kcamera camera, f32 amount) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->euler_rotation.y += amount;
        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}

void kcamera_pitch(kcamera camera, f32 amount) {
    if (kcamera_is_valid(camera)) {
        kcamera_data* data = &state_ptr->cameras[camera];
        data->euler_rotation.x += amount;

        // Clamp to avoid Gimball lock.
        static const f32 limit = 1.55334306f; // 89 degrees, or equivalent to deg_to_rad(89.0f);
        data->euler_rotation.x = KCLAMP(data->euler_rotation.x, -limit, limit);

        data->flags = FLAG_SET(data->flags, KCAMERA_FLAG_TRANSFORM_DIRTY_BIT, true);
    }
}
