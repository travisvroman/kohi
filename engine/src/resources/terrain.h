#pragma once

#include "core/frame_data.h"
#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

/*
Need to modify the geometry structure/functions to allow for multiple materials.
Write shader to handle 8 material weights/blending
Multi-materials (combine maps using uv offsets)?
Load terrain configuration from file
Load heightmaps
calculate normals/tangents (copy-pasta of existing, but using terrain_vertex
structure) New material type? (terrain/multi material)
*/

typedef struct terrain_vertex {
    /** @brief The position of the vertex */
    vec3 position;
    /** @brief The normal of the vertex. */
    vec3 normal;
    /** @brief The texture coordinate of the vertex. */
    vec2 texcoord;
    /** @brief The colour of the vertex. */
    vec4 colour;
    /** @brief The tangent of the vertex. */
    vec4 tangent;

    /** @brief A collection of material weights for this vertex. */
    f32 material_weights[TERRAIN_MAX_MATERIAL_COUNT];
} terrain_vertex;

typedef struct terrain_vertex_data {
    f32 height;
} terrain_vertex_data;

typedef struct terrain_config {
    char *name;
    u32 tile_count_x;
    u32 tile_count_z;
    // How large each tile is on the x axis.
    f32 tile_scale_x;
    // How large each tile is on the z axis.
    f32 tile_scale_z;
    // The max height of the generated terrain.
    f32 scale_y;

    transform xform;

    u32 vertex_data_length;
    terrain_vertex_data *vertex_datas;

    u32 material_count;
    char **material_names;
} terrain_config;

typedef struct terrain {
    char *name;
    transform xform;
    u32 tile_count_x;
    u32 tile_count_z;
    // How large each tile is on the x axis.
    f32 tile_scale_x;
    // How large each tile is on the z axis.
    f32 tile_scale_z;
    // The max height of the generated terrain.
    f32 scale_y;

    u32 vertex_data_length;
    terrain_vertex_data *vertex_datas;

    extents_3d extents;
    vec3 origin;

    u32 vertex_count;
    terrain_vertex *vertices;

    u32 index_count;
    u32 *indices;

    geometry geo;

    u32 material_count;
    char **material_names;
} terrain;

KAPI b8 terrain_create(const terrain_config *config, terrain *out_terrain);
KAPI void terrain_destroy(terrain *t);

KAPI b8 terrain_initialize(terrain *t);
KAPI b8 terrain_load(terrain *t);
KAPI b8 terrain_unload(terrain *t);

KAPI b8 terrain_update(terrain *t);
