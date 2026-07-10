#include "kui_textbox.h"

#include <containers/darray.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/input.h>
#include <defines.h>
#include <input_types.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <renderer/nine_slice.h>
#include <renderer/renderer_frontend.h>
#include <stdint.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/kshader_system.h>
#include <systems/ktransform_system.h>

#include "controls/kui_label.h"
#include "controls/kui_panel.h"
#include "debug/kassert.h"
#include "kui_defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/math_types.h"
#include "renderer/kui_renderer.h"

static f32 kui_textbox_calculate_cursor_offset (kui_state *state, u32 string_pos, const char *full_string, kui_textbox_control *typed_textbox_control);
static void kui_textbox_update_highlight_box (kui_state *state, kui_base_control *self);
static void kui_textbox_update_cursor_position (kui_state *state, kui_base_control *self);
static b8 kui_textbox_on_key (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_textbox_on_paste (u16 code, void *sender, void *listener_inst, event_context context);
static void kui_textbox_on_focus (struct kui_state *state, kui_control self);
static void kui_textbox_on_unfocus (struct kui_state *state, kui_control self);

kui_control kui_textbox_control_create (kui_state *state, const char *name, font_type font_type, kname font_name, u16 font_size, const char *text, kui_textbox_type type) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_TEXTBOX);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;

	// Reasonable defaults.
	typed_control->size = (vec2i){200, font_size + 10}; // add padding
	typed_control->colour = vec4_one();
	typed_control->type = type;

	FLAG_SET(base->flags, KUI_CONTROL_FLAG_FOCUSABLE_BIT, true);

	// Assign function pointers.
	base->destroy = kui_textbox_control_destroy;
	base->update = kui_textbox_control_update;
	base->render = kui_textbox_control_render;
	base->on_focus = kui_textbox_on_focus;
	base->on_unfocus = kui_textbox_on_unfocus;

	if (type == KUI_TEXTBOX_TYPE_FLOAT) {
		// Verify the text content. If numeric is required and it isn't numeric, blank it out.
		f32 f = 0;
		if (!string_to_f32(text, &f)) {
			text = "";
		}
	} else if (type == KUI_TEXTBOX_TYPE_INT) {
		i64 i = 0;
		if (!string_to_i64(text, &i)) {
			text = "";
		}
	}

	// Label internal control
	char *buffer = string_format("%s_textbox_internal_label", name);
	typed_control->content_label = kui_label_control_create(state, buffer, font_type, font_name, font_size, text);
	string_free(buffer);
	typed_control->label_line_height = kui_label_line_height_get(state, typed_control->content_label);

	// Use a panel as the cursor.
	buffer = string_format("%s_textbox_cursor_panel", name);
	typed_control->cursor = kui_panel_control_create(state, buffer, (vec2){1.0f, font_size - 4.0f}, (vec4){1.0f, 1.0f, 1.0f, 1.0f});
	string_free(buffer);

	// Highlight box.
	buffer = string_format("%s_textbox_highlight_panel", name);
	typed_control->highlight_box = kui_panel_control_create(state, buffer, (vec2){1.0f, font_size}, (vec4){0.0f, 0.5f, 0.9f, 0.5f});
	string_free(buffer);

	typed_control->listener = kallocate(sizeof(kui_textbox_event_listener), MEMORY_TAG_UI);
	typed_control->listener->state = state;
	typed_control->listener->control = handle;

	// load

	vec2i atlas_size = (vec2i){state->atlas_texture_size.x, state->atlas_texture_size.y};

	vec2i corner_size;
	{
		vec2 min = state->atlas.textbox.normal.extents.min;
		vec2 max = state->atlas.textbox.normal.extents.max;
		vec2i atlas_min = (vec2i){min.x, min.y};
		vec2i atlas_max = (vec2i){max.x, max.y};
		vec2 cps = state->atlas.textbox.normal.corner_px_size;
		vec2 cs = state->atlas.textbox.normal.corner_size;
		vec2i local_corner_px_size = (vec2i){cps.x, cps.y};
		vec2i local_corner_size = (vec2i){cs.x, cs.y};
		KASSERT(nine_slice_create(base->name, typed_control->size, atlas_size, atlas_min, atlas_max, local_corner_px_size, local_corner_size, &typed_control->nslice));

		corner_size = local_corner_size;
	}

	{
		vec2 min = state->atlas.textbox.focused.extents.min;
		vec2 max = state->atlas.textbox.focused.extents.max;
		vec2i atlas_min = (vec2i){min.x, min.y};
		vec2i atlas_max = (vec2i){max.x, max.y};
		vec2 cps = state->atlas.textbox.focused.corner_px_size;
		vec2 cs = state->atlas.textbox.focused.corner_size;
		vec2i local_corner_px_size = (vec2i){cps.x, cps.y};
		vec2i local_corner_size = (vec2i){cs.x, cs.y};
		KASSERT(nine_slice_create(base->name, typed_control->size, atlas_size, atlas_min, atlas_max, local_corner_px_size, local_corner_size, &typed_control->focused_nslice));
	}

	base->bounds.x = 0.0f;
	base->bounds.y = 0.0f;
	base->bounds.width = typed_control->size.x;
	base->bounds.height = typed_control->size.y;
	// Setup textbox clipping mask geometry.
	base->clipping_area.type = KUI_CLIP_TYPE_SCISSOR;
	vec3 pos = ktransform_world_position_get(base->ktransform);
	base->clipping_area.scissor_data.clipping_area = (rect_2di){
		.x = pos.x,
		.y = pos.y,
		.width = typed_control->size.x - (corner_size.x * 2),
		.height = typed_control->size.y};

	// NOTE: If wanting to use stencil buffer for clipping, use the below instead.
	/* base->clipping_area.stencil_data.reference_id = 1;
	kgeometry quad = geometry_generate_quad(typed_control->size.x - (corner_size.x * 2), typed_control->size.y, 0, 0, 0, 0, kname_create("textbox_clipping_box"));
	KASSERT(renderer_geometry_upload(&quad));
	base->clipping_area.stencil_data.geometry = quad;
	base->clipping_area.stencil_data.render_data.model = mat4_identity();
	base->clipping_area.stencil_data.render_data.unique_id = base->clipping_area.stencil_data.reference_id;
	base->clipping_area.stencil_data.render_data.vertex_count = base->clipping_area.stencil_data.geometry.vertex_count;
	base->clipping_area.stencil_data.render_data.vertex_element_size = base->clipping_area.stencil_data.geometry.vertex_element_size;
	base->clipping_area.stencil_data.render_data.vertex_buffer_offset = base->clipping_area.stencil_data.geometry.vertex_buffer_offset;
	base->clipping_area.stencil_data.render_data.index_count = base->clipping_area.stencil_data.geometry.index_count;
	base->clipping_area.stencil_data.render_data.index_element_size = base->clipping_area.stencil_data.geometry.index_element_size;
	base->clipping_area.stencil_data.render_data.index_buffer_offset = base->clipping_area.stencil_data.geometry.index_buffer_offset;
	base->clipping_area.stencil_data.render_data.diffuse_colour = vec4_zero(); // transparent;
	base->clipping_area.stencil_data.transform = ktransform_from_position((vec3){corner_size.x, 0.0f, 0.0f}, 0);
	ktransform_parent_set(base->clipping_area.stencil_data.transform, base->ktransform); */

	// Acquire group resources for this control.
	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));

	// Acquire binding set resources for this control.
	typed_control->binding_instance_id = INVALID_ID;
	typed_control->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	KASSERT(typed_control->binding_instance_id != INVALID_ID);

	// NOTE: Only parenting the transform, the control. This is to have control over how the
	// clipping mask is attached and drawn. See the render function for the other half of this.
	// TODO: Adjustable padding
	kui_base_control *label_base = kui_system_get_base(state, typed_control->content_label);
	KASSERT(label_base);
	label_base->parent = handle;
	ktransform_parent_set(label_base->ktransform, base->ktransform);
	ktransform_position_set(label_base->ktransform, (vec3){typed_control->nslice.corner_size.x, -2.0f, 0.0f}); // padding/2 for y

	// Create the cursor and attach it as a child.
	kui_base_control *cursor_base = kui_system_get_base(state, typed_control->cursor);
	KASSERT(cursor_base);
	if (!kui_system_control_add_child(state, handle, typed_control->cursor)) {
		KERROR("Failed to parent textbox system text.");
	} else {
		// Set an initial position.
		ktransform_position_set(cursor_base->ktransform, (vec3){typed_control->nslice.corner_size.x, typed_control->label_line_height - 4.0f, 0.0f});
	}

	// Ensure the cursor position is correct.
	kui_textbox_update_cursor_position(state, base);

	// Create the highlight box and attach it as a child.
	// NOTE: Only parenting the transform, the control. This is to have control over how the
	// clipping mask is attached and drawn. See the render function for the other half of this.

	// Set an initial position.
	kui_base_control *highlight_base = kui_system_get_base(state, typed_control->highlight_box);
	FLAG_SET(highlight_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, false);
	ktransform_parent_set(highlight_base->ktransform, base->ktransform);

	// Ensure the highlight box size and position is correct.
	kui_textbox_update_highlight_box(state, base);

	// FIXME: Need to just pass handle here, since the pointer _will_ change as the internal darray is expanded...

	event_register(EVENT_CODE_KEY_PRESSED, typed_control->listener, kui_textbox_on_key);
	event_register(EVENT_CODE_KEY_RELEASED, typed_control->listener, kui_textbox_on_key);

	return handle;
}

