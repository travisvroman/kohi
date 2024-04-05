#include "nine_slice.h"

#include "memory/kmemory.h"
#include "logger.h"
#include "strings/kstring.h"
//
#include "debug/kassert.h"

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

    KASSERT_MSG(false, "Move this to runtime.");
    // FIXME: Move this to runtime
    // if (nslice->is_dirty) {
    //     // Upload the new vertex data.
    //     renderer_geometry_vertex_update(nslice->g, 0, nslice->g->vertex_count, nslice->g->vertices, true);
    //     nslice->is_dirty = false;
    // }
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

    KASSERT_MSG(false, "Move this to runtime.");
    // FIXME: Move this to runtime

    // // Get UI geometry from config. NOTE: this uploads to GPU
    // out_nine_slice->g = geometry_system_acquire_from_config(out_config, true);

    // // Use the same arrays as for the config.
    // out_nine_slice->g->vertices = out_config.vertices;
    // out_nine_slice->g->indices = out_config.indices;
    // out_nine_slice->g->vertex_element_size = out_config.vertex_size;
    // out_nine_slice->g->vertex_count = out_config.vertex_count;
    // out_nine_slice->g->index_element_size = out_config.index_size;
    // out_nine_slice->g->index_count = out_config.index_count;

    return true;
}