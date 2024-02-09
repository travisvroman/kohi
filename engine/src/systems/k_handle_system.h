/**
 * @file k_handle_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the k_handle system, which handles the acquisition
 * and releasing of handles within the engine. Handles are unique identifiers
 * that are used in lieu of pointers to avoid stale pointers. These handles
 * ultimately contain an index into an array of a registered resource type,
 * which can then be looked up in that corresponding system.
 * @version 1.0
 * @date 2024-02-08
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "core/khandle.h"
#include "defines.h"

/** @brief The k_handle system configuration */
typedef struct k_handle_system_config {
    /** @brief NOTE: not used */
    u32 dummy;
} k_handle_system_config;

/**
 * @brief Initializes the handle system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (k_handle_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 k_handle_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the handle system.
 *
 * @param state The state block of memory for this system.
 */
void k_handle_system_shutdown(void* state);

/**
 * @brief Registers a new resource type handler to use with the handle system.
 * @param handler The resource handler to register.
 *
 * @returns True on success; otherwise false.
 */
KAPI b8 k_handle_system_register_resource_type_handler(k_handle_system_resource_type_handler handler);

/**
 * @brief Acquires a new handle.
 * @param resource_type The resource type to acquire a handle for. Must match a registered resource system.
 *
 * @return A new handle.
 */
KAPI k_handle k_handle_system_acquire(u16 resource_type);

/**
 * @brief Releases the given handle.
 *
 * @param handle The handle to be released.
 */
KAPI void k_handle_system_release(k_handle handle);
