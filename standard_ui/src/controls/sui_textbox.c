#include "sui_textbox.h"

#include <containers/darray.h>
#include <core/event.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <core/logger.h>
#include <core/systems_manager.h>
#include <math/kmath.h>
#include <math/transform.h>
#include <renderer/renderer_frontend.h>
#include <systems/geometry_system.h>
#include <systems/shader_system.h>

#include "controls/sui_panel.h"
#include "core/input.h"
#include "math/geometry_utils.h"
#include "resources/resource_types.h"
#include "sui_label.h"
#include "systems/font_system.h"

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context);

static f32 sui_textbox_calculate_cursor_pos(u32 string_pos, u32 string_view_offset, const char* full_string, font_data* font) {
    if (string_pos == 0) {
        return 0;
    }
    // Measure font string based on the mid of the string starting at string_view_offset to string_pos using full_string and font.
    char* mid_target = string_duplicate(full_string);
    u32 original_length = string_length(mid_target);
    string_mid(mid_target, full_string, string_view_offset, string_pos - string_view_offset);

    vec2 size = font_system_measure_string(font, mid_target);
    KTRACE("measure string x/y: %.2f/%.2f", size.x, size.y);

    // Make sure to cleanup the string.
    kfree(mid_target, sizeof(char) * original_length + 1, MEMORY_TAG_STRING);

    // Use the x-axis of the mesurement to place the cursor.
    return size.x;
}

static void sui_textbox_update_cursor_position(sui_control* self) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    sui_label_internal_data* label_data = typed_data->content_label.internal_data;
    vec3 pos = {0};
    pos.x = typed_data->nslice.corner_size.x + sui_textbox_calculate_cursor_pos(typed_data->cursor_position, typed_data->text_view_offset, label_data->text, label_data->data);
    pos.y = 2.0f;
    transform_position_set(&typed_data->cursor.xform, pos);
}

b8 sui_textbox_control_create(const char* name, font_type type, const char* font_name, u16 font_size, const char* text, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_textbox_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_textbox_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->size = (vec2i){200, font_size + 4};
    typed_data->colour = vec4_one();

    // Assign function pointers.
    out_control->destroy = sui_textbox_control_destroy;
    out_control->load = sui_textbox_control_load;
    out_control->unload = sui_textbox_control_unload;
    out_control->update = sui_textbox_control_update;
    out_control->render = sui_textbox_control_render;

    out_control->internal_mouse_down = sui_textbox_on_mouse_down;
    out_control->internal_mouse_up = sui_textbox_on_mouse_up;
    /* out_control->internal_mouse_out = sui_textbox_on_mouse_out;
    out_control->internal_mouse_over = sui_textbox_on_mouse_over; */

    out_control->name = string_duplicate(name);

    char buffer[512] = {0};
    string_format(buffer, "%s_textbox_internal_label", name);
    if (!sui_label_control_create(buffer, type, font_name, font_size, text, &typed_data->content_label)) {
        KERROR("Failed to create internal label control for textbox. Textbox creation failed.");
        return false;
    }

    // Use a panel as the cursor.
    kzero_memory(buffer, sizeof(char) * 512);
    string_format(buffer, "%s_textbox_cursor_panel", name);
    if (!sui_panel_control_create(buffer, (vec2){3.0f, font_size}, &typed_data->cursor)) {
        KERROR("Failed to create internal cursor control for textbox. Textbox creation failed.");
        return false;
    }

    return true;
}

void sui_textbox_control_destroy(struct sui_control* self) {
    sui_base_control_destroy(self);
}

b8 sui_textbox_control_size_set(struct sui_control* self, i32 width, i32 height) {
    if (!self) {
        return false;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;
    typed_data->size.x = width;
    typed_data->size.y = height;
    typed_data->nslice.size.x = width;
    typed_data->nslice.size.y = height;

    self->bounds.height = height;
    self->bounds.width = width;

    update_nine_slice(&typed_data->nslice, 0);

    return true;
}
b8 sui_textbox_control_width_set(struct sui_control* self, i32 width) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_textbox_control_size_set(self, width, typed_data->size.y);
}
b8 sui_textbox_control_height_set(struct sui_control* self, i32 height) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_textbox_control_size_set(self, typed_data->size.x, height);
}

