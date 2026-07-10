#include "kui_scrollable.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/kshader_system.h>

#include "controls/kui_button.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kui_defines.h"
#include "kui_system.h"
#include "kui_types.h"
#include "math/math_types.h"
#include "renderer/kui_renderer.h"
#include "systems/ktransform_system.h"

static b8 dec_y_on_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 inc_y_on_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_mouse_wheel (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_y_drag_start (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_y_drag (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 on_y_drag_end (kui_state *state, kui_control self, struct kui_mouse_event event);

kui_control kui_scrollable_control_create (kui_state *state, const char *name, vec2 size, b8 scroll_x, b8 scroll_y) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_SCROLLABLE);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);

	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	base->bounds = vec4_create(0, 0, size.x, size.y);

	// Reasonable defaults.
	typed_control->scroll_x = true;
	typed_control->scroll_y = true;

	typed_control->kui_state = state;

	// Assign function pointers.
	base->destroy = kui_scrollable_control_destroy;
	base->update = kui_scrollable_control_update;
	base->render = kui_scrollable_control_render;

	// Clips child controls.
	FLAG_SET(base->flags, KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT, true);

	// Setup clipping area.
	base->clipping_area.type = KUI_CLIP_TYPE_SCISSOR;
	vec3 pos = ktransform_world_position_get(base->ktransform);
	// FIXME: This will break when scaled.
	base->clipping_area.scissor_data.clipping_area = (rect_2di){
		.x = pos.x,
		.y = pos.y,
		.width = base->bounds.width,
		.height = base->bounds.height};

	// NOTE: To use stencil buffer, do the below instead.
	/* base->clip_mask.stencil_data.reference_id = 1;
	base->clip_mask.stencil_data.clip_geometry = geometry_generate_quad(size.x, size.y, 0, 0, 0, 0, kname_create("scrollable_clipping_box"));
	KASSERT(renderer_geometry_upload(&base->clip_mask.stencil_data.clip_geometry));
	base->clip_mask.stencil_data.render_data.model = mat4_identity();
	base->clip_mask.stencil_data.render_data.unique_id = base->clip_mask.stencil_data.reference_id;
	base->clip_mask.stencil_data.render_data.vertex_count = base->clip_mask.stencil_data.clip_geometry.vertex_count;
	base->clip_mask.stencil_data.render_data.vertex_element_size = base->clip_mask.stencil_data.clip_geometry.vertex_element_size;
	base->clip_mask.stencil_data.render_data.vertex_buffer_offset = base->clip_mask.stencil_data.clip_geometry.vertex_buffer_offset;
	base->clip_mask.stencil_data.render_data.index_count = base->clip_mask.stencil_data.clip_geometry.index_count;
	base->clip_mask.stencil_data.render_data.index_element_size = base->clip_mask.stencil_data.clip_geometry.index_element_size;
	base->clip_mask.stencil_data.render_data.index_buffer_offset = base->clip_mask.stencil_data.clip_geometry.index_buffer_offset;
	base->clip_mask.stencil_data.render_data.diffuse_colour = vec4_zero(); // transparent;
	base->clip_mask.stencil_data.clip_ktransform = ktransform_create(0);
	ktransform_parent_set(base->clip_mask.stencil_data.clip_ktransform, base->ktransform); */

	const char *content_name = string_format("%s_content", name);
	typed_control->content_wrapper = kui_base_control_create(state, content_name, KUI_CONTROL_TYPE_BASE);
	string_free(content_name);

	kui_system_control_add_child(state, handle, typed_control->content_wrapper);

	// The content wrapper itself shouldn't block events.
	kui_base_control *content_wrapper_base = kui_system_get_base(state, typed_control->content_wrapper);
	FLAG_SET(content_wrapper_base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, false);

	vec2i atlas_size = (vec2i){state->atlas_texture_size.x, state->atlas_texture_size.y};

	// Acquire group resources for this control.
	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));

	// TODO: configurable
	typed_control->scrollbar_width = 30.0f;
	// Scrollbar background 9-slice
	/* vec2i corner_size; */
	{
		vec2 min = state->atlas.scrollbar.extents.min;
		vec2 max = state->atlas.scrollbar.extents.max;
		vec2i atlas_min = (vec2i){min.x, min.y};
		vec2i atlas_max = (vec2i){max.x, max.y};
		vec2 cps = state->atlas.scrollbar.corner_px_size;
		vec2 cs = state->atlas.scrollbar.corner_size;
		vec2i local_corner_px_size = (vec2i){cps.x, cps.y};
		vec2i local_corner_size = (vec2i){cs.x, cs.y};
		KASSERT(nine_slice_create(base->name, (vec2i){typed_control->scrollbar_width + 8, 100}, atlas_size, atlas_min, atlas_max, local_corner_px_size, local_corner_size, &typed_control->scrollbar_y.bg));
		typed_control->scrollbar_y.bg_transform = ktransform_create(0);
		ktransform_parent_set(typed_control->scrollbar_y.bg_transform, base->ktransform);

		/* corner_size = local_corner_size; */

		// Acquire binding set resources for this control.
		typed_control->scrollbar_y.bg_binding_instance_id = INVALID_ID;
		typed_control->scrollbar_y.bg_binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
		KASSERT(typed_control->scrollbar_y.bg_binding_instance_id != INVALID_ID);
	}

	const char *scroll_y_name = KNULL;

	scroll_y_name = string_format("%s_scroll_y_dec", name);
	typed_control->scrollbar_y.dec_button = kui_button_control_create_uparrow(state, scroll_y_name);
	string_free(scroll_y_name);
	kui_system_control_add_child(state, handle, typed_control->scrollbar_y.dec_button);
	kui_button_control_width_set(state, typed_control->scrollbar_y.dec_button, typed_control->scrollbar_width);
	kui_button_control_height_set(state, typed_control->scrollbar_y.dec_button, typed_control->scrollbar_width);
	kui_control_set_is_visible(state, typed_control->scrollbar_y.dec_button, false);
	kui_control_position_set(state, typed_control->scrollbar_y.dec_button, (vec3){size.x - typed_control->scrollbar_width, 4, 0});
	kui_control_set_on_click(state, typed_control->scrollbar_y.dec_button, dec_y_on_clicked);

	scroll_y_name = string_format("%s_scroll_y_inc", name);
	typed_control->scrollbar_y.inc_button = kui_button_control_create_downarrow(state, scroll_y_name);
	string_free(scroll_y_name);
	kui_system_control_add_child(state, handle, typed_control->scrollbar_y.inc_button);
	kui_button_control_width_set(state, typed_control->scrollbar_y.inc_button, typed_control->scrollbar_width);
	kui_button_control_height_set(state, typed_control->scrollbar_y.inc_button, typed_control->scrollbar_width);
	kui_control_set_is_visible(state, typed_control->scrollbar_y.inc_button, false);
	kui_control_position_set(state, typed_control->scrollbar_y.inc_button, (vec3){size.x - typed_control->scrollbar_width, base->bounds.height - typed_control->scrollbar_width - 4, 0});
	kui_control_set_on_click(state, typed_control->scrollbar_y.inc_button, inc_y_on_clicked);

	scroll_y_name = string_format("%s_scroll_y_thumb", name);
	typed_control->scrollbar_y.thumb_button = kui_button_control_create(state, scroll_y_name);
	string_free(scroll_y_name);
	kui_system_control_add_child(state, handle, typed_control->scrollbar_y.thumb_button);
	kui_button_control_width_set(state, typed_control->scrollbar_y.thumb_button, typed_control->scrollbar_width);
	kui_button_control_height_set(state, typed_control->scrollbar_y.thumb_button, typed_control->scrollbar_width);
	kui_control_set_is_visible(state, typed_control->scrollbar_y.thumb_button, false);
	kui_control_position_set(state, typed_control->scrollbar_y.thumb_button, (vec3){size.x - typed_control->scrollbar_width, base->bounds.height - (typed_control->scrollbar_width * 2) - 4, 0});
	kui_base_control *thumb_base = kui_system_get_base(state, typed_control->scrollbar_y.thumb_button);
	thumb_base->on_mouse_drag_begin = on_y_drag_start;
	thumb_base->on_mouse_drag = on_y_drag;
	thumb_base->on_mouse_drag_end = on_y_drag_end;

	base->on_mouse_wheel = on_mouse_wheel;

	return handle;
}

