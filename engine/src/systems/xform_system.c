#include "xform_system.h"

#include "containers/darray.h"
#include "core/asserts.h"
#include "core/identifier.h"
#include "core/khandle.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"

typedef struct xform_system_state {
    // darray
    xform* xforms;
} xform_system_state;

static xform_system_state* state_ptr;

static mat4 xform_local_get_internal(xform* x);
static mat4 xform_world_get_internal(xform* x);
// Validates the handle itself, as well as compares it against the provided transform.
static b8 validate_handle(const k_handle* handle, const xform* x);
// Gets a mutable pointer to an xform. Performs handle validation.
static xform* xform_from_handle_internal(k_handle t);

b8 xform_system_initialize(u64* memory_requirement, void* state, void* config) {
    *memory_requirement = sizeof(xform_system_state);

    if (!state) {
        return true;
    }

    state_ptr = state;

    xform_system_config* typed_config = config;

    // Reserve space for a lot of these, and invalidate them.
    state_ptr->xforms = darray_reserve(xform, typed_config->initial_slot_count);
    darray_length_set(state_ptr->xforms, typed_config->initial_slot_count);
    for (u32 i = 0; i < typed_config->initial_slot_count; ++i) {
        state_ptr->xforms[i].unique_id.uniqueid = INVALID_ID_U64;
    }

    return true;
}

void xform_system_shutdown(void* state) {
    if (state_ptr) {
        darray_destroy(state_ptr->xforms);

        state_ptr = 0;
    }
}

b8 xform_system_update(void* state, struct frame_data* p_frame_data) {
    xform_system_state* typed_state = state;
    u32 count = darray_length(typed_state->xforms);
    for (u32 i = 0; i < count; ++i) {
        xform_world_get_internal(&typed_state->xforms[i]);
    }
    // TODO: implement this
    return true;
}

static k_handle handle_create(void) {
    KASSERT_MSG(state_ptr, "xform_system state pointer accessed before initialized");

    k_handle handle;
    u32 xform_count = darray_length(state_ptr->xforms);
    for (u32 i = 0; i < xform_count; ++i) {
        if (state_ptr->xforms[i].unique_id.uniqueid == INVALID_ID_U64) {
            // Found an entry. Fill out the handle, and update the unique_id.uniqueid
            handle = k_handle_create(i);
            state_ptr->xforms[i].unique_id.uniqueid = handle.unique_id.uniqueid;
            return handle;
        }
    }

    // No open slots, expand array and use the last slot.
    handle = k_handle_create(xform_count);
    darray_push(state_ptr->xforms, handle);
    state_ptr->xforms[xform_count].unique_id.uniqueid = handle.unique_id.uniqueid;
    return handle;
}

static void handle_destroy(k_handle* t) {
    KASSERT_MSG(state_ptr, "xform_system state pointer accessed before initialized");

    if (t->handle_index != INVALID_ID) {
        state_ptr->xforms[t->handle_index].unique_id.uniqueid = INVALID_ID_U64;
    }

    k_handle_invalidate(t);
}

k_handle xform_create(void) {
    k_handle handle = {0};
    if (state_ptr) {
        handle = handle_create();
        xform* x = &state_ptr->xforms[handle.handle_index];
        x->position = vec3_zero();
        x->rotation = quat_identity();
        x->scale = vec3_one();
        x->local = mat4_identity();
        x->is_dirty = false;
        x->parent = k_handle_invalid();
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = k_handle_invalid();
    }
    return handle;
}

k_handle xform_from_position(vec3 position) {
    k_handle handle = {0};
    if (state_ptr) {
        handle = handle_create();
        xform* x = &state_ptr->xforms[handle.handle_index];
        x->position = position;
        x->rotation = quat_identity();
        x->scale = vec3_one();
        x->local = mat4_identity();
        x->is_dirty = true;
        x->parent = k_handle_invalid();
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = k_handle_invalid();
    }
    return handle;
}

k_handle xform_from_rotation(quat rotation) {
    k_handle handle = {0};
    if (state_ptr) {
        handle = handle_create();
        xform* x = &state_ptr->xforms[handle.handle_index];
        x->position = vec3_zero();
        x->rotation = rotation;
        x->scale = vec3_one();
        x->local = mat4_identity();
        x->is_dirty = true;
        x->parent = k_handle_invalid();
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = k_handle_invalid();
    }
    return handle;
}

k_handle xform_from_position_rotation(vec3 position, quat rotation) {
    k_handle handle = {0};
    if (state_ptr) {
        handle = handle_create();
        xform* x = &state_ptr->xforms[handle.handle_index];
        x->position = position;
        x->rotation = rotation;
        x->scale = vec3_one();
        x->local = mat4_identity();
        x->is_dirty = true;
        x->parent = k_handle_invalid();
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = k_handle_invalid();
    }
    return handle;
}

