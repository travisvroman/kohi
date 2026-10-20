#include "kui_progress_bar.h"

#include "controls/kui_panel.h"
#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/kmath.h"
#include "strings/kstring.h"

static kui_control child_panel_create (kui_state *state, kui_control parent, const char *name, const char *suffix, vec2 size, vec4 colour) {
	char *child_name = string_format("%s_%s", name, suffix);
	kui_control panel = kui_panel_control_create(state, child_name, size, colour);
	string_free(child_name);
	kui_system_control_add_child(state, parent, panel);
	// The bar, not its parts, receives the mouse.
	kui_control_set_flag(state, panel, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	return panel;
}

kui_control kui_progress_bar_create (kui_state *state, const char *name, vec2 size, vec4 track_colour, vec4 fill_colour) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_PROGRESS_BAR);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_progress_bar_control *typed_data = (kui_progress_bar_control *)base;

	base->bounds = vec4_create(0, 0, size.x, size.y);
	base->destroy = kui_progress_bar_destroy;
	base->update = kui_progress_bar_update;
	base->render = kui_progress_bar_render;

	typed_data->track = child_panel_create(state, handle, name, "track", size, track_colour);
	typed_data->fill = child_panel_create(state, handle, name, "fill", (vec2){0.0f, size.y}, fill_colour);
	return handle;
}

void kui_progress_bar_destroy (kui_state *state, kui_control *self) {
	kui_base_control_destroy(state, self);
}

void kui_progress_bar_value_set (kui_state *state, kui_control self, f32 value) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_progress_bar_control *typed_data = (kui_progress_bar_control *)base;

	typed_data->value = KCLAMP(value, 0.0f, 1.0f);
	kui_panel_set_width(state, typed_data->fill, base->bounds.width * typed_data->value);
}

f32 kui_progress_bar_value_get (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	return ((kui_progress_bar_control *)base)->value;
}

void kui_progress_bar_colours_set (kui_state *state, kui_control self, vec4 track_colour, vec4 fill_colour) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_progress_bar_control *typed_data = (kui_progress_bar_control *)base;

	((kui_panel_control *)kui_system_get_base(state, typed_data->track))->colour = track_colour;
	((kui_panel_control *)kui_system_get_base(state, typed_data->fill))->colour = fill_colour;
}

b8 kui_progress_bar_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	return kui_base_control_update(state, self, p_frame_data);
}

b8 kui_progress_bar_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}