void kui_textbox_control_destroy (kui_state *state, kui_control *self) {

	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;
	// unload
	// TODO: unload sub-controls that aren't children (i.e content_label and highlight_box)
	event_unregister(EVENT_CODE_KEY_PRESSED, typed_control->listener, kui_textbox_on_key);
	event_unregister(EVENT_CODE_KEY_RELEASED, typed_control->listener, kui_textbox_on_key);

	nine_slice_destroy(&typed_control->nslice);
	nine_slice_destroy(&typed_control->focused_nslice);

	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		renderer_geometry_destroy(&base->clipping_area.stencil_data.geometry);
		geometry_destroy(&base->clipping_area.stencil_data.geometry);
	}

	kfree(typed_control->listener, sizeof(kui_textbox_event_listener), MEMORY_TAG_UI);

	kui_base_control_destroy(state, self);
}

b8 kui_textbox_control_size_set (kui_state *state, kui_control self, i32 width, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;

	typed_control->size.x = width;
	typed_control->size.y = height;
	typed_control->nslice.size.x = width;
	typed_control->nslice.size.y = height;
	typed_control->focused_nslice.size.x = width;
	typed_control->focused_nslice.size.y = height;

	base->bounds.height = height;
	base->bounds.width = width;

	nine_slice_update(&typed_control->nslice, 0);
	nine_slice_update(&typed_control->focused_nslice, 0);

	// HACK: TODO: remove hardcoded stuff.
	vec2i corner_size = (vec2i){10, 10};

	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		kgeometry *vg = &base->clipping_area.stencil_data.geometry;

		kgeometry quad = geometry_generate_quad(typed_control->size.x - (corner_size.x * 2), typed_control->size.y, 0, 0, 0, 0, 0);
		kfree(quad.indices, quad.index_element_size * quad.index_count, MEMORY_TAG_ARRAY);
		kfree(vg->vertices, vg->vertex_element_size * vg->vertex_count, MEMORY_TAG_ARRAY);
		vg->vertices = quad.vertices;
		vg->extents = quad.extents;

		renderer_geometry_vertex_update(&base->clipping_area.stencil_data.geometry, 0, vg->vertex_count, vg->vertices, false);
	} else {
		vec3 pos = ktransform_world_position_get(base->ktransform);
		base->clipping_area.scissor_data.clipping_area = (rect_2di){
			.x = pos.x,
			.y = pos.y,
			.width = typed_control->size.x - (corner_size.x * 2),
			.height = typed_control->size.y};
	}

	return true;
}
b8 kui_textbox_control_width_set (kui_state *state, kui_control self, i32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;
	return kui_textbox_control_size_set(state, self, width, typed_control->size.y);
}
b8 kui_textbox_control_height_set (kui_state *state, kui_control self, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;
	return kui_textbox_control_size_set(state, self, typed_control->size.x, height);
}

