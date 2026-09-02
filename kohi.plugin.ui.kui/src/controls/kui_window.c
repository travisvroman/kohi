#include "kui_button.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/kshader_system.h>

#include "controls/kui_label.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kui_defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "renderer/kui_renderer.h"
#include "renderer/nine_slice.h"
#include "strings/kname.h"
#include "systems/ktransform_system.h"
/*
void button_create_internal (kui_state *state, kui_window_control *typed_data, kui_window_type button_type) {
	kui_base_control *base = (kui_base_control *)typed_data;

	// Reasonable defaults.
	typed_data->colour = vec4_one();

	// Assign function pointers.
	base->destroy = kui_window_control_destroy;
	base->update = kui_window_control_update;
	base->render = kui_window_control_render;

	base->internal_mouse_down = kui_window_internal_mouse_down;
	base->internal_mouse_up = kui_window_internal_mouse_up;
	base->internal_mouse_out = kui_window_internal_mouse_out;
	base->internal_mouse_over = kui_window_internal_mouse_over;

	typed_data->button_type = button_type;

	base->bounds.x = 0.0f;
	base->bounds.y = 0.0f;
	base->bounds.width = 200;
	base->bounds.height = 40;

	kui_atlas_window_control_config *atlas_config = &state->atlas.button;
	switch (button_type) {
	case KUI_window_TYPE_BASIC:
	case KUI_window_TYPE_TEXT:
		atlas_config = &state->atlas.button;
		break;
	case KUI_window_TYPE_UPARROW:
		atlas_config = &state->atlas.button_uparrow;
		base->bounds.width = 30;
		base->bounds.height = 30;
		break;
	case KUI_window_TYPE_DOWNARROW:
		atlas_config = &state->atlas.button_downarrow;
		base->bounds.width = 30;
		base->bounds.height = 30;
		break;
	}

	uvec2 uatlas_size = state->atlas_texture_size;
	vec2i atlas_size = (vec2i){uatlas_size.x, uatlas_size.y};
	vec2 min = atlas_config->normal.extents.min;
	vec2 max = atlas_config->normal.extents.max;
	vec2i atlas_min = (vec2i){min.x, min.y};
	vec2i atlas_max = (vec2i){max.x, max.y};
	vec2i corner_px_size = (vec2i){atlas_config->normal.corner_px_size.x, atlas_config->normal.corner_px_size.y};
	vec2i corner_size = (vec2i){atlas_config->normal.corner_size.x, atlas_config->normal.corner_size.y};
	KASSERT(nine_slice_create(base->name, (vec2i){base->bounds.width, base->bounds.height}, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice));

	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	typed_data->binding_instance_id = INVALID_ID;
	typed_data->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	KASSERT(typed_data->binding_instance_id != INVALID_ID);
}*/

kui_control kui_window_control_create (kui_state *state, const char *name, kui_window_flags flags) {
	// TODO: finish internal create and call from here.
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_BUTTON);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	/* kui_window_control *typed_data = (kui_window_control *)base; */

	/* button_create_internal(state, typed_data, KUI_window_TYPE_BASIC); */

	return handle;
}

kui_control kui_window_control_create_with_title (kui_state *state, const char *name, kui_font_data font, const char *title, kui_window_flags flags) {
	// TODO: adjust and verify passed-in flags to make sure they are sane.
	kui_control handle = kui_window_control_create(state, name, flags);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;

	// Add a label control.
	char *buffer = string_format("%s_text_label", name);
	typed_data->title_text = kui_label_control_create(state, buffer, font.type, font.font_name, font.font_size, title);
	string_free(buffer);

	kui_base_control *title_base = kui_system_get_base(state, typed_data->title_text);
	KASSERT(title_base);

	FLAG_SET(title_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_system_control_add_child(state, handle, typed_data->title_text);

	return handle;
}

kui_control kui_window_control_create_dialog (kui_state *state, const char *name, kui_font_data font, const char *title, const char *body, kui_window_flags flags) {
	// TODO:

	return INVALID_KUI_CONTROL;
}

void kui_window_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;
	nine_slice_destroy(&typed_data->nslice);

	kui_base_control_destroy(state, self);
}

b8 kui_window_control_height_set (kui_state *state, kui_control self, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;

	typed_data->nslice.size.y = height;

	base->bounds.height = height;

	nine_slice_update(&typed_data->nslice, 0);

	// TODO: does layout need updating?
	// TODO: Notify content of this control's width change (may need relayout)

	return true;
}

b8 kui_window_control_width_set (kui_state *state, kui_control self, i32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;
	typed_data->nslice.size.x = width;

	base->bounds.width = width;

	nine_slice_update(&typed_data->nslice, 0);

	// TODO: does layout need updating?
	// Update title bar->close button pos?
	// NOTE: Another thing that could be solved by layout containers
	// TODO: Notify content of this control's width change (may need relayout)

	return true;
}

b8 kui_window_control_title_set (kui_state *state, kui_control self, const char *text) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;
	if (FLAG_GET(typed_data->flags, KUI_WINDOW_FLAG_HAS_TITLE)) {
		kui_label_text_set(state, typed_data->title_text, text);
	} else {
		KWARN("%s - called on a non-title window. Nothing to do.");
		return false;
	}

	return true;
}

const char *kui_window_control_title_get (kui_state *state, const kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_window_control *typed_data = (kui_window_control *)base;
	return kui_label_text_get(state, typed_data->title_text);
}

b8 kui_window_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 kui_window_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}
	/*
		kui_base_control *base = kui_system_get_base(state, self);
		KASSERT(base);
		kui_window_control *typed_data = (kui_window_control *)base;
		nine_slice_render_frame_prepare(&typed_data->nslice, p_frame_data);

		if (typed_data->nslice.vertex_data.elements) {
			kui_renderable renderable = {0};
			renderable.render_data.unique_id = 0;
			renderable.render_data.vertex_count = typed_data->nslice.vertex_data.element_count;
			renderable.render_data.vertex_element_size = typed_data->nslice.vertex_data.element_size;
			renderable.render_data.vertex_buffer_offset = typed_data->nslice.vertex_data.buffer_offset;
			renderable.render_data.index_count = typed_data->nslice.index_data.element_count;
			renderable.render_data.index_element_size = typed_data->nslice.index_data.element_size;
			renderable.render_data.index_buffer_offset = typed_data->nslice.index_data.buffer_offset;
			renderable.render_data.model = ktransform_world_get(base->ktransform);
			renderable.render_data.diffuse_colour = vec4_one(); // white. TODO: pull from object properties.

			renderable.binding_instance_id = typed_data->binding_instance_id;
			renderable.atlas_override = INVALID_KTEXTURE;

			darray_push(render_data->renderables, &renderable);
		}

		if (typed_data->button_type == KUI_window_TYPE_TEXT) {
			kui_base_control *label_base = kui_system_get_base(state, typed_data->label);
			if (!label_base->render(state, typed_data->label, p_frame_data, render_data)) {
				KERROR("Failed to render content label for button '%s'", base->name);
				return false;
			}
		}
		*/

	return true;
}
