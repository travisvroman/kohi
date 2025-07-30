#include "sui_textbox.h"

#include <containers/darray.h>
#include <core/event.h>
#include <core/input.h>
#include <defines.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <resources/resource_types.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/kshader_system.h>
#include <systems/ktransform_system.h>

#include "../standard_ui_system.h"
#include "controls/sui_label.h"
#include "controls/sui_panel.h"
#include "standard_ui_defines.h"
#include "strings/kname.h"

static b8 sui_textbox_on_key(u16 code, void* sender, void* listener_inst, event_context context);

static f32 sui_textbox_calculate_cursor_offset(standard_ui_state* state, u32 string_pos, const char* full_string, sui_textbox_internal_data* internal_data) {
    if (string_pos == 0) {
        return 0;
    }

    char* copy = string_duplicate(full_string);
    char* mid_target = copy;
    string_mid(mid_target, full_string, 0, string_pos);

    vec2 size = vec2_zero();
    sui_label_internal_data* label_data = ((sui_label_internal_data*)internal_data->content_label.internal_data);
    if (label_data->type == FONT_TYPE_BITMAP) {
        font_system_bitmap_font_measure_string(state->font_system, label_data->bitmap_font, mid_target, &size);
    } else if (label_data->type == FONT_TYPE_SYSTEM) {
        font_system_system_font_measure_string(state->font_system, label_data->system_font, mid_target, &size);
    } else {
        KFATAL("hwhat");
    }

    // Make sure to cleanup the string.
    string_free(copy);

    // Use the x-axis of the mesurement to place the cursor.
    return size.x;
}

static void sui_textbox_update_highlight_box(standard_ui_state* state, sui_control* self) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    sui_label_internal_data* label_data = typed_data->content_label.internal_data;

    if (typed_data->highlight_range.size == 0) {
        typed_data->highlight_box.is_visible = false;
        return;
    }

    typed_data->highlight_box.is_visible = true;

    // Offset from the start of the string.
    f32 offset_start = sui_textbox_calculate_cursor_offset(state, typed_data->highlight_range.offset, label_data->text, self->internal_data);
    f32 offset_end = sui_textbox_calculate_cursor_offset(state, typed_data->highlight_range.offset + typed_data->highlight_range.size, label_data->text, self->internal_data);
    f32 width = offset_end - offset_start;
    /* f32 padding = typed_data->nslice.corner_size.x; */

    vec3 initial_pos = ktransform_position_get(typed_data->highlight_box.ktransform);
    initial_pos.y = -typed_data->label_line_height + 10.0f;
    ktransform_position_set(typed_data->highlight_box.ktransform, (vec3){offset_start, initial_pos.y, initial_pos.z});
    ktransform_scale_set(typed_data->highlight_box.ktransform, (vec3){width, 1.0f, 1.0f});
}

static void sui_textbox_update_cursor_position(standard_ui_state* state, sui_control* self) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    sui_label_internal_data* label_data = typed_data->content_label.internal_data;

    // Offset from the start of the string.
    f32 offset = sui_textbox_calculate_cursor_offset(state, typed_data->cursor_position, label_data->text, self->internal_data);
    f32 padding = typed_data->nslice.corner_size.x;

    // The would-be cursor position, not yet taking padding into account.
    vec3 cursor_pos = {0};
    cursor_pos.x = offset + typed_data->text_view_offset;
    cursor_pos.y = 6.0f; // TODO: configurable

    // Ensure the cursor is within the bounds of the textbox.
    // Don't take the padding into account just yet.
    f32 clip_width = typed_data->size.x - (padding * 2);
    f32 clip_x_min = padding;
    f32 clip_x_max = clip_x_min + clip_width;
    f32 diff = 0;
    if (cursor_pos.x > clip_width) {
        diff = clip_width - cursor_pos.x;
        // Set the cursor right up against the edge, taking padding into account.
        cursor_pos.x = clip_x_max;
    } else if (cursor_pos.x < 0) {
        diff = 0 - cursor_pos.x;
        // Set the cursor right up against the edge, taking padding into account.
        cursor_pos.x = clip_x_min;
    } else {
        // Use the position as-is, but add padding.
        cursor_pos.x += padding;
    }
    // Save the view offset.
    typed_data->text_view_offset += diff;
    // Translate the label forward/backward to line up with the cursor, taking padding into account.
    vec3 label_pos = ktransform_position_get(typed_data->content_label.ktransform);
    ktransform_position_set(typed_data->content_label.ktransform, (vec3){padding + typed_data->text_view_offset, label_pos.y, label_pos.z});

    // Translate the cursor to it's new position.
    ktransform_position_set(typed_data->cursor.ktransform, cursor_pos);
}

