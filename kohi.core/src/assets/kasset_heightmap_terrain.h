#pragma once

#include "defines.h"
#include "kasset_types.h"
#include "math/math_types.h"

struct kasset;

/**
 * Represents an asset used to create a heightmap terrain.
 */
typedef struct kasset_heightmap_terrain {
    /** @brief The base asset data. */
    kasset base;
} kasset_heightmap_terrain;
