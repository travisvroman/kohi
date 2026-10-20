#include "kui_dropdown.h"

#include "controls/kui_label.h"
#include "controls/kui_list.h"
#include "controls/kui_panel.h"
#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/kmath.h"
#include "strings/kstring.h"

#define DROPDOWN_HEADER_COLOUR ((vec4){0.2f, 0.2f, 0.25f, 1.0f})
#define DROPDOWN_TEXT_PADDING 6.0f

static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_navigate (struct kui_state *state, kui_control self, kui_nav_action action);
static void on_unfocus (struct kui_state *state, kui_control self);
static void on_list_activated (struct kui_state *state, kui_control list, kui_list_event event);

static kui_dropdown_control *get_dropdown (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	return (kui_dropdown_control *)base;
}

kui_control kui_dropdown_create (kui_state *state, const char *name, f32 width, u32 max_rows, font_type type, kname font_name, u16 font_size) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_DROPDOWN);
	kui_dropdown_control *typed_data = get_dropdown(state, handle);
	kui_base_control *base = &typed_data->base;

	f32 header_height = (f32)font_size + 8.0f;
	typed_data->selected = -1;
	base->bounds = vec4_create(0, 0, width, header_height);
	base->destroy = kui_dropdown_destroy;
	base->update = kui_dropdown_update;
	base->render = kui_dropdown_render;
	base->on_click = on_click;
	base->on_navigate = on_navigate;
	base->on_unfocus = on_unfocus;
	FLAG_SET(base->flags, KUI_CONTROL_FLAG_NAVIGABLE_BIT, true);

	char *child_name = string_format("%s_header", name);
	typed_data->header = kui_panel_control_create(state, child_name, (vec2){width, header_height}, DROPDOWN_HEADER_COLOUR);
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->header);
	kui_control_set_flag(state, typed_data->header, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	child_name = string_format("%s_header_label", name);
	typed_data->header_label = kui_label_control_create(state, child_name, type, font_name, font_size, "");
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->header_label);
	kui_control_set_flag(state, typed_data->header_label, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_control_position_set(state, typed_data->header_label, (vec3){DROPDOWN_TEXT_PADDING, (f32)font_size * -0.5f, 0.0f});

	child_name = string_format("%s_arrow", name);
	typed_data->arrow_label = kui_label_control_create(state, child_name, type, font_name, font_size, "v");
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->arrow_label);
	kui_control_set_flag(state, typed_data->arrow_label, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_control_position_set(state, typed_data->arrow_label, (vec3){width - (f32)font_size - DROPDOWN_TEXT_PADDING, (f32)font_size * -0.5f, 0.0f});

	child_name = string_format("%s_list", name);
	typed_data->list = kui_list_create(state, child_name, width, max_rows, type, font_name, font_size);
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->list);
	kui_control_position_set(state, typed_data->list, (vec3){0.0f, header_height, 0.0f});
	kui_control_set_is_visible(state, typed_data->list, false);
	// Only the dropdown is reached by navigation; it drives its list.
	kui_control_set_flag(state, typed_data->list, KUI_CONTROL_FLAG_NAVIGABLE_BIT, false);
	kui_list_set_on_activated(state, typed_data->list, on_list_activated);
	return handle;
}

void kui_dropdown_destroy (kui_state *state, kui_control *self) {
	kui_base_control_destroy(state, self);
}

void kui_dropdown_item_add (kui_state *state, kui_control self, const char *text) {
	kui_list_item_add(state, get_dropdown(state, self)->list, text);
}

static void header_text_update (kui_state *state, kui_dropdown_control *typed_data) {
	const char *text = typed_data->selected >= 0 ? kui_list_item_get(state, typed_data->list, (u32)typed_data->selected) : "";
	kui_label_text_set(state, typed_data->header_label, text ? text : "");
}

void kui_dropdown_open_set (kui_state *state, kui_control self, b8 open) {
	kui_dropdown_control *typed_data = get_dropdown(state, self);
	if (open == typed_data->open) {
		return;
	}
	typed_data->open = open;
	kui_control_set_is_visible(state, typed_data->list, open);
	kui_label_text_set(state, typed_data->arrow_label, open ? "^" : "v");
	// Whether it opened or closed, the highlight starts from the chosen item next time.
	kui_list_selected_set(state, typed_data->list, typed_data->selected);
	if (open) {
		// While open, a click anywhere else takes focus away, which closes it. The flag is only
		// set now so that a closed dropdown never captures the keyboard from a mouse click.
		FLAG_SET(typed_data->base.flags, KUI_CONTROL_FLAG_FOCUSABLE_BIT, true);
		if (!kui_system_is_control_focused(state, self)) {
			kui_system_focus_control(state, self);
		}
	} else {
		FLAG_SET(typed_data->base.flags, KUI_CONTROL_FLAG_FOCUSABLE_BIT, false);
	}
}

b8 kui_dropdown_is_open (kui_state *state, kui_control self) {
	return get_dropdown(state, self)->open;
}

void kui_dropdown_selected_set (kui_state *state, kui_control self, i32 index) {
	kui_dropdown_control *typed_data = get_dropdown(state, self);
	if (index < -1 || index >= (i32)kui_list_count_get(state, typed_data->list)) {
		return;
	}
	b8 changed = index != typed_data->selected;
	typed_data->selected = index;
	header_text_update(state, typed_data);
	kui_dropdown_open_set(state, self, false);
	kui_list_selected_set(state, typed_data->list, index);
	if (changed && typed_data->on_selected) {
		typed_data->on_selected(state, self, (kui_list_event){.index = index});
	}
}

i32 kui_dropdown_selected_get (kui_state *state, kui_control self) {
	return get_dropdown(state, self)->selected;
}

void kui_dropdown_set_on_selected (kui_state *state, kui_control self, PFN_list_event_callback callback) {
	get_dropdown(state, self)->on_selected = callback;
}

b8 kui_dropdown_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	return kui_base_control_update(state, self, p_frame_data);
}

b8 kui_dropdown_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}

static kui_control owner_of_list (kui_state *state, kui_control list) {
	return kui_system_get_base(state, list)->parent;
}

static void on_list_activated (struct kui_state *state, kui_control list, kui_list_event event) {
	kui_dropdown_selected_set(state, owner_of_list(state, list), event.index);
}

static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_dropdown_control *typed_data = get_dropdown(state, self);
	// A click on the header toggles. (Clicks on the open list go to the list.)
	if ((f32)event.local_y < typed_data->base.bounds.height) {
		kui_dropdown_open_set(state, self, !typed_data->open);
	}
	return false;
}

static void on_unfocus (struct kui_state *state, kui_control self) {
	kui_dropdown_open_set(state, self, false);
}

static b8 on_navigate (struct kui_state *state, kui_control self, kui_nav_action action) {
	kui_dropdown_control *typed_data = get_dropdown(state, self);
	if (!typed_data->open) {
		if (action == KUI_NAV_ACCEPT) {
			kui_dropdown_open_set(state, self, true);
			return true;
		}
		return false;
	}
	switch (action) {
	case KUI_NAV_UP:
	case KUI_NAV_DOWN: {
		kui_base_control *list_base = kui_system_get_base(state, typed_data->list);
		list_base->on_navigate(state, typed_data->list, action);
		return true;
	}
	case KUI_NAV_ACCEPT:
		kui_dropdown_selected_set(state, self, kui_list_selected_get(state, typed_data->list));
		return true;
	case KUI_NAV_CANCEL:
		kui_dropdown_open_set(state, self, false);
		return true;
	default:
		return true; // Left and right do nothing while open, and focus stays put.
	}
}
