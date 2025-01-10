#pragma once

#include "defines.h"
#include "math/math_types.h"

#define HEIGHTMAP_TERRAIN_MAX_MATERIAL_COUNT

/** @brief Represents a single vertex of a heightmap terrain. */
typedef struct kheightmap_terrain_vertex {
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
    f32 material_weights[HEIGHTMAP_TERRAIN_MAX_MATERIAL_COUNT];
} kheightmap_terrain_vertex;

/**
 * @brief Represents a Level Of Detail for a single heightmap terrain chunk.
 *
 * Level of details in heightmap terrains are achieved by skipping vertices on
 * an increasing basis per level of detail. For example, LOD level 0 renders all
 * vertices (and thus contains indices for all vertices), while level 1 renders
 * every other vertex (thus containing indices for every other vertex), level 2
 * renders every 4th vertex, level 3 every 8th, and so on.
 */
typedef struct heightmap_terrain_chunk_lod {
    /** @brief The index count for the chunk surface. */
    u32 surface_index_count;
    /** @brief The total index count, including those for side skirts. */
    u32 total_index_count;
    /** @brief The index data. */
    u32* indices;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;
} heightmap_terrain_chunk_lod;

typedef struct heightmap_terrain_chunk {
    /** @brief The chunk generation. Incremented every time the geometry changes. */
    u16 generation;
    u32 surface_vertex_count;
    u32 total_vertex_count;

    // The vertex data.
    kheightmap_terrain_vertex* vertices;
    // The offset in bytes into the vertex buffer.
    u64 vertex_buffer_offset;

    heightmap_terrain_chunk_lod* lods;

    /** @brief The center of the geometry in local coordinates. */
    vec3 center;
    /** @brief The extents of the geometry in local coordinates. */
    extents_3d extents;

    /** @brief A pointer to the material associated with this geometry. */
    // NOTE: While it's possible to have this live at the terrain level, it's
    // more flexible to have it here, as it then theoretically makes the limit
    // of materials to HEIGHTMAP_TERRAIN_MAX_MATERIAL_COUNT per _chunk_ instead of
    // for the _entire heightmap terrain_. While the implementation may not currently
    // support this, keeping this here makes this easier to work toward in the future.
    struct material* material;
    /** @brief The current level of detail for this chunk. */
    u8 current_lod;
} heightmap_terrain_chunk;
