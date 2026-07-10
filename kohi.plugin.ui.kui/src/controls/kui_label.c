#include "kui_label.h"
#include "kui_types.h"

#include <containers/darray.h>
#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/kshader_system.h>

#include "kui_defines.h"
#include "kui_system.h"
#include "renderer/kui_renderer.h"

static b8 regenerate_label_geometry (kui_state *state, const kui_control self, font_geometry *pending_data);

kui_control kui_label_control_create (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text) {
	return kui_label_control_create_ex(state, name, type, font_name, font_size, text, 0, KUI_LABEL_FLAG_NONE);
}

kui_control kui_label_control_create_ex (kui_state *state, const char *name, font_type type, kname font_name, u16 font_size, const char *text, f32 max_width, kui_label_flags flags) {
	kui_control handle = kui_base_control_create(state, name, KUI_CONTROL_TYPE_LABEL);

	kui_base_control *base = kui_system_get_base(state, handle);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;

	// Reasonable defaults.
	typed_control->colour = vec4_one();
	typed_control->flags = flags;
	if (!max_width && (FLAG_GET(flags, KUI_LABEL_FLAG_TRUNCATE_BIT) || FLAG_GET(flags, KUI_LABEL_FLAG_TRUNCATE_ELLIPSIS_BIT) || FLAG_GET(flags, KUI_LABEL_FLAG_WRAP_BIT))) {
		KWARN("%s - max_width cannot be zero when truncation or wrap flags are set. Defaulting to width of 100.", __FUNCTION__);
		max_width = 100.0f;
	}
	typed_control->max_width = max_width;

	// Assign function pointers.
	base->destroy = kui_label_control_destroy;
	base->update = kui_label_control_update;
	base->render = kui_label_control_render;

	// Assign the type first
	typed_control->type = type;

	// Acquire the font of the correct type and assign its internal data.
	// This also gets the atlas texture.
	switch (typed_control->type) {
	case FONT_TYPE_BITMAP:
		KASSERT(font_system_bitmap_font_acquire(state->font_system, font_name, &typed_control->bitmap_font));
		break;
	case FONT_TYPE_SYSTEM:
		KASSERT(font_system_system_font_acquire(state->font_system, font_name, font_size, &typed_control->system_font));
		// Verify atlas has the glyphs needed.
		KASSERT(font_system_system_font_verify_atlas(state->font_system, typed_control->system_font, text));
		break;
	}

	typed_control->vertex_buffer_offset = INVALID_ID_U64;
	typed_control->vertex_buffer_size = INVALID_ID_U64;
	typed_control->index_buffer_offset = INVALID_ID_U64;
	typed_control->index_buffer_size = INVALID_ID_U64;

	// Default quad count is 0 until the first geometry regeneration happens.
	typed_control->quad_count = 0;

	// Set text if applicable.
	if (text && string_length(text) > 0) {
		kui_label_text_set(state, handle, text);
	} else {
		kui_label_text_set(state, handle, KNULL);
	}

	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	typed_control->binding_instance_id = INVALID_ID;
	typed_control->binding_instance_id = kshader_acquire_binding_set_instance(kui_shader, 1);
	KASSERT(typed_control->binding_instance_id != INVALID_ID);

	// load
	if (typed_control->text && typed_control->text[0] != 0) {
		// Flag it as dirty to ensure it gets updated on the next frame.
		typed_control->is_dirty = true;
	}

	return handle;
}

void kui_label_control_destroy (kui_state *state, kui_control *self) {
	// unload
	kui_base_control *base = kui_system_get_base(state, *self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;

	if (typed_control->text) {
		string_free(typed_control->text);
		typed_control->text = KNULL;
	}

	// Free from the vertex buffer.
	krenderbuffer vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	if (typed_control->vertex_buffer_offset != INVALID_ID_U64) {
		if (typed_control->max_text_length > 0) {
			renderer_renderbuffer_free(state->renderer, vertex_buffer, sizeof(vertex_2d) * 4 * typed_control->max_quad_count, typed_control->vertex_buffer_offset);
		}
		typed_control->vertex_buffer_offset = INVALID_ID_U64;
	}

	// Free from the index buffer.
	if (typed_control->index_buffer_offset != INVALID_ID_U64) {
		krenderbuffer index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));
		static const u64 quad_index_size = (sizeof(u32) * 6);
		if (typed_control->max_text_length > 0 || typed_control->index_buffer_offset != INVALID_ID_U64) {
			renderer_renderbuffer_free(state->renderer, index_buffer, quad_index_size * typed_control->max_quad_count, typed_control->index_buffer_offset);
		}
		typed_control->index_buffer_offset = INVALID_ID_U64;
	}

	// Release group/draw resources.
	kshader kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	kshader_release_binding_set_instance(kui_shader, 1, typed_control->binding_instance_id);
	typed_control->binding_instance_id = INVALID_ID;

	kui_base_control_destroy(state, self);
}

b8 kui_label_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (!kui_base_control_update(state, self, p_frame_data)) {
		return false;
	}

	//

	return true;
}

