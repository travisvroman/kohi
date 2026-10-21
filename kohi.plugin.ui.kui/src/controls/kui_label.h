#pragma once

#include "kui_system.h"

KAPI kui_control kui_label_control_create (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text);
KAPI kui_control kui_label_control_create_ex (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text, f32 max_width, kui_label_flags flags);
KAPI void kui_label_control_destroy (kui_state *state, kui_control *self);
KAPI b8 kui_label_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_label_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, struct kui_render_data *render_data);

/**
 * @brief Sets the text on the given label object.
 *
 * @param u_text A pointer to the label whose text will be set.
 * @param text The text to be set.
 */
KAPI void kui_label_text_set (kui_state *state, kui_control self, const char *text);

KAPI const char *kui_label_text_get (kui_state *state, kui_control self);
KAPI void kui_label_colour_set (kui_state *state, kui_control self, vec4 colour);

KAPI f32 kui_label_line_height_get (kui_state *state, kui_control self);

KAPI vec2 kui_label_measure_string (kui_state *state, kui_control self);