b8 sui_textbox_control_create(standard_ui_state* state, const char* name, font_type type, kname font_name, u16 font_size, const char* text, struct sui_control* out_control) {
    if (!sui_base_control_create(state, name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_textbox_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_textbox_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->size = (vec2i){200, font_size + 10}; // add padding
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

    char* buffer = string_format("%s_textbox_internal_label", name);
    if (!sui_label_control_create(state, buffer, type, font_name, font_size, text, &typed_data->content_label)) {
        KERROR("Failed to create internal label control for textbox. Textbox creation failed.");
        string_free(buffer);
        return false;
    }
    string_free(buffer);
    typed_data->label_line_height = sui_label_line_height_get(state, &typed_data->content_label);

    // Use a panel as the cursor.
    buffer = string_format("%s_textbox_cursor_panel", name);
    if (!sui_panel_control_create(state, buffer, (vec2){1.0f, font_size - 4.0f}, (vec4){1.0f, 1.0f, 1.0f, 1.0f}, &typed_data->cursor)) {
        KERROR("Failed to create internal cursor control for textbox. Textbox creation failed.");
        string_free(buffer);
        return false;
    }
    string_free(buffer);

    // Highlight box.
    buffer = string_format("%s_textbox_highlight_panel", name);
    if (!sui_panel_control_create(state, buffer, (vec2){1.0f, font_size}, (vec4){0.0f, 0.5f, 0.9f, 0.5f}, &typed_data->highlight_box)) {
        KERROR("Failed to create internal highlight box control for textbox. Textbox creation failed.");
        string_free(buffer);
        return false;
    }
    string_free(buffer);

    // HACK: Storing a pointer to the system state here, since the UI system can only pass a
    // single pointer which is already occupied by "self". This needs to be rethought.
    typed_data->state = state;

    return true;
}

void sui_textbox_control_destroy(standard_ui_state* state, struct sui_control* self) {
    sui_base_control_destroy(state, self);
}

b8 sui_textbox_control_size_set(standard_ui_state* state, struct sui_control* self, i32 width, i32 height) {
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

    nine_slice_update(&typed_data->nslice, 0);

    return true;
}
b8 sui_textbox_control_width_set(standard_ui_state* state, struct sui_control* self, i32 width) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_textbox_control_size_set(state, self, width, typed_data->size.y);
}
b8 sui_textbox_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height) {
    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_textbox_control_size_set(state, self, typed_data->size.x, height);
}

