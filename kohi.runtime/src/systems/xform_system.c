#include "xform_system.h"

#include <stdio.h>

#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "identifiers/identifier.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

// Going with a SOA here so that like data is grouped together.
typedef struct xform_system_state {
    /** @brief The cached local matrices in the world, indexed by handle. */
    mat4* local_matrices;

    /** @brief The cached world matrices in the world, indexed by handle. */
    mat4* world_matrices;

    /** @brief The positions in the world, indexed by handle. */
    vec3* positions;

    /** @brief The rotations in the world, indexed by handle. */
    quat* rotations;

    /** @brief The scales in the world, indexed by handle. */
    vec3* scales;

    /** @brief A globally unique id used to validate handles against the xform they were created for. Indexed by handle. */
    identifier* ids;

    /** @brief A list of handle ids that represent dirty local xforms. */
    u32* local_dirty_handles;
    u32 local_dirty_count;

    /** The number of currently-allocated slots available (NOT the allocated space in bytes!) */
    u32 allocated;

} xform_system_state;

/**
 * @brief Ensures the state has enough space for the provided slot count.
 * Reallocates as needed if not.
 * @param state A pointer to the state.
 * @param slot_count The number of slots to ensure exist.
 */
static void ensure_allocated(xform_system_state* state, u32 slot_count);
static void dirty_list_reset(xform_system_state* state);
static void dirty_list_add(xform_system_state* state, khandle t);
static khandle handle_create(xform_system_state* state);
static void handle_destroy(xform_system_state* state, khandle* t);
// Validates the handle itself, as well as compares it against the xform at the handle's index position.
static b8 validate_handle(xform_system_state* state, khandle handle);

b8 xform_system_initialize(u64* memory_requirement, void* state, void* config) {
    *memory_requirement = sizeof(xform_system_state);

    if (!state) {
        return true;
    }

    xform_system_config* typed_config = config;
    xform_system_state* typed_state = state;

    kzero_memory(state, sizeof(xform_system_state));

    if (typed_config->initial_slot_count == 0) {
        KERROR("xform_system_config->initial_slot_count must be greater than 0. Defaulting to 128 instead.");
        typed_config->initial_slot_count = 128;
    }

    ensure_allocated(state, typed_config->initial_slot_count);

    // Invalidate all IDs.
    for (u32 i = 0; i < typed_config->initial_slot_count; ++i) {
        typed_state->ids[i].uniqueid = INVALID_ID_U64;
    }

    dirty_list_reset(state);

    return true;
}

void xform_system_shutdown(void* state) {
    if (state) {
        xform_system_state* typed_state = state;

        if (typed_state->local_matrices) {
            kfree_aligned(typed_state->local_matrices, sizeof(mat4) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->local_matrices = 0;
        }
        if (typed_state->world_matrices) {
            kfree_aligned(typed_state->world_matrices, sizeof(mat4) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->world_matrices = 0;
        }
        if (typed_state->positions) {
            kfree_aligned(typed_state->positions, sizeof(vec3) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->positions = 0;
        }
        if (typed_state->rotations) {
            kfree_aligned(typed_state->rotations, sizeof(quat) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->rotations = 0;
        }
        if (typed_state->scales) {
            kfree_aligned(typed_state->scales, sizeof(vec3) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->scales = 0;
        }
        if (typed_state->ids) {
            kfree_aligned(typed_state->ids, sizeof(identifier) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->ids = 0;
        }
        if (typed_state->local_dirty_handles) {
            kfree_aligned(typed_state->local_dirty_handles, sizeof(u32) * typed_state->allocated, 16, MEMORY_TAG_TRANSFORM);
            typed_state->local_dirty_handles = 0;
        }
    }
}

b8 xform_system_update(void* state, struct frame_data* p_frame_data) {
    // TODO: update locals for dirty xforms, reset list.
    return true;
}

khandle xform_create(void) {
    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = vec3_zero();
        state->rotations[i] = quat_identity();
        state->scales[i] = vec3_one();
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // NOTE: This is not added to the dirty list because the defualts form an identity matrix.
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = khandle_invalid();
    }
    return handle;
}

khandle xform_from_position(vec3 position) {
    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = position;
        state->rotations[i] = quat_identity();
        state->scales[i] = vec3_one();
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // Add to the dirty list.
        dirty_list_add(state, handle);
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = khandle_invalid();
    }
    return handle;
}

khandle xform_from_rotation(quat rotation) {
    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = vec3_zero();
        state->rotations[i] = rotation;
        state->scales[i] = vec3_one();
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // Add to the dirty list.
        dirty_list_add(state, handle);
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = khandle_invalid();
    }
    return handle;
}

khandle xform_from_position_rotation(vec3 position, quat rotation) {
    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = position;
        state->rotations[i] = rotation;
        state->scales[i] = vec3_one();
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // Add to the dirty list.
        dirty_list_add(state, handle);
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = khandle_invalid();
    }
    return handle;
}

khandle xform_from_position_rotation_scale(vec3 position, quat rotation, vec3 scale) {
    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = position;
        state->rotations[i] = rotation;
        state->scales[i] = scale;
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // Add to the dirty list.
        dirty_list_add(state, handle);
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = khandle_invalid();
    }
    return handle;
}

khandle xform_from_matrix(mat4 m) {
    // TODO: decompose matrix
    KASSERT_MSG(false, "Not implemented.");
    return khandle_invalid();
}

void xform_destroy(khandle* t) {
    handle_destroy(engine_systems_get()->xform_system, t);
}

vec3 xform_position_get(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, returning zero vector as position.");
        return vec3_zero();
    }
    return state->positions[t.handle_index];
}