void kui_scrollable_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	// destroy clipping mask
	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		renderer_geometry_destroy(&base->clipping_area.stencil_data.geometry);
		geometry_destroy(&base->clipping_area.stencil_data.geometry);
	}

	nine_slice_destroy(&typed_control->scrollbar_y.bg);
	ktransform_destroy(&typed_control->scrollbar_y.bg_transform);

	kui_base_control_destroy(state, self);
}

b8 kui_scrollable_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	nine_slice_render_frame_prepare(&typed_control->scrollbar_y.bg, p_frame_data);

	return true;
}

b8 kui_scrollable_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	if (typed_control->is_dirty) {
		// TODO: reupload vertex geometry for clipping mask?
		/* renderer_geometry_vertex_update(&typed_control->g, 0, typed_control->g.vertex_count, typed_control->g.vertices, true); */
		typed_control->is_dirty = false;
	}

	// Render the nine-slice.
	b8 y_visible = typed_control->min_offset.y < 0;
	if (y_visible) {
		nine_slice *ns = &typed_control->scrollbar_y.bg;

		if (ns->vertex_data.elements) {
			kui_renderable nineslice_renderable = {0};
			nineslice_renderable.render_data.unique_id = 0;
			nineslice_renderable.render_data.vertex_count = ns->vertex_data.element_count;
			nineslice_renderable.render_data.vertex_element_size = ns->vertex_data.element_size;
			nineslice_renderable.render_data.vertex_buffer_offset = ns->vertex_data.buffer_offset;
			nineslice_renderable.render_data.index_count = ns->index_data.element_count;
			nineslice_renderable.render_data.index_element_size = ns->index_data.element_size;
			nineslice_renderable.render_data.index_buffer_offset = ns->index_data.buffer_offset;
			nineslice_renderable.render_data.model = ktransform_world_get(typed_control->scrollbar_y.bg_transform);
			nineslice_renderable.render_data.diffuse_colour = vec4_one();

			nineslice_renderable.binding_instance_id = typed_control->scrollbar_y.bg_binding_instance_id;
			nineslice_renderable.atlas_override = INVALID_KTEXTURE;

			darray_push(render_data->renderables, nineslice_renderable);
		}
	}

	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		base->clipping_area.stencil_data.render_data.model = ktransform_world_get(base->clipping_area.stencil_data.transform);
	} else if (base->clipping_area.type == KUI_CLIP_TYPE_SCISSOR) {
		vec3 pos = ktransform_world_position_get(base->ktransform);
		// FIXME: This will break when scaled.
		base->clipping_area.scissor_data.clipping_area = (rect_2di){
			.x = pos.x,
			.y = pos.y,
			.width = base->bounds.width,
			.height = base->bounds.height};
	}

	return true;
}
vec2 kui_scrollable_size (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);

	return (vec2){base->bounds.width, base->bounds.height};
}

