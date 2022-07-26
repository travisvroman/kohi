/**
 * @file camera_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The camera system is responsible for managing cameras throughout the engine.
 * @version 1.0
 * @date 2022-05-21
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "renderer/camera.h"

/** @brief The camera system configuration. */
typedef struct camera_system_config {
    /**
     * @brief NOTE: The maximum number of cameras that can be managed by
     * the system.
     */
    u16 max_camera_count;

} camera_system_config;

/** @brief The name of the default camera. */
#define DEFAULT_CAMERA_NAME "default"

/**
 * @brief Initializes the camera system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration for this system.
 * @return True on success; otherwise false.
 */
b8 camera_system_initialize(u64* memory_requirement, void* state, camera_system_config config);

/**
 * @brief Shuts down the geometry camera.
 *
 * @param state The state block of memory.
 */
void camera_system_shutdown(void* state);

/**
 * @brief Acquires a pointer to a camera by name.
 * If one is not found, a new one is created and retuned.
 * Internal reference counter is incremented.
 * 
 * @param name The name of the camera to acquire.
 * @return A pointer to a camera if successful; 0 if an error occurs.
 */
KAPI camera* camera_system_acquire(const char* name);

/**
 * @brief Releases a camera with the given name. Intenral reference
 * counter is decremented. If this reaches 0, the camera is reset,
 * and the reference is usable by a new camera.
 * 
 * @param name The name of the camera to release.
 */
KAPI void camera_system_release(const char* name);

/**
 * @brief Gets a pointer to the default camera.
 * 
 * @return A pointer to the default camera.
 */
KAPI camera* camera_system_get_default();
