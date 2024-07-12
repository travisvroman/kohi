/**
 * @file identifier.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains a system for creating numeric identifiers.
 * @version 2.0
 * @date 2023-10-22
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include "defines.h"

/**
 * A Globally/Universally Unique identifier in 64-bit unsigned integer format.
 * To be used primarily as an identifier for resources. (De)serialization friendly.
 */
typedef struct identifier {
    // The actual internal identifier.
    u64 uniqueid;
} identifier;

/**
 * @brief Generates a new unique identifier.
 */
KAPI identifier identifier_create(void);

/**
 * @brief Creates an identifier from a known value. Useful for deserialization.
 */
KAPI identifier identifier_from_u64(u64 uniqueid);

/**
 * @brief Indicates if the provided identifiers are equal.
 */
KAPI b8 identifiers_equal(identifier a, identifier b);
