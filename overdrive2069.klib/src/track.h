#pragma once

#include "math/geometry.h"
#include "systems/material_system.h"
#include <math/math_types.h>

typedef struct track_point {
    vec3 position;
    // How wide the left side of the track is from the position;
    f32 left_width;
    // The height difference from the center on the left side.
    f32 left_height;
    // How wide the left side of the track is from the position;
    f32 right_width;
    // The height difference from the center on the left side.
    f32 right_height;

    // Segments may only be rotated on y.
    f32 rotation_y;

} track_point;

typedef struct track {

    // darray of points (darray so it's editable)
    track_point* points;

    // Geometry used to visualize the track.
    kgeometry geometry;

    // How many times each segment gets tessellated.
    u32 segment_resolution;

    material_instance material;
} track;

b8 track_create(track* out_track);

b8 track_initialize(track* t);
b8 track_load(track* t);
void track_unload(track* t);
void track_destroy(track* t);

vec3 track_constrain_object(vec3 object_position, track* t);
