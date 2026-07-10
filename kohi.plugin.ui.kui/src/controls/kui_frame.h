#pragma once

#include "kui_system.h"
#include "kui_types.h"
#include "utils/kcolour.h"

KAPI kui_control kui_frame_control_create (kui_state *state, const char *name);

KAPI void kui_frame_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_frame_control_size_set (kui_state *state, kui_control self, i32 width, i32 height);
KAPI b8 kui_frame_control_width_set (kui_state *state, kui_control self, i32 width);
KAPI b8 kui_frame_control_height_set (kui_state *state, kui_control self, i32 height);
KAPI void kui_frame_control_colour_set (kui_state *state, kui_control self, colour4 colour);

KAPI b8 kui_frame_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);

KAPI b8 kui_frame_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI const char *kui_frame_text_get (kui_state *state, kui_control self);
KAPI void kui_frame_text_set (kui_state *state, kui_control self, const char *text);
KAPI void kui_frame_i64_set (kui_state *state, kui_control self, i64 i);
KAPI void kui_frame_f32_set (kui_state *state, kui_control self, f32 f);

// Deletes text at cursor position. If a highlight range exists, the entire range is deleted.
// Updates cursor position and highlight range accordingly.
KAPI void kui_frame_delete_at_cursor (kui_state *state, kui_control self);

// Select all and set cursor to the end.
KAPI void kui_frame_select_all (kui_state *state, kui_control self);
// Select none and set cursor to the beginning.
KAPI void kui_frame_select_none (kui_state *state, kui_control self);
