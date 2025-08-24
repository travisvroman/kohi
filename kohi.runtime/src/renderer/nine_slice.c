#include "nine_slice.h"

#include "defines.h"
#include "logger.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"

typedef struct nine_slice_pos_tc {
    f32 tx_min, ty_min, tx_max, ty_max;
    f32 posx_min, posy_min, posx_max, posy_max;
} nine_slice_pos_tc;

b8 nine_slice_update(nine_slice* nslice, vertex_2d* vertices) {
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
        vertices = nslice->vertex_data.elements;
    }

    // update the 9 quads.
    for (u32 i = 0; i < 9; ++i) {
        // Vertices
        u32 v_index = i * 4;

        vertices[v_index + 0].position.x = pt[i].posx_min; // 0    3
        vertices[v_index + 0].position.y = pt[i].posy_min; //
        vertices[v_index + 0].texcoord.x = pt[i].tx_min;   //
        vertices[v_index + 0].texcoord.y = pt[i].ty_min;   // 2    1

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

void nine_slice_render_frame_prepare(nine_slice* nslice, const struct frame_data* p_frame_data) {
    if (!nslice) {
        return;
    }

    if (nslice->is_dirty) {
        // Upload the new vertex data.
        krenderbuffer vertex_buffer = renderer_renderbuffer_get(kname_create(KRENDERBUFFER_NAME_GLOBAL_VERTEX));
        u32 size = nslice->vertex_data.element_size * nslice->vertex_data.element_count;
        if (!renderer_renderbuffer_load_range(vertex_buffer, nslice->vertex_data.buffer_offset, size, nslice->vertex_data.elements, true)) {
            KERROR("vulkan_renderer_geometry_vertex_update failed to upload to the vertex buffer!");
        }

        nslice->is_dirty = false;
    }
}

b8 nine_slice_create(const char* name, vec2i size, vec2i atlas_px_size, vec2i atlas_px_min, vec2i atlas_px_max, vec2i corner_px_size, vec2i corner_size, nine_slice* out_nine_slice) {
    if (!out_nine_slice) {
        return false;
    }

    out_nine_slice->size = size;
    out_nine_slice->atlas_px_size = atlas_px_size;
    out_nine_slice->atlas_px_min = atlas_px_min;
    out_nine_slice->atlas_px_max = atlas_px_max;
    out_nine_slice->corner_size = corner_size;
    out_nine_slice->corner_px_size = corner_px_size;

    const u32 vert_size = sizeof(vertex_2d);
    const u32 vert_count = 4 * 9;
    out_nine_slice->vertex_data.element_size = vert_size;
    out_nine_slice->vertex_data.element_count = vert_count;
    out_nine_slice->vertex_data.elements = kallocate(vert_size * vert_count, MEMORY_TAG_ARRAY);
    out_nine_slice->vertex_data.buffer_offset = INVALID_ID_U64;
    const u32 idx_size = sizeof(u32);
    const u32 idx_count = 6 * 9;
    out_nine_slice->index_data.element_size = idx_size;
    out_nine_slice->index_data.element_count = idx_count;
    out_nine_slice->index_data.elements = kallocate(idx_size * idx_count, MEMORY_TAG_ARRAY);
    out_nine_slice->index_data.buffer_offset = INVALID_ID_U64;

    u32* indices = (u32*)out_nine_slice->index_data.elements;

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

    if (!nine_slice_update(out_nine_slice, out_nine_slice->vertex_data.elements)) {
        KERROR("Failed to update nine slice. See logs for more details.");
    }

    // Vertex data.
    krenderbuffer vertex_buffer = renderer_renderbuffer_get(kname_create(KRENDERBUFFER_NAME_GLOBAL_VERTEX));
    // Allocate space in the buffer.
    if (!renderer_renderbuffer_allocate(vertex_buffer, vert_size * vert_count, &out_nine_slice->vertex_data.buffer_offset)) {
        KERROR("Failed to allocate from the vertex buffer!");
        return false;
    }

    // Load the data.
    // NOTE: Offload was set to false.
    if (!renderer_renderbuffer_load_range(vertex_buffer, out_nine_slice->vertex_data.buffer_offset, vert_size * vert_count, out_nine_slice->vertex_data.elements, false)) {
        KERROR("Failed to upload nine-slice vertex data to the vertex buffer!");
        return false;
    }

    // Index data
    krenderbuffer index_buffer = renderer_renderbuffer_get(kname_create(KRENDERBUFFER_NAME_GLOBAL_INDEX));
    // Allocate space in the buffer.
    if (!renderer_renderbuffer_allocate(index_buffer, idx_size * idx_count, &out_nine_slice->index_data.buffer_offset)) {
        KERROR("Failed to allocate from the index buffer!");
        return false;
    }

    // Load the data.
    // NOTE: Offload was set to false.
    if (!renderer_renderbuffer_load_range(index_buffer, out_nine_slice->index_data.buffer_offset, idx_size * idx_count, out_nine_slice->index_data.elements, false)) {
        KERROR("Failed to upload nine-slice index data to the index buffer!");
        return false;
    }

    return true;
}