void kui_textbox_control_colour_set (kui_state *state, kui_control self, colour4 colour) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;
	typed_control->colour = colour;
}

b8 kui_textbox_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;

	nine_slice_render_frame_prepare(&typed_control->nslice, p_frame_data);
	nine_slice_render_frame_prepare(&typed_control->focused_nslice, p_frame_data);

	return true;
}

b8 kui_textbox_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	b8 is_focused = kui_system_is_control_focused(state, self);

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_control = (kui_textbox_control *)base;

	kui_base_control *label_base = kui_system_get_base(state, typed_control->content_label);
	KASSERT(label_base);
	/* kui_textbox_control* label_typed_control = (kui_textbox_control*)label_base; */

	kui_base_control *cursor_base = kui_system_get_base(state, typed_control->cursor);
	KASSERT(cursor_base);
	/* kui_textbox_control* cursor_typed_control = (kui_textbox_control*)cursor_base; */

	kui_base_control *highlight_base = kui_system_get_base(state, typed_control->highlight_box);
	KASSERT(highlight_base);
	/* kui_textbox_control* highlight_typed_control = (kui_textbox_control*)highlight_base; */

	// Render the nine-slice.
	nine_slice *ns = 0;
	if (state->focused.val == self.val) {
		ns = &typed_control->focused_nslice;
	} else {
		ns = &typed_control->nslice;
	}

	if (ns->vertex_data.elements) {
		kui_renderable nineslice_renderable = {0};
		nineslice_renderable.render_data.unique_id = 0;
		nineslice_renderable.render_data.vertex_count = ns->vertex_data.element_count;
		nineslice_renderable.render_data.vertex_element_size = ns->vertex_data.element_size;
		nineslice_renderable.render_data.vertex_buffer_offset = ns->vertex_data.buffer_offset;
		nineslice_renderable.render_data.index_count = ns->index_data.element_count;
		nineslice_renderable.render_data.index_element_size = ns->index_data.element_size;
		nineslice_renderable.render_data.index_buffer_offset = ns->index_data.buffer_offset;
		nineslice_renderable.render_data.model = ktransform_world_get(base->ktransform);
		nineslice_renderable.render_data.diffuse_colour = vec4_mul(is_focused ? state->focused_base_colour : state->unfocused_base_colour, typed_control->colour);

		nineslice_renderable.binding_instance_id = typed_control->binding_instance_id;
		nineslice_renderable.atlas_override = INVALID_KTEXTURE;

		u32 len = darray_length(render_data->renderables);
		darray_insert_at(render_data->renderables, len - 1, nineslice_renderable);

		// darray_push(render_data->renderables, nineslice_renderable);
	}

	FLAG_SET(cursor_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, is_focused);

	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		base->clipping_area.stencil_data.render_data.model = ktransform_world_get(base->clipping_area.stencil_data.transform);
	} else if (base->clipping_area.type == KUI_CLIP_TYPE_SCISSOR) {
		vec3 pos = ktransform_world_position_get(base->ktransform);
		base->clipping_area.scissor_data.clipping_area = (rect_2di){
			.x = pos.x + typed_control->nslice.corner_size.x,
			.y = pos.y,
			.width = typed_control->size.x - (typed_control->nslice.corner_size.x * 2),
			.height = typed_control->size.y};
	}

	/* kgeometry* vg = &typed_control->clip_mask.clip_geometry; */

	// Render the highlight box manually so the clip mask can be attached to it.
	// This ensures the highlight boxis rendered and clipped before the cursor or other
	// children are drawn.
	if (FLAG_GET(highlight_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT)) {
		if (!highlight_base->render(state, typed_control->highlight_box, p_frame_data, render_data)) {
			KERROR("Failed to render highlight box for textbox '%s'", base->name);
			return false;
		}
	}

	// Attach clipping mask to highlight box, which would be the last element added.
	/* u32 renderable_count = darray_length(render_data->renderables);
	render_data->renderables[renderable_count - 1].clip_mask_render_data = &typed_control->clip_mask.render_data; */

	// Render the content label manually so the clip mask can be attached to it.
	// This ensures the content label is rendered and clipped before the cursor or other
	// children are drawn. Also render it last so it appears over the highlight box for clarity.
	if (!label_base->render(state, typed_control->content_label, p_frame_data, render_data)) {
		KERROR("Failed to render content label for textbox '%s'", base->name);
		return false;
	}

	/* kui_renderable clip_end_renderable = {.type = KUI_RENDERABLE_TYPE_CLIP_END};

	darray_push(render_data->renderables, clip_end_renderable); */

	return true;
}

