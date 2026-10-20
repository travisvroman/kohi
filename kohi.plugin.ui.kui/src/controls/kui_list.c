#include "kui_list.h"

#include "containers/darray.h"
#include "controls/kui_label.h"
#include "controls/kui_panel.h"
#include "debug/kassert.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/kmath.h"
#include "strings/kstring.h"

#define LIST_BACKGROUND_COLOUR ((vec4){0.12f, 0.12f, 0.15f, 1.0f})
#define LIST_HIGHLIGHT_COLOUR ((vec4){0.25f, 0.55f, 0.85f, 1.0f})
#define LIST_TEXT_PADDING 6.0f

static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_mouse_wheel (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_navigate (struct kui_state *state, kui_control self, kui_nav_action action);
static void refresh (kui_state *state, kui_list_control *typed_data);
static void activate (kui_state *state, kui_control self);

static kui_list_control *get_list (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	return (kui_list_control *)base;
}

kui_control kui_list_create (kui_state *state, const char *name, f32 width, u32 visible_rows, font_type type, kname font_name, u16 font_size) {
	KASSERT(visible_rows > 0);
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_LIST);
	kui_list_control *typed_data = get_list(state, handle);
	kui_base_control *base = &typed_data->base;

	typed_data->row_height = (f32)font_size + 8.0f;
	typed_data->visible_rows = visible_rows;
	typed_data->selected = -1;
	typed_data->items = darray_create(char *);
	typed_data->rows = darray_create(kui_control);

	base->bounds = vec4_create(0, 0, width, typed_data->row_height * (f32)visible_rows);
	base->destroy = kui_list_destroy;
	base->update = kui_list_update;
	base->render = kui_list_render;
	base->on_click = on_click;
	base->on_mouse_wheel = on_mouse_wheel;
	base->on_navigate = on_navigate;
	FLAG_SET(base->flags, KUI_CONTROL_FLAG_NAVIGABLE_BIT, true);

	char *child_name = string_format("%s_background", name);
	typed_data->background = kui_panel_control_create(state, child_name, (vec2){base->bounds.width, base->bounds.height}, LIST_BACKGROUND_COLOUR);
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->background);
	kui_control_set_flag(state, typed_data->background, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	child_name = string_format("%s_highlight", name);
	typed_data->highlight = kui_panel_control_create(state, child_name, (vec2){base->bounds.width, typed_data->row_height}, LIST_HIGHLIGHT_COLOUR);
	string_free(child_name);
	kui_system_control_add_child(state, handle, typed_data->highlight);
	kui_control_set_flag(state, typed_data->highlight, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_control_set_is_visible(state, typed_data->highlight, false);

	for (u32 i = 0; i < visible_rows; ++i) {
		child_name = string_format("%s_row_%u", name, i);
		kui_control row = kui_label_control_create(state, child_name, type, font_name, font_size, "");
		string_free(child_name);
		kui_system_control_add_child(state, handle, row);
		kui_control_set_flag(state, row, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
		// Labels are positioned by their baseline, so lift them to sit inside the row.
		kui_control_position_set(state, row, (vec3){LIST_TEXT_PADDING, typed_data->row_height * (f32)i + (f32)font_size * -0.5f, 0.0f});
		darray_push(typed_data->rows, &row);
	}
	return handle;
}

void kui_list_destroy (kui_state *state, kui_control *self) {
	kui_list_control *typed_data = get_list(state, *self);
	u32 count = typed_data->items ? darray_length(typed_data->items) : 0;
	for (u32 i = 0; i < count; ++i) {
		string_free(typed_data->items[i]);
	}
	darray_destroy(typed_data->items);
	typed_data->items = KNULL;
	darray_destroy(typed_data->rows);
	typed_data->rows = KNULL;
	kui_base_control_destroy(state, self);
}

void kui_list_item_add (kui_state *state, kui_control self, const char *text) {
	kui_list_control *typed_data = get_list(state, self);
	char *copy = string_duplicate(text);
	darray_push(typed_data->items, &copy);
	refresh(state, typed_data);
}

void kui_list_clear (kui_state *state, kui_control self) {
	kui_list_control *typed_data = get_list(state, self);
	u32 count = darray_length(typed_data->items);
	for (u32 i = 0; i < count; ++i) {
		string_free(typed_data->items[i]);
	}
	darray_length_set(typed_data->items, 0);
	typed_data->first = 0;
	typed_data->selected = -1;
	refresh(state, typed_data);
}

u32 kui_list_count_get (kui_state *state, kui_control self) {
	return darray_length(get_list(state, self)->items);
}

const char *kui_list_item_get (kui_state *state, kui_control self, u32 index) {
	kui_list_control *typed_data = get_list(state, self);
	return index < darray_length(typed_data->items) ? typed_data->items[index] : KNULL;
}

void kui_list_selected_set (kui_state *state, kui_control self, i32 index) {
	kui_list_control *typed_data = get_list(state, self);
	if (index < -1 || index >= (i32)darray_length(typed_data->items)) {
		return;
	}
	b8 changed = index != typed_data->selected;
	typed_data->selected = index;

	// Scroll so the selection is on screen.
	if (index >= 0) {
		if ((u32)index < typed_data->first) {
			typed_data->first = (u32)index;
		} else if ((u32)index >= typed_data->first + typed_data->visible_rows) {
			typed_data->first = (u32)index - typed_data->visible_rows + 1;
		}
	}
	refresh(state, typed_data);

	if (changed && typed_data->on_selected) {
		typed_data->on_selected(state, self, (kui_list_event){.index = index});
	}
}

i32 kui_list_selected_get (kui_state *state, kui_control self) {
	return get_list(state, self)->selected;
}

void kui_list_set_on_selected (kui_state *state, kui_control self, PFN_list_event_callback callback) {
	get_list(state, self)->on_selected = callback;
}

void kui_list_set_on_activated (kui_state *state, kui_control self, PFN_list_event_callback callback) {
	get_list(state, self)->on_activated = callback;
}

b8 kui_list_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	return kui_base_control_update(state, self, p_frame_data);
}

b8 kui_list_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}

