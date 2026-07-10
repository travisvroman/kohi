#include "kui_frame.h"

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

#include "debug/kassert.h"
#include "kui_defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/math_types.h"
#include "renderer/kui_renderer.h"

kui_control kui_frame_control_create (kui_state *state, const char *name) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_FRAME);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;

	// Reasonable defaults.
	typed_control->size = (vec2i){200, 200};
	typed_control->colour = vec4_one();

	// Assign function pointers.
	base->destroy = kui_frame_control_destroy;
	base->update = kui_frame_control_update;
	base->render = kui_frame_control_render;

	// load

	vec2i atlas_size = (vec2i){state->atlas_texture_size.x, state->atlas_texture_size.y};

	{
		vec2 min = state->atlas.frame.extents.min;
		vec2 max = state->atlas.frame.extents.max;
		vec2i atlas_min = (vec2i){min.x, min.y};
		vec2i atlas_max = (vec2i){max.x, max.y};
		vec2 cps = state->atlas.frame.corner_px_size;
		vec2 cs = state->atlas.frame.corner_size;
		vec2i local_corner_px_size = (vec2i){cps.x, cps.y};
		vec2i local_corner_size = (vec2i){cs.x, cs.y};
		KASSERT(nine_slice_create(base->name, typed_control->size, atlas_size, atlas_min, atlas_max, local_corner_px_size, local_corner_size, &typed_control->nslice));
	}

	base->bounds.x = 0.0f;
	base->bounds.y = 0.0f;
	base->bounds.width = typed_control->size.x;
	base->bounds.height = typed_control->size.y;

	// Acquire group resources for this control.
	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));

	// Acquire binding set resources for this control.
	typed_control->binding_instance_id = INVALID_ID;
	typed_control->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	KASSERT(typed_control->binding_instance_id != INVALID_ID);

	return handle;
}

void kui_frame_control_destroy (kui_state *state, kui_control *self) {

	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;
	// unload

	nine_slice_destroy(&typed_control->nslice);

	kui_base_control_destroy(state, self);
}

b8 kui_frame_control_size_set (kui_state *state, kui_control self, i32 width, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;

	typed_control->size.x = width;
	typed_control->size.y = height;
	typed_control->nslice.size.x = width;
	typed_control->nslice.size.y = height;

	base->bounds.height = height;
	base->bounds.width = width;

	nine_slice_update(&typed_control->nslice, 0);

	return true;
}
b8 kui_frame_control_width_set (kui_state *state, kui_control self, i32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;
	return kui_frame_control_size_set(state, self, width, typed_control->size.y);
}
b8 kui_frame_control_height_set (kui_state *state, kui_control self, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;
	return kui_frame_control_size_set(state, self, typed_control->size.x, height);
}

void kui_frame_control_colour_set (kui_state *state, kui_control self, colour4 colour) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;
	typed_control->colour = colour;
}

b8 kui_frame_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;

	nine_slice_render_frame_prepare(&typed_control->nslice, p_frame_data);

	return true;
}

b8 kui_frame_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	b8 is_focused = kui_system_is_control_focused(state, self);

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_frame_control *typed_control = (kui_frame_control *)base;

	// Render the nine-slice.
	nine_slice *ns = &typed_control->nslice;

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

	return true;
}
