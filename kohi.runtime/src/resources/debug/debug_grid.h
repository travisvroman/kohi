#include "defines.h"
#include "identifiers/identifier.h"
#include "math/geometry.h"
#include "math/math_types.h"

typedef struct debug_grid_config {
    kname name;
    grid_orientation orientation;
    /** @brief The space count in the first dimension of the orientation from both directions outward from origin. */
    u32 segment_count_dim_0;
    /** @brief The space count in the second dimension of the orientation from both directions outward from origin. */
    u32 segment_count_dim_1;
    /** @brief How large each tile is on the both axes, relative to one unit (default = 1.0). */
    f32 segment_size;

    /** @brief Indicates if a third axis is to be rendered. */
    b8 use_third_axis;
} debug_grid_config;

typedef struct debug_grid {
    identifier id;
    kname name;
    grid_orientation orientation;
    /** @brief The space count in the first dimension of the orientation from both directions outward from origin. */
    u32 segment_count_dim_0;
    /** @brief The space count in the second dimension of the orientation from both directions outward from origin. */
    u32 segment_count_dim_1;
    /** @brief How large each tile is on the both axes, relative to one unit (default = 1.0). */
    f32 segment_size;

    /** @brief Indicates if a third axis is to be rendered. */
    b8 use_third_axis;

    vec3 origin;

    kgeometry geometry;
} debug_grid;

KAPI b8 debug_grid_create(const debug_grid_config* config, debug_grid* out_grid);
KAPI void debug_grid_destroy(debug_grid* grid);

KAPI b8 debug_grid_initialize(debug_grid* grid);
KAPI b8 debug_grid_load(debug_grid* grid);
KAPI b8 debug_grid_unload(debug_grid* grid);

KAPI b8 debug_grid_update(debug_grid* grid);