b8 sui_textbox_control_load(standard_ui_state* state, struct sui_control* self) {
    if (!sui_base_control_load(state, self)) {
        return false;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;

    // HACK: TODO: remove hardcoded stuff.
    /* vec2i atlas_size = (vec2i){state->ui_atlas.texture->width, state->ui_atlas.texture->height}; */
    vec2i atlas_size = (vec2i){512, 512};
    vec2i atlas_min = (vec2i){180, 31};
    vec2i atlas_max = (vec2i){193, 43};
    vec2i corner_px_size = (vec2i){3, 3};
    vec2i corner_size = (vec2i){10, 10};
    // NOTE: Also uploads to the GPU.
    if (!nine_slice_create(self->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice)) {
        KERROR("Failed to generate nine slice.");
        return false;
    }

    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = typed_data->size.x;
    self->bounds.height = typed_data->size.y;

    // Setup textbox clipping mask geometry.
    typed_data->clip_mask.reference_id = 1; // TODO: move creation/reference_id assignment.

    kgeometry quad = geometry_generate_quad(typed_data->size.x - (corner_size.x * 2), typed_data->size.y, 0, 0, 0, 0, kname_create("textbox_clipping_box"));
    if (!renderer_geometry_upload(&quad)) {
        KERROR("sui_textbox_control_load - Failed to upload geometry quad");
        return false;
    }

    typed_data->clip_mask.render_data.model = mat4_identity();
    // FIXME: Convert this to generate just verts/indices, and upload via the new
    // renderer api functions instead of deprecated geometry functions.
    typed_data->clip_mask.render_data.unique_id = typed_data->clip_mask.reference_id;

    typed_data->clip_mask.render_data.vertex_count = typed_data->clip_mask.clip_geometry.vertex_count;
    typed_data->clip_mask.render_data.vertex_element_size = typed_data->clip_mask.clip_geometry.vertex_element_size;
    typed_data->clip_mask.render_data.vertex_buffer_offset = typed_data->clip_mask.clip_geometry.vertex_buffer_offset;

    typed_data->clip_mask.render_data.index_count = typed_data->clip_mask.clip_geometry.index_count;
    typed_data->clip_mask.render_data.index_element_size = typed_data->clip_mask.clip_geometry.index_element_size;
    typed_data->clip_mask.render_data.index_buffer_offset = typed_data->clip_mask.clip_geometry.index_buffer_offset;

    typed_data->clip_mask.render_data.diffuse_colour = vec4_zero(); // transparent;

    typed_data->clip_mask.clip_ktransform = ktransform_from_position((vec3){corner_size.x, 0.0f, 0.0f});

    // Acquire group resources for this control.
    kshader sui_shader = kshader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));

    if (!kshader_system_shader_group_acquire(sui_shader, &typed_data->group_id)) {
        KFATAL("Unable to acquire shader group resources for textbox.");
        return false;
    }
    typed_data->group_generation = INVALID_ID_U16;

    // Also acquire per-draw resources.
    if (!kshader_system_shader_per_draw_acquire(sui_shader, &typed_data->draw_id)) {
        KFATAL("Unable to acquire shader per-draw resources for textbox.");
        return false;
    }
    typed_data->draw_generation = INVALID_ID_U16;

    // Load up a label control to use as the text.
    if (!typed_data->content_label.load(state, &typed_data->content_label)) {
        KERROR("Failed to setup label within textbox.");
        return false;
    }

    if (!standard_ui_system_register_control(state, &typed_data->content_label)) {
        KERROR("Unable to register control.");
    } else {
        // NOTE: Only parenting the transform, the control. This is to have control over how the
        // clipping mask is attached and drawn. See the render function for the other half of this.
        // TODO: Adjustable padding
        typed_data->content_label.parent = self;
        ktransform_position_set(typed_data->content_label.ktransform, (vec3){typed_data->nslice.corner_size.x, typed_data->label_line_height - 5.0f, 0.0f}); // padding/2 for y
        typed_data->content_label.is_active = true;
        if (!standard_ui_system_update_active(state, &typed_data->content_label)) {
            KERROR("Unable to update active state for textbox system text.");
        }
    }

    // Load up a panel control for the cursor.
    if (!typed_data->cursor.load(state, &typed_data->cursor)) {
        KERROR("Failed to setup cursor within textbox.");
        return false;
    }

    // Create the cursor and attach it as a child.
    if (!standard_ui_system_register_control(state, &typed_data->cursor)) {
        KERROR("Unable to register control.");
    } else {
        if (!standard_ui_system_control_add_child(state, self, &typed_data->cursor)) {
            KERROR("Failed to parent textbox system text.");
        } else {
            // Set an initial position.
            ktransform_position_set(typed_data->cursor.ktransform, (vec3){typed_data->nslice.corner_size.x, typed_data->label_line_height - 4.0f, 0.0f});
            typed_data->cursor.is_active = true;
            if (!standard_ui_system_update_active(state, &typed_data->cursor)) {
                KERROR("Unable to update active state for textbox cursor.");
            }
        }
    }

    // Ensure the cursor position is correct.
    sui_textbox_update_cursor_position(state, self);

    // Load up a panel control for the highlight box.
    if (!typed_data->cursor.load(state, &typed_data->highlight_box)) {
        KERROR("Failed to setup highlight box within textbox.");
        return false;
    }

    // Create the highlight box and attach it as a child.
    if (!standard_ui_system_register_control(state, &typed_data->highlight_box)) {
        KERROR("Unable to register control.");
    } else {
        // NOTE: Only parenting the transform, the control. This is to have control over how the
        // clipping mask is attached and drawn. See the render function for the other half of this.

        // Set an initial position.
        ktransform_position_set(typed_data->highlight_box.ktransform, (vec3){typed_data->nslice.corner_size.x, typed_data->label_line_height - 4.0f, 0.0f});
        typed_data->highlight_box.is_active = true;
        typed_data->highlight_box.is_visible = false;
        /* typed_data->highlight_box.parent = self; */
        if (!standard_ui_system_update_active(state, &typed_data->highlight_box)) {
            KERROR("Unable to update active state for textbox highlight box.");
        }
    }

    // Ensure the highlight box size and position is correct.
    sui_textbox_update_highlight_box(state, self);

    event_register(EVENT_CODE_KEY_PRESSED, self, sui_textbox_on_key);
    event_register(EVENT_CODE_KEY_RELEASED, self, sui_textbox_on_key);

    return true;
}

