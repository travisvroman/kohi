#pragma once

#include "defines.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "systems/ktransform_system.h"

typedef struct debug_line3d {
    char* name;
    vec3 point_0;
    vec3 point_1;
    vec4 colour;
    ktransform ktransform;
    ktransform ktransform_parent;
    b8 is_dirty;

    kgeometry geometry;
} debug_line3d;

struct frame_data;

KAPI b8 debug_line3d_create(vec3 point_0, vec3 point_1, ktransform parent_ktransform, debug_line3d* out_line);
KAPI void debug_line3d_destroy(debug_line3d* line);

KAPI void debug_line3d_parent_set(debug_line3d* line, ktransform parent_ktransform);
KAPI void debug_line3d_colour_set(debug_line3d* line, vec4 colour);
KAPI void debug_line3d_points_set(debug_line3d* line, vec3 point_0, vec3 point_1);

KAPI void debug_line3d_render_frame_prepare(debug_line3d* line, const struct frame_data* p_frame_data);

KAPI b8 debug_line3d_initialize(debug_line3d* line);
KAPI b8 debug_line3d_load(debug_line3d* line);
KAPI b8 debug_line3d_unload(debug_line3d* line);

KAPI b8 debug_line3d_update(debug_line3d* line);
