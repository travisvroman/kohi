#include "kui_panel.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/kshader_system.h>

#include "debug/kassert.h"
#include "kui_defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "renderer/kui_renderer.h"

kui_control kui_panel_control_create (kui_state *state, const char *name, vec2 size, vec4 colour) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_PANEL);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);

	kui_panel_control *typed_control = (kui_panel_control *)base;

	base->bounds = vec4_create(0, 0, size.x, size.y);

	// Reasonable defaults.
	typed_control->colour = colour;
	typed_control->is_dirty = true;

	// Assign function pointers.
	base->destroy = kui_panel_control_destroy;
	base->update = kui_panel_control_update;
	base->render = kui_panel_control_render;

	// load

	// Generate UVs.
	f32 xmin, ymin, xmax, ymax;
	generate_uvs_from_image_coords(state->atlas_texture_size.x, state->atlas_texture_size.y, state->atlas.panel.extents.min.x, state->atlas.panel.extents.min.y, &xmin, &ymin);
	generate_uvs_from_image_coords(state->atlas_texture_size.x, state->atlas_texture_size.y, state->atlas.panel.extents.max.x, state->atlas.panel.extents.max.y, &xmax, &ymax);

	// Create a simple plane.
	typed_control->g = geometry_generate_quad(base->bounds.width, base->bounds.height, xmin, xmax, ymin, ymax, kname_create(base->name));
	if (!renderer_geometry_upload(&typed_control->g)) {
		KERROR("kui_panel_control_load - Failed to upload geometry quad");
		kui_base_control_destroy(state, &handle);
		return handle;
	}

	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	typed_control->binding_instance_id = INVALID_ID;
	typed_control->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	if (typed_control->binding_instance_id == INVALID_ID) {
		KFATAL("Unable to acquire shader binding set resources for label.");
		kui_base_control_destroy(state, &handle);
	}
	return handle;
}

void kui_panel_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_panel_control *typed_control = (kui_panel_control *)base;

	renderer_geometry_destroy(&typed_control->g);
	geometry_destroy(&typed_control->g);

	kui_base_control_destroy(state, self);
}

b8 kui_panel_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 kui_panel_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_panel_control *typed_control = (kui_panel_control *)base;

	if (typed_control->is_dirty) {
		renderer_geometry_vertex_update(&typed_control->g, 0, typed_control->g.vertex_count, typed_control->g.vertices, true);
		typed_control->is_dirty = false;
	}

	if (typed_control->g.vertices) {
		kui_renderable renderable = {0};
		renderable.render_data.unique_id = 0;
		renderable.render_data.vertex_count = typed_control->g.vertex_count;
		renderable.render_data.vertex_element_size = typed_control->g.vertex_element_size;
		renderable.render_data.vertex_buffer_offset = typed_control->g.vertex_buffer_offset;
		renderable.render_data.index_count = typed_control->g.index_count;
		renderable.render_data.index_element_size = typed_control->g.index_element_size;
		renderable.render_data.index_buffer_offset = typed_control->g.index_buffer_offset;
		renderable.render_data.model = ktransform_world_get(base->ktransform);
		renderable.render_data.diffuse_colour = typed_control->colour;

		renderable.binding_instance_id = typed_control->binding_instance_id;
		renderable.atlas_override = INVALID_KTEXTURE;

		darray_push(render_data->renderables, renderable);
	}

	return true;
}
vec2 kui_panel_size (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);

	return (vec2){base->bounds.width, base->bounds.height};
}

void kui_panel_set_height (kui_state *state, kui_control self, f32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_panel_control_resize(state, self, (vec2){base->bounds.width, height});
}
void kui_panel_set_width (kui_state *state, kui_control self, f32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_panel_control_resize(state, self, (vec2){width, base->bounds.height});
}

b8 kui_panel_control_resize (kui_state *state, kui_control self, vec2 new_size) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);

	kui_panel_control *typed_control = (kui_panel_control *)base;

	base->bounds.width = new_size.x;
	base->bounds.height = new_size.y;
	vertex_2d *vertices = typed_control->g.vertices;
	vertices[1].position.y = new_size.y;
	vertices[1].position.x = new_size.x;
	vertices[2].position.y = new_size.y;
	vertices[3].position.x = new_size.x;
	typed_control->is_dirty = true;

	return true;
}
