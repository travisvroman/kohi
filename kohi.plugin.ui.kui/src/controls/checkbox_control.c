#include "checkbox_control.h"

#include "controls/image_box_control.h"
#include "controls/kui_label.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "strings/kstring.h"

static b8 is_checked (kui_checkbox_control *typed_data);
static b8 is_state_active (kui_checkbox_control *typed_data);
static b8 is_active (kui_checkbox_control *typed_data);
static kui_checkbox_state get_state (b8 active, b8 checked);
static void set_state (kui_state *state, kui_checkbox_control *typed_data, kui_checkbox_state cb_state);
static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static void active_changed (struct kui_state *state, kui_control self, b8 is_active);

kui_control kui_checkbox_control_create (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_CHECKBOX);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	base->on_click = on_click;

	typed_data->state = KUI_CHECKBOX_STATE_ENABLED_UNCHECKED;

	// Assign function pointers.
	base->destroy = kui_checkbox_control_destroy;
	base->update = kui_checkbox_control_update;
	base->render = kui_checkbox_control_render;
	base->active_changed = active_changed;

	char *buffer = KNULL;

	// Image box
	buffer = string_format("%s_checkbox_image", name);
	typed_data->check_image = kui_image_box_control_create(state, buffer, state->atlas.checkbox.image_box_size);
	kui_image_box_control_set_rect(state, typed_data->check_image, state->atlas.checkbox.enabled_unchecked_rect);
	string_free(buffer);
	kui_control_position_set(state, typed_data->check_image, vec3_create(0.0f, 2.0f, 0.0f));
	kui_system_control_add_child(state, handle, typed_data->check_image);
	// Image should not have mouse interactivity.
	kui_control_set_flag(state, typed_data->check_image, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	// Label
	buffer = string_format("%s_checkbox_label", name);
	typed_data->label = kui_label_control_create(state, buffer, type, font_name, font_size, text);
	string_free(buffer);
	kui_control_position_set(state, typed_data->label, vec3_create(state->atlas.checkbox.image_box_size.x + 5.0f, font_size * -0.3, 0.0f));
	kui_system_control_add_child(state, handle, typed_data->label);
	// Label should not have mouse interactivity.
	kui_control_set_flag(state, typed_data->label, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	// Bounds should account for padding and string size.
	vec2 string_size = kui_label_measure_string(state, typed_data->label);
	base->bounds.width = state->atlas.checkbox.image_box_size.x + 5.0f + string_size.x;
	base->bounds.height = state->atlas.checkbox.image_box_size.y + (2.0f * 2);

	return handle;
}
void kui_checkbox_control_destroy (kui_state *state, kui_control *self) {
	/* kui_base_control* base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_checkbox_control* typed_data = (kui_checkbox_control*)base; */

	kui_base_control_destroy(state, self);
}

void kui_checkbox_set_checked (kui_state *state, kui_control self, b8 checked) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	kui_checkbox_state new_state = get_state(true, checked);
	set_state(state, typed_data, new_state);
}
b8 kui_checkbox_get_checked (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	return is_checked(typed_data);
}

void kui_checkbox_set_on_checked (kui_state *state, kui_control self, PFN_checkbox_event_callback callback) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	typed_data->on_checked_changed = callback;
}

b8 kui_checkbox_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	return true;
}
b8 kui_checkbox_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	return true;
}

static b8 is_checked (kui_checkbox_control *typed_data) {
	return typed_data->state == KUI_CHECKBOX_STATE_ENABLED_CHECKED || typed_data->state == KUI_CHECKBOX_STATE_DISABLED_CHECKED;
}

static b8 is_state_active (kui_checkbox_control *typed_data) {
	return typed_data->state == KUI_CHECKBOX_STATE_ENABLED_UNCHECKED || typed_data->state == KUI_CHECKBOX_STATE_ENABLED_CHECKED;
}

static b8 is_active (kui_checkbox_control *typed_data) {
	return FLAG_GET(typed_data->base.flags, KUI_CONTROL_FLAG_ACTIVE_BIT);
}

static kui_checkbox_state get_state (b8 active, b8 checked) {
	if (active) {
		return checked ? KUI_CHECKBOX_STATE_ENABLED_CHECKED : KUI_CHECKBOX_STATE_ENABLED_UNCHECKED;
	} else {
		return checked ? KUI_CHECKBOX_STATE_DISABLED_CHECKED : KUI_CHECKBOX_STATE_DISABLED_UNCHECKED;
	}
}

static void set_state (kui_state *state, kui_checkbox_control *typed_data, kui_checkbox_state cb_state) {
	typed_data->state = cb_state;
	switch (cb_state) {
	case KUI_CHECKBOX_STATE_ENABLED_UNCHECKED:
		kui_image_box_control_set_rect(state, typed_data->check_image, state->atlas.checkbox.enabled_unchecked_rect);
		break;
	case KUI_CHECKBOX_STATE_ENABLED_CHECKED:
		kui_image_box_control_set_rect(state, typed_data->check_image, state->atlas.checkbox.enabled_checked_rect);
		break;
	case KUI_CHECKBOX_STATE_DISABLED_UNCHECKED:
		kui_image_box_control_set_rect(state, typed_data->check_image, state->atlas.checkbox.disabled_unchecked_rect);
		break;
	case KUI_CHECKBOX_STATE_DISABLED_CHECKED:
		kui_image_box_control_set_rect(state, typed_data->check_image, state->atlas.checkbox.disabled_checked_rect);
		break;
	}
}

static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	// Only bother with this if actually active.
	if (is_state_active(typed_data)) {
		// Flip the checked state and apply it.
		b8 currently_checked = is_checked(typed_data);
		kui_checkbox_state new_state = get_state(true, !currently_checked);
		set_state(state, typed_data, new_state);

		if (typed_data->on_checked_changed) {
			kui_checkbox_event evt = {
				.checked = is_checked(typed_data)};
			typed_data->on_checked_changed(state, self, evt);
		}
	}

	return false;
}

static void active_changed (struct kui_state *state, kui_control self, b8 is_active) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_checkbox_control *typed_data = (kui_checkbox_control *)base;

	KTRACE("active changed called");

	b8 cur_active = is_active;
	b8 state_active = is_state_active(typed_data);
	if (cur_active != state_active) {
		// State change is needed.
		kui_checkbox_state new_state = get_state(cur_active, is_checked(typed_data));
		set_state(state, typed_data, new_state);
	}
}
