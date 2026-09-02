#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_flow_control_create (kui_state *state, const char *name, vec2 size);

KAPI kui_control kui_flow_control_create_with_options (
	kui_state *state,
	const char *name,
	vec2 size,
	kui_flow_horizontal horizontal,
	kui_flow_vertical vertical,
	kui_flow_overflow overflow);

KAPI void kui_flow_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_flow_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_flow_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
