#include "ui_text.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/identifier.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "renderer/renderer_types.inl"
#include "renderer/renderer_frontend.h"

#include "systems/font_system.h"
#include "systems/shader_system.h"

void regenerate_geometry(ui_text* text);

b8 ui_text_create(ui_text_type type, const char* font_name, u16 font_size, const char* text_content, ui_text* out_text) {
    if (!font_name || !text_content || !out_text) {
        KERROR("ui_text_create requires a valid pointer to font_name, text_content and out_text");
        return false;
    }

    kzero_memory(out_text, sizeof(ui_text));

    // Assign the type first
    out_text->type = type;

    // Acquire the font of the correct type and assign its internal data.
    // This also gets the atlas texture.
    if (!font_system_acquire(font_name, font_size, out_text)) {
        KERROR("Unable to acquire font: '%s'. ui_text cannot be created.", font_name);
        return false;
    }

    out_text->text = string_duplicate(text_content);
    out_text->transform = transform_create();

    out_text->instance_id = INVALID_ID;
    out_text->render_frame_number = INVALID_ID_U64;

    static const u64 quad_size = (sizeof(vertex_2d) * 4);

    u32 text_length = string_length(out_text->text);
    // In the case of an empty string, cannot create an empty buffer so just create enough to hold one for now.
    if (text_length < 1) {
        text_length = 1;
    }

    // Acquire resources for font texture map.
    shader* ui_shader = shader_system_get("Shader.Builtin.UI");  // TODO: text shader.
    texture_map* font_maps[1] = {&out_text->data->atlas};
    if (!renderer_shader_acquire_instance_resources(ui_shader, font_maps, &out_text->instance_id)) {
        KFATAL("Unable to acquire shader resources for font texture map.");
        return false;
    }

    // Generate the vertex buffer.
    if (!renderer_renderbuffer_create(RENDERBUFFER_TYPE_VERTEX, text_length * quad_size, false, &out_text->vertex_buffer)) {
        KERROR("ui_text_create failed to create vertex renderbuffer.");
        return false;
    }
    if (!renderer_renderbuffer_bind(&out_text->vertex_buffer, 0)) {
        KERROR("ui_text_create failed to bind vertex renderbuffer.");
        return false;
    }

    // Generate an index buffer.
    static const u8 quad_index_size = sizeof(u32) * 6;
    if (!renderer_renderbuffer_create(RENDERBUFFER_TYPE_INDEX, text_length * quad_index_size, false, &out_text->index_buffer)) {
        KERROR("ui_text_create failed to create index renderbuffer.");
        return false;
    }
    if (!renderer_renderbuffer_bind(&out_text->index_buffer, 0)) {
        KERROR("ui_text_create failed to bind index renderbuffer.");
        return false;
    }

    // Verify atlas has the glyphs needed.
    if (!font_system_verify_atlas(out_text->data, text_content)) {
        KERROR("Font atlas verification failed.");
        return false;
    }

    // Generate geometry.
    regenerate_geometry(out_text);

    // Get a unique identifier for the text object.
    out_text->unique_id = identifier_aquire_new_id(out_text);

    return true;
}

void ui_text_destroy(ui_text* text) {
    if (text) {
        // Release the unique identifier.
        identifier_release_id(text->unique_id);

        if (text->text) {
            u32 text_length = string_length(text->text);
            kfree(text->text, sizeof(char) * text_length, MEMORY_TAG_STRING);
            text->text = 0;
        }

        // Destroy buffers.
        renderer_renderbuffer_destroy(&text->vertex_buffer);
        renderer_renderbuffer_destroy(&text->index_buffer);

        // Release resources for font texture map.
        shader* ui_shader = shader_system_get("Shader.Builtin.UI");  // TODO: text shader.
        if (!renderer_shader_release_instance_resources(ui_shader, text->instance_id)) {
            KFATAL("Unable to release shader resources for font texture map.");
        }
    }
    kzero_memory(text, sizeof(ui_text));
}

