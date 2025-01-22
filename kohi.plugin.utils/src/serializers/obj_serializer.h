#pragma once

#include "core_render_types.h"
#include <assets/kasset_types.h>
#include <defines.h>
#include <math/math_types.h>
#include <resources/resource_types.h>

typedef struct obj_source_geometry {
    /** @brief The number of vertices. */
    u32 vertex_count;
    /** @brief An array of vertices. */
    vertex_3d* vertices;
    /** @brief The number of indices. */
    u32 index_count;
    /** @brief An array of indices. */
    u32* indices;

    vec3 center;
    extents_3d extents;

    /** @brief The name of the geometry. */
    const char* name;
    /** @brief The name of the material asset used by the geometry. */
    const char* material_asset_name;
} obj_source_geometry;

typedef struct obj_source_asset {
    u32 geometry_count;
    obj_source_geometry* geometries;
    // Global extents for the entire thing. Untransformed.
    extents_3d extents;
    // The center point of the asset.
    vec3 center;
    // The material file name (.mtl file).
    const char* material_file_name;
} obj_source_asset;

KAPI b8 obj_serializer_serialize(const obj_source_asset* out_source_asset, const char** out_file_text);

/**
 * Attempts to deserialize the contents of Wavefront OBJ file.
 *
 * @param obj_file_text The obj file content. Required.
 * @param out_source_asset A pointer to hold the deserialized obj data. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 obj_serializer_deserialize(const char* obj_file_text, obj_source_asset* out_source_asset);
