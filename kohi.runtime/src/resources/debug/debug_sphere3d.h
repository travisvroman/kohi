#pragma once

#include "defines.h"
#include "identifiers/identifier.h"
#include "identifiers/khandle.h"
#include "math/geometry.h"
#include "math/math_types.h"

typedef struct debug_sphere3d {
    identifier id;
    kname name;
    f32 radius;
    vec4 colour;
    khandle xform;
    khandle parent_xform;

    b8 is_dirty;

    kgeometry geometry;
} debug_sphere3d;

struct frame_data;

KAPI b8 debug_sphere3d_create(f32 radius, vec4 colour, khandle parent_xform, debug_sphere3d* out_sphere);
KAPI void debug_sphere3d_destroy(debug_sphere3d* sphere);

KAPI void debug_sphere3d_parent_set(debug_sphere3d* sphere, khandle parent_xform);
KAPI void debug_sphere3d_colour_set(debug_sphere3d* sphere, vec4 colour);

KAPI void debug_sphere3d_render_frame_prepare(debug_sphere3d* sphere, const struct frame_data* p_frame_data);

KAPI b8 debug_sphere3d_initialize(debug_sphere3d* sphere);
KAPI b8 debug_sphere3d_load(debug_sphere3d* sphere);
KAPI b8 debug_sphere3d_unload(debug_sphere3d* sphere);

KAPI b8 debug_sphere3d_update(debug_sphere3d* sphere);