b8 sui_textbox_control_load(struct sui_control* self) {
    if (!sui_base_control_load(self)) {
        return false;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;
    standard_ui_state* typed_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);

    // HACK: TODO: remove hardcoded stuff.
    /* vec2i atlas_size = (vec2i){typed_state->ui_atlas.texture->width, typed_state->ui_atlas.texture->height}; */
    vec2i atlas_size = (vec2i){512, 512};
    vec2i atlas_min = (vec2i){180, 31};
    vec2i atlas_max = (vec2i){193, 43};
    vec2i corner_px_size = (vec2i){3, 3};
    vec2i corner_size = (vec2i){10, 10};
    if (!generate_nine_slice(self->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice)) {
        KERROR("Failed to generate nine slice.");
        return false;
    }

    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = typed_data->size.x;
    self->bounds.height = typed_data->size.y;

    // Setup textbox clipping mask geometry.
    typed_data->clip_mask.reference_id = 1;  // TODO: move creation/reference_id assignment.

    geometry_config clip_config;
    generate_quad_2d("textbox_clipping_box", typed_data->size.x - (corner_size.x * 2), typed_data->size.y, 0, 0, 0, 0, &clip_config);
    typed_data->clip_mask.clip_geometry = geometry_system_acquire_from_config(clip_config, false);

    typed_data->clip_mask.render_data.model = mat4_identity();
    typed_data->clip_mask.render_data.material = 0;
    typed_data->clip_mask.render_data.unique_id = typed_data->clip_mask.reference_id;

    typed_data->clip_mask.render_data.vertex_count = typed_data->clip_mask.clip_geometry->vertex_count;
    typed_data->clip_mask.render_data.vertex_element_size = typed_data->clip_mask.clip_geometry->vertex_element_size;
    typed_data->clip_mask.render_data.vertex_buffer_offset = typed_data->clip_mask.clip_geometry->vertex_buffer_offset;

    typed_data->clip_mask.render_data.index_count = typed_data->clip_mask.clip_geometry->index_count;
    typed_data->clip_mask.render_data.index_element_size = typed_data->clip_mask.clip_geometry->index_element_size;
    typed_data->clip_mask.render_data.index_buffer_offset = typed_data->clip_mask.clip_geometry->index_buffer_offset;

    typed_data->clip_mask.render_data.diffuse_colour = vec4_zero();  // transparent;

    typed_data->clip_mask.clip_xform = transform_from_position((vec3){corner_size.x, 0.0f, 0.0f});
    transform_parent_set(&typed_data->clip_mask.clip_xform, &self->xform);

    // Acquire instance resources for this control.
    texture_map* maps[1] = {&typed_state->ui_atlas};
    shader* s = shader_system_get("Shader.StandardUI");
    renderer_shader_instance_resources_acquire(s, 1, maps, &typed_data->instance_id);

    // Load up a label control to use as the text.
    if (!typed_data->content_label.load(&typed_data->content_label)) {
        KERROR("Failed to setup label within textbox.");
        return false;
    }

    if (!standard_ui_system_register_control(typed_state, &typed_data->content_label)) {
        KERROR("Unable to register control.");
    } else {
        // NOTE: Only parenting the transform, the control. This is to have control over how the
        // clipping mask is attached and drawn. See the render function for the other half of this.
        sui_label_internal_data* label_data = typed_data->content_label.internal_data;
        // TODO: Adjustable padding
        transform_position_set(&typed_data->content_label.xform, (vec3){typed_data->nslice.corner_size.x, label_data->data->line_height - 4.0f, 0.0f});
        transform_parent_set(&typed_data->content_label.xform, &self->xform);
        typed_data->content_label.is_active = true;
        if (!standard_ui_system_update_active(typed_state, &typed_data->content_label)) {
            KERROR("Unable to update active state for textbox system text.");
        }
    }

    // Load up a panel control for the cursor.
    if (!typed_data->cursor.load(&typed_data->cursor)) {
        KERROR("Failed to setup cursor within textbox.");
        return false;
    }

    // Create the cursor and attach it as a child.
    if (!standard_ui_system_register_control(typed_state, &typed_data->cursor)) {
        KERROR("Unable to register control.");
    } else {
        if (!standard_ui_system_control_add_child(typed_state, self, &typed_data->cursor)) {
            KERROR("Failed to parent textbox system text.");
        } else {
            sui_label_internal_data* label_data = typed_data->content_label.internal_data;
            // Set an initial position.
            transform_position_set(&typed_data->cursor.xform, (vec3){typed_data->nslice.corner_size.x, label_data->data->line_height - 4.0f, 0.0f});
            typed_data->cursor.is_active = true;
            if (!standard_ui_system_update_active(typed_state, &typed_data->cursor)) {
                KERROR("Unable to update active state for textbox cursor.");
            }
        }
    }

    // Ensure the cursor position is correct.
    sui_textbox_update_cursor_position(self);

    event_register(EVENT_CODE_KEY_PRESSED, self, sui_textbox_on_key);
    event_register(EVENT_CODE_KEY_RELEASED, self, sui_textbox_on_key);

    return true;
}

