#pragma once

#include "kui_system.h"
#include "kui_types.h"

#include <systems/font_system.h>

typedef void (*PFN_kui_window_callback)(kui_state *state, kui_control self, i32 result, void *user_data);

KAPI kui_control kui_window_control_create (kui_state *state, const char *name, kui_window_flags flags);
KAPI kui_control kui_window_control_create_with_title (kui_state *state, const char *name, kui_font_data font, const char *title, kui_window_flags flags);
KAPI kui_control kui_window_control_create_dialog (kui_state *state, const char *name, kui_font_data font, const char *title, const char *body, kui_window_flags flags);
KAPI void kui_window_control_destroy (kui_state *state, kui_control *self);

KAPI void kui_window_open (kui_state *state, kui_control self);
// NOTE: does not destroy state/window, only essentially hides it and makes close callback.
KAPI void kui_window_close (kui_state *state, kui_control self);

KAPI void kui_window_set_on_opened (kui_state *state, kui_control self, PFN_kui_window_callback callback, void *user_data);
KAPI void kui_window_set_on_closed (kui_state *state, kui_control self, PFN_kui_window_callback callback, void *user_data);

KAPI b8 kui_window_control_height_set (kui_state *state, kui_control self, i32 height);
KAPI b8 kui_window_control_width_set (kui_state *state, kui_control self, i32 width);
KAPI b8 kui_window_control_title_set (kui_state *state, kui_control self, const char *text);
KAPI const char *kui_window_control_title_get (kui_state *state, const kui_control self);

KAPI b8 kui_window_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_window_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
