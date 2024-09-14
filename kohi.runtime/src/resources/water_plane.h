#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

struct texture_map;

typedef struct water_plane_vertex {
    vec4 position;
} water_plane_vertex;

typedef enum water_plane_maps {
    WATER_PLANE_MAP_REFLECTION = 0,
    WATER_PLANE_MAP_REFRACTION = 1,
    WATER_PLANE_MAP_DUDV = 2,
    WATER_PLANE_MAP_NORMAL = 3,
    WATER_PLANE_MAP_SHADOW = 4,
    WATER_PLANE_MAP_IBL_CUBE = 5,
    WATER_PLANE_MAP_REFRACT_DEPTH = 6,
    WATER_PLANE_MAP_COUNT
} water_plane_maps;

typedef struct water_plane {
    mat4 model;
    water_plane_vertex vertices[4];
    u32 indices[6];
    u64 index_buffer_offset;
    u64 vertex_buffer_offset;
    u32 instance_id;

    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;

    // Texture maps for reflect/refract normals.
    u32 map_count;
    struct kresource_texture_map* maps;

    // Refraction target textures.
    kresource_texture* refraction_colour;
    kresource_texture* refraction_depth;
    // Reflection target textures.
    kresource_texture* reflection_colour;
    kresource_texture* reflection_depth;

    // Pointer to dudv texture.
    kresource_texture* dudv_texture;

    // Pointer to normal texture.
    kresource_texture* normal_texture;
} water_plane;

KAPI b8 water_plane_create(water_plane* out_plane);
KAPI void water_plane_destroy(water_plane* plane);

KAPI b8 water_plane_initialize(water_plane* plane);
KAPI b8 water_plane_load(water_plane* plane);
KAPI b8 water_plane_unload(water_plane* plane);

KAPI b8 water_plane_update(water_plane* plane);
