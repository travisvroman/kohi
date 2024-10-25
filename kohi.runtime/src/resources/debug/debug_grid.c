#include "debug_grid.h"

#include "defines.h"
#include "identifiers/identifier.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"

b8 debug_grid_create(const debug_grid_config* config, debug_grid* out_grid) {
    if (!config || !out_grid) {
        return false;
    }

    kzero_memory(out_grid, sizeof(debug_grid));
    out_grid->name = config->name;
    out_grid->segment_count_dim_0 = config->segment_count_dim_0;
    out_grid->segment_count_dim_1 = config->segment_count_dim_1;
    out_grid->segment_size = config->segment_size;
    out_grid->orientation = config->orientation;
    out_grid->use_third_axis = config->use_third_axis;

    // FIXME: do we need this?
    out_grid->origin = vec3_zero();
    out_grid->id = identifier_create();

    return true;
}

void debug_grid_destroy(debug_grid* grid) {
    // TODO: zero out, etc.
    grid->id.uniqueid = INVALID_ID_U64;
}

b8 debug_grid_initialize(debug_grid* grid) {
    if (!grid) {
        return false;
    }

    grid->geometry = geometry_generate_grid(
        grid->orientation,
        grid->segment_count_dim_0,
        grid->segment_count_dim_1,
        grid->segment_size,
        grid->use_third_axis,
        grid->name);

    return true;
}

b8 debug_grid_load(debug_grid* grid) {
    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_geometry_upload(&grid->geometry)) {
        return false;
    }
    return true;
}

b8 debug_grid_unload(debug_grid* grid) {
    renderer_geometry_destroy(&grid->geometry);

    grid->id.uniqueid = INVALID_ID_U64;

    return true;
}

b8 debug_grid_update(debug_grid* grid) {
    return true;
}
