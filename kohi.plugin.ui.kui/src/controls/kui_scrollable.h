#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_scrollable_control_create (kui_state *state, const char *name, vec2 size, b8 scroll_x, b8 scroll_y);
KAPI void kui_scrollable_control_destroy (kui_state *state, kui_control *self);

KAPI b8 kui_scrollable_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_scrollable_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI vec2 kui_scrollable_size (kui_state *state, kui_control self);
KAPI b8 kui_scrollable_control_resize (kui_state *state, kui_control self, vec2 new_size);
KAPI void kui_scrollable_set_height (kui_state *state, kui_control self, f32 height);
KAPI void kui_scrollable_set_width (kui_state *state, kui_control self, f32 width);

KAPI kui_control kui_scrollable_control_get_content_container (kui_state *state, kui_control self);
KAPI void kui_scrollable_control_scroll_y (kui_state *state, kui_control self, f32 amount);
KAPI void kui_scrollable_control_scroll_x (kui_state *state, kui_control self, f32 amount);

// Set scroll from top (0) to bottom (1)
KAPI void kui_scrollable_control_scroll_y_set (kui_state *state, kui_control self, f32 pos);
// Set scroll from left (0) to right (1)
KAPI void kui_scrollable_control_scroll_x_set (kui_state *state, kui_control self, f32 pos);

KAPI void kui_scrollable_set_content_size (kui_state *state, kui_control self, f32 width, f32 height);
KAPI b8 kui_scrollable_can_scroll_x (kui_state *state, kui_control self);
KAPI b8 kui_scrollable_can_scroll_y (kui_state *state, kui_control self);