const char *kui_textbox_text_get (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;

	return kui_label_text_get(state, typed_data->content_label);
}

void kui_textbox_text_set (kui_state *state, kui_control self, const char *text) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;

	if (string_length(text)) {
		if (typed_data->type == KUI_TEXTBOX_TYPE_FLOAT) {
			// Verify the text content. If numeric is required and it isn't numeric, blank it out.
			f32 f = 0;
			if (!string_to_f32(text, &f)) {
				text = "";
				KWARN("%s - Textbox '%s' is of type float, but input does not parse to float. Blanking out.", __FUNCTION__, base->name);
			}
		} else if (typed_data->type == KUI_TEXTBOX_TYPE_INT) {
			i64 i = 0;
			if (!string_to_i64(text, &i)) {
				KWARN("%s - Textbox '%s' is of type int, but input does not parse to int. Blanking out.", __FUNCTION__, base->name);
				text = "";
			}
		}
	}

	kui_label_text_set(state, typed_data->content_label, text);

	// Reset the cursor position when the text is set.
	typed_data->cursor_position = 0;
	kui_textbox_update_cursor_position(state, base);
}

void kui_textbox_i64_set (kui_state *state, kui_control self, i64 i) {
	const char *str = i64_to_string(i);
	kui_textbox_text_set(state, self, str);
	string_free(str);
}

void kui_textbox_f32_set (kui_state *state, kui_control self, f32 f) {
	const char *str = f32_to_string(f);
	kui_textbox_text_set(state, self, str);
	string_free(str);
}

void kui_textbox_delete_at_cursor (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;
	const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
	u32 len = string_length(entry_control_text);

	if (!len) {
		kui_label_text_set(state, typed_data->content_label, "");
		typed_data->cursor_position = 0;
		return;
	}

	char *str = string_duplicate(entry_control_text);
	if (typed_data->highlight_range.size == (i32)len) {
		// Whole range is selected, delete and reset cursor position.
		str = string_empty(str);
		typed_data->cursor_position = 0;
	} else {
		if (typed_data->highlight_range.size > 0) {
			// If there is a selection, delete it.
			string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
			typed_data->cursor_position = typed_data->highlight_range.offset;
		} else if (typed_data->cursor_position < len) {
			// Otherwise delete one character at the selection point.
			string_remove_at(str, entry_control_text, typed_data->cursor_position, 1);
		}
	}

	// Clear the highlight range.
	typed_data->highlight_range.offset = 0;
	typed_data->highlight_range.size = 0;
	kui_textbox_update_highlight_box(state, base);

	kui_label_text_set(state, typed_data->content_label, str);
	// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
	kfree((char *)str, len + 1, MEMORY_TAG_STRING);
	kui_textbox_update_cursor_position(state, base);
}

