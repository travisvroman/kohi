#include "kui_renderer.h"

#include <core/engine.h>
#include <logger.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <systems/kshader_system.h>
#include <systems/texture_system.h>

#include "debug/kassert.h"
#include "kui_defines.h"
#include "kui_types.h"

b8 kui_renderer_create (kui_renderer *out_renderer) {

	// Pointer to the renderer system state.
	const engine_system_states *systems = engine_systems_get();
	out_renderer->renderer_state = systems->renderer_system;

	out_renderer->standard_vertex_buffer = renderer_renderbuffer_get(out_renderer->renderer_state, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	out_renderer->index_buffer = renderer_renderbuffer_get(out_renderer->renderer_state, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// SUI pass state.
	{
		out_renderer->kui_pass.kui_shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	}

	return true;
}

void kui_renderer_destroy (kui_renderer *renderer) {
	if (renderer) {
		// TODO: do the thing
	}
}

static void set_render_state_defaults (rect_2di vp_rect) {
	renderer_begin_debug_label("frame defaults", vec3_zero());

	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_set_stencil_test_enabled(false);
	renderer_set_stencil_compare_mask(0);
	renderer_set_stencil_op(
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_COMPARE_OP_EQUAL);
	renderer_set_depth_bias_enabled(false);
	renderer_set_depth_bias(0.0f, 0.0f, 0.0f);

	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
	// Default winding is counter clockwise
	renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

	rect_2di viewport_rect = {vp_rect.x, vp_rect.y + vp_rect.height, vp_rect.width, -(f32)vp_rect.height};
	renderer_viewport_set(viewport_rect);

	rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
	renderer_scissor_set(scissor_rect);

	renderer_end_debug_label();
}

b8 kui_renderer_render_frame (kui_renderer *renderer, frame_data *p_frame_data, kui_render_data *render_data) {
	renderer_begin_debug_label("sui", (vec3){0.5f, 0.5f, 0.5});

	rect_2di vp_rect = {0};
	if (!texture_dimensions_get(render_data->colour_buffer, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
		return false;
	}

	// SUI begin render
	renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 1, &render_data->colour_buffer, render_data->depth_stencil_buffer, 0);

	// Renderables
	if (!kshader_system_use(renderer->kui_pass.kui_shader, 0)) {
		KERROR("Failed to use StandardUI shader. Render frame failed.");
		return false;
	}
	set_render_state_defaults(vp_rect);

	// Make sure depth test/write is disabled for this pass.
	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);

	// Bind the viewport, flipped on the y axis. NOTE: this one really is needed.
	rect_2di viewport_rect = {vp_rect.x, vp_rect.height, vp_rect.width, -vp_rect.height};
	renderer_viewport_set(viewport_rect);
	renderer_scissor_set((rect_2di){vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height});

	// Set various state overrides.
	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);
	renderer_set_stencil_test_enabled(false);

	// Global UBO data.
	kui_global_ubo global_ubo_data = {
		.projection = render_data->projection,
		.view = render_data->view};
	kshader_set_binding_data(renderer->kui_pass.kui_shader, 0, render_data->shader_set0_binding_instance_id, 0, 0, &global_ubo_data, sizeof(kui_global_ubo));
	kshader_apply_binding_set(renderer->kui_pass.kui_shader, 0, render_data->shader_set0_binding_instance_id);

	b8 clip_mask_active = false;
	u8 stencil_reference_id = 0;

	u32 renderable_count = render_data->renderable_count;
	for (u32 i = 0; i < renderable_count; ++i) {
		kui_renderable *renderable = &render_data->renderables[i];

		// Per-control binding set.
		ktexture atlas = renderable->atlas_override != INVALID_KTEXTURE ? renderable->atlas_override : render_data->ui_atlas;
		kshader_set_binding_texture(renderer->kui_pass.kui_shader, 1, renderable->binding_instance_id, 0, 0, atlas);
		// HACK: Use nearest neighbor sampler for UI
		ksampler_backend samp = renderer_generic_sampler_get(renderer->renderer_state, SHADER_GENERIC_SAMPLER_NEAREST_CLAMP);
		kshader_set_binding_sampler(renderer->kui_pass.kui_shader, 1, renderable->binding_instance_id, 1, 0, samp);
		kshader_apply_binding_set(renderer->kui_pass.kui_shader, 1, renderable->binding_instance_id);

		// Render stencil clipping mask geometry if it exists.
		if (renderable->type == KUI_RENDERABLE_TYPE_STENCIL_CLIP_BEGIN) {
			KASSERT(!clip_mask_active);
			clip_mask_active = true;
			renderer_begin_debug_label("clip_mask", (vec3){0, 1, 0});
			// Enable writing, disable test.
			renderer_set_stencil_test_enabled(true);
			renderer_set_depth_test_enabled(false);
			renderer_set_depth_write_enabled(false);
			/* renderer_set_stencil_reference((u32)renderable->render_data.unique_id); */
			renderer_set_stencil_reference((u32)stencil_reference_id);
			stencil_reference_id++;
			renderer_set_stencil_write_mask(0xFF);
			renderer_set_stencil_op(
				RENDERER_STENCIL_OP_KEEP, // replace
				RENDERER_STENCIL_OP_REPLACE,
				RENDERER_STENCIL_OP_KEEP, // replace
				RENDERER_COMPARE_OP_ALWAYS);

			renderer_clear_depth_set(renderer->renderer_state, 1.0f);
			renderer_clear_stencil_set(renderer->renderer_state, 0.0f);

			// Immediates
			{
				kui_immediate_data immediate_data = {
					.model = renderable->render_data.model,
					.diffuse_colour = renderable->render_data.diffuse_colour};
				kshader_set_immediate_data(renderer->kui_pass.kui_shader, &immediate_data, sizeof(kui_immediate_data));
			}

			// Draw the clip mask geometry.
			renderer_geometry_draw(&renderable->render_data);

			// Disable writing, enable test.
			renderer_set_stencil_write_mask(0x00);
			renderer_set_stencil_test_enabled(true);
			renderer_set_stencil_compare_mask(0xFF);
			renderer_set_stencil_op(
				RENDERER_STENCIL_OP_KEEP,
				RENDERER_STENCIL_OP_KEEP, // replace
				RENDERER_STENCIL_OP_KEEP,
				RENDERER_COMPARE_OP_EQUAL);
			renderer_end_debug_label();
		} /*  else {
			 renderer_set_stencil_write_mask(0x00);
			 renderer_set_stencil_test_enabled(false);
		 } */

		// Use the scissor clipping area if it exists.
		if (renderable->type == KUI_RENDERABLE_TYPE_SCISSOR_CLIP_BEGIN) {
			renderer_scissor_set(renderable->clipping_area);
		}

		// Now render the actual renderable.
		if (renderable->type == KUI_RENDERABLE_TYPE_CONTROL) {
			// Immediates
			{
				kui_immediate_data immediate_data = {
					.model = renderable->render_data.model,
					.diffuse_colour = renderable->render_data.diffuse_colour};
				kshader_set_immediate_data(renderer->kui_pass.kui_shader, &immediate_data, sizeof(kui_immediate_data));
			}
			// Draw
			renderer_geometry_draw(&renderable->render_data);
		}

		// Turn off stencil tests if they were on.
		if (renderable->type == KUI_RENDERABLE_TYPE_STENCIL_CLIP_END) {
			KASSERT(clip_mask_active);
			clip_mask_active = false;
			// Turn off stencil testing.
			renderer_set_stencil_test_enabled(false);
			renderer_set_stencil_op(
				RENDERER_STENCIL_OP_KEEP,
				RENDERER_STENCIL_OP_KEEP,
				RENDERER_STENCIL_OP_KEEP,
				RENDERER_COMPARE_OP_ALWAYS);
		}

		// Ensure scissor is reset.
		if (renderable->type == KUI_RENDERABLE_TYPE_SCISSOR_CLIP_END) {
			/* renderer_scissor_reset(); */
			rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
			renderer_scissor_set(scissor_rect);
		}
	} // renderables

	if (clip_mask_active) {
		clip_mask_active = false;
		// Turn off stencil testing.
		renderer_set_stencil_test_enabled(false);
		renderer_set_stencil_op(
			RENDERER_STENCIL_OP_KEEP,
			RENDERER_STENCIL_OP_KEEP,
			RENDERER_STENCIL_OP_KEEP,
			RENDERER_COMPARE_OP_ALWAYS);

		// Ensure scissor is reset.
		/* renderer_scissor_reset(); */
		rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
		renderer_scissor_set(scissor_rect);
	}

	// SUI end render.
	renderer_end_rendering(renderer->renderer_state, p_frame_data);

	renderer_end_debug_label();

	return true;
}
