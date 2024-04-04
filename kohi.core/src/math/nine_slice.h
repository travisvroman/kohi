#pragma once

#include "math/geometry.h"

typedef struct nine_slice {
    struct geometry *g;
    // Actual corner w/h
    vec2i corner_size;
    // Sampled corner w/h
    vec2i corner_px_size;

    // Overall w/h of 9-slice.
    vec2i size;

    vec2i atlas_px_min;
    vec2i atlas_px_max;

    vec2i atlas_px_size;

    b8 is_dirty;
} nine_slice;

/**
 * Updates nine slice vertex data for the given nine slice. Optionally reuploads to GPU.
 * @param nslice A pointer to the nine-slice to be updated.
 * @param vertices An external array of vertices to populate. If 0/null, uses vertex array in nslice.
 * @returns True on success; otherwise false.
 */
KAPI b8 update_nine_slice(nine_slice *nslice, vertex_2d *vertices);
KAPI void nine_slice_render_frame_prepare(nine_slice *nslice, const struct frame_data *p_frame_data);
KAPI b8 generate_nine_slice(const char *name, vec2i size, vec2i atlas_px_size, vec2i atlas_px_min, vec2i atlas_px_max, vec2i corner_px_size, vec2i corner_size, nine_slice *out_nine_slice);