void kui_scrollable_set_height (kui_state *state, kui_control self, f32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control_resize(state, self, (vec2){base->bounds.width, height});
}
void kui_scrollable_set_width (kui_state *state, kui_control self, f32 width) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control_resize(state, self, (vec2){width, base->bounds.height});
}

static void recalculate (kui_state *state, kui_scrollable_control *typed_control) {
	kui_base_control *container_base = kui_system_get_base(state, typed_control->content_wrapper);

	typed_control->min_offset.y = KMIN(0.0f, typed_control->base.bounds.height - container_base->bounds.height);
	typed_control->min_offset.x = KMIN(0.0f, typed_control->base.bounds.width - container_base->bounds.width);

	typed_control->offset = vec2_clamped(typed_control->offset, typed_control->min_offset, vec2_zero());
	ktransform_position_set(container_base->ktransform, (vec3){typed_control->offset.x, typed_control->offset.y, 0});

	f32 pct_y = (typed_control->offset.y / typed_control->min_offset.y);

	f32 min_y = typed_control->scrollbar_width + 4;
	f32 max_y = typed_control->base.bounds.height - (typed_control->scrollbar_width * 2) - 4;
	f32 pos_y = min_y + (pct_y * (max_y - min_y));
	f32 pos_x = typed_control->base.bounds.width - (typed_control->scrollbar_width + 4);
	kui_base_control *y_thumb_base = kui_system_get_base(state, typed_control->scrollbar_y.thumb_button);
	ktransform_position_set(y_thumb_base->ktransform, (vec3){pos_x, pos_y, 0});

	// Determine if we can scroll in any direction, and show that scrollbar.
	b8 y_visible = typed_control->min_offset.y < 0;
	/* kui_control_set_is_visible(state, typed_control->scrollbar_y.background, y_visible); */
	kui_control_set_is_visible(state, typed_control->scrollbar_y.dec_button, y_visible);
	kui_control_set_is_visible(state, typed_control->scrollbar_y.inc_button, y_visible);
	kui_control_set_is_visible(state, typed_control->scrollbar_y.thumb_button, y_visible);
}

