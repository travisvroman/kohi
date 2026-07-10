#include "image_box_control.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "kui_defines.h"
#include "kui_types.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/kshader_system.h"
#include "systems/texture_system.h"

kui_control kui_image_box_control_create (kui_state *state, const char *name, vec2i size) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_IMAGE_BOX);
	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	// Assign function pointers.
	base->destroy = kui_image_box_control_destroy;
	base->update = kui_image_box_control_update;
	base->render = kui_image_box_control_render;

	base->bounds.x = 0.0f;
	base->bounds.y = 0.0f;
	base->bounds.width = size.x;
	base->bounds.height = size.y;

	typed_data->geometry = geometry_generate_plane_2d(size.x, size.y, 1, 1, 1.0f, 1.0f, kname_create("image_box_geometry"), false);
	KASSERT(renderer_geometry_upload(&typed_data->geometry));

	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	typed_data->binding_instance_id = INVALID_ID;
	typed_data->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	KASSERT(typed_data->binding_instance_id != INVALID_ID);

	typed_data->texture = INVALID_KTEXTURE;

	return handle;
}
void kui_image_box_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;
	renderer_geometry_destroy(&typed_data->geometry);
	geometry_destroy(&typed_data->geometry);

	kui_base_control_destroy(state, self);
}

void kui_image_box_control_height_set (kui_state *state, kui_control self, i32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	vertex_2d *verts = typed_data->geometry.vertices;
	verts[1].position.y = height;
	verts[2].position.y = height;

	// Upload the new vertex data.
	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	u32 size = typed_data->geometry.vertex_element_size * typed_data->geometry.vertex_count;
	if (!renderer_renderbuffer_load_range(renderer_system, vertex_buffer, typed_data->geometry.vertex_buffer_offset, size, typed_data->geometry.vertices, false)) {
		KERROR("renderer_renderbuffer_load_range failed to upload to the vertex buffer!");
	}
}

void kui_image_box_control_width_set (kui_state *state, kui_control self, i32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	vertex_2d *verts = typed_data->geometry.vertices;
	verts[1].position.x = width;
	verts[3].position.x = width;

	// Upload the new vertex data.
	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	u32 size = typed_data->geometry.vertex_element_size * typed_data->geometry.vertex_count;
	if (!renderer_renderbuffer_load_range(renderer_system, vertex_buffer, typed_data->geometry.vertex_buffer_offset, size, typed_data->geometry.vertices, false)) {
		KERROR("renderer_renderbuffer_load_range failed to upload to the vertex buffer!");
	}
}
b8 kui_image_box_control_texture_set_by_name (kui_state *state, kui_control self, kname image_asset_name, kname image_asset_package_name) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	// HACK: trying sync
	ktexture t = texture_acquire_from_package_sync(image_asset_name, image_asset_package_name);
	if (t != INVALID_KTEXTURE) {
		typed_data->texture = t;
		return true;
	}

	return false;
}
b8 kui_image_box_control_texture_set (kui_state *state, kui_control self, ktexture texture) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;
	if (texture != INVALID_KTEXTURE) {
		typed_data->texture = texture;
		return true;
	}

	return false;
}
ktexture kui_image_box_control_texture_get (kui_state *state, const kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;
	return typed_data->texture;
}

void kui_image_box_control_set_rect (kui_state *state, kui_control self, rect_2di rect) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	u32 width, height;
	if (typed_data->texture == INVALID_KTEXTURE) {
		width = state->atlas_texture_size.x;
		height = state->atlas_texture_size.y;
	} else {
		texture_dimensions_get(typed_data->texture, &width, &height);
	}

	vertex_2d *verts = typed_data->geometry.vertices;
	// top left
	verts[0].texcoord.x = (rect.x / (f32)width);
	verts[0].texcoord.y = (rect.y / (f32)height);

	// bottom right
	verts[1].texcoord.x = ((rect.x + rect.width) / (f32)width);
	verts[1].texcoord.y = ((rect.y + rect.height) / (f32)height);

	// bottom left
	verts[2].texcoord.x = (rect.x / (f32)width);
	verts[2].texcoord.y = ((rect.y + rect.height) / (f32)height);

	// top right
	verts[3].texcoord.x = ((rect.x + rect.width) / (f32)width);
	verts[3].texcoord.y = (rect.y / (f32)height);

	// Upload the new vertex data.
	struct renderer_system_state *renderer_system = engine_systems_get()->renderer_system;
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(renderer_system, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	u32 size = typed_data->geometry.vertex_element_size * typed_data->geometry.vertex_count;
	if (!renderer_renderbuffer_load_range(renderer_system, vertex_buffer, typed_data->geometry.vertex_buffer_offset, size, typed_data->geometry.vertices, false)) {
		KERROR("renderer_renderbuffer_load_range failed to upload to the vertex buffer!");
	}
}

b8 kui_image_box_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	return true;
}

b8 kui_image_box_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_image_box_control *typed_data = (kui_image_box_control *)base;

	if (typed_data->geometry.vertices) {
		kui_renderable renderable = {0};
		renderable.render_data.unique_id = 0;
		renderable.render_data.vertex_count = typed_data->geometry.vertex_count;
		renderable.render_data.vertex_element_size = typed_data->geometry.vertex_element_size;
		renderable.render_data.vertex_buffer_offset = typed_data->geometry.vertex_buffer_offset;
		renderable.render_data.index_count = typed_data->geometry.index_count;
		renderable.render_data.index_element_size = typed_data->geometry.index_element_size;
		renderable.render_data.index_buffer_offset = typed_data->geometry.index_buffer_offset;
		renderable.render_data.model = ktransform_world_get(base->ktransform);
		renderable.render_data.diffuse_colour = vec4_one(); // white. TODO: pull from object properties.

		renderable.binding_instance_id = typed_data->binding_instance_id;
		renderable.atlas_override = typed_data->texture;

		darray_push(render_data->renderables, renderable);
	}

	return true;
}