void kui_textbox_select_all (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;
	const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
	u32 len = string_length(entry_control_text);
	typed_data->highlight_range.size = len;
	typed_data->highlight_range.offset = 0;
	typed_data->cursor_position = len;
	kui_textbox_update_highlight_box(state, base);
	kui_textbox_update_cursor_position(state, base);
}

void kui_textbox_select_none (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;
	typed_data->highlight_range.size = 0;
	typed_data->highlight_range.offset = 0;
	typed_data->cursor_position = 0;
	kui_textbox_update_highlight_box(state, base);
	kui_textbox_update_cursor_position(state, base);
}

static f32 kui_textbox_calculate_cursor_offset (kui_state *state, u32 string_pos, const char *full_string, kui_textbox_control *typed_textbox_control) {
	if (string_pos == 0) {
		return 0;
	}

	char *copy = string_duplicate(full_string);
	u32 len = string_length(copy);
	char *mid_target = copy;
	string_mid(mid_target, full_string, 0, string_pos);

	vec2 size = vec2_zero();

	kui_base_control *label_base = kui_system_get_base(state, typed_textbox_control->content_label);
	kui_label_control *typed_label_control = (kui_label_control *)label_base;

	if (typed_label_control->type == FONT_TYPE_BITMAP) {
		font_system_bitmap_font_measure_string(state->font_system, typed_label_control->bitmap_font, mid_target, 0, &size);
	} else if (typed_label_control->type == FONT_TYPE_SYSTEM) {
		font_system_system_font_measure_string(state->font_system, typed_label_control->system_font, mid_target, 0, &size);
	} else {
		KFATAL("hwhat");
	}

	// Make sure to cleanup the string.
	// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
	kfree((char *)copy, len + 1, MEMORY_TAG_STRING);

	// Use the x-axis of the mesurement to place the cursor.
	return size.x;
}

static void kui_textbox_update_highlight_box (kui_state *state, kui_base_control *self) {
	kui_textbox_control *typed_control = (kui_textbox_control *)self;

	kui_base_control *highlight_base = kui_system_get_base(state, typed_control->highlight_box);
	/* kui_panel_control* typed_highlight_control = (kui_panel_control*)highlight_base; */

	if (typed_control->highlight_range.size == 0) {
		FLAG_SET(highlight_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, false);
		return;
	}

	kui_base_control *label_base = kui_system_get_base(state, typed_control->content_label);
	kui_label_control *typed_label_control = (kui_label_control *)label_base;

	FLAG_SET(highlight_base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, true);

	// Offset from the start of the string.
	f32 offset_start = kui_textbox_calculate_cursor_offset(state, typed_control->highlight_range.offset, typed_label_control->text, typed_control);
	f32 offset_end = kui_textbox_calculate_cursor_offset(state, typed_control->highlight_range.offset + typed_control->highlight_range.size, typed_label_control->text, typed_control);
	f32 width = offset_end - offset_start;
	f32 padding = typed_control->nslice.corner_size.x;
	f32 padding_y = typed_control->nslice.corner_size.y;

	vec3 initial_pos = ktransform_position_get(highlight_base->ktransform);
	// positive line height is below
	// negative should be above?
	initial_pos.y = padding_y - 1.0f;
	ktransform_position_set(highlight_base->ktransform, (vec3){padding + offset_start, initial_pos.y, initial_pos.z});
	ktransform_scale_set(highlight_base->ktransform, (vec3){width, 1.0f, 1.0f});
}

static void kui_textbox_update_cursor_position (kui_state *state, kui_base_control *self) {
	kui_textbox_control *typed_control = (kui_textbox_control *)self;

	kui_base_control *cursor_base = kui_system_get_base(state, typed_control->cursor);
	/* kui_panel_control* typed_cursor_control = (kui_panel_control*)cursor_base; */

	kui_base_control *label_base = kui_system_get_base(state, typed_control->content_label);
	kui_label_control *typed_label_control = (kui_label_control *)label_base;

	// Offset from the start of the string.
	f32 offset = kui_textbox_calculate_cursor_offset(state, typed_control->cursor_position, typed_label_control->text, typed_control);
	f32 padding = typed_control->nslice.corner_size.x;

	// The would-be cursor position, not yet taking padding into account.
	vec3 cursor_pos = {0};
	cursor_pos.x = offset + typed_control->text_view_offset;
	cursor_pos.y = 6.0f; // TODO: configurable

	// Ensure the cursor is within the bounds of the textbox.
	// Don't take the padding into account just yet.
	f32 clip_width = typed_control->size.x - (padding * 2);
	f32 clip_x_min = padding;
	f32 clip_x_max = clip_x_min + clip_width;
	f32 diff = 0;
	if (cursor_pos.x > clip_width) {
		diff = clip_width - cursor_pos.x;
		// Set the cursor right up against the edge, taking padding into account.
		cursor_pos.x = clip_x_max;
	} else if (cursor_pos.x < 0) {
		diff = 0 - cursor_pos.x;
		// Set the cursor right up against the edge, taking padding into account.
		cursor_pos.x = clip_x_min;
	} else {
		// Use the position as-is, but add padding.
		cursor_pos.x += padding;
	}
	// Save the view offset.
	typed_control->text_view_offset += diff;
	// Translate the label forward/backward to line up with the cursor, taking padding into account.
	vec3 label_pos = ktransform_position_get(label_base->ktransform);
	ktransform_position_set(label_base->ktransform, (vec3){padding + typed_control->text_view_offset, label_pos.y, label_pos.z});

	// Translate the cursor to it's new position.
	ktransform_position_set(cursor_base->ktransform, cursor_pos);
}

