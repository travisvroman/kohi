
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

#include "defines.h"

struct frame_data;

/** @brief The standard UI system configuration. */
typedef struct standard_ui_system_config {
    u16 dummy;
} standard_ui_system_config;

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
b8 standard_ui_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the standard UI system.
 *
 * @param state The state block of memory.
 */
void standard_ui_system_shutdown(void* state);

b8 standard_ui_system_update(void* state, const struct frame_data* p_frame_data);
