#pragma once

#include "core/identifier.h"
#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

typedef struct debug_box3d {
    identifier id;
    char *name;
    vec3 size;
    vec4 colour;
    transform xform;

    u32 vertex_count;
    colour_vertex_3d *vertices;

    b8 is_dirty;

    geometry geo;
} debug_box3d;

struct frame_data;

KAPI b8 debug_box3d_create(vec3 size, transform *parent, debug_box3d *out_box);
KAPI void debug_box3d_destroy(debug_box3d *box);

KAPI void debug_box3d_parent_set(debug_box3d *box, transform *parent);
KAPI void debug_box3d_colour_set(debug_box3d *box, vec4 colour);
KAPI void debug_box3d_extents_set(debug_box3d *box, extents_3d extents);
KAPI void debug_box3d_points_set(debug_box3d *box, vec4* points);

KAPI void debug_box3d_render_frame_prepare(debug_box3d* box, const struct frame_data* p_frame_data);

KAPI b8 debug_box3d_initialize(debug_box3d *box);
KAPI b8 debug_box3d_load(debug_box3d *box);
KAPI b8 debug_box3d_unload(debug_box3d *box);

KAPI b8 debug_box3d_update(debug_box3d *box);
