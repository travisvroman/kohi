#include "sui_label.h"

#include <containers/darray.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <core/logger.h>
#include <math/kmath.h>
#include <math/transform.h>
#include <renderer/renderer_frontend.h>
#include <systems/font_system.h>
#include <systems/shader_system.h>

#include "defines.h"

static void regenerate_label_geometry(sui_control* self);
static void sui_label_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data);

b8 sui_label_control_create(const char* name, font_type type, const char* font_name, u16 font_size, const char* text, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_label_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_label_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->colour = vec4_one();

    // Assign function pointers.
    out_control->destroy = sui_label_control_destroy;
    out_control->load = sui_label_control_load;
    out_control->unload = sui_label_control_unload;
    out_control->update = sui_label_control_update;
    out_control->render_prepare = sui_label_control_render_frame_prepare;
    out_control->render = sui_label_control_render;

    out_control->name = string_duplicate(name);

    // Assign the type first
    typed_data->type = type;

    // Acquire the font of the correct type and assign its internal data.
    // This also gets the atlas texture.
    typed_data->data = font_system_acquire(font_name, font_size, typed_data->type);
    if (!typed_data->data) {
        KERROR("Unable to acquire font: '%s'. ui_text cannot be created.", font_name);
        return false;
    }

    typed_data->text = string_duplicate(text);

    typed_data->instance_id = INVALID_ID;
    typed_data->frame_number = INVALID_ID_U64;

    typed_data->vertex_buffer_offset = INVALID_ID_U64;
    typed_data->index_buffer_offset = INVALID_ID_U64;

    // Acquire resources for font texture map.
    // TODO: Should there be an override option for the shader?
    texture_map* maps[1] = {&typed_data->data->atlas};
    shader* s = shader_system_get("Shader.StandardUI");
    u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    shader_instance_uniform_texture_config atlas_texture = {0};
    atlas_texture.uniform_location = atlas_location;
    atlas_texture.texture_map_count = 1;
    atlas_texture.texture_maps = maps;

    instance_resource_config.uniform_config_count = 1;
    instance_resource_config.uniform_configs = &atlas_texture;

    if (!renderer_shader_instance_resources_acquire(s, &instance_resource_config, &typed_data->instance_id)) {
        KFATAL("Unable to acquire shader resources for font texture map.");
        return false;
    }

    // Verify atlas has the glyphs needed.
    if (!font_system_verify_atlas(typed_data->data, text)) {
        KERROR("Font atlas verification failed.");
        return false;
    }

    return true;
}

void sui_label_control_destroy(struct sui_control* self) {
    sui_base_control_destroy(self);
}

b8 sui_label_control_load(struct sui_control* self) {
    if (!sui_base_control_load(self)) {
        return false;
    }

    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->text && typed_data->text[0] != 0) {
        static const u64 quad_vertex_size = (sizeof(vertex_2d) * 4);
        static const u64 quad_index_size = (sizeof(u32) * 6);
        u64 text_length = string_utf8_length(typed_data->text);

        // Allocate space in the buffers.
        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        if (!renderer_renderbuffer_allocate(vertex_buffer, quad_vertex_size * text_length, &typed_data->vertex_buffer_offset)) {
            KERROR("sui_label_control_load failed to allocate from the renderer's vertex buffer!");
            return false;
        }

        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        if (!renderer_renderbuffer_allocate(index_buffer, quad_index_size * text_length, &typed_data->index_buffer_offset)) {
            KERROR("sui_label_control_load failed to allocate from the renderer's index buffer!");
            return false;
        }
    }
    // Generate geometry.
    regenerate_label_geometry(self);

    return true;
}

