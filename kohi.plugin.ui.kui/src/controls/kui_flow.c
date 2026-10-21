#include "kui_flow.h"

#include <containers/darray.h>
#include <core/frame_data.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <strings/kstring.h>
#include <systems/ktransform_system.h>

#include "controls/kui_panel.h"
#include "kui_system.h"
#include "kui_types.h"

kui_control kui_flow_control_create (kui_state *state, const char *name, vec2 size) {
	return kui_flow_control_create_with_options(
		state,
		name,
		size,
		KUI_FLOW_HORIZONTAL_LEFT_TO_RIGHT,
		KUI_FLOW_VERTICAL_TOP_TO_BOTTOM,
		KUI_FLOW_OVERFLOW_NONE);
}

kui_control kui_flow_control_create_with_options (
	kui_state *state,
	const char *name,
	vec2 size,
	kui_flow_horizontal horizontal,
	kui_flow_vertical vertical,
	kui_flow_overflow overflow) {

	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_FLOW);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_flow_control *typed_data = (kui_flow_control *)base;

	base->destroy = kui_flow_control_destroy;
	base->update = kui_flow_control_update;
	base->render = kui_flow_control_render;

	typed_data->horizontal = horizontal;
	typed_data->vertical = vertical;
	typed_data->overflow = overflow;

	base->bounds = (rect_2d){0, 0, size.x, size.y};

#if KUI_FLOW_DEBUG_DISPLAY
	char *buffer = string_format("%s_debug_panel", name);
	typed_data->debug_panel = kui_panel_control_create(state, buffer, size, vec4_create(1.0f, 0.0f, 0.0f, 0.5f));
	string_free(buffer);

	kui_base_control *debug_panel_base = kui_system_get_base(state, typed_data->debug_panel);
	KASSERT(debug_panel_base);

	FLAG_SET(debug_panel_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_system_control_add_child(state, handle, typed_data->debug_panel);

	// HACK: Adding some child controls here to test layout changes.
	for (u8 i = 0; i < 20; ++i) {
		char *buffer = string_format("%s_test_panel_%u", name, i);
		kui_control test_handle = kui_panel_control_create(
			state,
			buffer,
			vec2_create(krandom_in_range(10, 200), krandom_in_range(5, 50)),
			vec4_create(
				kfrandom_in_range(0.0f, 1.0f),
				kfrandom_in_range(0.0f, 1.0f),
				kfrandom_in_range(0.0f, 1.0f),
				0.5f));
		string_free(buffer);

		kui_system_control_add_child(state, handle, test_handle);
	}
#endif

	return handle;
}

void kui_flow_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control_destroy(state, self);
}

typedef struct row_data {
	// total width of all controls in the row
	f32 total_width;
	f32 height;
	// darray
	kui_base_control **controls;
} row_data;

// Split data into what fits into each row first _without_ taking spacing into account.
// Left/right/centered/spread doesn't matter here, just need to group controls into rows.
// Returns darray built with frame allocator.
static row_data *generate_rows (kui_state *state, kui_flow_control *control, frame_data *p_frame_data) {
	kui_base_control *base = &control->base;

	u32 i = 0;
#if KUI_FLOW_DEBUG_DISPLAY
	i = 1; // Skip debug panel
#endif

	row_data *rows = darray_create(row_data); // darray_create_with_allocator(row_data, &p_frame_data->allocator);

	u32 x_offset = 0;

	u32 child_count = darray_length(base->children);

	row_data current_row;
	current_row.total_width = 0;
	current_row.height = 0;
	current_row.controls = darray_create(kui_base_control *); // darray_create_with_allocator(kui_base_control *, &p_frame_data->allocator);
	for (; i < child_count; ++i) {
		kui_base_control *child_base = kui_system_get_base(state, base->children[i]);
		if (child_base) {

			if (!x_offset || x_offset + child_base->bounds.width <= base->bounds.width) {
				// Within bounds, position can be used as-is.
				// However, ensure we track the highest control on this row.
				current_row.height = KMAX(current_row.height, child_base->bounds.height);
				// Add to the current row data.
				darray_push(current_row.controls, &child_base);
			} else {
				// Doesn't fit. Move down to next row.
				x_offset = 0;

				// Push current row into rows array and start new row.
				darray_push(rows, &current_row);
				current_row.total_width = 0;
				current_row.height = child_base->bounds.height;
				current_row.controls = darray_create(kui_base_control *); // darray_create_with_allocator(kui_base_control *, &p_frame_data->allocator);
				// Add to the new current row's controls
				darray_push(current_row.controls, &child_base);
			}

			current_row.total_width += child_base->bounds.width;
			x_offset += child_base->bounds.width;
		}
	}
	if (darray_length(current_row.controls)) {
		darray_push(rows, &current_row);
	}

	if (!darray_length(rows)) {
		darray_destroy(rows);
		return KNULL;
	}

	return rows;
}

