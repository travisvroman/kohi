#pragma once

#include "math/geometry.h"
#include "systems/material_system.h"
#include <math/math_types.h>

typedef struct triangle_with_adjacency {
    triangle tri;
    u32 index;
    u32 adjacent_triangles[3];
} triangle_with_adjacency;

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

    // Leftmost point, generated from geometry. Takes height into account.
    vec3 left;
    // Rightmost point, generated from geometry. Takes height into account.
    vec3 right;
} track_point;

typedef struct track_segment {
    u32 index;

    track_point* start;
    track_point* end;

    // Geometry used to visualize the segment.
    kgeometry geometry;

    u32 triangle_count;
    triangle_with_adjacency* triangles;
} track_segment;

typedef struct track {

    // darray of points (darray so it's editable)
    track_point* points;

    // Track segments
    track_segment* segments;

    // How many times each segment gets tessellated.
    u32 segment_resolution;

    material_instance material;
} track;

b8 track_create(track* out_track);

b8 track_initialize(track* t);
b8 track_load(track* t);
void track_unload(track* t);
void track_destroy(track* t);

vec3 constrain_to_track(vec3 vehicle_point, vec3 velocity, track* t, vec3* out_surface_normal);