static b8 kui_textbox_on_key (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_textbox_event_listener *listener = listener_inst;
	kui_state *state = listener->state;

	kui_control handle = listener->control;
	kui_base_control *base = kui_system_get_base(state, handle);
	kui_textbox_control *typed_data = (kui_textbox_control *)base;

	if (state->focused.val != typed_data->base.handle.val) {
		return false;
	}

	u16 key_code = context.data.u16[0];
	if (code == EVENT_CODE_KEY_PRESSED) {
		b8 shift_held = input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT) || input_is_key_down(KEY_SHIFT);
#if KPLATFORM_APPLE
		// On Apple, consider "command" as control.
		// TODO: Need to think this through better for the future to not have platform code here.
		b8 ctrl_held = input_is_key_down(KEY_LSUPER) || input_is_key_down(KEY_RSUPER);
#else
		b8 ctrl_held = input_is_key_down(KEY_LCONTROL) || input_is_key_down(KEY_RCONTROL) || input_is_key_down(KEY_CONTROL);
#endif

		const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
		u32 len = string_length(entry_control_text);
		if (key_code == KEY_BACKSPACE) {
			if (len == 0) {
				kui_label_text_set(state, typed_data->content_label, "");
			} else if ((typed_data->cursor_position > 0 || typed_data->highlight_range.size > 0)) {
				char *str = string_duplicate(entry_control_text);
				if (typed_data->highlight_range.size > 0) {
					if (typed_data->highlight_range.size == (i32)len) {
						str = string_empty(str);
						typed_data->cursor_position = 0;
					} else {
						string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
						typed_data->cursor_position = typed_data->highlight_range.offset;
					}
					// Clear the highlight range.
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
					kui_textbox_update_highlight_box(state, base);
				} else {
					string_remove_at(str, entry_control_text, typed_data->cursor_position - 1, 1);
					typed_data->cursor_position--;
				}
				kui_label_text_set(state, typed_data->content_label, str);
				// NOTE: Cannot just do a string_free because it will be shorter than the actual memory allocated.
				kfree((char *)str, len + 1, MEMORY_TAG_STRING);
				kui_textbox_update_cursor_position(state, &typed_data->base);
			}
		} else if (key_code == KEY_DELETE) {
			kui_textbox_delete_at_cursor(state, typed_data->base.handle);
		} else if (key_code == KEY_LEFT) {
			if (typed_data->cursor_position > 0) {
				if (shift_held) {
					if (typed_data->highlight_range.size == 0) {
						typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
					}
					if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset) {
						typed_data->highlight_range.offset--;
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
					} else {
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
					}
					typed_data->cursor_position--;
				} else {
					if (typed_data->highlight_range.size > 0) {
						typed_data->cursor_position = typed_data->highlight_range.offset;
					} else {
						typed_data->cursor_position--;
					}
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
				}
				kui_textbox_update_highlight_box(state, base);
				kui_textbox_update_cursor_position(state, &typed_data->base);
			}
		} else if (key_code == KEY_RIGHT) {
			// NOTE: cursor position can go past the end of the str so backspacing works right.
			if (typed_data->cursor_position < len) {
				if (shift_held) {
					if (typed_data->highlight_range.size == 0) {
						typed_data->highlight_range.offset = (i32)typed_data->cursor_position;
					}
					if ((i32)typed_data->cursor_position == typed_data->highlight_range.offset + typed_data->highlight_range.size) {
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size + 1, 0, (i32)len);
					} else {
						typed_data->highlight_range.offset = KCLAMP(typed_data->highlight_range.offset + 1, 0, (i32)len);
						typed_data->highlight_range.size = KCLAMP(typed_data->highlight_range.size - 1, 0, (i32)len);
					}
					typed_data->cursor_position++;
				} else {
					if (typed_data->highlight_range.size > 0) {
						typed_data->cursor_position = typed_data->highlight_range.offset + typed_data->highlight_range.size;
					} else {
						typed_data->cursor_position++;
					}
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
				}

				kui_textbox_update_highlight_box(state, base);
				kui_textbox_update_cursor_position(state, &typed_data->base);
			}
		} else if (key_code == KEY_HOME) {
			if (shift_held) {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = typed_data->cursor_position;
			} else {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = 0;
			}
			typed_data->cursor_position = 0;
			kui_textbox_update_highlight_box(state, base);
			kui_textbox_update_cursor_position(state, &typed_data->base);
		} else if (key_code == KEY_END) {
			if (shift_held) {
				typed_data->highlight_range.offset = typed_data->cursor_position;
				typed_data->highlight_range.size = len - typed_data->cursor_position;
			} else {
				typed_data->highlight_range.offset = 0;
				typed_data->highlight_range.size = 0;
			}
			typed_data->cursor_position = len;
			kui_textbox_update_highlight_box(state, base);
			kui_textbox_update_cursor_position(state, &typed_data->base);
		} else {
			// Use A-Z and 0-9 as-is.
			char char_code = key_code;
			if ((key_code >= KEY_A && key_code <= KEY_Z)) {
				if (ctrl_held) {
					if (key_code == KEY_A) {
						char_code = 0;
						kui_textbox_select_all(state, typed_data->base.handle);
					}

					// Paste
					if (key_code == KEY_V) {
						// Request a paste. This will consume the event.
						event_register_single(EVENT_CODE_CLIPBOARD_PASTE, &typed_data->base, kui_textbox_on_paste);
						platform_request_clipboard_content(engine_active_window_get());
						return true;
					}

					// Copy/cut
					if (key_code == KEY_C || key_code == KEY_X) {
						if (typed_data->highlight_range.size > 0) {

							// Copy the selected text to the clipboard, if any is selected.
							const range32 *hl = &typed_data->highlight_range;
							char *buf = kallocate(hl->size + 1, MEMORY_TAG_STRING);

							const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
							for (i32 i = 0; i < hl->size; ++i) {
								buf[i] = entry_control_text[hl->offset + i];
							}
							buf[hl->size + 1] = 0;

							platform_clipboard_content_set(engine_active_window_get(), KCLIPBOARD_CONTENT_TYPE_STRING, hl->size + 1, buf);

							// If cutting, remove the selected text from the textbox.
							if (key_code == KEY_X) {
								kui_textbox_delete_at_cursor(state, typed_data->base.handle);
							}
						}
						return true;
					}
				}
				// TODO: check caps lock.
				if (!shift_held && !ctrl_held) {
					char_code = key_code + 32;
				}
			} else if ((key_code >= KEY_0 && key_code <= KEY_9)) {
				if (shift_held) {
					// NOTE: this handles US standard keyboard layouts.
					// Will need to handle other layouts as well.
					switch (key_code) {
					case KEY_0:
						char_code = ')';
						break;
					case KEY_1:
						char_code = '!';
						break;
					case KEY_2:
						char_code = '@';
						break;
					case KEY_3:
						char_code = '#';
						break;
					case KEY_4:
						char_code = '$';
						break;
					case KEY_5:
						char_code = '%';
						break;
					case KEY_6:
						char_code = '^';
						break;
					case KEY_7:
						char_code = '&';
						break;
					case KEY_8:
						char_code = '*';
						break;
					case KEY_9:
						char_code = '(';
						break;
					}
				}
			} else {
				switch (key_code) {
				case KEY_SPACE:
					char_code = key_code;
					break;
				case KEY_MINUS:
					char_code = shift_held ? '_' : '-';
					break;
				case KEY_EQUAL:
					char_code = shift_held ? '+' : '=';
					break;
				case KEY_PERIOD:
					char_code = shift_held ? '>' : '.';
					break;
				case KEY_COMMA:
					char_code = shift_held ? '<' : ',';
					break;
				case KEY_SLASH:
					char_code = shift_held ? '?' : '/';
					break;
				case KEY_QUOTE:
					char_code = shift_held ? '"' : '\'';
					break;
				case KEY_SEMICOLON:
					char_code = shift_held ? ':' : ';';
					break;
				case KEY_LBRACKET:
					char_code = shift_held ? '{' : '[';
					break;
				case KEY_RBRACKET:
					char_code = shift_held ? '}' : ']';
					break;
				case KEY_BACKSLASH:
					char_code = shift_held ? '|' : '\\';
					break;

				default:
					// Not valid for entry, use 0
					char_code = 0;
					break;
				}
			}

			if (char_code != 0) {
				const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
				u32 len = string_length(entry_control_text);

				// Need to verify that the input is valid before doing it. Otherwise boot.
				if (typed_data->type == KUI_TEXTBOX_TYPE_INT || typed_data->type == KUI_TEXTBOX_TYPE_FLOAT) {
					if (!codepoint_is_numeric(char_code) && (char_code != '.' && char_code != '-' && char_code != '+')) {
						KWARN("not numeric or .-+");
						return true;
					}

					// Each of these is only allowed once.
					if (char_code == '.' || char_code == '-' || char_code == '+') {
						i32 index = string_index_of(entry_control_text, char_code);
						if (index != -1) {
							// Only if not about to be replaced.
							if (typed_data->highlight_range.size == 0 || !IS_IN_RANGE((u32)index, typed_data->cursor_position, (u32)typed_data->highlight_range.offset)) {
								KWARN("duplicate found: '%c'", char_code);
								return true;
							}
						}
					}

					// Decimals are only allowed for float types.
					if (char_code == '.' && typed_data->type == KUI_TEXTBOX_TYPE_INT) {
						KWARN("Decimal not allowed on int textboxes.");
						return true;
					}
				}
				char *str = kallocate(sizeof(char) * (len + 2), MEMORY_TAG_STRING);

				// If text is highlighted, delete highlighted text, then insert at cursor position.
				if (typed_data->highlight_range.size > 0) {
					if (typed_data->highlight_range.size == (i32)len) {
						str = string_empty(str);
						typed_data->cursor_position = 0;
					} else {
						string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
						typed_data->cursor_position = typed_data->highlight_range.offset;
					}
				} else {
					if (entry_control_text) {
						string_copy(str, entry_control_text);
					}
				}

				string_insert_char_at(str, str, typed_data->cursor_position, char_code);
				/* string_format_unsafe(str, "%s%c", entry_control_text, char_code); */

				kui_label_text_set(state, typed_data->content_label, str);
				kfree(str, len + 2, MEMORY_TAG_STRING);
				if (typed_data->highlight_range.size > 0) {
					// Clear the highlight range.
					typed_data->highlight_range.offset = 0;
					typed_data->highlight_range.size = 0;
					kui_textbox_update_highlight_box(state, base);
				}

				typed_data->cursor_position++;
				kui_textbox_update_cursor_position(state, &typed_data->base);
			}
		}
	}
	if (typed_data->base.on_key) {
		kui_keyboard_event evt = {0};
		evt.key = key_code;
		evt.type = code == EVENT_CODE_KEY_PRESSED ? KUI_KEYBOARD_EVENT_TYPE_PRESS : KUI_KEYBOARD_EVENT_TYPE_RELEASE;
		typed_data->base.on_key(state, typed_data->base.handle, evt);
		return true;
	}

	return false;
}