void xform_position_set(khandle t, vec3 position) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->positions[t.handle_index] = position;
        dirty_list_add(state, t);
    }
}

void xform_translate(khandle t, vec3 translation) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->positions[t.handle_index] = vec3_add(state->positions[t.handle_index], translation);
        dirty_list_add(state, t);
    }
}

quat xform_rotation_get(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, returning identity vector as rotation.");
        return quat_identity();
    }
    return state->rotations[t.handle_index];
}

void xform_rotation_set(khandle t, quat rotation) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->rotations[t.handle_index] = rotation;
        dirty_list_add(state, t);
    }
}

void xform_rotate(khandle t, quat rotation) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->rotations[t.handle_index] = quat_mul(state->rotations[t.handle_index], rotation);
        dirty_list_add(state, t);
    }
}

vec3 xform_scale_get(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, returning one vector as scale.");
        return vec3_zero();
    }
    return state->scales[t.handle_index];
}

void xform_scale_set(khandle t, vec3 scale) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->scales[t.handle_index] = scale;
        dirty_list_add(state, t);
    }
}

void xform_scale(khandle t, vec3 scale) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->scales[t.handle_index] = vec3_mul(state->scales[t.handle_index], scale);
        dirty_list_add(state, t);
    }
}

void xform_position_rotation_set(khandle t, vec3 position, quat rotation) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->positions[t.handle_index] = position;
        state->rotations[t.handle_index] = rotation;
        dirty_list_add(state, t);
    }
}

void xform_position_rotation_scale_set(khandle t, vec3 position, quat rotation, vec3 scale) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->positions[t.handle_index] = position;
        state->rotations[t.handle_index] = rotation;
        state->scales[t.handle_index] = scale;
        dirty_list_add(state, t);
    }
}

void xform_translate_rotate(khandle t, vec3 translation, quat rotation) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!validate_handle(state, t)) {
        KWARN("Invalid handle passed, nothing was done.");
    } else {
        state->positions[t.handle_index] = vec3_add(state->positions[t.handle_index], translation);
        state->rotations[t.handle_index] = quat_mul(state->rotations[t.handle_index], rotation);
        dirty_list_add(state, t);
    }
}

void xform_calculate_local(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!khandle_is_invalid(t)) {
        u32 index = t.handle_index;
        // TODO: investigate mat4_from_translation_rotation_scale
        state->local_matrices[index] = mat4_mul(quat_to_mat4(state->rotations[index]), mat4_translation(state->positions[index]));
        state->local_matrices[index] = mat4_mul(mat4_scale(state->scales[index]), state->local_matrices[index]);
    }
}

void xform_world_set(khandle t, mat4 world) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!khandle_is_invalid(t)) {
        state->world_matrices[t.handle_index] = world;
    }
}

mat4 xform_world_get(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!khandle_is_invalid(t)) {
        return state->world_matrices[t.handle_index];
    }

    KWARN("Invalid handle passed to xform_world_get. Returning identity matrix.");
    return mat4_identity();
}

mat4 xform_local_get(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!khandle_is_invalid(t)) {
        u32 index = t.handle_index;
        return state->local_matrices[index];
    }

    KWARN("Invalid handle passed to xform_local_get. Returning identity matrix.");
    return mat4_identity();
}

const char* xform_to_string(khandle t) {
    xform_system_state* state = engine_systems_get()->xform_system;
    if (!khandle_is_invalid(t)) {
        u32 index = t.handle_index;
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

    KERROR("Invalid handle passed to xform_to_string. Returning null.");
    return 0;
}

b8 xform_from_string(const char* str, khandle* out_xform) {
    if (!out_xform) {
        KERROR("string_to_scene_xform_config requires a valid pointer to out_xform.");
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
            KWARN("Format error: invalid xform provided. Identity transform will be used.");
            result = false;
        }
    }

    khandle handle = {0};
    xform_system_state* state = engine_systems_get()->xform_system;
    if (state) {
        handle = handle_create(state);
        u32 i = handle.handle_index;
        state->positions[i] = position;
        state->rotations[i] = rotation;
        state->scales[i] = scale;
        state->local_matrices[i] = mat4_identity();
        state->world_matrices[i] = mat4_identity();
        // Add to the dirty list.
        dirty_list_add(state, handle);
    } else {
        KERROR("Attempted to create a xform before the system was initialized.");
        *out_xform = khandle_invalid();
        return false;
    }

    *out_xform = handle;
    return result;
}