void sui_textbox_control_unload(struct sui_control* self) {
    //
    event_unregister(EVENT_CODE_KEY_PRESSED, self, sui_textbox_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, self, sui_textbox_on_key);
}

b8 sui_textbox_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

b8 sui_textbox_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(self, p_frame_data, render_data)) {
        return false;
    }

    // Render the nine-slice.
    sui_textbox_internal_data* typed_data = self->internal_data;
    if (typed_data->nslice.g) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = typed_data->nslice.g->material;
        renderable.render_data.vertex_count = typed_data->nslice.g->vertex_count;
        renderable.render_data.vertex_element_size = typed_data->nslice.g->vertex_element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->nslice.g->vertex_buffer_offset;
        renderable.render_data.index_count = typed_data->nslice.g->index_count;
        renderable.render_data.index_element_size = typed_data->nslice.g->index_element_size;
        renderable.render_data.index_buffer_offset = typed_data->nslice.g->index_buffer_offset;
        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = typed_data->colour;

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    // Render the content label manually so the clip mask can be attached to it.
    // This ensures the content label is rendered and clipped before the cursor or other
    // children are drawn.
    if (!typed_data->content_label.render(&typed_data->content_label, p_frame_data, render_data)) {
        KERROR("Failed to render content label for textbox '%s'", self->name);
        return false;
    }

    // Attach clipping mask to text, which would be the last element added.
    u32 renderable_count = darray_length(render_data->renderables);
    typed_data->clip_mask.render_data.model = transform_world_get(&typed_data->clip_mask.clip_xform);
    render_data->renderables[renderable_count - 1].clip_mask_render_data = &typed_data->clip_mask.render_data;

    return true;
}

