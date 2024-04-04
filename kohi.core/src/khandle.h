/**
 * @file khandle.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A global handle system for Kohi. Handles are used to obtain various resources
 * using a unique handle id.
 * @version 1.0
 * @date 2024-02-08
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#ifndef _K_HANDLE_H_
#define _K_HANDLE_H_

#include "identifier.h"
#include "defines.h"

#define INVALID_K_HANDLE INVALID_ID_U64

/**
 * @brief A handle is a unique identifier used a system in the engine to
 * avoid using raw pointers where possible.
 */
typedef struct k_handle {
    /** @brief Index into a resource table. Considered null if == INVALID_ID */
    u32 handle_index;
    /** @brief A globally unique identifier */
    identifier unique_id;
} k_handle;

/**
 * @brief Creates and returns a handle with the given handle index. Also creates a new unique identifier.
 */
KAPI k_handle k_handle_create(u32 handle_index);

/** @brief Creates and returns a handle based on the handle index and identifier provided. */
KAPI k_handle k_handle_create_with_identifier(u32 handle_index, identifier id);

/** @brief Creates and returns an invalid handle. */
KAPI k_handle k_handle_invalid(void);

/** @brief Indicates if the provided handle is invalid. */
KAPI b8 k_handle_is_invalid(k_handle handle);

/** @brief Invalidates the provided handle. */
KAPI void k_handle_invalidate(k_handle* handle);

#endif
