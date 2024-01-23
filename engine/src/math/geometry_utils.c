#include "geometry_utils.h"

#include "core/asserts.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "resources/terrain.h"
#include "systems/geometry_system.h"

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

void terrain_geometry_generate_normals(u32 vertex_count, terrain_vertex *vertices, u32 index_count, u32 *indices) {
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

void terrain_geometry_generate_tangents(u32 vertex_count, terrain_vertex *vertices, u32 index_count, u32 *indices) {
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

        vec4 t4 = vec4_from_vec3(vec3_mul_scalar(tangent, handedness), 0.0f);
        vertices[i0].tangent = t4;
        vertices[i1].tangent = t4;
        vertices[i2].tangent = t4;
    }
}

void generate_uvs_from_image_coords(u32 img_width, u32 img_height, u32 px_x, u32 px_y, f32 *out_tx, f32 *out_ty) {
    KASSERT_DEBUG(out_tx);
    KASSERT_DEBUG(out_ty);
    *out_tx = (f32)px_x / img_width;
    *out_ty = (f32)px_y / img_height;
}

void generate_quad_2d(const char *name, f32 width, f32 height, f32 tx_min, f32 tx_max, f32 ty_min, f32 ty_max, struct geometry_config *out_config) {
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

typedef struct nine_slice_pos_tc {
    f32 tx_min, ty_min, tx_max, ty_max;
    f32 posx_min, posy_min, posx_max, posy_max;
} nine_slice_pos_tc;

b8 update_nine_slice(nine_slice *nslice, vertex_2d *vertices) {
    if (!nslice) {
        return false;
    }

    // Generate UVs.
    nine_slice_pos_tc pt[9];
    u8 pt_index = 0;
    // Corners first
    {
        // Top left
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x,
            nslice->atlas_px_min.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.x,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = 0.0f;
        pt[pt_index].posy_min = 0.0f;
        pt[pt_index].posx_max = nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->corner_size.y;
    }
    {
        // Top right
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_min.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_min = 0.0f;
        pt[pt_index].posx_max = nslice->size.x;
        pt[pt_index].posy_max = nslice->corner_size.y;
    }
    {
        // Bottom right
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x,
            nslice->atlas_px_max.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_min = nslice->size.y - nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->size.x;
        pt[pt_index].posy_max = nslice->size.y;
    }
    {
        // Bottom left
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.x,
            nslice->atlas_px_max.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = 0.0f;
        pt[pt_index].posy_min = nslice->size.y - nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->size.y;
    }
    {
        // Top center
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.x,
            nslice->atlas_px_min.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->corner_size.x;
        pt[pt_index].posy_min = 0.0f;
        pt[pt_index].posx_max = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->corner_size.y;
    }
    {
        // Bottom center
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_max.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->corner_size.x;
        pt[pt_index].posy_min = nslice->size.y - nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->size.y;
    }
    {
        // Middle left
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = 0.0f;
        pt[pt_index].posy_min = nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->size.y - nslice->corner_size.y;
    }
    {
        // Middle right
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_min = nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->size.x;
        pt[pt_index].posy_max = nslice->size.y - nslice->corner_size.y;
    }
    {
        // Center
        pt_index++;
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_min.x + nslice->corner_px_size.y,
            nslice->atlas_px_min.y + nslice->corner_px_size.y,
            &pt[pt_index].tx_min, &pt[pt_index].ty_min);
        generate_uvs_from_image_coords(
            nslice->atlas_px_size.x,
            nslice->atlas_px_size.y,
            nslice->atlas_px_max.x - nslice->corner_px_size.x,
            nslice->atlas_px_max.y - nslice->corner_px_size.y,
            &pt[pt_index].tx_max, &pt[pt_index].ty_max);
        // Generate positions.
        pt[pt_index].posx_min = nslice->corner_size.x;
        pt[pt_index].posy_min = nslice->corner_size.y;
        pt[pt_index].posx_max = nslice->size.x - nslice->corner_size.x;
        pt[pt_index].posy_max = nslice->size.y - nslice->corner_size.y;
    }

    b8 using_geo_verts = false;
    if (!vertices) {
        using_geo_verts = true;
        vertices = nslice->g->vertices;
    }
    // update the 9 quads.
    for (u32 i = 0; i < 9; ++i) {
        // Vertices
        u32 v_index = i * 4;

        vertices[v_index + 0].position.x = pt[i].posx_min;  // 0    3
        vertices[v_index + 0].position.y = pt[i].posy_min;  //
        vertices[v_index + 0].texcoord.x = pt[i].tx_min;    //
        vertices[v_index + 0].texcoord.y = pt[i].ty_min;    // 2    1

        vertices[v_index + 1].position.x = pt[i].posx_max;
        vertices[v_index + 1].position.y = pt[i].posy_max;
        vertices[v_index + 1].texcoord.x = pt[i].tx_max;
        vertices[v_index + 1].texcoord.y = pt[i].ty_max;

        vertices[v_index + 2].position.x = pt[i].posx_min;
        vertices[v_index + 2].position.y = pt[i].posy_max;
        vertices[v_index + 2].texcoord.x = pt[i].tx_min;
        vertices[v_index + 2].texcoord.y = pt[i].ty_max;

        vertices[v_index + 3].position.x = pt[i].posx_max;
        vertices[v_index + 3].position.y = pt[i].posy_min;
        vertices[v_index + 3].texcoord.x = pt[i].tx_max;
        vertices[v_index + 3].texcoord.y = pt[i].ty_min;
    }

    if (using_geo_verts) {
        nslice->is_dirty = true;
    }

    return true;
}

