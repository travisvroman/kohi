
/**
 * @file standard_ui_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The standard UI system is responsible for managing standard ui elements throughout the engine.
 * @version 1.0
 * @date 2023-09-21
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include <math/math_types.h>

#include "defines.h"
struct frame_data;

/** @brief The standard UI system configuration. */
typedef struct standard_ui_system_config {
    u64 max_control_count;
} standard_ui_system_config;

typedef struct sui_control {
    transform xform;
    char* name;
    b8 is_active;
    b8 is_visible;
    // darray
    struct sui_control** children;

    void* internal_data;

    void (*destroy)(struct sui_control* self);
    b8 (*load)(struct sui_control* self);
    void (*unload)(struct sui_control* self);

    b8 (*update)(struct sui_control* self, struct frame_data* p_frame_data);
    b8 (*render)(struct sui_control* self, struct frame_data* p_frame_data);
} sui_control;

/**
 * @brief Initializes the standard UI system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (standard_ui_system_config) for this system.
 * @return True on success; otherwise false.
 */
KAPI b8 standard_ui_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the standard UI system.
 *
 * @param state The state block of memory.
 */
KAPI void standard_ui_system_shutdown(void* state);

KAPI b8 standard_ui_system_update(void* state, struct frame_data* p_frame_data);

KAPI b8 standard_ui_system_render(void* state, sui_control* root, struct frame_data* p_frame_data);

KAPI b8 standard_ui_system_update_active(void* state, sui_control* control);

KAPI b8 standard_ui_system_register_control(void* state, sui_control* control);

// ---------------------------
// Base control
// ---------------------------
KAPI b8 sui_base_control_create(const char* name, struct sui_control* out_control);
KAPI void sui_base_control_destroy(struct sui_control* self);

KAPI b8 sui_base_control_load(struct sui_control* self);
KAPI void sui_base_control_unload(struct sui_control* self);

KAPI b8 sui_base_control_update(struct sui_control* self, struct frame_data* p_frame_data);
KAPI b8 sui_base_control_render(struct sui_control* self, struct frame_data* p_frame_data);
