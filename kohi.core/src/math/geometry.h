#pragma once

#include "math/math_types.h"
#include "strings/kname.h"

/** @brief The maximum length of a geometry name. */
#define GEOMETRY_NAME_MAX_LENGTH 256

/**
 * @brief Indicates a geometry type, typically used to infer things like
 * vertex and index sizes.
 */
typedef enum kgeometry_type {
    /** @brief Unknown and invalid type of geometry. This being set generally indicates an error in code. */
    KGEOMETRY_TYPE_UNKNOWN = 0x00,
    /** @brief Used for 2d geometry that doesn't change. */
    KGEOMETRY_TYPE_2D_STATIC = 0x01,
    /** @brief Used for 2d geometry that changes often. */
    KGEOMETRY_TYPE_2D_DYNAMIC = 0x02,
    /** @brief Used for 3d geometry that doesn't change. */
    KGEOMETRY_TYPE_3D_STATIC = 0x03,
    /** @brief Used for 3d geometry that doesn't change, and only contains colour data. */
    KGEOMETRY_TYPE_3D_STATIC_COLOUR_ONLY = 0x04,
    /** @brief Used for 3d geometry that changes often. */
    KGEOMETRY_TYPE_3D_DYNAMIC = 0x05,
    /** @brief Used for skinned 3d geometry that changes potentially every frame, and includes bone/weight data. */
    KGEOMETRY_TYPE_3D_SKINNED = 0x06,
    /** @brief Used for heightmap terrain-specific geometry that rarely (if ever) changes - includes material index/weight data. */
    KGEOMETRY_TYPE_3D_HEIGHTMAP_TERRAIN = 0x07,
    /** @brief User-defined geometry type. Vertex/index size will only be looked at for this type. */
    KGEOMETRY_TYPE_CUSTOM = 0xFF,
} kgeometry_type;

/**
 * @brief Represents geometry to be used for various purposes (rendering objects in the
 * world, physics/collision, etc.).
 */
typedef struct kgeometry {
    /** @brief The geometry name. */
    kname name;

    /** @brief The geometry type. */
    kgeometry_type type;
    /** @brief The geometry generation. Incremented every time the geometry
     * changes. */
    u16 generation;
    /** @brief The center of the geometry in local coordinates. */
    vec3 center;
    /** @brief The extents of the geometry in local coordinates. */
    extents_3d extents;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The size of each vertex. Ignored unless type is KGEOMETRY_TYPE_CUSTOM. */
    u32 vertex_element_size;
    /** @brief The vertex data. */
    void* vertices;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. Ignored unless type is KGEOMETRY_TYPE_CUSTOM. */
    u32 index_element_size;
    /** @brief The index data. */
    void* indices;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;

    u32 triangle_count;
    triangle_3d* tris;
} kgeometry;

typedef enum grid_orientation {
    /**
     * @brief A grid that lies "flat" in the world along the ground plane (y-plane).
     * This is the default configuration.
     */
    GRID_ORIENTATION_XZ = 0,
    /**
     * @brief A grid that lies on the z-plane (facing the screen by default, orthogonal to the ground plane).
     */
    GRID_ORIENTATION_XY = 1,
    /**
     * @brief A grid that lies on the x-plane (orthogonal to the default screen plane and the ground plane).
     */
    GRID_ORIENTATION_YZ = 2
} grid_orientation;

/**
 * @brief Calculates normals for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
KAPI void geometry_generate_normals(u32 vertex_count, vertex_3d* vertices, u32 index_count, u32* indices);

/**
 * @brief Calculates tangents for the given vertex and index data. Modifies
 * vertices in place.
 *
 * @param vertex_count The number of vertices.
 * @param vertices An array of vertices.
 * @param index_count The number of indices.
 * @param indices An array of vertices.
 */
KAPI void geometry_generate_tangents(u32 vertex_count, vertex_3d* vertices, u32 index_count, u32* indices);

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
KAPI void geometry_deduplicate_vertices(u32 vertex_count, vertex_3d* vertices, u32 index_count, u32* indices, u32* out_vertex_count, vertex_3d** out_vertices);

/**
 * @brief Generates texture coordinates based on pixel position within an image's dimensions.
 *
 * @param img_width The width of the image in pixels.
 * @param img_height The height of the image in pixels.
 * @param px_x The pixel position along the x-axis.
 * @param px_y The pixel position along the y-axis.
 * @param out_tx A pointer to hold the generated texture coordinate on the x-axis.
 * @param out_ty A pointer to hold the generated texture coordinate on the y-axis.
 */
KAPI void generate_uvs_from_image_coords(u32 img_width, u32 img_height, u32 px_x, u32 px_y, f32* out_tx, f32* out_ty);

