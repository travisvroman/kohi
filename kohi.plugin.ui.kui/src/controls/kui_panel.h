#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_panel_control_create (kui_state *state, const char *name, vec2 size, vec4 colour);
KAPI void kui_panel_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_panel_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_panel_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI vec2 kui_panel_size (kui_state *state, kui_control self);
KAPI b8 kui_panel_control_resize (kui_state *state, kui_control self, vec2 new_size);
KAPI void kui_panel_set_height (kui_state *state, kui_control self, f32 height);
KAPI void kui_panel_set_width (kui_state *state, kui_control self, f32 width);
