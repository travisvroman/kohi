#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_checkbox_control_create (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text);
KAPI void kui_checkbox_control_destroy (kui_state *state, kui_control *self);

KAPI void kui_checkbox_set_checked (kui_state *state, kui_control self, b8 checked);
KAPI b8 kui_checkbox_get_checked (kui_state *state, kui_control self);
KAPI void kui_checkbox_set_on_checked (kui_state *state, kui_control self, PFN_checkbox_event_callback callback);

KAPI b8 kui_checkbox_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_checkbox_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
