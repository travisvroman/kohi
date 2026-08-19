#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_textbox_control_create (kui_state *state, const char *name, font_type font_type, kname font_name, u16 font_size, const char *text, kui_textbox_type type);

KAPI void kui_textbox_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_textbox_control_size_set (kui_state *state, kui_control self, i32 width, i32 height);
KAPI b8 kui_textbox_control_width_set (kui_state *state, kui_control self, i32 width);
KAPI b8 kui_textbox_control_height_set (kui_state *state, kui_control self, i32 height);
KAPI void kui_textbox_control_colour_set (kui_state *state, kui_control self, colour4 colour);

KAPI b8 kui_textbox_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);

KAPI b8 kui_textbox_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI const char *kui_textbox_text_get (kui_state *state, kui_control self);
KAPI void kui_textbox_text_set (kui_state *state, kui_control self, const char *text);
KAPI void kui_textbox_i64_set (kui_state *state, kui_control self, i64 i);
KAPI void kui_textbox_f32_set (kui_state *state, kui_control self, f32 f);

// Deletes text at cursor position. If a highlight range exists, the entire range is deleted.
// Updates cursor position and highlight range accordingly.
KAPI void kui_textbox_delete_at_cursor (kui_state *state, kui_control self);

// Select all and set cursor to the end.
KAPI void kui_textbox_select_all (kui_state *state, kui_control self);
// Select none and set cursor to the beginning.
KAPI void kui_textbox_select_none (kui_state *state, kui_control self);