void nine_slice_render_frame_prepare(nine_slice *nslice, const struct frame_data *p_frame_data) {
    if (!nslice) {
        return;
    }

    if (nslice->is_dirty) {
        // Upload the new vertex data.
        renderer_geometry_vertex_update(nslice->g, 0, nslice->g->vertex_count, nslice->g->vertices, true);
        nslice->is_dirty = false;
    }
}

b8 generate_nine_slice(const char *name, vec2i size, vec2i atlas_px_size, vec2i atlas_px_min, vec2i atlas_px_max, vec2i corner_px_size, vec2i corner_size, nine_slice *out_nine_slice) {
    if (!out_nine_slice) {
        return false;
    }

    out_nine_slice->size = size;
    out_nine_slice->atlas_px_size = atlas_px_size;
    out_nine_slice->atlas_px_min = atlas_px_min;
    out_nine_slice->atlas_px_max = atlas_px_max;
    out_nine_slice->corner_size = corner_size;
    out_nine_slice->corner_px_size = corner_px_size;

    geometry_config out_config = {0};
    out_config.vertex_size = sizeof(vertex_2d);
    out_config.vertex_count = 4 * 9;
    out_config.vertices = kallocate(out_config.vertex_size * out_config.vertex_count, MEMORY_TAG_ARRAY);
    out_config.index_size = sizeof(u32);
    out_config.index_count = 6 * 9;
    out_config.indices = kallocate(out_config.index_size * out_config.index_count, MEMORY_TAG_ARRAY);
    string_ncopy(out_config.name, name, GEOMETRY_NAME_MAX_LENGTH);

    u32 *indices = (u32 *)out_config.indices;

    // Generate index data for the 9 quads.
    for (u32 i = 0; i < 9; ++i) {
        // Vertices
        u32 v_index = i * 4;

        // Indices - counter-clockwise
        u32 i_index = i * 6;
        indices[i_index + 0] = v_index + 2;
        indices[i_index + 1] = v_index + 1;
        indices[i_index + 2] = v_index + 0;
        indices[i_index + 3] = v_index + 3;
        indices[i_index + 4] = v_index + 0;
        indices[i_index + 5] = v_index + 1;
    }

    if (!update_nine_slice(out_nine_slice, out_config.vertices)) {
        KERROR("Failed to update nine slice. See logs for more details.");
    }

    // Get UI geometry from config. NOTE: this uploads to GPU
    out_nine_slice->g = geometry_system_acquire_from_config(out_config, true);

    // Use the same arrays as for the config.
    out_nine_slice->g->vertices = out_config.vertices;
    out_nine_slice->g->indices = out_config.indices;
    out_nine_slice->g->vertex_element_size = out_config.vertex_size;
    out_nine_slice->g->vertex_count = out_config.vertex_count;
    out_nine_slice->g->index_element_size = out_config.index_size;
    out_nine_slice->g->index_count = out_config.index_count;

    return true;
}