b8 kui_scrollable_control_resize (kui_state *state, kui_control self, vec2 new_size) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	base->bounds.width = new_size.x;
	base->bounds.height = new_size.y;
	if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
		vertex_2d *vertices = base->clipping_area.stencil_data.geometry.vertices;
		vertices[1].position.y = new_size.y;
		vertices[1].position.x = new_size.x;
		vertices[2].position.y = new_size.y;
		vertices[3].position.x = new_size.x;
		renderer_geometry_vertex_update(&base->clipping_area.stencil_data.geometry, 0, 4, vertices, false);
	}

	/* typed_control->scrollbar_y.bg.size.x = new_size.x; */
	typed_control->scrollbar_y.bg.size.y = new_size.y;
	ktransform_position_set(typed_control->scrollbar_y.bg_transform, (vec3){new_size.x - (typed_control->scrollbar_width + 8), 0, 0});
	nine_slice_update(&typed_control->scrollbar_y.bg, 0);

	kui_control_position_set(state, typed_control->scrollbar_y.dec_button, (vec3){new_size.x - (typed_control->scrollbar_width + 4), 4, 0});
	kui_control_position_set(state, typed_control->scrollbar_y.inc_button, (vec3){new_size.x - (typed_control->scrollbar_width + 4), new_size.y - (typed_control->scrollbar_width) - 4, 0});

	recalculate(state, typed_control);

	return true;
}

kui_control kui_scrollable_control_get_content_container (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;
	return typed_control->content_wrapper;
}

void kui_scrollable_control_scroll_y (kui_state *state, kui_control self, f32 amount) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	typed_control->offset.y += amount;
	recalculate(state, typed_control);
}
void kui_scrollable_control_scroll_x (kui_state *state, kui_control self, f32 amount) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	typed_control->offset.x += amount;
	recalculate(state, typed_control);
}

void kui_scrollable_control_scroll_y_set (kui_state *state, kui_control self, f32 pos) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	pos = KCLAMP(pos, 0, 1);

	typed_control->offset.y = typed_control->min_offset.y * pos;
	recalculate(state, typed_control);
}
void kui_scrollable_control_scroll_x_set (kui_state *state, kui_control self, f32 pos) {
}

void kui_scrollable_set_content_size (kui_state *state, kui_control self, f32 width, f32 height) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;
	kui_base_control *container_base = kui_system_get_base(state, typed_control->content_wrapper);
	container_base->bounds.height = height;
	container_base->bounds.width = width;

	recalculate(state, typed_control);
}

b8 kui_scrollable_can_scroll_x (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;
	return typed_control->min_offset.x < 0;
}
b8 kui_scrollable_can_scroll_y (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;
	return typed_control->min_offset.y < 0;
}

static b8 dec_y_on_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *button_base = kui_system_get_base(state, self);
	KASSERT(button_base);
	kui_control parent = button_base->parent;

	kui_scrollable_control_scroll_y(state, parent, 40.0f);

	return false;
}

static b8 inc_y_on_clicked (struct kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *button_base = kui_system_get_base(state, self);
	KASSERT(button_base);
	kui_control parent = button_base->parent;

	kui_scrollable_control_scroll_y(state, parent, -40.0f);

	return false;
}

static b8 on_mouse_wheel (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;
	kui_scrollable_control_scroll_y(typed_control->kui_state, typed_control->base.handle, (f32)event.delta_z * 5.0f);
	return false;
}

static b8 on_y_drag_start (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *thumb_base = kui_system_get_base(state, self);
	KASSERT(thumb_base);
	kui_button_control *thumb = (kui_button_control *)thumb_base;

	kui_base_control *base = kui_system_get_base(state, thumb->base.parent);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	if (typed_control) {

		// Record the offset within the button.
		typed_control->scrollbar_y.drag_button_mouse_offset = event.local_y;
		typed_control->scrollbar_y.drag_button_offset_start = kui_control_position_get(state, self).y;

		KTRACE("drag start offset y: %f", (typed_control->scrollbar_width + 4) - event.local_y);
	}

	return false;
}

static b8 on_y_drag (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *thumb_base = kui_system_get_base(state, self);
	KASSERT(thumb_base);
	kui_button_control *thumb = (kui_button_control *)thumb_base;

	kui_base_control *base = kui_system_get_base(state, thumb->base.parent);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	if (typed_control) {

		// TODO: Bound this to the scrollbar, update scroll pct based on new position in scroll bar,
		// The set the scroll percentage manually before a recalculate().
		vec3 pos = kui_control_position_get(state, self);
		pos.y += event.delta_y;
		kui_control_position_set(state, self, pos);

		KTRACE("drag offset y: %f", (typed_control->scrollbar_width + 4) - event.local_y);
	}

	return false;
}

static b8 on_y_drag_end (kui_state *state, kui_control self, struct kui_mouse_event event) {
	kui_base_control *thumb_base = kui_system_get_base(state, self);
	KASSERT(thumb_base);
	kui_button_control *thumb = (kui_button_control *)thumb_base;

	kui_base_control *base = kui_system_get_base(state, thumb->base.parent);
	kui_scrollable_control *typed_control = (kui_scrollable_control *)base;

	if (typed_control) {
		KTRACE("drag end offset y: %f", (typed_control->scrollbar_width + 4) - event.local_y);
	}
	return false;
}
