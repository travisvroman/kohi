#pragma once

#include "math/math_types.h"

/** @brief The maximum length of a geometry name. */
#define GEOMETRY_NAME_MAX_LENGTH 256

/**
 * @brief Represents the configuration for a geometry.
 */
typedef struct geometry_config {
    /** @brief The size of each vertex. */
    u32 vertex_size;
    /** @brief The number of vertices. */
    u32 vertex_count;
    /** @brief An array of vertices. */
    void* vertices;
    /** @brief The size of each index. */
    u32 index_size;
    /** @brief The number of indices. */
    u32 index_count;
    /** @brief An array of indices. */
    void* indices;

    vec3 center;
    vec3 min_extents;
    vec3 max_extents;

    /** @brief The name of the geometry. */
    char name[GEOMETRY_NAME_MAX_LENGTH]; // FIXME: Should probably just dynamically allocate this.
    /** @brief The name of the material used by the geometry. */
    char material_name[256]; // FIXME: Should probably just dynamically allocate this.
} geometry_config;

/**
 * @brief Represents actual geometry in the world.
 * Typically (but not always, depending on use) paired with a material.
 */
typedef struct geometry {
    /** @brief The geometry identifier. */
    u32 id;
    /** @brief The geometry generation. Incremented every time the geometry
     * changes. */
    u16 generation;
    /** @brief The center of the geometry in local coordinates. */
    vec3 center;
    /** @brief The extents of the geometry in local coordinates. */
    extents_3d extents;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The size of each vertex. */
    u32 vertex_element_size;
    /** @brief The vertex data. */
    void* vertices;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. */
    u32 index_element_size;
    /** @brief The index data. */
    void* indices;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;

    /** @brief The geometry name. */
    char name[GEOMETRY_NAME_MAX_LENGTH];
    /** @brief A pointer to the material associated with this geometry.. */
    struct material* material;
} geometry;

#pragma once

#include "math/math_types.h"
#include "math/geometry.h"

struct frame_data;

/**
 * @brief Calculates normals for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
KAPI void geometry_generate_normals(u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices);

/**
 * @brief Calculates tangents for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
KAPI void geometry_generate_tangents(u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices);

/**
 * @brief De-duplicates vertices, leaving only unique ones. Leaves the original
 * vertices array intact. Allocates a new array in out_vertices. Modifies
 * indices in-place. Original vertex array should be freed by caller.
 *
 * @param vertex_count The number of vertices in the array.
 * @param vertices The original array of vertices to be de-duplicated. Not
 * modified.
 * @param index_count The number of indices in the array.
 * @param indices The array of indices. Modified in-place as vertices are
 * removed.
 * @param out_vertex_count A pointer to hold the final vertex count.
 * @param out_vertices A pointer to hold the array of de-duplicated vertices.
 */
KAPI void geometry_deduplicate_vertices(u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices, u32 *out_vertex_count, vertex_3d **out_vertices);

KAPI void generate_uvs_from_image_coords(u32 img_width, u32 img_height, u32 px_x, u32 px_y, f32 *out_tx, f32 *out_ty);
KAPI void generate_quad_2d(const char *name, f32 width, f32 height, f32 tx_min, f32 tx_max, f32 ty_min, f32 ty_max, geometry_config *out_config);
