#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "systems/material_system.h"

struct texture_map;

typedef struct water_plane_vertex {
    vec4 position;
} water_plane_vertex;

typedef struct water_plane {
    mat4 model;
    water_plane_vertex vertices[4];
    u32 indices[6];
    u64 index_buffer_offset;
    u64 vertex_buffer_offset;

    // Instance of water material.
    material_instance material;

} water_plane;

KAPI b8 water_plane_create(water_plane* out_plane);
KAPI void water_plane_destroy(water_plane* plane);

KAPI b8 water_plane_initialize(water_plane* plane);
KAPI b8 water_plane_load(water_plane* plane);
KAPI b8 water_plane_unload(water_plane* plane);

KAPI b8 water_plane_update(water_plane* plane);
