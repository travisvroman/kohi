#pragma once

#include "math_types.h"

/**
 * @brief Calculates normals for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
void geometry_generate_normals(u32 vertex_count, vertex_3d *vertices,
                               u32 index_count, u32 *indices);

/**
 * @brief Calculates tangents for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
void geometry_generate_tangents(u32 vertex_count, vertex_3d *vertices,
                                u32 index_count, u32 *indices);

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
void geometry_deduplicate_vertices(u32 vertex_count, vertex_3d *vertices,
                                   u32 index_count, u32 *indices,
                                   u32 *out_vertex_count,
                                   vertex_3d **out_vertices);

struct terrain_vertex;

void terrain_geometry_generate_normals(u32 vertex_count,
                                       struct terrain_vertex *vertices,
                                       u32 index_count, u32 *indices);

void terrain_geometry_generate_tangents(u32 vertex_count,
                                        struct terrain_vertex *vertices,
                                        u32 index_count, u32 *indices);