void ui_text_set_position(ui_text* u_text, vec3 position) {
    transform_set_position(&u_text->transform, position);
}

void ui_text_set_text(ui_text* u_text, const char* text) {
    if (u_text && u_text->text) {
        // If strings are already equal, don't do anything.
        if (strings_equal(text, u_text->text)) {
            return;
        }

        u32 text_length = string_length(u_text->text);
        kfree(u_text->text, sizeof(char) * text_length, MEMORY_TAG_STRING);
        u_text->text = string_duplicate(text);

        // Verify atlas has the glyphs needed.
        if (!font_system_verify_atlas(u_text->data, text)) {
            KERROR("Font atlas verification failed.");
        }

        regenerate_geometry(u_text);
    }
}

void ui_text_draw(ui_text* u_text) {
    // TODO: utf8 length
    u32 text_length = string_length(u_text->text);
    static const u64 quad_vert_count = 4;
    if (!renderer_renderbuffer_draw(&u_text->vertex_buffer, 0, text_length * quad_vert_count, true)) {
        KERROR("Failed to draw ui font vertex buffer.");
    }

    static const u8 quad_index_count = 6;
    if (!renderer_renderbuffer_draw(&u_text->index_buffer, 0, text_length * quad_index_count, false)) {
        KERROR("Failed to draw ui font index buffer.");
    }
}