k_handle xform_from_position_rotation_scale(vec3 position, quat rotation, vec3 scale) {
    k_handle handle = {0};
    if (state_ptr) {
        handle = handle_create();
        xform* x = &state_ptr->xforms[handle.handle_index];
        x->position = position;
        x->rotation = rotation;
        x->scale = scale;
        x->local = mat4_identity();
        x->is_dirty = true;
        x->parent = k_handle_invalid();
    } else {
        KERROR("Attempted to create a transform before the system was initialized.");
        handle = k_handle_invalid();
    }
    return handle;
}

void xform_destroy(k_handle* t) {
    handle_destroy(t);
}

const xform* xform_from_handle(k_handle t) {
    return xform_from_handle_internal(t);
}

k_handle xform_parent_get(k_handle t) {
    const xform* x = xform_from_handle(t);
    // Current xform exists, get parent.
    if (x && !k_handle_is_invalid(x->parent)) {
        return x->parent;
    }

    // Invalid handle or parent not found/does not exist.
    return k_handle_invalid();
}

void xform_parent_set(k_handle t, k_handle parent) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->parent = parent;
    }
}

vec3 xform_position_get(k_handle t) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        return x->position;
    }

    KWARN("Invalid handle passed, returning zero vector as position.");
    return vec3_zero();
}

void xform_position_set(k_handle t, vec3 position) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->position = position;
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_translate(k_handle t, vec3 translation) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->position = vec3_add(x->position, translation);
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

quat xform_rotation_get(k_handle t) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        return x->rotation;
    }

    KWARN("Invalid handle passed, returning identity vector as rotation.");
    return quat_identity();
}

void xform_rotation_set(k_handle t, quat rotation) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->rotation = rotation;
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_rotate(k_handle t, quat rotation) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->rotation = quat_mul(x->rotation, rotation);
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

vec3 xform_scale_get(k_handle t) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        return x->scale;
    }

    KWARN("Invalid handle passed, returning one vector as scale.");
    return vec3_zero();
}

void xform_scale_set(k_handle t, vec3 scale) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->scale = scale;
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_scale(k_handle t, vec3 scale) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->scale = vec3_mul(x->scale, scale);
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_position_rotation_set(k_handle t, vec3 position, quat rotation) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->position = position;
        x->rotation = rotation;
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_position_rotation_scale_set(k_handle t, vec3 position, quat rotation, vec3 scale) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->position = position;
        x->rotation = rotation;
        x->scale = scale;
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

void xform_translate_rotate(k_handle t, vec3 translation, quat rotation) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        x->position = vec3_add(x->position, translation);
        x->rotation = quat_mul(x->rotation, rotation);
        x->is_dirty = true;
        return;
    }

    KWARN("Invalid handle passed, nothing was done.");
}

static mat4 xform_local_get_internal(xform* x) {
    if (x->is_dirty) {
        mat4 tr = mat4_mul(quat_to_mat4(x->rotation), mat4_translation(x->position));
        tr = mat4_mul(mat4_scale(x->scale), tr);
        x->local = tr;
        x->is_dirty = false;
    }

    return x->local;
}

mat4 xform_local_get(k_handle t) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        return xform_local_get_internal(x);
    }
    KWARN("xform_local_get was provided a stale handle. Nothing was done.");
    return mat4_identity();
}

static mat4 xform_world_get_internal(xform* x) {
    // Get the "local" matrix, updated if needed.
    mat4 l = xform_local_get_internal(x);

    // Attempt to get the parent, if valid.
    xform* parent = xform_from_handle_internal(x->parent);
    if (parent) {
        // Update the local to include the parent.
        // This also recursively works up the tree to ensure that parent's matrix is clean.
        mat4 p = xform_world_get_internal(parent);
        mat4 r = mat4_mul(l, p);
        // Save off the determinant at the bottom-most level.
        x->determinant = mat4_determinant(r);
        return r;
    }

    // If no parent, do it against the local matrix instead.
    x->determinant = mat4_determinant(l);
    return l;
}

mat4 xform_world_get(k_handle t) {
    xform* x = xform_from_handle_internal(t);
    if (x) {
        return xform_world_get_internal(x);
    }
    KWARN("xform_world_get was provided a stale handle. Nothing was done.");
    return mat4_identity();
}

static b8 validate_handle(const k_handle* handle, const xform* x) {
    if (!handle || !x) {
        KTRACE("handle validation failed because the handle or the xform it's supposed to point to is null.");
        return false;
    }
    if (k_handle_is_invalid(*handle)) {
        KTRACE("Handle validation failed because the handle is invalid.");
        return false;
    }
    if (handle->handle_index >= darray_length(state_ptr->xforms)) {
        KTRACE("Provided handle index is out of bounds: %u", handle->handle_index);
        return false;
    }

    // Check for a match.
    return x->unique_id.uniqueid == handle->unique_id.uniqueid;
}

static xform* xform_from_handle_internal(k_handle t) {
    if (!state_ptr) {
        return 0;
    }

    // Validate that the handle is not stale before returning it.
    xform* x = &state_ptr->xforms[t.handle_index];
    if (validate_handle(&t, x)) {
        return x;
    }

    // If stale, don't return it.
    KWARN("xform_from_handle_internal was provided a stale handle. Nothing will be returned.");
    return 0;
}