void sui_label_control_unload(struct sui_control* self) {
    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->text) {
        u32 text_length = string_length(typed_data->text);
        kfree(typed_data->text, sizeof(char) * text_length + 1, MEMORY_TAG_STRING);
        typed_data->text = 0;
    }

    // Free from the vertex buffer.
    renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    if (typed_data->vertex_buffer_offset != INVALID_ID_U64) {
        if (typed_data->max_text_length > 0) {
            renderer_renderbuffer_free(vertex_buffer, sizeof(vertex_2d) * 4 * typed_data->max_text_length, typed_data->vertex_buffer_offset);
        }
        typed_data->vertex_buffer_offset = INVALID_ID_U64;
    }

    // Free from the index buffer.
    if (typed_data->index_buffer_offset != INVALID_ID_U64) {
        static const u64 quad_index_size = (sizeof(u32) * 6);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        if (typed_data->max_text_length > 0 || typed_data->index_buffer_offset != INVALID_ID_U64) {
            renderer_renderbuffer_free(index_buffer, quad_index_size * typed_data->max_text_length, typed_data->index_buffer_offset);
        }
        typed_data->index_buffer_offset = INVALID_ID_U64;
    }

    // Release resources for font texture map.
    shader* ui_shader = shader_system_get("Shader.StandardUI");  // TODO: text shader.
    if (!renderer_shader_instance_resources_release(ui_shader, typed_data->instance_id)) {
        KFATAL("Unable to release shader resources for font texture map.");
    }
    typed_data->instance_id = INVALID_ID;
}

b8 sui_label_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

b8 sui_label_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(self, p_frame_data, render_data)) {
        return false;
    }

    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->cached_ut8_length && typed_data->vertex_buffer_offset != INVALID_ID_U64) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = 0;
        renderable.render_data.vertex_count = typed_data->cached_ut8_length * 4;
        renderable.render_data.vertex_buffer_offset = typed_data->vertex_buffer_offset;
        renderable.render_data.vertex_element_size = sizeof(vertex_2d);
        renderable.render_data.index_count = typed_data->cached_ut8_length * 6;
        renderable.render_data.index_buffer_offset = typed_data->index_buffer_offset;
        renderable.render_data.index_element_size = sizeof(u32);

        // NOTE: Override the default UI atlas and use that of the loaded font instead.
        renderable.atlas_override = &typed_data->data->atlas;

        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = typed_data->colour;

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}

void sui_label_text_set(struct sui_control* self, const char* text) {
    if (self) {
        sui_label_internal_data* typed_data = self->internal_data;

        // If strings are already equal, don't do anything.
        if (strings_equal(text, typed_data->text)) {
            return;
        }

        if (typed_data->text) {
            u32 text_length = string_length(typed_data->text);
            kfree(typed_data->text, sizeof(char) * text_length + 1, MEMORY_TAG_STRING);
        }
        typed_data->text = string_duplicate(text);

        // Verify atlas has the glyphs needed.
        if (!font_system_verify_atlas(typed_data->data, text)) {
            KERROR("Font atlas verification failed.");
        }

        typed_data->is_dirty = true;
    }
}

const char* sui_label_text_get(struct sui_control* self) {
    if (self && self->internal_data) {
        sui_label_internal_data* typed_data = self->internal_data;
        return typed_data->text;
    }
    return 0;
}

