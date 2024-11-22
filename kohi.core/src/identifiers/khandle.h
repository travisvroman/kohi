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

#ifndef _khandle_H_
#define _khandle_H_

#include "defines.h"
#include "identifiers/identifier.h"

#define INVALID_khandle INVALID_ID_U64

/**
 * @brief A handle is a unique identifier used a system in the engine to
 * avoid using raw pointers where possible.
 */
typedef struct khandle {
    /** @brief Index into a resource table. Considered null if == INVALID_ID */
    u32 handle_index;
    /** @brief A globally unique identifier */
    identifier unique_id;
} khandle;

/**
 * @brief Creates and returns a handle with the given handle index. Also creates a new unique identifier.
 */
KAPI khandle khandle_create(u32 handle_index);

/** @brief Creates and returns a handle based on the handle index and identifier provided. */
KAPI khandle khandle_create_with_identifier(u32 handle_index, identifier id);

/** @brief Creates and returns a handle based on the handle index provided, using the given u64 to create an identifier. */
KAPI khandle khandle_create_with_u64_identifier(u32 handle_index, u64 uniqueid);

/** @brief Creates and returns an invalid handle. */
KAPI khandle khandle_invalid(void);

/** @brief Indicates if the provided handle is valid. */
KAPI b8 khandle_is_valid(khandle handle);

/** @brief Indicates if the provided handle is invalid. */
KAPI b8 khandle_is_invalid(khandle handle);

/** @brief Invalidates the provided handle. */
KAPI void khandle_invalidate(khandle* handle);

/** @brief Indicates if the handle is stale/outdated). */
KAPI b8 khandle_is_stale(khandle handle, u64 uniqueid);

/** @brief Indicates if the handle is pristine (i.e. not stale/outdated). */
KAPI b8 khandle_is_pristine(khandle handle, u64 uniqueid);

#endif
