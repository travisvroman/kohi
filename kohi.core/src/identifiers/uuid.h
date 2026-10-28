/**
 * @file uuid.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A basic implementation of a UUID.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief A universally unique identifier (UUID).
 */
typedef struct uuid {
	char value[37];
} uuid;

/**
 * @brief Seeds the uuid generator with the given value.
 *
 * @param seed The seed value.
 */
KAPI void uuid_seed (u64 seed);

/**
 * @brief Generates a universally unique identifier (UUID).
 *
 * @return a newly-generated UUID.
 */
KAPI uuid uuid_generate (void);
