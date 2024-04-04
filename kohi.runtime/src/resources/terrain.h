#pragma once

#include "frame_data.h"
#include "identifier.h"
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
    char *resource_name;
} terrain_config;

typedef struct terrain_resource {
    char *name;
    u32 chunk_size;
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

    u32 material_count;
    char **material_names;
} terrain_resource;

typedef struct terrain_chunk_lod {
    /** @brief The index count for the chunk surface. */
    u32 surface_index_count;

    /** @brief The total index count, including those for side skirts. */
    u32 total_index_count;
    /** @brief The index data. */
    u32 *indices;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;
} terrain_chunk_lod;

typedef struct terrain_chunk {
    /** @brief The chunk generation. Incremented every time the geometry changes. */
    u16 generation;
    u32 surface_vertex_count;
    u32 total_vertex_count;
    terrain_vertex *vertices;
    u64 vertex_buffer_offset;

    terrain_chunk_lod *lods;

    /** @brief The center of the geometry in local coordinates. */
    vec3 center;
    /** @brief The extents of the geometry in local coordinates. */
    extents_3d extents;

    /** @brief A pointer to the material associated with this geometry.. */
    struct material *material;

    u8 current_lod;
} terrain_chunk;

typedef enum terrain_state {
    TERRAIN_STATE_UNDEFINED,
    TERRAIN_STATE_CREATED,
    TERRAIN_STATE_INITIALIZED,
    TERRAIN_STATE_LOADING,
    TERRAIN_STATE_LOADED
} terrain_state;

typedef struct terrain {
    identifier id;
    u32 generation;
    terrain_state state;
    char *name;
    char *resource_name;
    u32 tile_count_x;
    u32 tile_count_z;
    // How large each tile is on the x axis.
    f32 tile_scale_x;
    // How large each tile is on the z axis.
    f32 tile_scale_z;
    // The max height of the generated terrain.
    f32 scale_y;

    u32 chunk_size;

    u32 vertex_data_length;
    terrain_vertex_data *vertex_datas;

    extents_3d extents;
    vec3 origin;

    u32 chunk_count;
    // row by row, then column
    // 0, 1, 2, 3
    // 4, 5, 6, 7
    // 8, 9, ...
    terrain_chunk *chunks;

    u8 lod_count;

    u32 material_count;
    char **material_names;
} terrain;

KAPI b8 terrain_create(const terrain_config *config, terrain *out_terrain);
KAPI void terrain_destroy(terrain *t);

KAPI b8 terrain_initialize(terrain *t);
KAPI b8 terrain_load(terrain *t);
KAPI b8 terrain_chunk_load(terrain *t, terrain_chunk *chunk);
KAPI b8 terrain_unload(terrain *t);
KAPI b8 terrain_chunk_unload(terrain *t, terrain_chunk *chunk);

KAPI b8 terrain_update(terrain *t);

KAPI void terrain_geometry_generate_normals(u32 vertex_count, struct terrain_vertex *vertices, u32 index_count, u32 *indices);

KAPI void terrain_geometry_generate_tangents(u32 vertex_count, struct terrain_vertex *vertices, u32 index_count, u32 *indices);