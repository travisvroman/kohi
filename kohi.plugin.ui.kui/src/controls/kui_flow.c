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
#endif

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

	return handle;
}

void kui_flow_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control_destroy(state, self);
}

typedef struct row_data {
	f32 total_width;
	// darray
	kui_base_control **controls;
} row_data;

void relayout_centered_or_spread_top_bottom (kui_state *state, kui_flow_control *flow, struct frame_data *p_frame_data) {
	kui_base_control *base = &flow->base;

	u32 i = 0;
#if KUI_FLOW_DEBUG_DISPLAY
	i = 1; // Skip debug panel
#endif

	row_data *rows = darray_create_with_allocator(row_data, &p_frame_data->allocator);

	u32 x_offset = 0, y_offset = 0, highest_y = 0;

	u32 child_count = darray_length(base->children);
	row_data current_row;
	current_row.total_width = 0;
	current_row.controls = darray_create_with_allocator(kui_base_control *, &p_frame_data->allocator);
	for (; i < child_count; ++i) {
		kui_base_control *child_base = kui_system_get_base(state, base->children[i]);
		if (child_base) {

			if (!x_offset || x_offset + child_base->bounds.width <= base->bounds.width) {
				// Within bounds, position can be used as-is.
				// However, ensure we track the highest control on this row.
				if (child_base->bounds.height > highest_y) {
					highest_y = child_base->bounds.height;
				}
				// Add to the current row data.
				darray_push(current_row.controls, child_base);
			} else {
				// Doesn't fit. Move down to next row.
				x_offset = 0;
				y_offset += highest_y;
				highest_y = child_base->bounds.height;

				// Push current row into rows array and start new row.
				darray_push(rows, &current_row);
				current_row.total_width = 0;
				current_row.controls = darray_create_with_allocator(kui_base_control *, &p_frame_data->allocator);
				// Add to the new current row's controls
				darray_push(current_row.controls, child_base);
			}

			current_row.total_width += child_base->bounds.width;

			vec3 pos = vec3_create(x_offset, y_offset, 0);
			ktransform_position_set(child_base->ktransform, pos);

			x_offset += child_base->bounds.width;
		}
	}
	if (darray_length(current_row.controls)) {
		darray_push(rows, &current_row);
	}

	u32 len = darray_length(rows);
	for (u32 r = 0; r < len; ++r) {
		row_data *row = &rows[r];
		f32 diff = flow->base.bounds.width - row->total_width;

		if (diff < 0) {
			// Nothing to do with overflowing rows... this is likely due to a single control
			// occupying the entire space. Skip it.
			continue;
		}

		u32 control_count = darray_length(row->controls);
		f32 x_offset = 0;
		f32 spacing;
		if (flow->horizontal == KUI_FLOW_HORIZONTAL_CENTER) {
			// Spacing applied evenly before first control and after last.
			spacing = diff / (control_count - 1);
			x_offset += spacing;
			for (u32 c = 0; c < control_count; ++c) {
				kui_base_control *control = row->controls[c];
				vec3 pos = ktransform_position_get(control->ktransform);
				ktransform_position_set(control->ktransform, vec3_create(x_offset, pos.y, pos.z));
				x_offset += control->bounds.width;
			}

		} else if (flow->horizontal == KUI_FLOW_HORIZONTAL_SPREAD) {
			// Spacing applied evenly before first control, between each control, and after last.
			spacing = diff / (control_count + 1);

			x_offset += spacing;
			for (u32 c = 0; c < control_count; ++c) {
				kui_base_control *control = row->controls[c];
				vec3 pos = ktransform_position_get(control->ktransform);
				ktransform_position_set(control->ktransform, vec3_create(x_offset, pos.y, pos.z));
				x_offset += control->bounds.width + spacing;
			}
		} else {
			// Anything else is an error.
			KFATAL("Not a valid horizontal flow for this function.");
		}
	}
}

