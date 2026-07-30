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

static b8 kui_button_internal_mouse_out (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_button_internal_mouse_over (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_button_internal_mouse_down (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_button_internal_mouse_up (kui_state *state, kui_control self, struct kui_mouse_event event);
static void recenter_text (kui_state *state, kui_control self);

void button_create_internal (kui_state *state, kui_button_control *typed_data, kui_button_type button_type) {
	kui_base_control *base = (kui_base_control *)typed_data;

	// Reasonable defaults.
	typed_data->colour = vec4_one();

	// Assign function pointers.
	base->destroy = kui_button_control_destroy;
	base->update = kui_button_control_update;
	base->render = kui_button_control_render;

	base->internal_mouse_down = kui_button_internal_mouse_down;
	base->internal_mouse_up = kui_button_internal_mouse_up;
	base->internal_mouse_out = kui_button_internal_mouse_out;
	base->internal_mouse_over = kui_button_internal_mouse_over;

	typed_data->button_type = button_type;

	base->bounds.x = 0.0f;
	base->bounds.y = 0.0f;
	base->bounds.width = 200;
	base->bounds.height = 40;

	kui_atlas_button_control_config *atlas_config = &state->atlas.button;
	switch (button_type) {
	case KUI_BUTTON_TYPE_BASIC:
	case KUI_BUTTON_TYPE_TEXT:
		atlas_config = &state->atlas.button;
		break;
	case KUI_BUTTON_TYPE_UPARROW:
		atlas_config = &state->atlas.button_uparrow;
		base->bounds.width = 30;
		base->bounds.height = 30;
		break;
	case KUI_BUTTON_TYPE_DOWNARROW:
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
}

kui_control kui_button_control_create (kui_state *state, const char *name) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_BUTTON);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	button_create_internal(state, typed_data, KUI_BUTTON_TYPE_BASIC);

	return handle;
}

kui_control kui_button_control_create_uparrow (kui_state *state, const char *name) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_BUTTON);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	button_create_internal(state, typed_data, KUI_BUTTON_TYPE_UPARROW);

	return handle;
}

kui_control kui_button_control_create_downarrow (kui_state *state, const char *name) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_BUTTON);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	button_create_internal(state, typed_data, KUI_BUTTON_TYPE_DOWNARROW);

	return handle;
}

kui_control kui_button_control_create_with_text (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text_content) {
	kui_control handle = kui_button_control_create(state, name);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	typed_data->button_type = KUI_BUTTON_TYPE_TEXT;

	// Add a label control.
	char *buffer = string_format("%s_text_label", name);
	typed_data->label = kui_label_control_create(state, buffer, type, font_name, font_size, text_content);
	string_free(buffer);

	kui_base_control *label_base = kui_system_get_base(state, typed_data->label);
	KASSERT(label_base);

	FLAG_SET(label_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);
	kui_system_control_add_child(state, handle, typed_data->label);

	recenter_text(state, handle);

	return handle;
}

void kui_button_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	nine_slice_destroy(&typed_data->nslice);

	kui_base_control_destroy(state, self);
}

b8 kui_button_control_height_set (kui_state *state, kui_control self, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	typed_data->nslice.size.y = height;

	base->bounds.height = height;

	nine_slice_update(&typed_data->nslice, 0);

	recenter_text(state, self);

	return true;
}

b8 kui_button_control_width_set (kui_state *state, kui_control self, i32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	typed_data->nslice.size.x = width;

	base->bounds.width = width;

	nine_slice_update(&typed_data->nslice, 0);

	recenter_text(state, self);

	return true;
}

b8 kui_button_control_text_set (kui_state *state, kui_control self, const char *text) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	if (typed_data->button_type == KUI_BUTTON_TYPE_TEXT) {
		kui_label_text_set(state, typed_data->label, text);
		recenter_text(state, self);
	} else {
		KWARN("%s - called on a non-text button. Nothing to do.");
		return false;
	}

	return true;
}

const char *kui_button_control_text_get (kui_state *state, const kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	return kui_label_text_get(state, typed_data->label);
}

b8 kui_button_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 kui_button_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
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

	if (typed_data->button_type == KUI_BUTTON_TYPE_TEXT) {
		kui_base_control *label_base = kui_system_get_base(state, typed_data->label);
		if (!label_base->render(state, typed_data->label, p_frame_data, render_data)) {
			KERROR("Failed to render content label for button '%s'", base->name);
			return false;
		}
	}

	return true;
}