void regenerate_geometry(ui_text* text) {
    // Get the UTF-8 string length
    u32 text_length_utf8 = string_utf8_length(text->text);
    // Also get the length in characters.
    u32 char_length = string_length(text->text);

    // Calculate buffer sizes.
    static const u64 verts_per_quad = 4;
    static const u8 indices_per_quad = 6;
    u64 vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * text_length_utf8;
    u64 index_buffer_size = sizeof(u32) * indices_per_quad * text_length_utf8;

    // Resize the vertex buffer, but only if larger.
    if (vertex_buffer_size > text->vertex_buffer.total_size) {
        if (!renderer_renderbuffer_resize(&text->vertex_buffer, vertex_buffer_size)) {
            KERROR("regenerate_geometry for ui text failed to resize vertex renderbuffer.");
            return;
        }
    }

    // Resize the index buffer, but only if larger.
    if (index_buffer_size > text->index_buffer.total_size) {
        if (!renderer_renderbuffer_resize(&text->index_buffer, index_buffer_size)) {
            KERROR("regenerate_geometry for ui text failed to resize index renderbuffer.");
            return;
        }
    }

    // Generate new geometry for each character.
    f32 x = 0;
    f32 y = 0;
    // Temp arrays to hold vertex/index data.
    vertex_2d* vertex_buffer_data = kallocate(vertex_buffer_size, MEMORY_TAG_ARRAY);
    u32* index_buffer_data = kallocate(index_buffer_size, MEMORY_TAG_ARRAY);

    // Take the length in chars and get the correct codepoint from it.
    for (u32 c = 0, uc = 0; c < char_length; ++c) {
        i32 codepoint = text->text[c];

        // Continue to next line for newline.
        if (codepoint == '\n') {
            x = 0;
            y += text->data->line_height;
            // Increment utf-8 character count.
            uc++;
            continue;
        }

        if (codepoint == '\t') {
            x += text->data->tab_x_advance;
            uc++;
            continue;
        }

        // NOTE: UTF-8 codepoint handling.
        u8 advance = 0;
        if (!bytes_to_codepoint(text->text, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        font_glyph* g = 0;
        for (u32 i = 0; i < text->data->glyph_count; ++i) {
            if (text->data->glyphs[i].codepoint == codepoint) {
                g = &text->data->glyphs[i];
                break;
            }
        }

        if (!g) {
            // If not found, use the codepoint -1
            codepoint = -1;
            for (u32 i = 0; i < text->data->glyph_count; ++i) {
                if (text->data->glyphs[i].codepoint == codepoint) {
                    g = &text->data->glyphs[i];
                    break;
                }
            }
        }

        if (g) {
            // Found the glyph. generate points.
            f32 minx = x + g->x_offset;
            f32 miny = y + g->y_offset;
            f32 maxx = minx + g->width;
            f32 maxy = miny + g->height;
            f32 tminx = (f32)g->x / text->data->atlas_size_x;
            f32 tmaxx = (f32)(g->x + g->width) / text->data->atlas_size_x;
            f32 tminy = (f32)g->y / text->data->atlas_size_y;
            f32 tmaxy = (f32)(g->y + g->height) / text->data->atlas_size_y;
            // Flip the y axis for system text
            if (text->type == UI_TEXT_TYPE_SYSTEM) {
                tminy = 1.0f - tminy;
                tmaxy = 1.0f - tmaxy;
            }

            vertex_2d p0 = (vertex_2d){vec2_create(minx, miny), vec2_create(tminx, tminy)};
            vertex_2d p1 = (vertex_2d){vec2_create(maxx, miny), vec2_create(tmaxx, tminy)};
            vertex_2d p2 = (vertex_2d){vec2_create(maxx, maxy), vec2_create(tmaxx, tmaxy)};
            vertex_2d p3 = (vertex_2d){vec2_create(minx, maxy), vec2_create(tminx, tmaxy)};

            vertex_buffer_data[(uc * 4) + 0] = p0;  // 0    3
            vertex_buffer_data[(uc * 4) + 1] = p2;  //
            vertex_buffer_data[(uc * 4) + 2] = p3;  //
            vertex_buffer_data[(uc * 4) + 3] = p1;  // 2    1

            // Try to find kerning
            i32 kerning = 0;

            // Get the offset of the next character. If there is no advance, move forward one,
            // otherwise use advance as-is.
            u32 offset = c + advance;  //(advance < 1 ? 1 : advance);
            if (offset < text_length_utf8 - 1) {
                // Get the next codepoint.
                i32 next_codepoint = 0;
                u8 advance_next = 0;

                if (!bytes_to_codepoint(text->text, offset, &next_codepoint, &advance_next)) {
                    KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
                    codepoint = -1;
                } else {
                    for (u32 i = 0; i < text->data->kerning_count; ++i) {
                        font_kerning* k = &text->data->kernings[i];
                        if (k->codepoint_0 == codepoint && k->codepoint_1 == next_codepoint) {
                            kerning = k->amount;
                        }
                    }
                }
            }
            x += g->x_advance + kerning;

        } else {
            KERROR("Unable to find unknown codepoint. Skipping.");
            // Increment utf-8 character count.
            uc++;
            continue;
        }

        // Index data 210301
        index_buffer_data[(uc * 6) + 0] = (uc * 4) + 2;
        index_buffer_data[(uc * 6) + 1] = (uc * 4) + 1;
        index_buffer_data[(uc * 6) + 2] = (uc * 4) + 0;
        index_buffer_data[(uc * 6) + 3] = (uc * 4) + 3;
        index_buffer_data[(uc * 6) + 4] = (uc * 4) + 0;
        index_buffer_data[(uc * 6) + 5] = (uc * 4) + 1;

        // Now advance c
        c += advance - 1;  // Subtracting 1 because the loop always increments once for single-byte anyway.
        // Increment utf-8 character count.
        uc++;
    }

    // Load up the data.
    b8 vertex_load_result = renderer_renderbuffer_load_range(&text->vertex_buffer, 0, vertex_buffer_size, vertex_buffer_data);
    b8 index_load_result = renderer_renderbuffer_load_range(&text->index_buffer, 0, index_buffer_size, index_buffer_data);

    // Clean up.
    kfree(vertex_buffer_data, vertex_buffer_size, MEMORY_TAG_ARRAY);
    kfree(index_buffer_data, index_buffer_size, MEMORY_TAG_ARRAY);

    // Verify results.
    if (!vertex_load_result) {
        KERROR("regenerate_geometry failed to load data into vertex buffer range.");
    }
    if (!index_load_result) {
        KERROR("regenerate_geometry failed to load data into index buffer range.");
    }
}
