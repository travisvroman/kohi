#include "kui_slider.h"

#include "controls/kui_panel.h"
#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/kmath.h"
#include "strings/kstring.h"

#define SLIDER_TRACK_COLOUR ((vec4){0.15f, 0.15f, 0.18f, 1.0f})
#define SLIDER_FILL_COLOUR ((vec4){0.25f, 0.55f, 0.85f, 1.0f})
#define SLIDER_HANDLE_COLOUR ((vec4){0.9f, 0.9f, 0.95f, 1.0f})

static b8 on_mouse_down (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_mouse_drag (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static void value_from_local_x (kui_state *state, kui_control self, i16 local_x);
static void layout (kui_state *state, kui_slider_control *typed_data);
static f32 snap (const kui_slider_control *typed_data, f32 value);

static kui_control child_panel_create (kui_state *state, kui_control parent, const char *name, const char *suffix, vec2 size, vec4 colour) {
	char *child_name = string_format("%s_%s", name, suffix);
	kui_control panel = kui_panel_control_create(state, child_name, size, colour);
	string_free(child_name);
	kui_system_control_add_child(state, parent, panel);
	// The slider, not its parts, receives the mouse.
	kui_control_set_flag(state, panel, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	return panel;
}

kui_control kui_slider_create (kui_state *state, const char *name, vec2 size, f32 min, f32 max, f32 step, f32 value) {
	KASSERT(max > min);
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_SLIDER);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_slider_control *typed_data = (kui_slider_control *)base;

	base->bounds = vec4_create(0, 0, size.x, size.y);
	base->destroy = kui_slider_destroy;
	base->update = kui_slider_update;
	base->render = kui_slider_render;
	base->on_mouse_down = on_mouse_down;
	base->on_click = on_mouse_down; // A click that never reached mouse down, such as the agent's, still sets the value.
	base->on_mouse_drag = on_mouse_drag;

	typed_data->min = min;
	typed_data->max = max;
	typed_data->step = step > 0.0f ? step : 0.0f;

	// The track is a thin bar in the middle of the control's height; the handle is as tall as the control.
	f32 track_height = size.y * 0.4f;
	typed_data->track = child_panel_create(state, handle, name, "track", (vec2){size.x, track_height}, SLIDER_TRACK_COLOUR);
	kui_control_position_set(state, typed_data->track, (vec3){0.0f, (size.y - track_height) * 0.5f, 0.0f});
	typed_data->fill = child_panel_create(state, handle, name, "fill", (vec2){0.0f, track_height}, SLIDER_FILL_COLOUR);
	kui_control_position_set(state, typed_data->fill, (vec3){0.0f, (size.y - track_height) * 0.5f, 0.0f});
	typed_data->handle = child_panel_create(state, handle, name, "handle", (vec2){KMAX(size.y * 0.5f, 8.0f), size.y}, SLIDER_HANDLE_COLOUR);

	typed_data->value = snap(typed_data, value);
	layout(state, typed_data);
	return handle;
}

void kui_slider_destroy (kui_state *state, kui_control *self) {
	kui_base_control_destroy(state, self);
}

static f32 snap (const kui_slider_control *typed_data, f32 value) {
	value = KCLAMP(value, typed_data->min, typed_data->max);
	if (typed_data->step > 0.0f) {
		value = typed_data->min + kfloor(((value - typed_data->min) / typed_data->step) + 0.5f) * typed_data->step;
		value = KCLAMP(value, typed_data->min, typed_data->max);
	}
	return value;
}

void kui_slider_value_set (kui_state *state, kui_control self, f32 value) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_slider_control *typed_data = (kui_slider_control *)base;

	f32 snapped = snap(typed_data, value);
	if (snapped == typed_data->value) {
		return;
	}
	typed_data->value = snapped;
	layout(state, typed_data);
	if (typed_data->on_value_changed) {
		typed_data->on_value_changed(state, self, (kui_slider_event){.value = snapped});
	}
}

f32 kui_slider_value_get (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	return ((kui_slider_control *)base)->value;
}

void kui_slider_value_adjust (kui_state *state, kui_control self, i32 steps) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_slider_control *typed_data = (kui_slider_control *)base;

	f32 step = typed_data->step > 0.0f ? typed_data->step : (typed_data->max - typed_data->min) * 0.01f;
	kui_slider_value_set(state, self, typed_data->value + (f32)steps * step);
}

void kui_slider_set_on_value_changed (kui_state *state, kui_control self, PFN_slider_event_callback callback) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	((kui_slider_control *)base)->on_value_changed = callback;
}

b8 kui_slider_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	return kui_base_control_update(state, self, p_frame_data);
}

b8 kui_slider_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}

/** @brief Moves the fill and the handle to where the value is. */
static void layout (kui_state *state, kui_slider_control *typed_data) {
	f32 t = (typed_data->value - typed_data->min) / (typed_data->max - typed_data->min);
	f32 width = typed_data->base.bounds.width;
	f32 handle_width = kui_panel_size(state, typed_data->handle).x;
	kui_panel_set_width(state, typed_data->fill, width * t);
	kui_control_position_set(state, typed_data->handle, (vec3){(width - handle_width) * t, 0.0f, 0.0f});
}

static void value_from_local_x (kui_state *state, kui_control self, i16 local_x) {
	kui_base_control *base = kui_system_get_base(state, self);
	kui_slider_control *typed_data = (kui_slider_control *)base;
	f32 t = KCLAMP(((f32)local_x - base->bounds.x) / base->bounds.width, 0.0f, 1.0f);
	kui_slider_value_set(state, self, typed_data->min + t * (typed_data->max - typed_data->min));
}

static b8 on_mouse_down (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	value_from_local_x(state, self, event.local_x);
	return false;
}

static b8 on_mouse_drag (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	value_from_local_x(state, self, event.local_x);
	return false;
}