b8 kui_label_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;

	// render render_prepare

	if (typed_control->is_dirty) {
		if (typed_control->type == FONT_TYPE_SYSTEM) {
			// Verify atlas has the glyphs needed.
			if (!font_system_system_font_verify_atlas(state->font_system, typed_control->system_font, typed_control->text)) {
				KERROR("Font atlas verification failed.");
				typed_control->quad_count = 0; // Keep it from drawing.
				goto kui_label_frame_prepare_cleanup;
			}
		}

		font_geometry new_geometry = {0};
		if (!regenerate_label_geometry(state, self, &new_geometry)) {
			KERROR("Error regenerating label geometry.");
			typed_control->quad_count = 0; // Keep it from drawing.
			goto kui_label_frame_prepare_cleanup;
		}

		krenderbuffer vertex_buffer = state->vertex_buffer;
		krenderbuffer index_buffer = state->index_buffer;

		u64 old_vertex_size = typed_control->vertex_buffer_size;
		u64 old_vertex_offset = typed_control->vertex_buffer_offset;
		u64 old_index_size = typed_control->index_buffer_size;
		u64 old_index_offset = typed_control->index_buffer_offset;

		// Use the new offsets unless a realloc is needed.
		u64 new_vertex_size = new_geometry.vertex_buffer_size;
		u64 new_vertex_offset = old_vertex_offset;
		u64 new_index_size = new_geometry.index_buffer_size;
		u64 new_index_offset = old_index_offset;

		// A reallocation is required if the text is longer than it previously was.
		b8 needs_realloc = new_geometry.quad_count > typed_control->max_quad_count;
		if (needs_realloc) {
			if (!renderer_renderbuffer_allocate(state->renderer, vertex_buffer, new_vertex_size, &new_vertex_offset)) {
				KERROR("kui_label_control_render_frame_prepare failed to allocate from the renderer's vertex buffer: size=%u, offset=%u", new_vertex_size, new_vertex_offset);
				typed_control->quad_count = 0; // Keep it from drawing.
				goto kui_label_frame_prepare_cleanup;
			}

			if (!renderer_renderbuffer_allocate(state->renderer, index_buffer, new_index_size, &new_index_offset)) {
				KERROR("kui_label_control_render_frame_prepare failed to allocate from the renderer's index buffer: size=%u, offset=%u", new_index_size, new_index_offset);
				typed_control->quad_count = 0; // Keep it from drawing.
				goto kui_label_frame_prepare_cleanup;
			}
		}

		// Load up the data, if there is data to load.
		if (new_geometry.vertex_buffer_data) {
			if (!renderer_renderbuffer_load_range(state->renderer, vertex_buffer, new_vertex_offset, new_vertex_size, new_geometry.vertex_buffer_data, true)) {
				KERROR("kui_label_control_render_frame_prepare failed to load data into vertex buffer range: size=%u, offset=%u", new_vertex_size, new_vertex_offset);
			}
		}
		if (new_geometry.index_buffer_data) {
			if (!renderer_renderbuffer_load_range(state->renderer, index_buffer, new_index_offset, new_index_size, new_geometry.index_buffer_data, true)) {
				KERROR("kui_label_control_render_frame_prepare failed to load data into index buffer range: size=%u, offset=%u", new_index_size, new_index_offset);
			}
		}

		if (needs_realloc) {
			// Release the old vertex/index data from the buffers and update the sizes/offsets.
			if (old_vertex_offset != INVALID_ID_U64 && old_vertex_size != INVALID_ID_U64) {
				if (!renderer_renderbuffer_free(state->renderer, vertex_buffer, old_vertex_size, old_vertex_offset)) {
					KERROR("Failed to free from renderer vertex buffer: size=%u, offset=%u", old_vertex_size, old_vertex_offset);
				}
			}
			if (old_index_offset != INVALID_ID_U64 && old_index_size != INVALID_ID_U64) {
				if (!renderer_renderbuffer_free(state->renderer, index_buffer, old_index_size, old_index_offset)) {
					KERROR("Failed to free from renderer index buffer: size=%u, offset=%u", old_index_size, old_index_offset);
				}
			}

			typed_control->vertex_buffer_offset = new_vertex_offset;
			typed_control->vertex_buffer_size = new_vertex_size;
			typed_control->index_buffer_offset = new_index_offset;
			typed_control->index_buffer_size = new_index_size;
		}

		typed_control->quad_count = new_geometry.quad_count;

		// Update the max length if the string is now longer.
		if (new_geometry.quad_count > typed_control->max_quad_count) {
			typed_control->max_quad_count = new_geometry.quad_count;
		}

		// No longer dirty.
		typed_control->is_dirty = false;

	kui_label_frame_prepare_cleanup:
		if (new_geometry.vertex_buffer_data) {
			kfree(new_geometry.vertex_buffer_data, new_geometry.vertex_buffer_size, MEMORY_TAG_ARRAY);
		}
		if (new_geometry.index_buffer_data) {
			kfree(new_geometry.index_buffer_data, new_geometry.index_buffer_size, MEMORY_TAG_ARRAY);
		}
	}

	if (!kui_base_control_render(state, self, p_frame_data, render_data)) {
		return false;
	}

	// render
	if (typed_control->quad_count && typed_control->vertex_buffer_offset != INVALID_ID_U64) {
		kui_renderable renderable = {0};
		renderable.render_data.unique_id = 0;
		renderable.render_data.vertex_count = typed_control->quad_count * 4;
		renderable.render_data.vertex_buffer_offset = typed_control->vertex_buffer_offset;
		renderable.render_data.vertex_element_size = sizeof(vertex_2d);
		renderable.render_data.index_count = typed_control->quad_count * 6;
		renderable.render_data.index_buffer_offset = typed_control->index_buffer_offset;
		renderable.render_data.index_element_size = sizeof(u32);

		// NOTE: Override the default UI atlas and use that of the loaded font instead.
		// TODO: At this point, should probably have a separate font shader anyway, since
		// the future will require things like SDF, etc.
		if (typed_control->type == FONT_TYPE_BITMAP) {
			renderable.atlas_override = font_system_bitmap_font_atlas_get(state->font_system, typed_control->bitmap_font);
		} else if (typed_control->type == FONT_TYPE_SYSTEM) {
			renderable.atlas_override = font_system_system_font_atlas_get(state->font_system, typed_control->system_font);
		}

		KASSERT_DEBUG(renderable.atlas_override != INVALID_KTEXTURE);

		renderable.render_data.model = ktransform_world_get(base->ktransform);
		renderable.render_data.diffuse_colour = typed_control->colour;

		renderable.binding_instance_id = typed_control->binding_instance_id;

		darray_push(render_data->renderables, renderable);
	}

	return true;
}

