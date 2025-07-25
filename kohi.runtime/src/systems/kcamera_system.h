/**
 * @file camera_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The camera system is responsible for managing cameras throughout the engine.
 * @version 1.0
 * @date 2022-05-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 * if(KNAME("something") == 42)
 */

#pragma once

#include "defines.h"
#include "math/math_types.h"

/** @brief The camera system configuration. */
typedef struct kcamera_system_config {
    /**
     * @brief NOTE: The maximum number of cameras that can be managed by
     * the system.
     */
    u8 max_camera_count;

} kcamera_system_config;

typedef u8 kcamera;

typedef enum kcamera_type {
    // Will use orthographic projection
    KCAMERA_TYPE_2D,
    // Will use perspective projection.
    KCAMERA_TYPE_3D
} kcamera_type;

#define DEFAULT_KCAMERA 0

/**
 * @brief Initializes the camera system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (camera_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 kcamera_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the camera system.
 *
 * @param state The state block of memory.
 */
void kcamera_system_shutdown(void* state);

/**
 * @brief Creates a new camera using the provided parameters and returns a handle to it.
 *
 * @param type The type of the camera to create. 2D = ortho projection, 3D = perspective projection.
 * @param vp_rect The "viewport" rectange (i.e. the dimensions of the camera's "face").
 * @param position The camera starting position.
 * @param rotation The camera starting rotation in Euler angles (pitch/yaw/roll) in radians.
 * @param fov_radians
 * @param near_clip
 * @param far_clip
 *
 * @return A pointer to a camera if successful; 0 if an error occurs.
 */
KAPI kcamera kcamera_create(kcamera_type type, rect_2di vp_rect, vec3 position, vec3 euler_rotation, f32 fov_radians, f32 near_clip, f32 far_clip);

/**
 * @brief Releases the given camera.
 *
 * @param name The camera to destroy.
 */
KAPI void kcamera_destroy(kcamera camera);

/**
 * @brief Gets a handle to the default camera.
 *
 * @return A handle to the default camera.
 */
KAPI kcamera kcamera_system_get_default(void);

KAPI vec3 kcamera_get_position(kcamera camera);
KAPI void kcamera_set_position(kcamera camera, vec3 position);
KAPI vec3 kcamera_get_euler_rotation(kcamera camera);
KAPI void kcamera_set_euler_rotation(kcamera camera, vec3 euler_rotation);
KAPI void kcamera_set_euler_rotation_radians(kcamera camera, vec3 euler_rotation_radians);
KAPI f32 kcamera_get_fov(kcamera camera);
KAPI void kcamera_set_fov(kcamera camera, f32 fov);
KAPI f32 kcamera_get_near_clip(kcamera camera);
KAPI void kcamera_set_near_clip(kcamera camera, f32 near_clip);
KAPI f32 kcamera_get_far_clip(kcamera camera);
KAPI void kcamera_set_far_clip(kcamera camera, f32 far_clip);
KAPI rect_2di kcamera_get_vp_rect(kcamera camera);
KAPI void kcamera_set_vp_rect(kcamera camera, rect_2di vp_rect);

KAPI kfrustum kcamera_get_frustum(kcamera camera);
KAPI mat4 kcamera_get_view(kcamera camera);
KAPI mat4 kcamera_get_transform(kcamera camera);
KAPI mat4 kcamera_get_projection(kcamera camera);

/**
 * @brief Returns a copy of the camera's forward vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's forward vector.
 */
KAPI vec3 kcamera_forward(kcamera camera);

/**
 * @brief Returns a copy of the camera's backward vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's backward vector.
 */
KAPI vec3 kcamera_backward(kcamera camera);

/**
 * @brief Returns a copy of the camera's left vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's left vector.
 */
KAPI vec3 kcamera_left(kcamera camera);

/**
 * @brief Returns a copy of the camera's right vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's right vector.
 */
KAPI vec3 kcamera_right(kcamera camera);

/**
 * @brief Returns a copy of the camera's up vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's up vector.
 */
KAPI vec3 kcamera_up(kcamera camera);

/**
 * @brief Returns a copy of the camera's down vector.
 *
 * @param camera A handle to a camera.
 * @return A copy of the camera's down vector.
 */
KAPI vec3 kcamera_down(kcamera camera);

/**
 * @brief Moves the camera forward by the given amount.
 *
 * @param camera A handle to a camera.
 * @param direction A direction vector.
 * @param normalize_dir Indicates if direction should be normalized.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_direction(kcamera camera, vec3 direction, b8 normalize_dir, f32 amount);

/**
 * @brief Moves the camera forward by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_forward(kcamera camera, f32 amount);

/**
 * @brief Moves the camera backward by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_backward(kcamera camera, f32 amount);

/**
 * @brief Moves the camera left by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_left(kcamera camera, f32 amount);

/**
 * @brief Moves the camera right by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_right(kcamera camera, f32 amount);

/**
 * @brief Moves the camera up (straight along the y-axis, not the camera's up vector) by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_up(kcamera camera, f32 amount);

/**
 * @brief Moves the camera down (straight along the y-axis, not the camera's up vector) by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to move.
 */
KAPI void kcamera_move_down(kcamera camera, f32 amount);

/**
 * @brief Adjusts the camera's yaw by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to adjust by.
 */
KAPI void kcamera_yaw(kcamera camera, f32 amount);

/**
 * @brief Adjusts the camera's pitch by the given amount.
 *
 * @param camera A handle to a camera.
 * @param amount The amount to adjust by.
 */
KAPI void kcamera_pitch(kcamera camera, f32 amount);