static void ensure_allocated(xform_system_state* state, u32 slot_count) {
    KASSERT_MSG(slot_count % 8 == 0, "ensure_allocated requires new slot_count to be a multiple of 8.");

    if (state->allocated < slot_count) {
        // Setup the arrays of data, starting with the matrices. These should be 16-bit
        // aligned so that SIMD is an easy addition later on.
        mat4* new_local_matrices = kallocate_aligned(sizeof(mat4) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->local_matrices) {
            kcopy_memory(new_local_matrices, state->local_matrices, sizeof(mat4) * state->allocated);
            kfree_aligned(state->local_matrices, sizeof(mat4) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->local_matrices = new_local_matrices;

        mat4* new_world_matrices = kallocate_aligned(sizeof(mat4) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->world_matrices) {
            kcopy_memory(new_world_matrices, state->world_matrices, sizeof(mat4) * state->allocated);
            kfree_aligned(state->world_matrices, sizeof(mat4) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->world_matrices = new_world_matrices;

        // Also align positions, rotations and scales for future SIMD purposes.
        vec3* new_positions = kallocate_aligned(sizeof(vec3) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->positions) {
            kcopy_memory(new_positions, state->positions, sizeof(vec3) * state->allocated);
            kfree_aligned(state->positions, sizeof(vec3) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->positions = new_positions;

        quat* new_rotations = kallocate_aligned(sizeof(quat) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->rotations) {
            kcopy_memory(new_rotations, state->rotations, sizeof(quat) * state->allocated);
            kfree_aligned(state->rotations, sizeof(quat) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->rotations = new_rotations;

        vec3* new_scales = kallocate_aligned(sizeof(vec3) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->scales) {
            kcopy_memory(new_scales, state->scales, sizeof(vec3) * state->allocated);
            kfree_aligned(state->scales, sizeof(vec3) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->scales = new_scales;

        // Identifiers don't *need* to be aligned, but do it anyways since everything else is.
        identifier* new_ids = kallocate_aligned(sizeof(identifier) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->ids) {
            kcopy_memory(new_ids, state->ids, sizeof(identifier) * state->allocated);
            kfree_aligned(state->ids, sizeof(identifier) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->ids = new_ids;

        // Dirty handle list doesn't *need* to be aligned, but do it anyways since everything else is.
        u32* new_dirty_handles = kallocate_aligned(sizeof(u32) * slot_count, 16, MEMORY_TAG_TRANSFORM);
        if (state->local_dirty_handles) {
            kcopy_memory(new_dirty_handles, state->local_dirty_handles, sizeof(u32) * state->allocated);
            kfree_aligned(state->local_dirty_handles, sizeof(u32) * state->allocated, 16, MEMORY_TAG_TRANSFORM);
        }
        state->local_dirty_handles = new_dirty_handles;

        // Make sure the allocated count is up to date.
        state->allocated = slot_count;
    }
}

static void dirty_list_reset(xform_system_state* state) {
    for (u32 i = 0; i < state->local_dirty_count; ++i) {
        state->local_dirty_handles[i] = INVALID_ID;
    }
    state->local_dirty_count = 0;
}

static void dirty_list_add(xform_system_state* state, khandle t) {
    for (u32 i = 0; i < state->local_dirty_count; ++i) {
        if (state->local_dirty_handles[i] == t.handle_index) {
            // Already there, do nothing.
            return;
        }
    }
    state->local_dirty_handles[state->local_dirty_count] = t.handle_index;
    state->local_dirty_count++;
}

static khandle handle_create(xform_system_state* state) {
    KASSERT_MSG(state, "xform_system state pointer accessed before initialized");

    khandle handle;
    u32 xform_count = state->allocated;
    for (u32 i = 0; i < xform_count; ++i) {
        if (state->ids[i].uniqueid == INVALID_ID_U64) {
            // Found an entry. Fill out the handle, and update the unique_id.uniqueid
            handle = khandle_create(i);
            state->ids[i].uniqueid = handle.unique_id.uniqueid;
            return handle;
        }
    }

    // No open slots, expand array and use the first slot of the new memory.
    ensure_allocated(state, state->allocated * 2);
    handle = khandle_create(xform_count);
    state->ids[xform_count].uniqueid = handle.unique_id.uniqueid;
    return handle;
}

static void handle_destroy(xform_system_state* state, khandle* t) {
    KASSERT_MSG(state, "xform_system state pointer accessed before initialized");

    if (t->handle_index != INVALID_ID) {
        state->ids[t->handle_index].uniqueid = INVALID_ID_U64;
    }

    khandle_invalidate(t);
}

static b8 validate_handle(xform_system_state* state, khandle handle) {
    if (khandle_is_invalid(handle)) {
        KTRACE("Handle validation failed because the handle is invalid.");
        return false;
    }

    if (handle.handle_index >= state->allocated) {
        KTRACE("Provided handle index is out of bounds: %u", handle.handle_index);
        return false;
    }

    // Check for a match.
    return state->ids[handle.handle_index].uniqueid == handle.unique_id.uniqueid;
}
