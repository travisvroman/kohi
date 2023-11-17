#pragma once

#include <math/geometry_utils.h>
#include <renderer/renderer_types.h>

#include "standard_ui_system.h"

/*
 * TODO: Textbox items
 *
 * - The ability to hightlight text, then add/remove/overwrite highlighted text.
 */
typedef struct sui_textbox_internal_data {
    vec2i size;
    vec4 colour;
    nine_slice nslice;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;
    sui_control content_label;
    sui_control cursor;
    u32 cursor_position;
    f32 text_view_offset;
    sui_clip_mask clip_mask;
} sui_textbox_internal_data;

KAPI b8 sui_textbox_control_create(const char* name, font_type type, const char* font_name, u16 font_size, const char* text, struct sui_control* out_control);

KAPI void sui_textbox_control_destroy(struct sui_control* self);

KAPI b8 sui_textbox_control_size_set(struct sui_control* self, i32 width, i32 height);
KAPI b8 sui_textbox_control_width_set(struct sui_control* self, i32 width);
KAPI b8 sui_textbox_control_height_set(struct sui_control* self, i32 height);

KAPI b8 sui_textbox_control_load(struct sui_control* self);

KAPI void sui_textbox_control_unload(struct sui_control* self);

KAPI b8 sui_textbox_control_update(struct sui_control* self, struct frame_data* p_frame_data);

KAPI b8 sui_textbox_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data);

KAPI const char* sui_textbox_text_get(struct sui_control* self);
KAPI void sui_textbox_text_set(struct sui_control* self, const char* text);
KAPI void sui_textbox_on_mouse_down(struct sui_control* self, struct sui_mouse_event event);
KAPI void sui_textbox_on_mouse_up(struct sui_control* self, struct sui_mouse_event event);
