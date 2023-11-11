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
#include <systems/shader_system.h>

#include "sui_label.h"

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context);

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

    return sui_label_control_create("testbed_UTF_test_sys_text", type, font_name, font_size, text, &typed_data->content_label);
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

    // Acquire instance resources for this control.
    texture_map* maps[1] = {&typed_state->ui_atlas};
    shader* s = shader_system_get("Shader.StandardUI");
    renderer_shader_instance_resources_acquire(s, 1, maps, &typed_data->instance_id);

    if (!sui_label_control_load(&typed_data->content_label)) {
        KERROR("Failed to setup label within textbox.");
        return false;
    }

    if (!standard_ui_system_register_control(typed_state, &typed_data->content_label)) {
        KERROR("Unable to register control.");
    } else {
        if (!standard_ui_system_control_add_child(typed_state, self, &typed_data->content_label)) {
            KERROR("Failed to parent textbox system text.");
        } else {
            sui_label_internal_data* label_data = typed_data->content_label.internal_data;
            // TODO: Adjustable padding
            transform_position_set(&typed_data->content_label.xform, (vec3){typed_data->nslice.corner_size.x, label_data->data->line_height - 4.0f, 0.0f});
            typed_data->content_label.is_active = true;
            if (!standard_ui_system_update_active(typed_state, &typed_data->content_label)) {
                KERROR("Unable to update active state for textbox system text.");
            }
        }
    }

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

    return sui_label_control_render(&typed_data->content_label, p_frame_data, render_data);
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

        if (key_code == KEY_BACKSPACE) {
            const char* entry_control_text = sui_label_text_get(&typed_data->content_label);
            u32 len = string_length(entry_control_text);
            if (len > 0) {
                char* str = string_duplicate(entry_control_text);
                str[len - 1] = 0;
                sui_label_text_set(&typed_data->content_label, str);
                kfree(str, len + 1, MEMORY_TAG_STRING);
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
                string_format(new_text, "%s%c", entry_control_text, char_code);
                sui_label_text_set(&typed_data->content_label, new_text);
                kfree(new_text, len + 1, MEMORY_TAG_STRING);
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
