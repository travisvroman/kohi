#pragma once

#include "defines.h"
#include "math/math_types.h"

struct texture_map;

typedef struct water_plane {
    mat4 model;
    vec4 vertices[4];
    u32 indices[6];
    u64 index_buffer_offset;
    u64 vertex_buffer_offset;
    u32 instance_id;
    u32 map_count;
    struct texture_map* maps;
} water_plane;

KAPI b8 water_plane_create(water_plane* out_plane);
KAPI void water_plane_destroy(water_plane* plane);

KAPI b8 water_plane_initialize(water_plane* plane);
KAPI b8 water_plane_load(water_plane* plane);
KAPI b8 water_plane_unload(water_plane* plane);

KAPI b8 water_plane_update(water_plane* plane);