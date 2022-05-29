/**
 * @file camera.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief 
 * @version 1.0
 * @date 2022-05-21
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "math/math_types.h"

/**
 * @brief Represents a camera that can be used for
 * a variety of things, especially rendering. Ideally,
 * these are created and managed by the camera system.
 */
typedef struct camera {
    /**
     * @brief The position of this camera.
     * NOTE: Do not set this directly, use camera_positon_set() instead
     * so the view matrix is recalculated when needed.
     */
    vec3 position;
    /**
     * @brief The rotation of this camera using Euler angles (pitch, yaw, roll).
     * NOTE: Do not set this directly, use camera_rotation_euler_set() instead
     * so the view matrix is recalculated when needed.
     */
    vec3 euler_rotation;
    /** @brief Internal flag used to determine when the view matrix needs to be rebuilt. */
    b8 is_dirty;

    /**
     * @brief The view matrix of this camera.
     * NOTE: IMPORTANT: Do not get this directly, use camera_view_get() instead
     * so the view matrix is recalculated when needed.
     */
    mat4 view_matrix;
} camera;

/**
 * @brief Creates a new camera with default zero position
 * and rotation, and view identity matrix. Ideally, the
 * camera system should be used to create this instead
 * of doing so directly.
 *
 * @return A copy of a newly-created camera.
 */
KAPI camera camera_create();

/**
 * @brief Defaults the provided camera to default zero
 * rotation and position, and view matrix to identity.
 *
 * @param c A pointer to the camera to be reset.
 */
KAPI void camera_reset(camera* c);

/**
 * @brief Gets a copy of the camera's position.
 *
 * @param c A constant pointer to a camera.
 * @return A copy of the camera's position.
 */
KAPI vec3 camera_position_get(const camera* c);

/**
 * @brief Sets the provided camera's position.
 *
 * @param c A pointer to a camera.
 * @param position The position to be set.
 */
KAPI void camera_position_set(camera* c, vec3 position);

/**
 * @brief Gets a copy of the camera's rotation in Euler angles.
 *
 * @param c A constant pointer to a camera.
 * @return A copy of the camera's rotation in Euler angles.
 */
KAPI vec3 camera_rotation_euler_get(const camera* c);

/**
 * @brief Sets the provided camera's rotation in Euler angles.
 *
 * @param c A pointer to a camera.
 * @param position The rotation in Euler angles to be set.
 */
KAPI void camera_rotation_euler_set(camera* c, vec3 rotation);

/**
 * @brief Obtains a copy of the camera's view matrix. If camera is
 * dirty, a new one is created, set and returned.
 *
 * @param c A pointer to a camera.
 * @return A copy of the up-to-date view matrix.
 */
KAPI mat4 camera_view_get(camera* c);

/**
 * @brief Returns a copy of the camera's forward vector.
 *
 * @param c A pointer to a camera.
 * @return A copy of the camera's forward vector.
 */
KAPI vec3 camera_forward(camera* c);

/**
 * @brief Returns a copy of the camera's backward vector.
 *
 * @param c A pointer to a camera.
 * @return A copy of the camera's backward vector.
 */
KAPI vec3 camera_backward(camera* c);

/**
 * @brief Returns a copy of the camera's left vector.
 *
 * @param c A pointer to a camera.
 * @return A copy of the camera's left vector.
 */
KAPI vec3 camera_left(camera* c);

/**
 * @brief Returns a copy of the camera's right vector.
 *
 * @param c A pointer to a camera.
 * @return A copy of the camera's right vector.
 */
KAPI vec3 camera_right(camera* c);

/**
 * @brief Moves the camera forward by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_forward(camera* c, f32 amount);

/**
 * @brief Moves the camera backward by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_backward(camera* c, f32 amount);

/**
 * @brief Moves the camera left by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_left(camera* c, f32 amount);

/**
 * @brief Moves the camera right by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_right(camera* c, f32 amount);

/**
 * @brief Moves the camera up (straight along the y-axis) by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_up(camera* c, f32 amount);

/**
 * @brief Moves the camera down (straight along the y-axis) by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to move.
 */
KAPI void camera_move_down(camera* c, f32 amount);

/**
 * @brief Adjusts the camera's yaw by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to adjust by.
 */
KAPI void camera_yaw(camera* c, f32 amount);

/**
 * @brief Adjusts the camera's pitch by the given amount.
 *
 * @param c A pointer to a camera.
 * @param amount The amount to adjust by.
 */
KAPI void camera_pitch(camera* c, f32 amount);
