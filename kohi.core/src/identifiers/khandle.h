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

/**
 * khandle16 - a 16-bit implementation of the khandle that uses one u16 for the
 * index and a second for the uniqueid. This results in a much smaller handle, although
 * coming with a limitation of a maximum of 65534 values (65535 is INVALID_ID_U16) as a
 * maximum array size for anything this references.
 */
typedef struct khandle16 {
    /** @brief Index into a resource table. Considered invalid if == INVALID_ID_U16 */
    u16 handle_index;
    /**
     * @brief A generation used to indicate if a handle is stale. Typically incremented
     * when a resource is updated. Considered invalid if == INVALID_ID_U16.
     */
    u16 generation;
} khandle16;

/**
 * @brief Creates and returns a handle with the given handle index. Also creates a new unique identifier.
 */
KAPI khandle16 khandle16_create(u16 handle_index);

/** @brief Creates and returns a handle based on the handle index provided, using the given u64 to create an identifier. */
KAPI khandle16 khandle16_create_with_u16_generation(u16 handle_index, u16 generation);

/** @brief Creates and returns an invalid handle. */
KAPI khandle16 khandle16_invalid(void);

/** @brief Indicates if the provided handle is valid. */
KAPI b8 khandle16_is_valid(khandle16 handle);

/** @brief Indicates if the provided handle is invalid. */
KAPI b8 khandle16_is_invalid(khandle16 handle);

/** @brief Updates the provided handle, incrementing the generation. */
KAPI void khandle16_update(khandle16* handle);

/** @brief Invalidates the provided handle. */
KAPI void khandle16_invalidate(khandle16* handle);

/** @brief Indicates if the handle is stale/outdated). */
KAPI b8 khandle16_is_stale(khandle16 handle, u16 uniqueid);

/** @brief Indicates if the handle is pristine (i.e. not stale/outdated). */
KAPI b8 khandle16_is_pristine(khandle16 handle, u16 uniqueid);

#endif
