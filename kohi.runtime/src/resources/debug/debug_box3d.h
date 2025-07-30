#pragma once

#include "defines.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "systems/ktransform_system.h"

typedef struct debug_box3d {
    kname name;
    vec3 size;
    vec4 colour;
    ktransform ktransform;
    ktransform parent_ktransform;

    b8 is_dirty;

    kgeometry geometry;
} debug_box3d;

struct frame_data;

KAPI b8 debug_box3d_create(vec3 size, ktransform parent_ktransform, debug_box3d* out_box);
KAPI void debug_box3d_destroy(debug_box3d* box);

KAPI void debug_box3d_parent_set(debug_box3d* box, ktransform parent_ktransform);
KAPI void debug_box3d_colour_set(debug_box3d* box, vec4 colour);
KAPI void debug_box3d_extents_set(debug_box3d* box, extents_3d extents);
KAPI void debug_box3d_points_set(debug_box3d* box, vec3 points[8]);

KAPI void debug_box3d_render_frame_prepare(debug_box3d* box, const struct frame_data* p_frame_data);

KAPI b8 debug_box3d_initialize(debug_box3d* box);
KAPI b8 debug_box3d_load(debug_box3d* box);
KAPI b8 debug_box3d_unload(debug_box3d* box);

KAPI b8 debug_box3d_update(debug_box3d* box);