/** @brief Puts the right text in each row and the highlight on the selected row. */
static void refresh (kui_state *state, kui_list_control *typed_data) {
	u32 item_count = darray_length(typed_data->items);
	for (u32 r = 0; r < typed_data->visible_rows; ++r) {
		u32 item = typed_data->first + r;
		kui_control row = typed_data->rows[r];
		if (item < item_count) {
			kui_label_text_set(state, row, typed_data->items[item]);
			kui_control_set_is_visible(state, row, true);
		} else {
			kui_label_text_set(state, row, "");
			kui_control_set_is_visible(state, row, false);
		}
	}

	i32 row = typed_data->selected - (i32)typed_data->first;
	b8 show = typed_data->selected >= 0 && row >= 0 && row < (i32)typed_data->visible_rows;
	kui_control_set_is_visible(state, typed_data->highlight, show);
	if (show) {
		kui_control_position_set(state, typed_data->highlight, (vec3){0.0f, typed_data->row_height * (f32)row, 0.0f});
	}
}

static b8 on_click (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_list_control *typed_data = get_list(state, self);
	i32 row = (i32)((f32)(event.local_y - typed_data->base.bounds.y) / typed_data->row_height);
	i32 item = (i32)typed_data->first + row;
	if (row >= 0 && item < (i32)darray_length(typed_data->items)) {
		kui_list_selected_set(state, self, item);
		activate(state, self);
	}
	return false;
}

static b8 on_mouse_wheel (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_list_control *typed_data = get_list(state, self);
	u32 item_count = darray_length(typed_data->items);
	if (item_count <= typed_data->visible_rows) {
		return true; // Nothing to scroll; let the wheel through.
	}
	i32 first = (i32)typed_data->first - event.delta_z;
	i32 last_first = (i32)(item_count - typed_data->visible_rows);
	typed_data->first = (u32)KCLAMP(first, 0, last_first);
	refresh(state, typed_data);
	return false;
}

// Up and down move the selection and give focus back at either end.
static b8 on_navigate (struct kui_state *state, kui_control self, kui_nav_action action) {
	kui_list_control *typed_data = get_list(state, self);
	i32 item_count = (i32)darray_length(typed_data->items);
	switch (action) {
	case KUI_NAV_UP:
	case KUI_NAV_DOWN: {
		if (item_count == 0) {
			return false;
		}
		i32 step = action == KUI_NAV_UP ? -1 : 1;
		i32 target = typed_data->selected < 0 ? (step > 0 ? 0 : item_count - 1) : typed_data->selected + step;
		if (target < 0 || target >= item_count) {
			return false;
		}
		kui_list_selected_set(state, self, target);
		return true;
	}
	case KUI_NAV_ACCEPT:
		if (typed_data->selected < 0) {
			return false;
		}
		activate(state, self);
		return true;
	default:
		return false;
	}
}

// A click or an accept on the selected item.
static void activate (kui_state *state, kui_control self) {
	kui_list_control *typed_data = get_list(state, self);
	if (typed_data->on_activated) {
		typed_data->on_activated(state, self, (kui_list_event){.index = typed_data->selected});
	}
}
