#pragma once

#include "kui_system.h"
#include "kui_types.h"

#include <systems/font_system.h>

KAPI kui_control kui_button_control_create (kui_state *state, const char *name);
KAPI kui_control kui_button_control_create_uparrow (kui_state *state, const char *name);
KAPI kui_control kui_button_control_create_downarrow (kui_state *state, const char *name);
KAPI kui_control kui_button_control_create_with_text (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text_content);
KAPI void kui_button_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_button_control_height_set (kui_state *state, kui_control self, i32 height);
KAPI b8 kui_button_control_width_set (kui_state *state, kui_control self, i32 width);
KAPI b8 kui_button_control_text_set (kui_state *state, kui_control self, const char *text);
KAPI const char *kui_button_control_text_get (kui_state *state, const kui_control self);

KAPI b8 kui_button_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_button_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
