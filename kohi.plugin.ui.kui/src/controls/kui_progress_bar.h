#pragma once

#include "kui_system.h"
#include "kui_types.h"

/** @brief A bar that fills from the left as its value goes from 0 to 1. Not interactive. */
KAPI kui_control kui_progress_bar_create (kui_state *state, const char *name, vec2 size, vec4 track_colour, vec4 fill_colour);
KAPI void kui_progress_bar_destroy (kui_state *state, kui_control *self);

/** @brief Sets the fill, clamped to 0..1. */
KAPI void kui_progress_bar_value_set (kui_state *state, kui_control self, f32 value);
KAPI f32 kui_progress_bar_value_get (kui_state *state, kui_control self);
KAPI void kui_progress_bar_colours_set (kui_state *state, kui_control self, vec4 track_colour, vec4 fill_colour);

KAPI b8 kui_progress_bar_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_progress_bar_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