static void regenerate_label_geometry(sui_control* self) {
    sui_label_internal_data* typed_data = self->internal_data;

    // Get the UTF-8 string length
    u32 text_length_utf8 = string_utf8_length(typed_data->text);
    typed_data->cached_ut8_length = text_length_utf8;
    // Also get the length in characters.
    u32 char_length = string_length(typed_data->text);

    b8 needs_realloc = text_length_utf8 > typed_data->max_text_length;

    // Don't try to regenerate geometry for something that doesn't have any text.
    if (text_length_utf8 < 1) {
        return;
    }

    // Calculate buffer sizes.
    static const u64 verts_per_quad = 4;
    static const u8 indices_per_quad = 6;

    // Save the data off to a pending structure.
    typed_data->pending_data.prev_vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * typed_data->max_text_length;
    typed_data->pending_data.prev_index_buffer_size = sizeof(u32) * indices_per_quad * typed_data->max_text_length;
    typed_data->pending_data.new_length = char_length;
    typed_data->pending_data.new_utf8_length = text_length_utf8;
    typed_data->pending_data.vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * text_length_utf8;
    typed_data->pending_data.index_buffer_size = sizeof(u32) * indices_per_quad * text_length_utf8;
    // Temp arrays to hold vertex/index data.
    typed_data->pending_data.vertex_buffer_data = kallocate(typed_data->pending_data.vertex_buffer_size, MEMORY_TAG_ARRAY);
    typed_data->pending_data.index_buffer_data = kallocate(typed_data->pending_data.index_buffer_size, MEMORY_TAG_ARRAY);

    renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    if (needs_realloc) {
        // Allocate new space in the buffer, but don't upload it yet.
        if (!renderer_renderbuffer_allocate(vertex_buffer, typed_data->pending_data.vertex_buffer_size, &typed_data->pending_data.vertex_buffer_offset)) {
            KERROR("regenerate_label_geometry failed to allocate from the renderer's vertex buffer!");
            return;
        }

        // Allocate new space in the buffer, but don't upload it yet.
        if (!renderer_renderbuffer_allocate(index_buffer, typed_data->pending_data.index_buffer_size, &typed_data->pending_data.index_buffer_offset)) {
            KERROR("regenerate_label_geometry failed to allocate from the renderer's index buffer!");
            return;
        }
    }

    // Generate new geometry for each character.
    f32 x = 0;
    f32 y = 0;

    // Take the length in chars and get the correct codepoint from it.
    for (u32 c = 0, uc = 0; c < char_length; ++c) {
        i32 codepoint = typed_data->text[c];

        // Continue to next line for newline.
        if (codepoint == '\n') {
            x = 0;
            y += typed_data->data->line_height;
            // Increment utf-8 character count.
            uc++;
            continue;
        }

        if (codepoint == '\t') {
            x += typed_data->data->tab_x_advance;
            uc++;
            continue;
        }

        // NOTE: UTF-8 codepoint handling.
        u8 advance = 0;
        if (!bytes_to_codepoint(typed_data->text, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        font_glyph* g = 0;
        for (u32 i = 0; i < typed_data->data->glyph_count; ++i) {
            if (typed_data->data->glyphs[i].codepoint == codepoint) {
                g = &typed_data->data->glyphs[i];
                break;
            }
        }

        if (!g) {
            // If not found, use the codepoint -1
            codepoint = -1;
            for (u32 i = 0; i < typed_data->data->glyph_count; ++i) {
                if (typed_data->data->glyphs[i].codepoint == codepoint) {
                    g = &typed_data->data->glyphs[i];
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
            f32 tminx = (f32)g->x / typed_data->data->atlas_size_x;
            f32 tmaxx = (f32)(g->x + g->width) / typed_data->data->atlas_size_x;
            f32 tminy = (f32)g->y / typed_data->data->atlas_size_y;
            f32 tmaxy = (f32)(g->y + g->height) / typed_data->data->atlas_size_y;
            // Flip the y axis for system text
            if (typed_data->type == FONT_TYPE_SYSTEM) {
                tminy = 1.0f - tminy;
                tmaxy = 1.0f - tmaxy;
            }

            vertex_2d p0 = (vertex_2d){vec2_create(minx, miny), vec2_create(tminx, tminy)};
            vertex_2d p1 = (vertex_2d){vec2_create(maxx, miny), vec2_create(tmaxx, tminy)};
            vertex_2d p2 = (vertex_2d){vec2_create(maxx, maxy), vec2_create(tmaxx, tmaxy)};
            vertex_2d p3 = (vertex_2d){vec2_create(minx, maxy), vec2_create(tminx, tmaxy)};

            typed_data->pending_data.vertex_buffer_data[(uc * 4) + 0] = p0;  // 0    3
            typed_data->pending_data.vertex_buffer_data[(uc * 4) + 1] = p2;  //
            typed_data->pending_data.vertex_buffer_data[(uc * 4) + 2] = p3;  //
            typed_data->pending_data.vertex_buffer_data[(uc * 4) + 3] = p1;  // 2    1

            // Try to find kerning
            i32 kerning = 0;

            // Get the offset of the next character. If there is no advance, move forward one,
            // otherwise use advance as-is.
            u32 offset = c + advance;  //(advance < 1 ? 1 : advance);
            if (offset < text_length_utf8 - 1) {
                // Get the next codepoint.
                i32 next_codepoint = 0;
                u8 advance_next = 0;

                if (!bytes_to_codepoint(typed_data->text, offset, &next_codepoint, &advance_next)) {
                    KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
                    codepoint = -1;
                } else {
                    for (u32 i = 0; i < typed_data->data->kerning_count; ++i) {
                        font_kerning* k = &typed_data->data->kernings[i];
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
        typed_data->pending_data.index_buffer_data[(uc * 6) + 0] = (uc * 4) + 2;
        typed_data->pending_data.index_buffer_data[(uc * 6) + 1] = (uc * 4) + 1;
        typed_data->pending_data.index_buffer_data[(uc * 6) + 2] = (uc * 4) + 0;
        typed_data->pending_data.index_buffer_data[(uc * 6) + 3] = (uc * 4) + 3;
        typed_data->pending_data.index_buffer_data[(uc * 6) + 4] = (uc * 4) + 0;
        typed_data->pending_data.index_buffer_data[(uc * 6) + 5] = (uc * 4) + 1;

        // Now advance c
        c += advance - 1;  // Subtracting 1 because the loop always increments once for single-byte anyway.
        // Increment utf-8 character count.
        uc++;
    }

    // Mark the label as dirty. The work should be offloaded until the next prepare.
    typed_data->is_dirty = true;
}

static void sui_label_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data) {
    if (self) {
        sui_label_internal_data* typed_data = self->internal_data;
        if (typed_data->is_dirty && typed_data->vertex_buffer_offset != INVALID_ID_U64 && typed_data->index_buffer_offset != INVALID_ID_U64) {
            regenerate_label_geometry(self);

            b8 needs_realloc = typed_data->pending_data.new_utf8_length > typed_data->max_text_length;

            renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
            renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

            // Load up the data.
            b8 vertex_load_result = renderer_renderbuffer_load_range(vertex_buffer, typed_data->pending_data.vertex_buffer_offset, typed_data->pending_data.vertex_buffer_size, typed_data->pending_data.vertex_buffer_data, true);
            b8 index_load_result = renderer_renderbuffer_load_range(index_buffer, typed_data->pending_data.index_buffer_offset, typed_data->pending_data.index_buffer_size, typed_data->pending_data.index_buffer_data, true);

            // Clean up pending data.
            if (typed_data->pending_data.vertex_buffer_data) {
                kfree(typed_data->pending_data.vertex_buffer_data, typed_data->pending_data.vertex_buffer_size, MEMORY_TAG_ARRAY);
            }

            if (typed_data->pending_data.index_buffer_data) {
                kfree(typed_data->pending_data.index_buffer_data, typed_data->pending_data.index_buffer_size, MEMORY_TAG_ARRAY);
            }

            if (needs_realloc) {
                // Release old data from the vertex buffer.
                if (typed_data->pending_data.prev_vertex_buffer_size > 0) {
                    if (!renderer_renderbuffer_free(vertex_buffer, typed_data->pending_data.prev_vertex_buffer_size, typed_data->vertex_buffer_offset)) {
                        KERROR("Failed to free from renderer vertex buffer: size=%u, offset=%u", typed_data->pending_data.vertex_buffer_size, typed_data->vertex_buffer_offset);
                    }
                }

                // Release old data from the index buffer.
                if (typed_data->pending_data.prev_index_buffer_size > 0) {
                    if (!renderer_renderbuffer_free(index_buffer, typed_data->pending_data.prev_index_buffer_size, typed_data->index_buffer_offset)) {
                        KERROR("Failed to free from renderer index buffer: size=%u, offset=%u", typed_data->pending_data.index_buffer_size, typed_data->index_buffer_offset);
                    }
                }
            }

            // Update the buffer offsets with the pending values.
            typed_data->vertex_buffer_offset = typed_data->pending_data.vertex_buffer_offset;
            typed_data->index_buffer_offset = typed_data->pending_data.index_buffer_offset;

            // Verify results.
            if (!vertex_load_result) {
                KERROR("sui_label_control_render_frame_prepare failed to load data into vertex buffer range.");
            }
            if (!index_load_result) {
                KERROR("sui_label_control_render_frame_prepare failed to load data into index buffer range.");
            }

            // Update the max length if the string is now longer.
            if (typed_data->pending_data.new_utf8_length > typed_data->max_text_length) {
                typed_data->max_text_length = typed_data->pending_data.new_utf8_length;
            }

            kzero_memory(&typed_data->pending_data, sizeof(sui_label_pending_data));

            typed_data->is_dirty = false;
        }
    }
}
