#pragma once

#include "kui_system.h"
#include "kui_types.h"

KAPI kui_control kui_tree_item_control_create (
	kui_state *state,
	const char *name,
	u16 initial_width,
	font_type type,
	kname font_name,
	u16 font_size,
	const char *text,
	b8 show_toggle_button);

#define KUI_TREE_ITEM_HEIGHT 40.0f

KAPI void kui_tree_item_control_destroy (kui_state *state, kui_control *self);
KAPI b8 kui_tree_item_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_tree_item_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);

KAPI void kui_tree_item_control_width_set (kui_state *state, kui_control self, u16 width);

KAPI void kui_tree_item_text_set (kui_state *state, kui_control self, const char *text);
KAPI const char *kui_tree_item_text_get (kui_state *state, kui_control self);

KAPI u64 kui_tree_item_context_get (kui_state *state, kui_control self);
KAPI void kui_tree_item_context_set (kui_state *state, kui_control self, u64 context);

KAPI void kui_tree_item_set_on_expanded (kui_state *state, kui_control self, PFN_mouse_event_callback callback);
KAPI void kui_tree_item_set_on_collapsed (kui_state *state, kui_control self, PFN_mouse_event_callback callback);