const char* sui_textbox_text_get(struct sui_control* self) {
    if (!self) {
        return 0;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_label_text_get(&typed_data->content_label);
}

void sui_textbox_text_set(struct sui_control* self, const char* text) {
    if (self) {
        sui_textbox_internal_data* typed_data = self->internal_data;
        sui_label_text_set(&typed_data->content_label, text);
    }
}

void sui_textbox_on_mouse_down(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        return;

        /* sui_button_internal_data* typed_data = self->internal_data;
        typed_data->nslice.atlas_px_min.x = 151;
        typed_data->nslice.atlas_px_min.y = 21;
        typed_data->nslice.atlas_px_max.x = 158;
        typed_data->nslice.atlas_px_max.y = 28;
        update_nine_slice(&typed_data->nslice, 0); */
    }
}
void sui_textbox_on_mouse_up(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        /* // TODO: DRY
        sui_button_internal_data* typed_data = self->internal_data;
        if (self->is_hovered) {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 31;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 37;
        } else {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 31;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 37;
        }
        update_nine_slice(&typed_data->nslice, 0); */
    }
}

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    sui_control* self = listener_inst;
    standard_ui_state* typed_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);
    sui_textbox_internal_data* typed_data = self->internal_data;
    if (typed_state->focused_id != self->id.uniqueid) {
        return false;
    }
    u16 key_code = context.data.u16[0];
    if (code == EVENT_CODE_KEY_PRESSED) {
        b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);

        const char* entry_control_text = sui_label_text_get(&typed_data->content_label);
        u32 len = string_length(entry_control_text);
        if (key_code == KEY_BACKSPACE) {
            if (len > 0 && typed_data->cursor_position > 0) {
                char* str = string_duplicate(entry_control_text);
                string_remove_at(str, entry_control_text, typed_data->cursor_position - 1, 1);  // TODO: selected chars
                sui_label_text_set(&typed_data->content_label, str);
                kfree(str, len + 1, MEMORY_TAG_STRING);
                // TODO: "view scrolling" when outside box bounds.
                typed_data->cursor_position--;
                sui_textbox_update_cursor_position(self);
            }
        } else if (key_code == KEY_DELETE) {
            if (len > 0 && typed_data->cursor_position < len) {
                char* str = string_duplicate(entry_control_text);
                string_remove_at(str, entry_control_text, typed_data->cursor_position, 1);  // TODO: selected chars
                sui_label_text_set(&typed_data->content_label, str);
                kfree(str, len + 1, MEMORY_TAG_STRING);
                sui_textbox_update_cursor_position(self);
            }
        } else if (key_code == KEY_LEFT) {
            if (typed_data->cursor_position > 0) {
                typed_data->cursor_position--;
                // TODO: "view scrolling" when outside box bounds.
                sui_textbox_update_cursor_position(self);
            }
        } else if (key_code == KEY_RIGHT) {
            // NOTE: cursor position can go past the end of the str so backspacing works right.
            if (typed_data->cursor_position < len) {
                typed_data->cursor_position++;
                // TODO: "view scrolling" when outside box bounds.
                sui_textbox_update_cursor_position(self);
            }
        } else {
            // Use A-Z and 0-9 as-is.
            char char_code = key_code;
            if ((key_code >= KEY_A && key_code <= KEY_Z)) {
                // TODO: check caps lock.
                if (!shift_held) {
                    char_code = key_code + 32;
                }
            } else if ((key_code >= KEY_0 && key_code <= KEY_9)) {
                if (shift_held) {
                    // NOTE: this handles US standard keyboard layouts.
                    // Will need to handle other layouts as well.
                    switch (key_code) {
                        case KEY_0:
                            char_code = ')';
                            break;
                        case KEY_1:
                            char_code = '!';
                            break;
                        case KEY_2:
                            char_code = '@';
                            break;
                        case KEY_3:
                            char_code = '#';
                            break;
                        case KEY_4:
                            char_code = '$';
                            break;
                        case KEY_5:
                            char_code = '%';
                            break;
                        case KEY_6:
                            char_code = '^';
                            break;
                        case KEY_7:
                            char_code = '&';
                            break;
                        case KEY_8:
                            char_code = '*';
                            break;
                        case KEY_9:
                            char_code = '(';
                            break;
                    }
                }
            } else {
                switch (key_code) {
                    case KEY_SPACE:
                        char_code = key_code;
                        break;
                    case KEY_MINUS:
                        char_code = shift_held ? '_' : '-';
                        break;
                    case KEY_EQUAL:
                        char_code = shift_held ? '+' : '=';
                        break;
                    default:
                        // Not valid for entry, use 0
                        char_code = 0;
                        break;
                }
            }

            // HACK: TODO: Fix input from any position.
            if (char_code != 0) {
                const char* entry_control_text = sui_label_text_get(&typed_data->content_label);
                u32 len = string_length(entry_control_text);
                char* new_text = kallocate(len + 2, MEMORY_TAG_STRING);

                string_insert_char_at(new_text, entry_control_text, typed_data->cursor_position, char_code);
                /* string_format(new_text, "%s%c", entry_control_text, char_code); */

                sui_label_text_set(&typed_data->content_label, new_text);
                kfree(new_text, len + 2, MEMORY_TAG_STRING);
                typed_data->cursor_position++;
                sui_textbox_update_cursor_position(self);
            }
        }
    }
    if (self->on_key) {
        sui_keyboard_event evt = {0};
        evt.key = key_code;
        evt.type = code == EVENT_CODE_KEY_PRESSED ? SUI_KEYBOARD_EVENT_TYPE_PRESS : SUI_KEYBOARD_EVENT_TYPE_RELEASE;
        self->on_key(self, evt);
    }

    return false;
}