static kui_atlas_button_control_config *get_button_atlas_config_for_type (kui_state *state, kui_button_type button_type) {
	switch (button_type) {
	case KUI_BUTTON_TYPE_BASIC:
	case KUI_BUTTON_TYPE_TEXT:
		return &state->atlas.button;
	case KUI_BUTTON_TYPE_UPARROW:
		return &state->atlas.button_uparrow;
	case KUI_BUTTON_TYPE_DOWNARROW:
		return &state->atlas.button_downarrow;
	}
}

static b8 kui_button_internal_mouse_out (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	kui_atlas_button_control_mode_config *mode = &get_button_atlas_config_for_type(state, typed_data->button_type)->normal;
	typed_data->nslice.atlas_px_min.x = mode->extents.min.x;
	typed_data->nslice.atlas_px_min.y = mode->extents.min.y;
	typed_data->nslice.atlas_px_max.x = mode->extents.max.x;
	typed_data->nslice.atlas_px_max.y = mode->extents.max.y;
	nine_slice_update(&typed_data->nslice, 0);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_out ? base->on_mouse_out(state, self, event) : true;
}

static b8 kui_button_internal_mouse_over (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	kui_atlas_button_control_mode_config *mode = &get_button_atlas_config_for_type(state, typed_data->button_type)->hover;
	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_PRESSED_BIT)) {
		mode = &state->atlas.button.pressed;
	}
	typed_data->nslice.atlas_px_min.x = mode->extents.min.x;
	typed_data->nslice.atlas_px_min.y = mode->extents.min.y;
	typed_data->nslice.atlas_px_max.x = mode->extents.max.x;
	typed_data->nslice.atlas_px_max.y = mode->extents.max.y;
	nine_slice_update(&typed_data->nslice, 0);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_over ? base->on_mouse_over(state, self, event) : false;
}
static b8 kui_button_internal_mouse_down (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	kui_atlas_button_control_mode_config *mode = &get_button_atlas_config_for_type(state, typed_data->button_type)->pressed;
	typed_data->nslice.atlas_px_min.x = mode->extents.min.x;
	typed_data->nslice.atlas_px_min.y = mode->extents.min.y;
	typed_data->nslice.atlas_px_max.x = mode->extents.max.x;
	typed_data->nslice.atlas_px_max.y = mode->extents.max.y;
	nine_slice_update(&typed_data->nslice, 0);
	// Block event propagation by default. User events can override this.
	return base->on_mouse_down ? base->on_mouse_down(state, self, event) : false;
}
static b8 kui_button_internal_mouse_up (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;
	kui_atlas_button_control_config *atlas_config = get_button_atlas_config_for_type(state, typed_data->button_type);
	kui_atlas_button_control_mode_config *mode = &atlas_config->normal;
	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_HOVERED_BIT)) {
		mode = &atlas_config->hover;
	}
	typed_data->nslice.atlas_px_min.x = mode->extents.min.x;
	typed_data->nslice.atlas_px_min.y = mode->extents.min.y;
	typed_data->nslice.atlas_px_max.x = mode->extents.max.x;
	typed_data->nslice.atlas_px_max.y = mode->extents.max.y;
	nine_slice_update(&typed_data->nslice, 0);
	// Block event propagation by default. User events can override this.
	return base->on_mouse_up ? base->on_mouse_up(state, self, event) : false;
}

static void recenter_text (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_button_control *typed_data = (kui_button_control *)base;

	if (typed_data->button_type == KUI_BUTTON_TYPE_TEXT) {
		// Center the text. If the text is larger than the button, left-justify and clip it.
		// Also retain the z position, if set.
		vec2 text_size = kui_label_measure_string(state, typed_data->label);
		f32 offsetx = KMAX(0.0f, (base->bounds.width - text_size.x) * 0.5f);
		f32 offsety = KMAX(0.0f, (base->bounds.height - text_size.y) * 0.5f);
		// FIXME: This shouldn't be needed, but works for now...
		offsety *= -1.0f;

		kui_base_control *label_base = kui_system_get_base(state, typed_data->label);
		KASSERT(label_base);

		vec3 pos = ktransform_position_get(label_base->ktransform);
		ktransform_position_set(label_base->ktransform, (vec3){offsetx, offsety, pos.z});
	}
}