void sui_textbox_control_unload(standard_ui_state* state, struct sui_control* self) {
    // TODO: unload sub-controls that aren't children (i.e content_label and highlight_box)
    event_unregister(EVENT_CODE_KEY_PRESSED, self, sui_textbox_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, self, sui_textbox_on_key);
}

b8 sui_textbox_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(state, self, p_frame_data)) {
        return false;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;
    mat4 parent_world = ktransform_world_get(self->ktransform);

    // Update clip mask ktransform
    ktransform_calculate_local(typed_data->clip_mask.clip_ktransform);
    mat4 local = ktransform_local_get(typed_data->clip_mask.clip_ktransform);
    mat4 self_world = mat4_mul(local, parent_world);
    ktransform_world_set(typed_data->clip_mask.clip_ktransform, self_world);

    // Update highlight box ktransform
    // FIXME: The transform of this highlight box is wrong.
    ktransform_calculate_local(typed_data->highlight_box.ktransform);
    mat4 hb_local = ktransform_local_get(typed_data->highlight_box.ktransform);
    mat4 hb_world = mat4_mul(hb_local, parent_world);
    ktransform_world_set(typed_data->highlight_box.ktransform, hb_world);
    return true;
}

b8 sui_textbox_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
        return false;
    }

    // Render the nine-slice.
    sui_textbox_internal_data* typed_data = self->internal_data;
    if (typed_data->nslice.vertex_data.elements) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.vertex_count = typed_data->nslice.vertex_data.element_count;
        renderable.render_data.vertex_element_size = typed_data->nslice.vertex_data.element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->nslice.vertex_data.buffer_offset;
        renderable.render_data.index_count = typed_data->nslice.index_data.element_count;
        renderable.render_data.index_element_size = typed_data->nslice.index_data.element_size;
        renderable.render_data.index_buffer_offset = typed_data->nslice.index_data.buffer_offset;
        renderable.render_data.model = ktransform_world_get(self->ktransform);
        renderable.render_data.diffuse_colour = typed_data->colour;

        renderable.group_id = &typed_data->group_id;
        renderable.per_draw_id = &typed_data->draw_id;

        darray_push(render_data->renderables, renderable);
    }

    // Render the content label manually so the clip mask can be attached to it.
    // This ensures the content label is rendered and clipped before the cursor or other
    // children are drawn.
    if (!typed_data->content_label.render(state, &typed_data->content_label, p_frame_data, render_data)) {
        KERROR("Failed to render content label for textbox '%s'", self->name);
        return false;
    }

    // Only attach clipping mask if the content label actually has... content.
    if (string_utf8_length(sui_label_text_get(state, &typed_data->content_label))) {
        // Attach clipping mask to text, which would be the last element added.
        u32 renderable_count = darray_length(render_data->renderables);
        typed_data->clip_mask.render_data.model = ktransform_world_get(typed_data->clip_mask.clip_ktransform);
        render_data->renderables[renderable_count - 1].clip_mask_render_data = &typed_data->clip_mask.render_data;
    }

    // Only perform highlight_box logic if it is visible.
    if (typed_data->highlight_box.is_visible) {
        // Render the highlight box manually so the clip mask can be attached to it.
        // This ensures the highlight boxis rendered and clipped before the cursor or other
        // children are drawn.
        if (!typed_data->highlight_box.render(state, &typed_data->highlight_box, p_frame_data, render_data)) {
            KERROR("Failed to render highlight box for textbox '%s'", self->name);
            return false;
        }

        // Attach clipping mask to text, which would be the last element added.
        /* u32 renderable_count = darray_length(render_data->renderables);
        render_data->renderables[renderable_count - 1].clip_mask_render_data = &typed_data->clip_mask.render_data; */
    }

    return true;
}

const char* sui_textbox_text_get(standard_ui_state* state, struct sui_control* self) {
    if (!self) {
        return 0;
    }

    sui_textbox_internal_data* typed_data = self->internal_data;
    return sui_label_text_get(state, &typed_data->content_label);
}

