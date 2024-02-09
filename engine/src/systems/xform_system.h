#ifndef _XFORM_SYSTEM_H_
#define _XFORM_SYSTEM_H_

#include "core/identifier.h"
#include "core/khandle.h"
#include "math/math_types.h"

struct frame_data;

/**
 * @brief Represents the xform of an object in the world.
 * xforms can have a parent whose own xform is then
 * taken into account. NOTE: The properties of this should not
 * be edited directly, but done via the functions in xform.h
 * to ensure proper matrix generation.
 */
typedef struct xform {
    /** @brief The position in the world. */
    vec3 position;
    /** @brief The rotation in the world. */
    quat rotation;
    /** @brief The scale in the world. */
    vec3 scale;
    /**
     * @brief Indicates if the position, rotation or scale have changed,
     * indicating that the local matrix needs to be recalculated.
     */
    b8 is_dirty;
    /**
     * @brief The local xformation matrix, updated whenever
     * the position, rotation or scale have changed.
     */
    mat4 local;

    f32 determinant;

    /** @brief A handle to a parent xform if one is assigned. */
    k_handle parent;

    /** @brief A globally unique id used to validate the handle against the object it was created for. */
    identifier unique_id;
} xform;

typedef struct xform_system_config {
    // The initial number of slots to allocate for xforms on startup.
    u32 initial_slot_count;
} xform_system_config;

b8 xform_system_initialize(u64* memory_requirement, void* state, void* config);

void xform_system_shutdown(void* state);

b8 xform_system_update(void* state, struct frame_data* p_frame_data);

/**
 * @brief Creates and returns a new xform, using a zero
 * vector for position, identity quaternion for rotation, and
 * a one vector for scale. Also has a null parent. Marked dirty
 * by default.
 * @return A handle to the new xform.
 */
KAPI k_handle xform_create(void);

/**
 * @brief Creates a xform from the given position.
 * Uses a zero rotation and a one scale.
 *
 * @param position The position to be used.
 * @return A handle to the new xform.
 */
KAPI k_handle xform_from_position(vec3 position);

/**
 * @brief Creates a xform from the given rotation.
 * Uses a zero position and a one scale.
 *
 * @param rotation The rotation to be used.
 * @return A handle to the new xform.
 */
KAPI k_handle xform_from_rotation(quat rotation);

/**
 * @brief Creates a xform from the given position and rotation.
 * Uses a one scale.
 *
 * @param position The position to be used.
 * @param rotation The rotation to be used.
 * @return A handle to the new xform.
 */
KAPI k_handle xform_from_position_rotation(vec3 position, quat rotation);

/**
 * @brief Creates a xform from the given position, rotation and scale.
 *
 * @param position The position to be used.
 * @param rotation The rotation to be used.
 * @param scale The scale to be used.
 * @return A handle to the new xform.
 */
KAPI k_handle xform_from_position_rotation_scale(vec3 position, quat rotation, vec3 scale);

/**
 * @brief Destroys the xform with the given handle, and frees the handle.
 * @param t A pointer to a handle to the transform to be destroyed. The handle itself is also invalidated.
 */
KAPI void xform_destroy(k_handle* t);

/**
 * @brief Returns a constant pointer to a xform if one exists for the given handle.
 * @param t A handle to the xform.
 *
 * @returns A constant pointer to an xform if the handle is valid; otherwise null.
 */
KAPI const xform* xform_from_handle(k_handle t);

/**
 * @brief Returns a handle to the provided xform's parent.
 *
 * @param t A handle to the xform whose parent to retrieve.
 * @return A handle to the parent xform. If not found, an invalid handle.
 */
KAPI k_handle xform_parent_get(k_handle handle);

/**
 * @brief Sets the parent of the provided xform.
 *
 * @param t A handle to the xform whose parent will be set.
 * @param parent A handle to the parent xform.
 */
KAPI void xform_parent_set(k_handle t, k_handle parent);

/**
 * @brief Returns the position of the given xform.
 *
 * @param t A handle whose position to get.
 * @return A copy of the position.
 */
KAPI vec3 xform_position_get(k_handle t);

/**
 * @brief Sets the position of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 */
KAPI void xform_position_set(k_handle t, vec3 position);

/**
 * @brief Applies a translation to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param translation The translation to be applied.
 */
KAPI void xform_translate(k_handle t, vec3 translation);

/**
 * @brief Returns the rotation of the given xform.
 *
 * @param t A handle whose rotation to get.
 * @return A copy of the rotation.
 */
KAPI quat xform_rotation_get(k_handle t);

/**
 * @brief Sets the rotation of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param rotation The rotation to be set.
 */
KAPI void xform_rotation_set(k_handle t, quat rotation);

/**
 * @brief Applies a rotation to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param rotation The rotation to be applied.
 */
KAPI void xform_rotate(k_handle t, quat rotation);

/**
 * @brief Returns the scale of the given xform.
 *
 * @param t A handle whose scale to get.
 * @return A copy of the scale.
 */
KAPI vec3 xform_scale_get(k_handle t);

/**
 * @brief Sets the scale of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param scale The scale to be set.
 */
KAPI void xform_scale_set(k_handle t, vec3 scale);

/**
 * @brief Applies a scale to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param scale The scale to be applied.
 */
KAPI void xform_scale(k_handle t, vec3 scale);

/**
 * @brief Sets the position and rotation of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 * @param rotation The rotation to be set.
 */
KAPI void xform_position_rotation_set(k_handle t, vec3 position, quat rotation);

/**
 * @brief Sets the position, rotation and scale of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 * @param rotation The rotation to be set.
 * @param scale The scale to be set.
 */
KAPI void xform_position_rotation_scale_set(k_handle t, vec3 position, quat rotation, vec3 scale);

/**
 * @brief Applies translation and rotation to the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param translation The translation to be applied.
 * @param rotation The rotation to be applied.
 * @return KAPI
 */
KAPI void xform_translate_rotate(k_handle t, vec3 translation, quat rotation);

/**
 * @brief Retrieves the local xformation matrix from the provided xform.
 * Automatically recalculates the matrix if it is dirty. Otherwise, the already
 * calculated one is returned.
 *
 * @param t A handle to the xform whose matrix to retrieve.
 * @return A copy of the local xformation matrix.
 */
KAPI mat4 xform_local_get(k_handle t);

/**
 * @brief Obtains the world matrix of the given xform
 * by examining its parent (if there is one) and multiplying it
 * against the local matrix.
 *
 * @param t A handle to the xform whose world matrix to retrieve.
 * @return A copy of the world matrix.
 */
KAPI mat4 xform_world_get(k_handle t);

#endif