void relayout_left_right_top_bottom (kui_state *state, kui_flow_control *flow, struct frame_data *p_frame_data) {
	kui_base_control *base = &flow->base;

	u32 i = 0;
#if KUI_FLOW_DEBUG_DISPLAY
	i = 1; // Skip debug panel
#endif

	u32 x_offset = 0, y_offset = 0, highest_y = 0;

	// LEFTOFF: here
	u32 child_count = darray_length(base->children);
	for (; i < child_count; ++i) {
		kui_base_control *child_base = kui_system_get_base(state, base->children[i]);
		if (child_base) {

			if (!x_offset || x_offset + child_base->bounds.width <= base->bounds.width) {
				// Within bounds, position can be used as-is.
				// However, ensure we track the highest control on this row.
				if (child_base->bounds.height > highest_y) {
					highest_y = child_base->bounds.height;
				}
			} else {
				// Doesn't fit. Move down to next row.
				x_offset = 0;
				y_offset += highest_y;
				highest_y = child_base->bounds.height;
			}

			vec3 pos = vec3_create(x_offset, y_offset, 0);
			ktransform_position_set(child_base->ktransform, pos);

			x_offset += child_base->bounds.width;
		}
	}
}

void relayout_right_left_top_bottom (kui_state *state, kui_flow_control *flow, struct frame_data *p_frame_data) {
	kui_base_control *base = &flow->base;

	u32 i = 0;
#if KUI_FLOW_DEBUG_DISPLAY
	i = 1; // Skip debug panel
#endif

	u32 x_offset = (u32)base->bounds.width, y_offset = 0, highest_y = 0;

	u32 child_count = darray_length(base->children);
	for (; i < child_count; ++i) {
		kui_base_control *child_base = kui_system_get_base(state, base->children[i]);
		if (child_base) {

			if (x_offset == (u32)base->bounds.width || x_offset + child_base->bounds.width <= base->bounds.width) {
				// Within bounds, position can be used as-is.
				// However, ensure we track the highest control on this row.
				if (child_base->bounds.height > highest_y) {
					highest_y = child_base->bounds.height;
				}
			} else {
				// Doesn't fit. Move down to next row.
				x_offset = 0;
				y_offset += highest_y;
				highest_y = child_base->bounds.height;
			}

			vec3 pos = vec3_create(x_offset, y_offset, 0);
			ktransform_position_set(child_base->ktransform, pos);

			x_offset += child_base->bounds.width;
		}
	}
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
	if (flow->horizontal == KUI_FLOW_HORIZONTAL_RIGHT_TO_LEFT) {

	} else if (flow->horizontal == KUI_FLOW_HORIZONTAL_LEFT_TO_RIGHT) {
		if (flow->vertical == KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {

		} else if (flow->vertical == KUI_FLOW_VERTICAL_TOP_TO_BOTTOM) {
			relayout_left_right_top_bottom(state, flow, p_frame_data);
		} else if (flow->vertical == KUI_FLOW_VERTICAL_MIDDLE) {

		} else if (flow->vertical == KUI_FLOW_VERTICAL_SPREAD) {

		} else {
			// Anything else is an error.
		}
	} else if (flow->horizontal == KUI_FLOW_HORIZONTAL_CENTER || flow->horizontal == KUI_FLOW_HORIZONTAL_SPREAD) {
		if (flow->vertical == KUI_FLOW_VERTICAL_BOTTOM_TO_TOP) {

		} else if (flow->vertical == KUI_FLOW_VERTICAL_TOP_TO_BOTTOM) {
			relayout_centered_or_spread_top_bottom(state, flow, p_frame_data);
		} else if (flow->vertical == KUI_FLOW_VERTICAL_MIDDLE) {

		} else if (flow->vertical == KUI_FLOW_VERTICAL_SPREAD) {

		} else {
			// Anything else is an error.
		}
	} else {
		// Anything else is an error.
	}

	return true;
}

b8 kui_flow_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	return kui_base_control_render(state, self, p_frame_data, render_data);
}
