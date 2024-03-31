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
void uuid_seed(u64 seed);

/**
 * @brief Generates a universally unique identifier (UUID).
 *
 * @return a newly-generated UUID.
 */
KAPI uuid uuid_generate(void);
