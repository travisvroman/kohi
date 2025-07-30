#ifndef _XFORM_SYSTEM_H_
#define _XFORM_SYSTEM_H_

#include <core_resource_types.h>
#include <math/math_types.h>

struct frame_data;

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
KAPI ktransform xform_create(void);

/**
 * @brief Creates a xform from the given position.
 * Uses a zero rotation and a one scale.
 *
 * @param position The position to be used.
 * @return A handle to the new xform.
 */
KAPI ktransform xform_from_position(vec3 position);

/**
 * @brief Creates a xform from the given rotation.
 * Uses a zero position and a one scale.
 *
 * @param rotation The rotation to be used.
 * @return A handle to the new xform.
 */
KAPI ktransform xform_from_rotation(quat rotation);

/**
 * @brief Creates a xform from the given position and rotation.
 * Uses a one scale.
 *
 * @param position The position to be used.
 * @param rotation The rotation to be used.
 * @return A handle to the new xform.
 */
KAPI ktransform xform_from_position_rotation(vec3 position, quat rotation);

/**
 * @brief Creates a xform from the given position, rotation and scale.
 *
 * @param position The position to be used.
 * @param rotation The rotation to be used.
 * @param scale The scale to be used.
 * @return A handle to the new xform.
 */
KAPI ktransform xform_from_position_rotation_scale(vec3 position, quat rotation, vec3 scale);

/**
 * @brief Creates a xform from the provided matrix.
 *
 * @param m The matrix to decompose and extract a transform from.
 * @return A handle to the new xform.
 */
KAPI ktransform xform_from_matrix(mat4 m);

/**
 * @brief Destroys the xform with the given handle, and frees the handle.
 * @param t A pointer to a handle to the transform to be destroyed. The handle itself is also invalidated.
 */
KAPI void xform_destroy(ktransform* t);

/**
 * @brief Returns the position of the given xform.
 *
 * @param t A handle whose position to get.
 * @return A copy of the position.
 */
KAPI vec3 xform_position_get(ktransform t);

/**
 * @brief Sets the position of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 */
KAPI void xform_position_set(ktransform t, vec3 position);

/**
 * @brief Applies a translation to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param translation The translation to be applied.
 */
KAPI void xform_translate(ktransform t, vec3 translation);

/**
 * @brief Returns the rotation of the given xform.
 *
 * @param t A handle whose rotation to get.
 * @return A copy of the rotation.
 */
KAPI quat xform_rotation_get(ktransform t);

/**
 * @brief Sets the rotation of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param rotation The rotation to be set.
 */
KAPI void xform_rotation_set(ktransform t, quat rotation);

/**
 * @brief Applies a rotation to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param rotation The rotation to be applied.
 */
KAPI void xform_rotate(ktransform t, quat rotation);

/**
 * @brief Returns the scale of the given xform.
 *
 * @param t A handle whose scale to get.
 * @return A copy of the scale.
 */
KAPI vec3 xform_scale_get(ktransform t);

/**
 * @brief Sets the scale of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param scale The scale to be set.
 */
KAPI void xform_scale_set(ktransform t, vec3 scale);

/**
 * @brief Applies a scale to the given xform. Not the
 * same as setting.
 *
 * @param t A handle to the xform to be updated.
 * @param scale The scale to be applied.
 */
KAPI void xform_scale(ktransform t, vec3 scale);

/**
 * @brief Sets the position and rotation of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 * @param rotation The rotation to be set.
 */
KAPI void xform_position_rotation_set(ktransform t, vec3 position, quat rotation);

/**
 * @brief Sets the position, rotation and scale of the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param position The position to be set.
 * @param rotation The rotation to be set.
 * @param scale The scale to be set.
 */
KAPI void xform_position_rotation_scale_set(ktransform t, vec3 position, quat rotation, vec3 scale);

/**
 * @brief Applies translation and rotation to the given xform.
 *
 * @param t A handle to the xform to be updated.
 * @param translation The translation to be applied.
 * @param rotation The rotation to be applied.
 * @return KAPI
 */
KAPI void xform_translate_rotate(ktransform t, vec3 translation, quat rotation);

/**
 * Recalculates the local matrix for the transform with the given handle.
 */
KAPI void xform_calculate_local(ktransform t);

/**
 * @brief Retrieves the local xformation matrix from the provided xform.
 * Automatically recalculates the matrix if it is dirty. Otherwise, the already
 * calculated one is returned.
 *
 * @param t A handle to the xform whose matrix to retrieve.
 * @return A copy of the local xformation matrix.
 */
KAPI mat4 xform_local_get(ktransform t);

KAPI void xform_world_set(ktransform t, mat4 world);

/**
 * @brief Obtains the world matrix of the given xform.
 *
 * @param t A handle to the xform whose world matrix to retrieve.
 * @return A copy of the world matrix.
 */
KAPI mat4 xform_world_get(ktransform t);

/**
 * @brief Returns a string representation of the xform pointed to by the given handle.
 *
 * @param t A handle to the xform to retrieve as a string.
 * @return The xform in string format.
 */
KAPI const char* xform_to_string(ktransform t);

/**
 * @brief Creates an xform from the given string.
 *
 * @param str The string from which to create the xform. Should be either 'x y z qx qy qz qw sx sy sz' (quaternion rotation) OR 'x y z ex ey ez sx sy sz' (euler rotation)
 * @param out_xform A pointer to hold the handle to the newly created xform.
 * @returns True on success; otherwise false.
 */
KAPI b8 xform_from_string(const char* str, ktransform* out_xform);

#endif