static b8 kui_textbox_on_paste (u16 code, void *sender, void *listener_inst, event_context context) {

	kclipboard_context *clip = context.data.custom_data.data;

	// Only handle string data.
	if (clip->content_type == KCLIPBOARD_CONTENT_TYPE_STRING) {
		kui_base_control *self = listener_inst;

		kui_textbox_control *typed_data = (kui_textbox_control *)self;
		kui_state *state = typed_data->listener->state;

		const char *entry_control_text = kui_label_text_get(state, typed_data->content_label);
		u32 insert_length = string_length(clip->content);

		// Verify the text content. If numeric is required and it isn't numeric, cancel the paste.
		if (typed_data->type == KUI_TEXTBOX_TYPE_FLOAT) {
			f32 f = 0;
			if (!string_to_f32(clip->content, &f)) {
				// Consider the event handled, don't let anything else have it.
				return true;
			}
		} else if (typed_data->type == KUI_TEXTBOX_TYPE_INT) {
			i64 i = 0;
			if (!string_to_i64(clip->content, &i)) {
				// Consider the event handled, don't let anything else have it.
				return true;
			}
		}

		u32 len = string_length(entry_control_text);
		char *str = kallocate(sizeof(char) * (len + insert_length + 1), MEMORY_TAG_STRING);

		// If text is highlighted, delete highlighted text, then insert at cursor position.
		if (typed_data->highlight_range.size > 0) {
			if (typed_data->highlight_range.size == (i32)len) {
				str = string_empty(str);
				typed_data->cursor_position = 0;
			} else {
				string_remove_at(str, entry_control_text, typed_data->highlight_range.offset, typed_data->highlight_range.size);
				typed_data->cursor_position = typed_data->highlight_range.offset;
			}
		} else {
			string_copy(str, entry_control_text);
		}

		string_insert_str_at(str, str, typed_data->cursor_position, clip->content);

		kui_label_text_set(state, typed_data->content_label, str);
		kfree(str, len + insert_length + 1, MEMORY_TAG_STRING);
		if (typed_data->highlight_range.size > 0) {
			// Clear the highlight range.
			typed_data->highlight_range.offset = 0;
			typed_data->highlight_range.size = 0;
			kui_textbox_update_highlight_box(state, self);
		}

		typed_data->cursor_position += insert_length;
		kui_textbox_update_cursor_position(state, self);
	}

	// Consider the event handled, don't let anything else have it.
	return true;
}

static void kui_textbox_on_focus (struct kui_state *state, kui_control self) {
	kui_textbox_select_all(state, self);
}

static void kui_textbox_on_unfocus (struct kui_state *state, kui_control self) {
	kui_textbox_select_none(state, self);
}