static void reorder_rows (kui_state *state, kui_flow_control *control, row_data *rows) {
	if (control->vertical == KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {
		/* darray_reverse(rows); */
		u32 len = darray_length(rows);
		u32 i = 0;
		u32 j = len - 1;
		while (i < j) {
			row_data temp = rows[i];
			rows[i] = rows[j];
			rows[j] = temp;

			++i;
			--j;
		}
	}

	// NOTE: The other vertical alignment types assume top->bottom
}

static void relayout (kui_state *state, kui_flow_control *control, row_data *rows) {
	f32 x_offset = 0, y_offset = 0;

	u32 row_count = darray_length(rows);

	if (control->vertical == KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {
		// If bottom->top, start from the bottom.
		y_offset = control->base.bounds.height;
	}

	f32 y_diff = 0, total_height = 0;
	for (u32 i = 0; i < row_count; ++i) {
		total_height += rows[i].height;
	}
	y_diff = control->base.bounds.height - total_height;
	f32 y_space_divided = 0;
	if (y_diff > 0) {
		if (control->vertical == KUI_FLOW_VERTICAL_MIDDLE) {
			y_space_divided = y_diff / 2;
			y_offset += y_space_divided;
		} else if (control->vertical == KUI_FLOW_VERTICAL_SPREAD) {
			y_space_divided = y_diff / (row_count > 1 ? (row_count - 1) : 1);
		}
	}

	for (u32 i = 0; i < row_count; ++i) {
		row_data *row = &rows[i];

		u32 control_count = darray_length(row->controls);
		f32 x_diff = control->base.bounds.width - row->total_width;
		f32 x_space_divided = 0;

		x_offset = 0;
		// Spacing only applies if there is actually leftover space.
		// Diff can be negative in the case of a single control being on
		// a row, but is wider than the flow container.
		if (x_diff > 0) {
			if (control->horizontal == KUI_FLOW_HORIZONTAL_CENTER) {
				// Only pad before the first item and after the last.
				x_space_divided = x_diff / 2;
			} else if (control->horizontal == KUI_FLOW_HORIZONTAL_EVEN_SPACED) {
				// Pad before the first item, after the last, and between each item.
				x_space_divided = x_diff / (control_count + 1);
			} else if (control->horizontal == KUI_FLOW_HORIZONTAL_SPREAD) {
				// No padding before first item or after last. Even between each item.
				x_space_divided = x_diff / (control_count > 1 ? (control_count - 1) : 1);
			}
		}

		if (control->horizontal == KUI_FLOW_HORIZONTAL_CENTER || control->horizontal == KUI_FLOW_HORIZONTAL_EVEN_SPACED) {
			// Apply spacing first for these 2 layouts.
			x_offset += x_space_divided;
		} else if (control->horizontal == KUI_FLOW_HORIZONTAL_RIGHT_TO_LEFT) {
			x_offset = control->base.bounds.width;
		}

		if (control->vertical == KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {
			y_offset -= row->height;
		}

		for (u32 j = 0; j < control_count; ++j) {
			// Each control on the row.

			kui_base_control *child_base = row->controls[j];

			if (control->horizontal == KUI_FLOW_HORIZONTAL_RIGHT_TO_LEFT) {
				x_offset -= child_base->bounds.width;
			}

			vec3 pos = vec3_create(x_offset, y_offset, 0);
			ktransform_position_set(child_base->ktransform, pos);

			if (control->horizontal != KUI_FLOW_HORIZONTAL_RIGHT_TO_LEFT) {
				x_offset += child_base->bounds.width;
			}

			if (control->horizontal == KUI_FLOW_HORIZONTAL_EVEN_SPACED || control->horizontal == KUI_FLOW_HORIZONTAL_SPREAD) {
				// Apply spacing between controls.
				x_offset += x_space_divided;
			}
		}

		if (control->vertical != KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {
			y_offset += row->height;
		}

		if (control->vertical == KUI_FLOW_VERTICAL_SPREAD) {
			y_offset += y_space_divided;
		}
	} // each row
}

static void cleanup_rows (row_data *rows) {
	u32 row_count = darray_length(rows);
	for (u32 i = 0; i < row_count; ++i) {
		darray_destroy(rows[i].controls);
	}
	darray_destroy(rows);
}

b8 kui_flow_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_flow_control *flow = (kui_flow_control *)base;

	// Recalculate flow for all child controls.

	// HACK: manually adjust size per frame... for now.
	{
		static f32 test = 0;
		test += 0.01f;
		f32 frame_mod = ((ksin(test) + 1.0f) * 0.5f) * 200.0f;
		base->bounds.width = 100 + frame_mod;
#if KUI_FLOW_DEBUG_DISPLAY
		kui_panel_control_resize(state, flow->debug_panel, vec2_create(base->bounds.width, base->bounds.height));
#endif
	}

	// Recalculate flow.
	row_data *rows = generate_rows(state, flow, p_frame_data);
	if (rows) {
		reorder_rows(state, flow, rows);
		relayout(state, flow, rows);
		cleanup_rows(rows);
	}
	return true;
}

b8 kui_flow_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}