void sui_textbox_text_set(standard_ui_state* state, struct sui_control* self, const char* text) {
    if (self) {
        sui_textbox_internal_data* typed_data = self->internal_data;
        sui_label_text_set(state, &typed_data->content_label, text);

        // Reset the cursor position when the text is set.
        typed_data->cursor_position = 0;
        sui_textbox_update_cursor_position(state, self);
    }
}

void sui_textbox_on_mouse_down(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
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
void sui_textbox_on_mouse_up(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
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
    sui_textbox_internal_data* typed_data = self->internal_data;
    standard_ui_state* state = typed_data->state;
    if (state->focused_id != self->id.uniqueid) {
        return false;
    }

    u16 key_code = context.data.u16[0];
    if (code == EVENT_CODE_KEY_PRESSED) {
        b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);
        b8 ctrl_held = input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL) || input_is_key_down(KEY_CONTROL);

        const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
        u32 len = string_length(entry_control_text);
        if (key_code == KEY_BACKSPACE) {
            if (len == 0) {
                sui_label_text_set(state, &typed_data->content_label, "");
            } else if ((typed_data->cursor_position > 0 || typed_data->highlight_range.size > 0)) {
                char* str = string_duplicate(entry_control_text);
                if (typed_data->highlight_range.size > 0) {
                    if (typed_data->highlight_range.size == (i32)len) {
                        str = string_empty(str);
                        typed_data->cursor_position = 0;
                    } else {
                        string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
                        typed_data->cursor_position = typed_data->highlight_range.offset;
                    }
                    // Clear the highlight range.
                    typed_data->highlight_range.offset = 0;
                    typed_data->highlight_range.size = 0;
                    sui_textbox_update_highlight_box(state, self);
                } else {
                    string_remove_at(str, entry_control_text, typed_data->cursor_position - 1, 1);
                    typed_data->cursor_position--;
                }
                sui_label_text_set(state, &typed_data->content_label, str);
                string_free(str);
                sui_textbox_update_cursor_position(state, self);
            }
        } else if (key_code == KEY_DELETE) {
            if (len == 0) {
                sui_label_text_set(state, &typed_data->content_label, "");
            } else if (typed_data->cursor_position == len && typed_data->highlight_range.size == (i32)len) {
                char* str = string_duplicate(entry_control_text);
                str = string_empty(str);
                typed_data->cursor_position = 0;
                // Clear the highlight range.
                typed_data->highlight_range.offset = 0;
                typed_data->highlight_range.size = 0;
                sui_textbox_update_highlight_box(state, self);
                sui_label_text_set(state, &typed_data->content_label, str);
                string_free(str);
                sui_textbox_update_cursor_position(state, self);
            } else if (typed_data->cursor_position <= len) {
                char* str = string_duplicate(entry_control_text);
                if (typed_data->highlight_range.size > 0) {
                    string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
                    typed_data->cursor_position = typed_data->highlight_range.offset;
                    // Clear the highlight range.
                    typed_data->highlight_range.offset = 0;
                    typed_data->highlight_range.size = 0;
                    sui_textbox_update_highlight_box(state, self);
                } else {
                    string_remove_at(str, entry_control_text, typed_data->cursor_position, 1);
                }
                sui_label_text_set(state, &typed_data->content_label, str);
                string_free(str);
                sui_textbox_update_cursor_position(state, self);
            }
        } else if (key_code == KEY_LEFT) {
            if (typed_data->cursor_position > 0) {
                if (shift_held) {
                    if (typed_data->highlight_range.size == 0) {
                        typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
                    }
                    if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset) {
                        typed_data->highlight_range.offset--;
                        typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
                    } else {
                        typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
                    }
                    typed_data->cursor_position--;
                } else {
                    if (typed_data->highlight_range.size > 0) {
                        typed_data->cursor_position = typed_data->highlight_range.offset;
                    } else {
                        typed_data->cursor_position--;
                    }
                    typed_data->highlight_range.offset = 0;
                    typed_data->highlight_range.size = 0;
                }
                sui_textbox_update_highlight_box(state, self);
                sui_textbox_update_cursor_position(state, self);
            }
        } else if (key_code == KEY_RIGHT) {
            // NOTE: cursor position can go past the end of the str so backspacing works right.
            if (typed_data->cursor_position < len) {
                if (shift_held) {
                    if (typed_data->highlight_range.size == 0) {
                        typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
                    }
                    if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset + typed_data->highlight_range.size) {
                        typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
                    } else {
                        typed_data->highlight_range.offset = KCLAMP(typed_data->highlight_range.offset + 1, 0, (i32)len);
                        typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
                    }
                    typed_data->cursor_position++;
                } else {
                    if (typed_data->highlight_range.size > 0) {
                        typed_data->cursor_position = typed_data->highlight_range.offset + typed_data->highlight_range.size;
                    } else {
                        typed_data->cursor_position++;
                    }
                    typed_data->highlight_range.offset = 0;
                    typed_data->highlight_range.size = 0;
                }

                sui_textbox_update_highlight_box(state, self);
                sui_textbox_update_cursor_position(state, self);
            }
        } else if (key_code == KEY_HOME) {
            if (shift_held) {
                typed_data->highlight_range.offset = 0;
                typed_data->highlight_range.size = typed_data->cursor_position;
            } else {
                typed_data->highlight_range.offset = 0;
                typed_data->highlight_range.size = 0;
            }
            typed_data->cursor_position = 0;
            sui_textbox_update_highlight_box(state, self);
            sui_textbox_update_cursor_position(state, self);
        } else if (key_code == KEY_END) {
            if (shift_held) {
                typed_data->highlight_range.offset = typed_data->cursor_position;
                typed_data->highlight_range.size = len - typed_data->cursor_position;
            } else {
                typed_data->highlight_range.offset = 0;
                typed_data->highlight_range.size = 0;
            }
            typed_data->cursor_position = len;
            sui_textbox_update_highlight_box(state, self);
            sui_textbox_update_cursor_position(state, self);
        } else {
            // Use A-Z and 0-9 as-is.
            char char_code = key_code;
            if ((key_code >= KEY_A && key_code <= KEY_Z)) {
                if (ctrl_held) {
                    if (key_code == KEY_A) {
                        char_code = 0;
                        // Select all and set cursor to the end.
                        typed_data->highlight_range.size = len;
                        typed_data->highlight_range.offset = 0;
                        typed_data->cursor_position = len;
                        sui_textbox_update_highlight_box(state, self);
                        sui_textbox_update_cursor_position(state, self);
                    }
                }
                // TODO: check caps lock.
                if (!shift_held && !ctrl_held) {
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
                case KEY_PERIOD:
                    char_code = shift_held ? '>' : '.';
                    break;
                case KEY_COMMA:
                    char_code = shift_held ? '<' : ',';
                    break;
                case KEY_SLASH:
                    char_code = shift_held ? '?' : '/';
                    break;
                case KEY_QUOTE:
                    char_code = shift_held ? '"' : '\'';
                    break;
                case KEY_SEMICOLON:
                    char_code = shift_held ? ':' : ';';
                    break;

                default:
                    // Not valid for entry, use 0
                    char_code = 0;
                    break;
                }
            }

            if (char_code != 0) {
                const char* entry_control_text = sui_label_text_get(state, &typed_data->content_label);
                u32 len = string_length(entry_control_text);
                char* str = kallocate(sizeof(char) * (len + 2), MEMORY_TAG_STRING);

                // If text is highlighted, delete highlighted text, then insert at cursor position.
                if (typed_data->highlight_range.size > 0) {
                    if (typed_data->highlight_range.size == (i32)len) {
                        str = string_empty(str);
                        typed_data->cursor_position = 0;
                    } else {
                        string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
                        typed_data->cursor_position = typed_data->highlight_range.offset;
                    }
                } else {
                    string_copy(str, entry_control_text);
                }

                string_insert_char_at(str, str, typed_data->cursor_position, char_code);
                /* string_format_unsafe(str, "%s%c", entry_control_text, char_code); */

                sui_label_text_set(state, &typed_data->content_label, str);
                kfree(str, len + 2, MEMORY_TAG_STRING);
                if (typed_data->highlight_range.size > 0) {
                    // Clear the highlight range.
                    typed_data->highlight_range.offset = 0;
                    typed_data->highlight_range.size = 0;
                    sui_textbox_update_highlight_box(state, self);
                } else {
                    typed_data->cursor_position++;
                }
                sui_textbox_update_cursor_position(state, self);
            }
        }
    }
    if (self->on_key) {
        sui_keyboard_event evt = {0};
        evt.key = key_code;
        evt.type = code == EVENT_CODE_KEY_PRESSED ? SUI_KEYBOARD_EVENT_TYPE_PRESS : SUI_KEYBOARD_EVENT_TYPE_RELEASE;
        self->on_key(state, self, evt);
    }

    return false;
}
