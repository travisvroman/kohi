#include "geometry.h"

#include "kassert.h"
#include "kmemory.h"
#include "kstring.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "math/geometry.h"

void geometry_generate_normals(u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices) {
    for (u32 i = 0; i < index_count; i += 3) {
        u32 i0 = indices[i + 0];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];

        vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
        vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

        vec3 normal = vec3_normalized(vec3_cross(edge1, edge2));

        // NOTE: This just generates a face normal. Smoothing out should be done in
        // a separate pass if desired.
        vertices[i0].normal = normal;
        vertices[i1].normal = normal;
        vertices[i2].normal = normal;
    }
}

void geometry_generate_tangents(u32 vertex_count, vertex_3d *vertices, u32 index_count, u32 *indices) {
    for (u32 i = 0; i < index_count; i += 3) {
        u32 i0 = indices[i + 0];
        u32 i1 = indices[i + 1];
        u32 i2 = indices[i + 2];

        vec3 edge1 = vec3_sub(vertices[i1].position, vertices[i0].position);
        vec3 edge2 = vec3_sub(vertices[i2].position, vertices[i0].position);

        f32 deltaU1 = vertices[i1].texcoord.x - vertices[i0].texcoord.x;
        f32 deltaV1 = vertices[i1].texcoord.y - vertices[i0].texcoord.y;

        f32 deltaU2 = vertices[i2].texcoord.x - vertices[i0].texcoord.x;
        f32 deltaV2 = vertices[i2].texcoord.y - vertices[i0].texcoord.y;

        f32 dividend = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
        f32 fc = 1.0f / dividend;

        vec3 tangent = (vec3){(fc * (deltaV2 * edge1.x - deltaV1 * edge2.x)),
                              (fc * (deltaV2 * edge1.y - deltaV1 * edge2.y)),
                              (fc * (deltaV2 * edge1.z - deltaV1 * edge2.z))};

        tangent = vec3_normalized(tangent);

        f32 sx = deltaU1, sy = deltaU2;
        f32 tx = deltaV1, ty = deltaV2;
        f32 handedness = ((tx * sy - ty * sx) < 0.0f) ? -1.0f : 1.0f;

        vec3 t4 = vec3_mul_scalar(tangent, handedness);
        vertices[i0].tangent = t4;
        vertices[i1].tangent = t4;
        vertices[i2].tangent = t4;
    }
}

b8 vertex3d_equal(vertex_3d vert_0, vertex_3d vert_1) {
    return vec3_compare(vert_0.position, vert_1.position, K_FLOAT_EPSILON) &&
           vec3_compare(vert_0.normal, vert_1.normal, K_FLOAT_EPSILON) &&
           vec2_compare(vert_0.texcoord, vert_1.texcoord, K_FLOAT_EPSILON) &&
           vec4_compare(vert_0.colour, vert_1.colour, K_FLOAT_EPSILON) &&
           vec3_compare(vert_0.tangent, vert_1.tangent, K_FLOAT_EPSILON);
}

void reassign_index(u32 index_count, u32 *indices, u32 from, u32 to) {
    for (u32 i = 0; i < index_count; ++i) {
        if (indices[i] == from) {
            indices[i] = to;
        } else if (indices[i] > from) {
            // Pull in all indicies higher than 'from' by 1.
            indices[i]--;
        }
    }
}

void geometry_deduplicate_vertices(u32 vertex_count, vertex_3d *vertices,
                                   u32 index_count, u32 *indices,
                                   u32 *out_vertex_count,
                                   vertex_3d **out_vertices) {
    // Create new arrays for the collection to sit in.
    vertex_3d *unique_verts =
        kallocate(sizeof(vertex_3d) * vertex_count, MEMORY_TAG_ARRAY);
    *out_vertex_count = 0;

    u32 found_count = 0;
    for (u32 v = 0; v < vertex_count; ++v) {
        b8 found = false;
        for (u32 u = 0; u < *out_vertex_count; ++u) {
            if (vertex3d_equal(vertices[v], unique_verts[u])) {
                // Reassign indices, do _not_ copy
                reassign_index(index_count, indices, v - found_count, u);
                found = true;
                found_count++;
                break;
            }
        }

        if (!found) {
            // Copy over to unique.
            unique_verts[*out_vertex_count] = vertices[v];
            (*out_vertex_count)++;
        }
    }

    // Allocate new vertices array
    *out_vertices =
        kallocate(sizeof(vertex_3d) * (*out_vertex_count), MEMORY_TAG_ARRAY);
    // Copy over unique
    kcopy_memory(*out_vertices, unique_verts,
                 sizeof(vertex_3d) * (*out_vertex_count));
    // Destroy temp array
    kfree(unique_verts, sizeof(vertex_3d) * vertex_count, MEMORY_TAG_ARRAY);

    KDEBUG("geometry_deduplicate_vertices: removed %d vertices, orig/now %d/%d.",
           vertex_count - *out_vertex_count, vertex_count, *out_vertex_count);
}

void generate_uvs_from_image_coords(u32 img_width, u32 img_height, u32 px_x, u32 px_y, f32 *out_tx, f32 *out_ty) {
    KASSERT_DEBUG(out_tx);
    KASSERT_DEBUG(out_ty);
    *out_tx = (f32)px_x / img_width;
    *out_ty = (f32)px_y / img_height;
}

void generate_quad_2d(const char *name, f32 width, f32 height, f32 tx_min, f32 tx_max, f32 ty_min, f32 ty_max, geometry_config *out_config) {
    if (out_config) {
        kzero_memory(out_config, sizeof(geometry_config));
        out_config->vertex_size = sizeof(vertex_2d);
        out_config->vertex_count = 4;
        out_config->vertices = kallocate(out_config->vertex_size * out_config->vertex_count, MEMORY_TAG_ARRAY);
        out_config->index_size = sizeof(u32);
        out_config->index_count = 6;
        out_config->indices = kallocate(out_config->index_size * out_config->index_count, MEMORY_TAG_ARRAY);
        string_ncopy(out_config->name, name, GEOMETRY_NAME_MAX_LENGTH);

        vertex_2d uiverts[4];
        uiverts[0].position.x = 0.0f;    // 0    3
        uiverts[0].position.y = 0.0f;    //
        uiverts[0].texcoord.x = tx_min;  //
        uiverts[0].texcoord.y = ty_min;  // 2    1

        uiverts[1].position.y = height;
        uiverts[1].position.x = width;
        uiverts[1].texcoord.x = tx_max;
        uiverts[1].texcoord.y = ty_max;

        uiverts[2].position.x = 0.0f;
        uiverts[2].position.y = height;
        uiverts[2].texcoord.x = tx_min;
        uiverts[2].texcoord.y = ty_max;

        uiverts[3].position.x = width;
        uiverts[3].position.y = 0.0;
        uiverts[3].texcoord.x = tx_max;
        uiverts[3].texcoord.y = ty_min;
        kcopy_memory(out_config->vertices, uiverts, out_config->vertex_size * out_config->vertex_count);

        // Indices - counter-clockwise
        u32 uiindices[6] = {2, 1, 0, 3, 0, 1};
        kcopy_memory(out_config->indices, uiindices, out_config->index_size * out_config->index_count);
    }
}