void kui_label_text_set (kui_state *state, kui_control self, const char *text) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;

	// If strings are already equal, don't do anything.
	if (typed_control->text && strings_equal(text, typed_control->text)) {
		return;
	}

	if (typed_control->text) {
		string_free(typed_control->text);
		typed_control->text = KNULL;
	}

	if (text) {

		typed_control->text = string_duplicate(text);

		vec2 string_size = vec2_one();
		if (typed_control->type == FONT_TYPE_BITMAP) {
			font_system_bitmap_font_measure_string(state->font_system, typed_control->bitmap_font, typed_control->text, typed_control->max_width, &string_size);
		} else {
			font_system_system_font_measure_string(state->font_system, typed_control->system_font, typed_control->text, typed_control->max_width, &string_size);
		}

		base->bounds.width = string_size.x;
		base->bounds.height = string_size.y;

		// NOTE: Only bother with verification and setting the dirty flag for non-empty strings.
		typed_control->is_dirty = true; // string_length(typed_data->text) > 0;
	}
}

const char *kui_label_text_get (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;
	return typed_control->text;
}

void kui_label_colour_set (kui_state *state, kui_control self, vec4 colour) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;
	typed_control->colour = colour;
}

f32 kui_label_line_height_get (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;
	if (typed_control->type == FONT_TYPE_BITMAP) {
		return font_system_bitmap_font_line_height_get(state->font_system, typed_control->bitmap_font);
	} else {
		return font_system_system_font_line_height_get(state->font_system, typed_control->system_font);
	}
}

vec2 kui_label_measure_string (kui_state *state, kui_control self) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;
	vec2 string_size = vec2_one();
	if (typed_control->type == FONT_TYPE_BITMAP) {
		font_system_bitmap_font_measure_string(state->font_system, typed_control->bitmap_font, typed_control->text, typed_control->max_width, &string_size);
	} else {
		font_system_system_font_measure_string(state->font_system, typed_control->system_font, typed_control->text, typed_control->max_width, &string_size);
	}
	if (typed_control->max_width > 0 && (FLAG_GET(typed_control->flags, KUI_LABEL_FLAG_TRUNCATE_BIT) || FLAG_GET(typed_control->flags, KUI_LABEL_FLAG_TRUNCATE_ELLIPSIS_BIT))) {
		string_size.x = typed_control->max_width;
	}
	return string_size;
}

static b8 regenerate_label_geometry (kui_state *state, const kui_control self, font_geometry *pending_data) {
	kui_base_control *base = kui_system_get_base(state, self);
	KASSERT(base);
	kui_label_control *typed_control = (kui_label_control *)base;

	b8 use_ellipsis = FLAG_GET(typed_control->flags, KUI_LABEL_FLAG_TRUNCATE_ELLIPSIS_BIT);
	b8 truncate = use_ellipsis || FLAG_GET(typed_control->flags, KUI_LABEL_FLAG_TRUNCATE_BIT);

	if (typed_control->type == FONT_TYPE_BITMAP) {
		return font_system_bitmap_font_generate_geometry(state->font_system, typed_control->bitmap_font, typed_control->text, typed_control->max_width, truncate, use_ellipsis, pending_data);
	} else if (typed_control->type == FONT_TYPE_SYSTEM) {
		return font_system_system_font_generate_geometry(state->font_system, typed_control->system_font, typed_control->text, typed_control->max_width, truncate, use_ellipsis, pending_data);
	}
	return false;
}