/**
 * @brief Generates a two-dimensional quad (two triangles) of geometry. Note that memory for the
 * vertex and index arrays are dynamically allocated, so this should be cleaned up
 * with geometry_destroy(). Dimensions account for the x- and y-axes only.
 *
 * @param width The width of the plane (x-axis).
 * @param height The height of the plane (y-axis).
 * @param tx_min The minimum texture coordinate along the x-axis.
 * @param ty_min The minimum texture coordinate along the y-axis.
 * @param tx_max The maximum texture coordinate along the x-axis.
 * @param ty_max The maximum texture coordinate along the y-axis.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_quad(f32 width, f32 height, f32 tx_min, f32 tx_max, f32 ty_min, f32 ty_max, kname name);

/**
 * @brief Generates a two-dimensional line of geometry. Note that the memory for the
 * vertex array is dynamically allocated, so this should be cleaned up with
 * geometry_destroy(). Note that index data is not used for this geometry.
 *
 * @param point_0 The first point of the line.
 * @param point_1 The second point of the line.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_line2d(vec2 point_0, vec2 point_1, kname name);

/**
 * @brief Generates a three-dimensional line of geometry. Note that the memory for the
 * vertex array is dynamically allocated, so this should be cleaned up with
 * geometry_destroy(). Note that index data is not used for this geometry.
 *
 * @param point_0 The first point of the line.
 * @param point_1 The second point of the line.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_line3d(vec3 point_0, vec3 point_1, kname name);

KAPI kgeometry geometry_generate_line_sphere3d(f32 radius, u32 segment_count, vec4 colour, kname name);

/**
 * @brief Generates a three-dimensional plane of geometry. Note that memory for the
 * vertex and index arrays are dynamically allocated, so this should be cleaned up
 * with geometry_destroy(). Dimensions account for the x- and y-axes, z is always 0.
 *
 * @param width The width of the plane (x-axis).
 * @param height The height of the plane (y-axis).
 * @param x_segment_count The number of segments to split the plane geometry into along the x-axis. Must be at least 1.
 * @param y_segment_count The number of segments to split the plane geometry into along the y-axis. Must be at least 1.
 * @param tile_x The amount of tiling of the textures on the face of the plane on the x-axis. 1.0 means the texture is stretched across the entire surface. 2.0 means it's repeated, etc.
 * @param tile_y The amount of tiling of the textures on the face of the plane on the y-axis. 1.0 means the texture is stretched across the entire surface. 2.0 means it's repeated, etc.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_plane(f32 width, f32 height, u32 x_segment_count, u32 y_segment_count, f32 tile_x, f32 tile_y, kname name);

/**
 * @brief Recalculates the vertices in the given geometry based off the given points.
 *
 * @param geometry A pointer to the geometry to modify.
 * @param points The 8 points (i.e. corners of a box) that will be used for the modification.
 */
KAPI void geometry_recalculate_line_box3d_by_points(kgeometry* geometry, vec3 points[8]);

/**
 * @brief Recalculates the vertices in the given geometry based off the given extents.
 *
 * @param geometry A pointer to the geometry to modify.
 * @param extents The extents of which to base the modification.
 */
KAPI void geometry_recalculate_line_box3d_by_extents(kgeometry* geometry, extents_3d extents);

/**
 * @brief Generates a line-based 3d box based on the provided size.
 *
 * @param size The size (extents) of the box.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_line_box3d(vec3 size, kname name);

/**
 * @brief Generates a three-dimensional cube of geometry. Note that memory for the
 * vertex and index arrays are dynamically allocated, so this should be cleaned up
 * with geometry_destroy().
 *
 * @param width The width of the cube (x-axis).
 * @param height The height of the cube (y-axis).
 * @param depth The depth of the cube (z-axis).
 * @param tile_x The amount of tiling of the textures on each face of the cube on the x-axis. 1.0 means the texture is stretched across the entire surface. 2.0 means it's repeated, etc.
 * @param tile_y The amount of tiling of the textures on each face of the cube on the y-axis. 1.0 means the texture is stretched across the entire surface. 2.0 means it's repeated, etc.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_cube(f32 width, f32 height, f32 depth, f32 tile_x, f32 tile_y, kname name);

/**
 * @brief Create a geometry-based grid using the given parameters. The grid is based on line
 * geometry and has no indices. Note that the vertex array data is dynamically allocated and
 * should be cleaned up using geometry_destroy().
 *
 * @param orientation The orientation of the grid.
 * @param segment_count_dim_0 The number of segments in the first orientation axis.
 * @param segment_count_dim_1 The number of segments in the second orientation axis.
 * @param segment_size The size of each individual segment along all axes.
 * @param use_third_axis Indicates if the grid should also display on a third axis, orthogonal to the two in the orientation.
 * @param name The name of the geometry.
 * @returns The newly-created geometry.
 */
KAPI kgeometry geometry_generate_grid(grid_orientation orientation, u32 segment_count_dim_0, u32 segment_count_dim_1, f32 segment_size, b8 use_third_axis, kname name);

/**
 * @brief Destroys the given geometry.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
KAPI void geometry_destroy(kgeometry* geometry);

/**
 * @brief Generates triangle data for the given geometry.
 *
 * @param geometry A pointer to the geometry for which to generate the data.
 * @return True on success; otherwise false.
 */
KAPI b8 geometry_calculate_triangles(kgeometry* geometry);
