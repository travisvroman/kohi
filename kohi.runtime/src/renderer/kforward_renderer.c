#include "kforward_renderer.h"
#include "systems/kmatrix_system.h"
#include "systems/ktransform_system.h"
#include "world/heightfield_terrain.h"

#include <core/engine.h>
#include <core/frame_data.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/kmaterial_renderer.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <runtime_defines.h>
#include <systems/kmaterial_system.h>
#include <systems/kshader_system.h>
#include <systems/ktimeline_system.h>
#include <systems/light_system.h>
#include <systems/texture_system.h>

#define VERTEX_LAYOUT_INDEX_STATIC 0
#define VERTEX_LAYOUT_INDEX_SKINNED 1

// per frame UBO FIXME: This should probably be located with the skybox files, or shader, or somewhere other than here...
typedef struct skybox_global_ubo_data {
	vec4 fog_colour;

	// Offsets into the matrix SSBO per type.
	u32 mat_ssbo_view_offset;
	u32 mat_ssbo_proj_offset;
} skybox_global_ubo_data;

// per frame UBO FIXME: This should probably be located with the skybox files, or shader, or somewhere other than here...
typedef struct skybox_immediate_data {
	u32 view_index;
	u32 projection_index;
} skybox_immediate_data;

// FIXME: This should be located elsewhere, since this isn't application specific. Perhaps in renderer types?
typedef struct shadow_staticmesh_global_ubo {
	mat4 view_projections[KMATERIAL_MAX_SHADOW_CASCADES];
} shadow_staticmesh_global_ubo;

// FIXME: This should be located elsewhere, since this isn't application specific. Perhaps in renderer types?
typedef struct shadow_staticmesh_immediate_data {
	u32 mat_ssbo_tran_offset;
	u32 transform_index;
	u32 cascade_index;
	u32 animation_index;
	u32 geo_type; // 0=static, 1=animated
} shadow_staticmesh_immediate_data;

// FIXME: This should be located elsewhere, since this isn't application specific. Perhaps in renderer types?
typedef struct shadow_hf_terrain_global_ubo {
	mat4 view_projections[KMATERIAL_MAX_SHADOW_CASCADES];
} shadow_hf_terrain_global_ubo;

// FIXME: This should be located elsewhere, since this isn't application specific. Perhaps in renderer types?
typedef struct shadow_hf_terrain_immediate_data {
	u32 cascade_index;
} shadow_hf_terrain_immediate_data;

typedef struct depth_prepass_global_ubo {
	// Offsets into the matrix SSBO per type.
	u32 mat_ssbo_view_offset;
	u32 mat_ssbo_proj_offset;
	u32 mat_ssbo_tran_offset;
	u32 mat_ssbo_genc_offset;
} depth_prepass_global_ubo;

typedef struct depth_prepass_immediate_data {
	u32 view_index;
	u32 projection_index;
	u32 transform_index;
} depth_prepass_immediate_data;

// NOTE: Heightfield terrain stuff.
typedef struct hf_terrain_global_ubo {
	mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
	vec4 cascade_splits;											  // 16 bytes

	f32 delta_time;
	f32 game_time;
	u32 render_mode;
	u32 use_pcf;

	// Shadow settings
	f32 shadow_bias;
	f32 shadow_distance;
	f32 shadow_fade_distance;
	f32 shadow_split_mult;

	vec4 fog_colour;
	f32 fog_start;
	f32 fog_end;
	f32 near_clip;

	f32 far_clip;
	// Offsets into the matrix SSBO per type.
	u32 mat_ssbo_view_offset;
	u32 mat_ssbo_proj_offset;
	u32 padding;
} hf_terrain_global_ubo;

typedef struct hf_terrain_immediate_data {
	// bytes 0-15
	u32 view_index;
	u32 projection_index;
	u32 dir_light_index;
	u32 base_material_index;

	// bytes 16-31
	// Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
	uvec2 packed_point_light_indices; // 8 bytes
	u32 num_p_lights;
	// Index into global irradiance cubemap texture array
	u32 irradiance_cubemap_index;

	// bytes 32-47
	vec4 clipping_plane;

	// bytes 48-63
	uvec4 material_indices;
} hf_terrain_immediate_data;

b8 kforward_renderer_create (ktexture colour_buffer, ktexture depth_stencil_buffer, kforward_renderer *out_renderer) {
	KASSERT_DEBUG(out_renderer);

	out_renderer->colour_buffer = colour_buffer;
	out_renderer->depth_stencil_buffer = depth_stencil_buffer;

	// Pointer to the renderer system state.
	const engine_system_states *systems = engine_systems_get();
	out_renderer->renderer_state = systems->renderer_system;
	out_renderer->material_system = systems->material_system;
	out_renderer->material_renderer = systems->material_renderer;
	out_renderer->matrix_system = systems->matrix_system;

	out_renderer->standard_vertex_buffer = renderer_renderbuffer_get(out_renderer->renderer_state, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	out_renderer->index_buffer = renderer_renderbuffer_get(out_renderer->renderer_state, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	// Shadow pass data
	{
		// Default shadowmap resolution. // TODO: configurable
		out_renderer->shadow_pass.resolution = 2048;

		// Load static mesh shadowmap shader.
		out_renderer->shadow_pass.staticmesh_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SHADOW_MODEL), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->shadow_pass.staticmesh_shader != KSHADER_INVALID);

		// NOTE: For static meshes, the alpha of transparent materials needs to be taken into
		// account when casting shadows. This means these each need a distinct group per distinct material.
		// Fully-opaque objects can be rendered using the same default opaque texture, and thus can all
		// be rendered under the same group.
		// Since terrains will never be transparent, they can all be rendered without using a texture at all.
		out_renderer->shadow_pass.default_base_colour = texture_acquire_sync(kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME));
		KASSERT_DEBUG(out_renderer->shadow_pass.default_base_colour != INVALID_KTEXTURE);

		// Get a binding instance for the global UBO.
		out_renderer->shadow_pass.sm_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->shadow_pass.staticmesh_shader, 0);

		// Get instance ids for use with transparent materials.
		out_renderer->shadow_pass.sm_set1_max_instances = kshader_binding_set_instance_count_get(out_renderer->shadow_pass.staticmesh_shader, 1);
		out_renderer->shadow_pass.sm_set1_instance_ids = KALLOC_TYPE_CARRAY(u32, out_renderer->shadow_pass.sm_set1_max_instances);
		for (u32 i = 0; i < out_renderer->shadow_pass.sm_set1_max_instances; ++i) {
			out_renderer->shadow_pass.sm_set1_instance_ids[i] = kshader_acquire_binding_set_instance(out_renderer->shadow_pass.staticmesh_shader, 1);
			KASSERT_DEBUG(out_renderer->shadow_pass.sm_set1_instance_ids[i] != INVALID_ID);
		}
		// Obtain an instance id for the default instance, used for non-transparent materials. Just use the first one in the list.
		out_renderer->shadow_pass.sm_default_instance_id = out_renderer->shadow_pass.sm_set1_instance_ids[0];

		// Load heightmap terrain shadowmap shader.
		out_renderer->shadow_pass.hmt_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SHADOW_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->shadow_pass.hmt_shader != KSHADER_INVALID);
		// Obtain an instance id global UBO.
		out_renderer->shadow_pass.hmt_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->shadow_pass.hmt_shader, 0);

		// Load heightfield terrain shadowmap shader.
		out_renderer->shadow_pass.hft_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SHADOW_HF_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->shadow_pass.hft_shader != KSHADER_INVALID);
		// Obtain an instance id global UBO.
		out_renderer->shadow_pass.hft_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->shadow_pass.hft_shader, 0);

		// Create the depth attachment for the directional light shadow.
		// This should take renderer buffering into account.
		ktexture_load_options options = {
			.type = KTEXTURE_TYPE_2D_ARRAY,
			.format = KPIXEL_FORMAT_RGBA8,
			.is_depth = true,
			.is_stencil = false,
			.name = kname_create("__shadow_pass_shadowmap__"),
			.width = out_renderer->shadow_pass.resolution,
			.height = out_renderer->shadow_pass.resolution,
			.layer_count = KMATERIAL_MAX_SHADOW_CASCADES,
			.multiframe_buffering = true,
			.mip_levels = 1};
		out_renderer->shadow_pass.shadow_tex = texture_acquire_with_options_sync(options);
		if (out_renderer->shadow_pass.shadow_tex == INVALID_KTEXTURE) {
			KERROR("Failed to request layered shadow map texture for shadow pass.");
			return false;
		}
	}

	// Depth prepass data
	{
		out_renderer->depth_prepass.depth_prepass_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_DEPTH_PREPASS), kname_create(PACKAGE_NAME_RUNTIME));
		out_renderer->depth_prepass.shader_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->depth_prepass.depth_prepass_shader, 0);
	}

	// Forward pass data
	{
		// Load Skybox shader and get shader binding set instances.
		out_renderer->forward_pass.sb_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_SKYBOX), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->forward_pass.sb_shader != KSHADER_INVALID);

		out_renderer->forward_pass.sb_shader_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->forward_pass.sb_shader, 0);

		out_renderer->forward_pass.default_cube_texture = texture_cubemap_acquire_sync(kname_create(DEFAULT_CUBE_TEXTURE_NAME));

		// Load Heightfield Terrain shader.
		out_renderer->forward_pass.hf_terrain_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_HF_TERRAIN), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->forward_pass.hf_terrain_shader != KSHADER_INVALID);
		out_renderer->forward_pass.shader_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->forward_pass.hf_terrain_shader, 0);
	}

#if KOHI_DEBUG
	// World debug pass state
	{
		// Load debug Debug3D shader and get shader.
		out_renderer->world_debug_pass.debug_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_DEBUG_3D), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->world_debug_pass.debug_shader != KSHADER_INVALID);
		out_renderer->world_debug_pass.colour_shader = kshader_system_get(kname_create(SHADER_NAME_RUNTIME_COLOUR_3D), kname_create(PACKAGE_NAME_RUNTIME));
		KASSERT_DEBUG(out_renderer->world_debug_pass.colour_shader != KSHADER_INVALID);

		out_renderer->world_debug_pass.debug_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->world_debug_pass.debug_shader, 0);
		out_renderer->world_debug_pass.colour_set0_instance_id = kshader_acquire_binding_set_instance(out_renderer->world_debug_pass.colour_shader, 0);
	}
#endif

	return true;
}

void kforward_renderer_destroy (kforward_renderer *renderer) {
	if (renderer) {
		kfree(renderer->shadow_pass.sm_set1_instance_ids);
	}
}

static void draw_geo_list (kforward_renderer *renderer, frame_data *p_frame_data, kdirectional_light_data directional_light, kmatrix_id view_id, kmatrix_id projection_id, f32 near_clip, f32 far_clip, vec4 clipping_plane, u32 meshes_by_material_count, kmaterial_render_data *meshes_by_material) {
	for (u32 m = 0; m < meshes_by_material_count; ++m) {
		kmaterial_render_data *material = &meshes_by_material[m];

		// Apply base-material-level (i.e. group-level) data.
		kmaterial_renderer_bind_base(renderer->material_renderer, material->base_material);

		// Each geometry
		for (u32 g = 0; g < material->geometry_count; ++g) {
			kgeometry_render_data *geo = &material->geometries[g];

			kmaterial_instance inst = {
				.base_material = material->base_material,
				.instance_id = geo->material_instance_id};

			b8 is_animated = geo->animation_id != INVALID_ID_U16;
			kmaterial_renderer_set_animated(renderer->material_renderer, is_animated);
			kmatrix_id m = ktransform_kmatrixid_get(geo->transform);

			kmaterial_render_immediate_data immediate_data = {
				.view_index = UNPACK_U32_U16_AT(view_id, 1),
				.projection_index = UNPACK_U32_U16_AT(projection_id, 1),
				.animation_index = is_animated ? geo->animation_id : 0,
				.base_material_index = material->base_material,
				.dir_light_index = directional_light.light,
				.irradiance_cubemap_index = 0, // TODO: pass in irradiance_cubemap_index from scene data
				.num_p_lights = geo->bound_point_light_count,
				.transform_index = UNPACK_U32_U16_AT(m, 1),
				.clipping_plane = clipping_plane,
				.geo_type = (u32)is_animated,
				.near_clip = near_clip,
				.far_clip = far_clip};

			// Pack the point light indices
			immediate_data.packed_point_light_indices.elements[0] = pack_u8_into_u32(geo->bound_point_light_indices[0], geo->bound_point_light_indices[1], geo->bound_point_light_indices[2], geo->bound_point_light_indices[3]);
			immediate_data.packed_point_light_indices.elements[1] = pack_u8_into_u32(geo->bound_point_light_indices[4], geo->bound_point_light_indices[5], geo->bound_point_light_indices[6], geo->bound_point_light_indices[7]);
			/* u8 written = 0;
			for (u8 i = 0; i < 2 && written < geo->bound_point_light_count; ++i) {
				u32 vi = 0;

				for (u8 p = 0; p < 4 && written < geo->bound_point_light_count; ++p) {

					// Pack the u8 into the given u32
					vi |= ((u32)geo->bound_point_light_indices[p] << ((3 - p) * 8));
					++written;
				}

				// Store the packed u32
				immediate_data.packed_point_light_indices.elements[i] = vi;
			} */

			// Apply material-instance-level immediate data.
			kmaterial_renderer_apply_immediates(renderer->material_renderer, inst, &immediate_data);

			// Invert winding if needed
			b8 winding_inverted = FLAG_GET(geo->flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT);
			if (winding_inverted) {
				renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
			}

			// For double-sided materials, turn off backface culling.
			b8 cull_disabled = false;
			if (kmaterial_flag_get(renderer->material_system, material->base_material, KMATERIAL_FLAG_DOUBLE_SIDED_BIT)) {
				renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);
				cull_disabled = true;
			}

			// Draw it.
			b8 includes_index_data = geo->index_count > 0;

			KASSERT_MSG(
				renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, geo->vertex_offset, geo->vertex_count, 0, includes_index_data),
				"renderer_renderbuffer_draw failed to draw vertex buffer");

			if (includes_index_data) {
				KASSERT_MSG(
					renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, geo->index_offset, geo->index_count, 0, !includes_index_data),
					"renderer_renderbuffer_draw failed to draw index buffer");
			}

			// Restore backface culling if needed
			if (cull_disabled) {
				renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
			}

			// Change back if needed
			if (winding_inverted) {
				renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
			}
		}
	}
}

static void set_render_state_defaults (rect_2di vp_rect) {
	renderer_begin_debug_label("frame defaults", vec3_zero());

	renderer_set_depth_test_enabled(false);
	renderer_set_depth_write_enabled(false);
	renderer_set_depth_bias(0.0f, 0.0f, 0.0f);
	renderer_set_depth_bias_enabled(false);
	renderer_set_stencil_test_enabled(false);
	renderer_set_stencil_compare_mask(0);
	renderer_set_stencil_op(
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_STENCIL_OP_KEEP,
		RENDERER_COMPARE_OP_EQUAL);

	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);
	// Default winding is counter clockwise
	renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

	rect_2di viewport_rect = {vp_rect.x, vp_rect.y + vp_rect.height, vp_rect.width, -(f32)vp_rect.height};
	renderer_viewport_set(viewport_rect);

	rect_2di scissor_rect = {vp_rect.x, vp_rect.y, vp_rect.width, vp_rect.height};
	renderer_scissor_set(scissor_rect);

	renderer_end_debug_label();
}

static b8 scene_pass (
	kforward_renderer *renderer,
	frame_data *p_frame_data,
	kdirectional_light_data directional_light,
	rect_2di vp_rect,
	kmatrix_id projection_id,
	kmatrix_id view_id,
	f32 near_clip,
	f32 far_clip,
	ktexture colour_handle,
	ktexture depth_handle,
	vec4 clipping_plane,
	u8 irradiance_cubemap_texture_count,
	ktexture *irradiance_cubemap_textures,
	const kskybox_render_data *skybox_data,
	const kscene_pass_render_data *pass_data,
	u32 water_plane_count,
	const kforward_pass_water_plane_render_data *water_planes,
	b8 do_depth_prepass,
	renderer_debug_view_mode view_mode) {

	// In wireframe mode, don't allow depth prepass so that all objects can be seen.
	if (view_mode == RENDERER_VIEW_MODE_WIREFRAME) {
		do_depth_prepass = false;
	}

	// HACK: turning off depth prepass for now.
	/* do_depth_prepass = false; */

	// Clear the textures
	renderer_clear_colour(renderer->renderer_state, colour_handle);
	renderer_clear_depth_stencil(renderer->renderer_state, depth_handle);

	// Depth Pre-pass
	if (do_depth_prepass) {
		renderer_begin_debug_label("depth prepass", vec3_zero());

		renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 0, 0, depth_handle, 0);
		set_render_state_defaults(vp_rect);

		kshader_system_use(renderer->depth_prepass.depth_prepass_shader, VERTEX_LAYOUT_INDEX_STATIC);

		renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);

		renderer_set_depth_test_enabled(true);
		renderer_set_depth_write_enabled(true);
		renderer_set_depth_bias_enabled(false);
		renderer_set_depth_bias(0.0f, 0.0f, 0.0f);

		// Apply global UBO.
		depth_prepass_global_ubo prepass_global_settings = {
			.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_VIEW),
			.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_PROJECTION),
			.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_TRANSFORM),
			.mat_ssbo_genc_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_GENERAL)};
		kshader_set_binding_data(renderer->depth_prepass.depth_prepass_shader, 0, renderer->depth_prepass.shader_set0_instance_id, 0, 0, &prepass_global_settings, sizeof(prepass_global_settings));
		kshader_apply_binding_set(renderer->depth_prepass.depth_prepass_shader, 0, renderer->depth_prepass.shader_set0_instance_id);

		// Render water planes first, this can eliminate a lot of overdraw afterward.
		if (water_plane_count && water_planes) {

			// Draw each plane.
			for (u32 i = 0; i < water_plane_count; ++i) {

				const kforward_pass_water_plane_render_data *plane = &water_planes[i];

				kmatrix_id m = ktransform_kmatrixid_get(plane->plane_render_data.transform);

				depth_prepass_immediate_data immediate_data = {
					.projection_index = UNPACK_U32_U16_AT(projection_id, 1),
					.view_index = UNPACK_U32_U16_AT(view_id, 1),
					.transform_index = UNPACK_U32_U16_AT(m, 1)};

				kshader_set_immediate_data(renderer->depth_prepass.depth_prepass_shader, &immediate_data, sizeof(immediate_data));

				// Draw based on vert/index data.
				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, plane->plane_render_data.vertex_buffer_offset, 4, 0, true)) {
					KERROR("Failed to bind standard vertex buffer data for water plane.");
					return false;
				}
				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, plane->plane_render_data.index_buffer_offset, 6, 0, false)) {
					KERROR("Failed to draw water plane using index data.");
					return false;
				}
			}
		}

		// TODO: Render HF terrain chunks in depth pre-pass

		// Render only opaque objects in the "standard" forward pass. Just static for now, too.
		for (u32 m = 0; m < pass_data->opaque_meshes_by_material_count; ++m) {
			kmaterial_render_data *material = &pass_data->opaque_meshes_by_material[m];

			// Each geometry
			for (u32 g = 0; g < material->geometry_count; ++g) {
				kgeometry_render_data *geo = &material->geometries[g];

				kmatrix_id m = ktransform_kmatrixid_get(geo->transform);

				depth_prepass_immediate_data immediate_data = {
					.projection_index = UNPACK_U32_U16_AT(projection_id, 1),
					.view_index = UNPACK_U32_U16_AT(view_id, 1),
					.transform_index = UNPACK_U32_U16_AT(m, 1)};

				kshader_set_immediate_data(renderer->depth_prepass.depth_prepass_shader, &immediate_data, sizeof(immediate_data));

				// Invert winding if needed
				b8 winding_inverted = FLAG_GET(geo->flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT);
				if (winding_inverted) {
					renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
				}

				// Draw it.
				b8 includes_index_data = geo->index_count > 0;

				KASSERT_MSG(
					renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, geo->vertex_offset, geo->vertex_count, 0, includes_index_data),
					"renderer_renderbuffer_draw failed to draw vertex buffer");

				if (includes_index_data) {
					KASSERT_MSG(
						renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, geo->index_offset, geo->index_count, 0, !includes_index_data),
						"renderer_renderbuffer_draw failed to draw index buffer");
				}

				// Change back if needed
				if (winding_inverted) {
					renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
				}
			}
		}

		renderer_end_rendering(renderer->renderer_state, p_frame_data);

		renderer_end_debug_label();
	} // end depth pre-pass

	// Render skybox. Assume no vertex count means not skybox.
	if (skybox_data->do_pass && skybox_data->sb_vertex_count) {
		renderer_begin_debug_label("scene - skybox", (vec3){0.5f, 0.5f, 1.0f});

		// Skybox begin render
		renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 1, &colour_handle, INVALID_KTEXTURE, 0);

		set_render_state_defaults(vp_rect);

		kshader_system_use(renderer->forward_pass.sb_shader, VERTEX_LAYOUT_INDEX_STATIC);

		renderer_cull_mode_set(RENDERER_CULL_MODE_FRONT);

		// Apply globals
		{
			skybox_global_ubo_data global_ubo_data = {
				.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_VIEW),
				.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_PROJECTION),
				.fog_colour = skybox_data->fog_colour};

			kshader_set_binding_data(renderer->forward_pass.sb_shader, 0, renderer->forward_pass.sb_shader_set0_instance_id, 0, 0, &global_ubo_data, sizeof(skybox_global_ubo_data));

			ktexture sbt = skybox_data->skybox_texture;
			/* sbt = renderer->forward_pass.default_cube_texture; */
			if (!texture_is_loaded(sbt)) {
				sbt = renderer->forward_pass.default_cube_texture;
			}
			kshader_set_binding_texture(renderer->forward_pass.sb_shader, 0, renderer->forward_pass.sb_shader_set0_instance_id, 2, 0, sbt);

			kshader_apply_binding_set(renderer->forward_pass.sb_shader, 0, renderer->forward_pass.sb_shader_set0_instance_id);
		}

		// Immediate data.
		skybox_immediate_data immediate = {
			.view_index = UNPACK_U32_U16_AT(view_id, 1),
			.projection_index = UNPACK_U32_U16_AT(projection_id, 1)};
		kshader_set_immediate_data(renderer->forward_pass.sb_shader, &immediate, sizeof(skybox_immediate_data));

		// Draw it.
		if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, skybox_data->sb_vertex_offset, skybox_data->sb_vertex_count, 0, true)) {
			KERROR("Renderer skybox: failed to draw vertex buffer.");
			return false;
		}
		if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, skybox_data->sb_index_offset, skybox_data->sb_index_count, 0, false)) {
			KERROR("Renderer skybox: failed to draw index buffer.");
			return false;
		}

		// Skybox end render
		renderer_end_rendering(renderer->renderer_state, p_frame_data);

		renderer_end_debug_label();
	} // End skybox render.

	// NOTE: Begin rendering the scene
	renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 1, &colour_handle, depth_handle, 0);

	// Heightfield Terrain
	{
		renderer_begin_debug_label("scene - HF terrain", (vec3){0.5f, 1.0f, 0.5f});

		const hf_terrain_render_data *trd = &pass_data->hf_terrain_data;

		set_render_state_defaults(vp_rect);

		// Ensure valid depth state. TODO: Do depth prepass for terrain.
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_write_enabled(true);

		// Ensure valid culling.
		renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);

		kshader shader = renderer->forward_pass.hf_terrain_shader;
		KASSERT(kshader_system_use(shader, VERTEX_LAYOUT_INDEX_STATIC));

		// Ensure wireframe mode is (un)set.
		KASSERT(kshader_system_set_wireframe(shader, view_mode == RENDERER_VIEW_MODE_WIREFRAME));

		kmaterial_settings_ubo *settings = &renderer->material_renderer->settings;

		// Upload the global UBO data
		hf_terrain_global_ubo global_ubo_data = {
			.mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_VIEW),
			.mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_PROJECTION),
			.fog_colour = settings->fog_colour,
			.fog_start = settings->fog_start,
			.fog_end = settings->fog_end,
			.game_time = settings->game_time,
			.delta_time = settings->delta_time,
			.render_mode = settings->render_mode,
			.use_pcf = settings->use_pcf,
			.shadow_bias = settings->shadow_bias,
			.shadow_distance = settings->shadow_distance,
			.shadow_split_mult = settings->shadow_split_mult,
			.shadow_fade_distance = settings->shadow_fade_distance,
			.cascade_splits = settings->cascade_splits};
		for (u8 i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
			global_ubo_data.directional_light_spaces[i] = settings->directional_light_spaces[i];
		}
		kshader_set_binding_data(shader, 0, renderer->forward_pass.shader_set0_instance_id, 0, 0, &global_ubo_data, sizeof(hf_terrain_global_ubo));
		kshader_set_binding_texture(shader, 0, renderer->forward_pass.shader_set0_instance_id, 3, 0, renderer->shadow_pass.shadow_tex);
		// Irradience textures provided by probes around in the world.
		for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
			ktexture t = irradiance_cubemap_textures[i] != INVALID_KTEXTURE ? irradiance_cubemap_textures[i] : renderer->forward_pass.default_cube_texture;
			if (!texture_is_loaded(t)) {
				t = renderer->forward_pass.default_cube_texture;
			}
			kshader_set_binding_texture(shader, 0, renderer->forward_pass.shader_set0_instance_id, 5, i, t);
		}

		// Textures are array-textures, so only need to be bound once.
		kshader_set_binding_texture(shader, 0, renderer->forward_pass.shader_set0_instance_id, 7, 0, trd->albedo_texture_array);
		kshader_set_binding_texture(shader, 0, renderer->forward_pass.shader_set0_instance_id, 9, 0, trd->normal_texture_array);
		kshader_set_binding_texture(shader, 0, renderer->forward_pass.shader_set0_instance_id, 11, 0, trd->mra_texture_array);

		kshader_apply_binding_set(shader, 0, renderer->forward_pass.shader_set0_instance_id);

		// Draw the terrain chunks. This assumes chunks have already been culled at this point.
		for (u32 b = 0; b < trd->block_count; ++b) {
			hf_terrain_block_render_data *block_data = &trd->blocks[b];

			// Splatmap
			kshader_set_binding_texture(shader, 1, block_data->shader_instance_id, 0, 0, block_data->splatmap);
			ksampler_backend splatmap_sampler = renderer_generic_sampler_get(renderer->renderer_state, SHADER_GENERIC_SAMPLER_LINEAR_CLAMP);
			kshader_set_binding_sampler(shader, 1, block_data->shader_instance_id, 1, 0, splatmap_sampler);

			// Ensure the binding set is applied.
			kshader_apply_binding_set(shader, 1, block_data->shader_instance_id);

			for (u32 c = 0; c < block_data->chunk_count; ++c) {
				hf_terrain_chunk_render_data *chunk_data = &block_data->chunks[c];

				// Immediate data.
				hf_terrain_immediate_data immediate = {
					.view_index = UNPACK_U32_U16_AT(view_id, 1),
					.projection_index = UNPACK_U32_U16_AT(projection_id, 1),
					.clipping_plane = clipping_plane,
					.num_p_lights = chunk_data->bound_point_light_count,
					.irradiance_cubemap_index = 0, // FIXME: hardcoded
					.base_material_index = chunk_data->base_material_index,
					.material_indices = chunk_data->material_indices,
				};

				// Pack the point light indices
				immediate.packed_point_light_indices.elements[0] = pack_u8_into_u32(chunk_data->bound_point_light_indices[0], chunk_data->bound_point_light_indices[1], chunk_data->bound_point_light_indices[2], chunk_data->bound_point_light_indices[3]);
				immediate.packed_point_light_indices.elements[1] = pack_u8_into_u32(chunk_data->bound_point_light_indices[4], chunk_data->bound_point_light_indices[5], chunk_data->bound_point_light_indices[6], chunk_data->bound_point_light_indices[7]);

				kshader_set_immediate_data(shader, &immediate, sizeof(hf_terrain_immediate_data));

				// Draw it.
				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, chunk_data->vertex_buffer_offset, chunk_data->vertex_count, 0, true)) {
					KERROR("Renderer HF Terrain: failed to draw vertex buffer.");
					return false;
				}
				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, trd->index_buffer_offset, trd->index_count, 0, false)) {
					KERROR("Renderer HF Terrain: failed to draw index buffer.");
					return false;
				}
			}
		}

		/* renderer_end_rendering(renderer->renderer_state, p_frame_data); */
		renderer_end_debug_label();
	}

	renderer_begin_debug_label("scene - meshes", (vec3){0.0f, 1.0f, 1.0f});

	// Mesh begin render
	/* renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 1, &colour_handle, depth_handle, 0); */
	set_render_state_defaults(vp_rect);

	// Ensure valid depth state.
	renderer_set_depth_test_enabled(true);
	renderer_set_depth_write_enabled(true);

	// Ensure valid culling.
	renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);

	// Prepare material globals
	{
		renderer->material_renderer->shadow_map_texture = renderer->shadow_pass.shadow_tex;

		// Irradience maps should be provided by probes around in the world.
		kmaterial_renderer_set_irradiance_cubemap_textures(renderer->material_renderer, irradiance_cubemap_texture_count, irradiance_cubemap_textures);

		// Apply the global material settings.
		kmaterial_renderer_apply_globals(renderer->material_renderer);
	}

	// Opaque geometies by material first.
	if (do_depth_prepass) {
		// Don't need to write these again.
		renderer_set_depth_write_enabled(false);
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_bias_enabled(true);
		renderer_set_depth_bias(0.00f, 0.0f, -1.0f);
	}
	// static geometries
	draw_geo_list(renderer, p_frame_data, directional_light, view_id, projection_id, near_clip, far_clip, clipping_plane, pass_data->opaque_meshes_by_material_count, pass_data->opaque_meshes_by_material);

	if (do_depth_prepass) {
		// Switch back on.
		renderer_set_depth_write_enabled(true);
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_bias_enabled(false);
		renderer_set_depth_bias(0.0f, 0.0f, 0.0f);
	}
	// animated geometries
	draw_geo_list(renderer, p_frame_data, directional_light, view_id, projection_id, near_clip, far_clip, clipping_plane, pass_data->animated_opaque_meshes_by_material_count, pass_data->animated_opaque_meshes_by_material);

	// Draw the water planes
	if (do_depth_prepass) {
		// Don't need to write these again.
		renderer_set_depth_write_enabled(false);
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_bias_enabled(true);
		renderer_set_depth_bias(0.00f, 0.0f, -1.0f);
	}
	if (water_plane_count && water_planes) {
		renderer_begin_debug_label("water planes", (vec3){0, 0, 1});

		// Water planes do not use animated geometry.
		kmaterial_renderer_set_animated(renderer->material_renderer, false);

		// Draw each plane.
		for (u32 i = 0; i < water_plane_count; ++i) {

			const kforward_pass_water_plane_render_data *plane = &water_planes[i];

			// Apply base-material-level (i.e. group-level) data.
			kmaterial_renderer_bind_base(renderer->material_renderer, plane->plane_render_data.material.base_material);

			// FIXME: Used to extract tiling/wave_strength/wave_speed. These should be material props in the SSBO
			const kmaterial_data *materials = kmaterial_system_get_all_base_materials(renderer->material_system);
			const kmaterial_data *material = &materials[plane->plane_render_data.material.base_material];

			kmatrix_id m = ktransform_kmatrixid_get(plane->plane_render_data.transform);

			kmaterial_render_immediate_data immediate_data = {
				.view_index = UNPACK_U32_U16_AT(view_id, 1),
				.projection_index = UNPACK_U32_U16_AT(projection_id, 1),
				.animation_index = 0, // NOTE: Can't use INVALID_ID_U16 here because it overflows the SSBO
				.base_material_index = plane->plane_render_data.material.base_material,
				.dir_light_index = directional_light.light,
				.irradiance_cubemap_index = 0, // TODO: pass in irradiance_cubemap_index from scene data
				.num_p_lights = plane->plane_render_data.bound_point_light_count,
				.transform_index = UNPACK_U32_U16_AT(m, 1),
				.clipping_plane = clipping_plane,
				.tiling = material->tiling,
				.wave_speed = material->wave_speed,
				.wave_strength = material->wave_strength,
				.geo_type = 0,
				.near_clip = near_clip,
				.far_clip = far_clip};

			// Pack the point light indices
			immediate_data.packed_point_light_indices.elements[0] = pack_u8_into_u32(plane->plane_render_data.bound_point_light_indices[0], plane->plane_render_data.bound_point_light_indices[1], plane->plane_render_data.bound_point_light_indices[2], plane->plane_render_data.bound_point_light_indices[3]);
			immediate_data.packed_point_light_indices.elements[1] = pack_u8_into_u32(plane->plane_render_data.bound_point_light_indices[4], plane->plane_render_data.bound_point_light_indices[5], plane->plane_render_data.bound_point_light_indices[6], plane->plane_render_data.bound_point_light_indices[7]);
			/* u8 written = 0;
			for (u8 i = 0; i < 2 && written < plane->plane_render_data.bound_point_light_count; ++i) {
				u32 vi = 0;

				for (u8 p = 0; p < 4 && written < plane->plane_render_data.bound_point_light_count; ++p) {

					// Pack the u8 into the given u32
					vi |= ((u32)immediate_data.packed_point_light_indices.elements[written] << ((3 - p) * 8));
					++written;
				}

				// Store the packed u32
				immediate_data.packed_point_light_indices.elements[i] = vi;
			} */

			// Apply material-instance-level (i.e. per-draw-level) data.
			kmaterial_renderer_apply_immediates(renderer->material_renderer, plane->plane_render_data.material, &immediate_data);

			// Draw based on vert/index data.
			if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, plane->plane_render_data.vertex_buffer_offset, 4, 0, true)) {
				KERROR("Failed to bind standard vertex buffer data for water plane.");
				return false;
			}
			if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, plane->plane_render_data.index_buffer_offset, 6, 0, false)) {
				KERROR("Failed to draw water plane using index data.");
				return false;
			}
		}

		renderer_end_debug_label();
	}
	if (do_depth_prepass) {
		// Switch back on.
		renderer_set_depth_write_enabled(true);
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_bias_enabled(false);
		renderer_set_depth_bias(0.0f, 0.0f, 0.0f);
	}

	// Transparent geometries done similar to above

	// Ensure valid depth state.
	/* renderer_set_depth_test_enabled(true);
	renderer_set_depth_write_enabled(false); */

	// static transparent
	draw_geo_list(renderer, p_frame_data, directional_light, view_id, projection_id, near_clip, far_clip, clipping_plane, pass_data->transparent_meshes_by_material_count, pass_data->transparent_meshes_by_material);

	// animated transparent
	draw_geo_list(renderer, p_frame_data, directional_light, view_id, projection_id, near_clip, far_clip, clipping_plane, pass_data->animated_transparent_meshes_by_material_count, pass_data->animated_transparent_meshes_by_material);

	// Mesh end render
	renderer_end_rendering(renderer->renderer_state, p_frame_data);
	renderer_end_debug_label();

	return true;
}

static b8 draw_shadow_material_geometries (kforward_renderer *renderer, u32 cascade_index, u32 instance_id, ktexture base_colour_texture, u16 count, kgeometry_render_data *geometries) {
	// Apply the appropriate texture.
	kshader_set_binding_texture(renderer->shadow_pass.staticmesh_shader, 1, instance_id, 0, 0, base_colour_texture);
	// Ensure the binding set is applied.
	kshader_apply_binding_set(renderer->shadow_pass.staticmesh_shader, 1, instance_id);

	// Now draw each mesh geometry.
	for (u32 g = 0; g < count; ++g) {

		kgeometry_render_data *geo_data = &geometries[g];

		b8 is_animated = geo_data->animation_id != INVALID_ID_U16;

		// Ensure the right vertex layout index is used.
		kshader_system_use(renderer->shadow_pass.staticmesh_shader, is_animated ? VERTEX_LAYOUT_INDEX_SKINNED : VERTEX_LAYOUT_INDEX_STATIC);
		renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

		kmatrix_id m = ktransform_kmatrixid_get(geo_data->transform);
		// Set immediate data.
		shadow_staticmesh_immediate_data immediate_data = {
			.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_TRANSFORM),
			.transform_index = UNPACK_U32_U16_AT(m, 1),
			.cascade_index = cascade_index,
			.geo_type = (u32)is_animated,
			.animation_index = is_animated ? geo_data->animation_id : 0};

		kshader_set_immediate_data(renderer->shadow_pass.staticmesh_shader, &immediate_data, sizeof(shadow_staticmesh_immediate_data));

		// Invert if needed
		b8 winding_inverted = FLAG_GET(geo_data->flags, KGEOMETRY_RENDER_DATA_FLAG_WINDING_INVERTED_BIT);
		if (winding_inverted) {
			renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
		}

		// Draw it.
		b8 includes_index_data = geo_data->index_count > 0;

		if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, geo_data->vertex_offset, geo_data->vertex_count, 0, includes_index_data)) {
			KERROR("renderer_renderbuffer_draw failed to draw standard vertex buffer;");
			return false;
		}
		if (includes_index_data) {
			if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, geo_data->index_offset, geo_data->index_count, 0, !includes_index_data)) {
				KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
				return false;
			}
		}

		// Change back if needed
		if (winding_inverted) {
			renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
		}
	}

	return true;
}

// render frame
b8 kforward_renderer_render_frame (kforward_renderer *renderer, frame_data *p_frame_data, kforward_renderer_render_data *render_data) {
	KASSERT_DEBUG(renderer)

	ktimeline game_timeline = ktimeline_system_get_game();

	// Global material renderer settings
	kmaterial_settings_ubo *settings = &renderer->material_renderer->settings;
	settings->game_time = ktimeline_system_total_get(game_timeline);
	settings->delta_time = ktimeline_system_delta_get(game_timeline);
	settings->render_mode = render_data->forward_data.render_mode;
	KCOPY_TYPE_CARRAY(settings->cascade_splits.elements, render_data->forward_data.cascade_splits, f32, 4);
	// FIXME: Allow multiple projection matrices for non screen-sized renders of the scene.
	KCOPY_TYPE_CARRAY(settings->directional_light_spaces, render_data->forward_data.directional_light_spaces, mat4, 4);
	settings->shadow_bias = render_data->forward_data.shadow_bias;
	settings->shadow_distance = render_data->forward_data.shadow_distance;
	settings->shadow_fade_distance = render_data->forward_data.shadow_fade_distance;
	settings->shadow_split_mult = render_data->forward_data.shadow_split_mult;

	settings->fog_colour = render_data->forward_data.fog_colour;
	settings->fog_start = render_data->forward_data.fog_near;
	settings->fog_end = render_data->forward_data.fog_far;

	settings->mat_ssbo_view_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_VIEW);
	settings->mat_ssbo_proj_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_PROJECTION);
	settings->mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_TRANSFORM);
	settings->mat_ssbo_genc_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_GENERAL);

	render_data->forward_data.skybox.fog_colour = settings->fog_colour;

	// Begin frame
	{

		renderer_begin_debug_label("kforward_renderer frame_begin", (vec3){0.75f, 0.75f, 0.75f});

		// NOTE: frame begin logic here, if required.

		// Set default dynamic state for the frame here.
		// TODO: This can probably be moved to the creation phase since these defaults really
		// only need to run once.

		// Enable depth state.
		renderer_set_depth_test_enabled(true);
		renderer_set_depth_write_enabled(true);

		// Use backface culling.
		renderer_cull_mode_set(RENDERER_CULL_MODE_BACK);

		// Default winding is counter clockwise
		renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);

		renderer_clear_depth_set(renderer->renderer_state, 1.0f);
		renderer_clear_stencil_set(renderer->renderer_state, 0);

		// Turn off stencil testing.
		renderer_set_stencil_test_enabled(false);
		renderer_set_stencil_op(
			RENDERER_STENCIL_OP_KEEP,
			RENDERER_STENCIL_OP_REPLACE,
			RENDERER_STENCIL_OP_KEEP,
			RENDERER_COMPARE_OP_ALWAYS);
		renderer_set_stencil_write_mask(0);
		renderer_set_stencil_reference(0);

		renderer_end_debug_label();
	}

	// Clear colour
	{
		renderer_begin_debug_label("clear_colour", (vec3){0.75f, 0.75f, 0.75f});

		if (!renderer_clear_colour(renderer->renderer_state, renderer->colour_buffer)) {
			KERROR("Failed to clear colour buffer.");
			return false;
		}

		renderer_end_debug_label();
	}

	// Clear depth stencil
	{
		renderer_begin_debug_label("clear_depth_stencil", (vec3){0.75f, 0.75f, 0.75f});

		renderer_clear_depth_set(renderer->renderer_state, 1.0f);
		renderer_clear_stencil_set(renderer->renderer_state, 0);

		if (!renderer_clear_depth_stencil(renderer->renderer_state, renderer->depth_stencil_buffer)) {
			KERROR("Failed to clear depth/stencil buffer");
			return false;
		}

		renderer_end_debug_label();
	}

	// Shadow pass
	if (render_data->shadow_data.do_pass) {
		renderer_begin_debug_label("shadow pass", (vec3){1.0f, 0.0f, 0.0f});

		// Clear the image first.
		renderer_clear_depth_stencil(renderer->renderer_state, renderer->shadow_pass.shadow_tex);

		rect_2di render_area = (rect_2di){0, 0, renderer->shadow_pass.resolution, renderer->shadow_pass.resolution};

		/* // Set the global UBO data first.
		{
			// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
			renderer_begin_debug_label("shadow_staticmesh_global", (vec3){1.0f, 0.0f, 0.0f});
			shadow_staticmesh_global_ubo global_ubo_data = {0};
			for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
				global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
			}
			kshader_set_binding_data(renderer->shadow_pass.staticmesh_shader, 0, renderer->shadow_pass.sm_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_staticmesh_global_ubo));
			renderer_end_debug_label();
		}

		// Set the global UBO data first.
		{
			// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
			renderer_begin_debug_label("shadow_heightmap_terrain_global", (vec3){1.0f, 0.0f, 0.0f});
			shadow_staticmesh_global_ubo global_ubo_data = {0};
			for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
				global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
			}
			kshader_set_binding_data(renderer->shadow_pass.hmt_shader, 0, renderer->shadow_pass.hmt_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_staticmesh_global_ubo));
			renderer_end_debug_label();
		}

		// Set the global UBO data first.
		{
			// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
			renderer_begin_debug_label("shadow_hf_terrain_global", (vec3){1.0f, 0.0f, 0.0f});
			shadow_hf_terrain_global_ubo global_ubo_data = {0};
			for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
				global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
			}
			kshader_set_binding_data(renderer->shadow_pass.hft_shader, 0, renderer->shadow_pass.hft_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_hf_terrain_global_ubo));
			renderer_end_debug_label();
		} */

		// One renderpass per cascade - directional light.
		for (u32 p = 0; p < render_data->shadow_data.cascade_count; ++p) {
			{
				char label_text[17] = "shadow_cascade_0";
				label_text[15] = '0' + p;
				renderer_begin_debug_label(label_text, (vec3){0.8f - (p * 0.1f), 0.0f, 0.0f});
			}

			// Set the global UBO data first.
			{
				// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
				renderer_begin_debug_label("shadow_staticmesh_global", (vec3){1.0f, 0.0f, 0.0f});
				shadow_staticmesh_global_ubo global_ubo_data = {0};
				for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
					global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
				}
				kshader_set_binding_data(renderer->shadow_pass.staticmesh_shader, 0, renderer->shadow_pass.sm_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_staticmesh_global_ubo));
				renderer_end_debug_label();
			}

			// Set the global UBO data first.
			{
				// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
				renderer_begin_debug_label("shadow_heightmap_terrain_global", (vec3){1.0f, 0.0f, 0.0f});
				shadow_staticmesh_global_ubo global_ubo_data = {0};
				for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
					global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
				}
				kshader_set_binding_data(renderer->shadow_pass.hmt_shader, 0, renderer->shadow_pass.hmt_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_staticmesh_global_ubo));
				renderer_end_debug_label();
			}

			// Set the global UBO data first.
			{
				// FIXME: Not sure this can be done here, may have to do inside loop below (i.e. within the 'render pass').
				renderer_begin_debug_label("shadow_hf_terrain_global", (vec3){1.0f, 0.0f, 0.0f});
				shadow_hf_terrain_global_ubo global_ubo_data = {0};
				for (u32 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
					global_ubo_data.view_projections[i] = render_data->shadow_data.cascades[i].view_projection;
				}
				kshader_set_binding_data(renderer->shadow_pass.hft_shader, 0, renderer->shadow_pass.hft_set0_instance_id, 0, 0, &global_ubo_data, sizeof(shadow_hf_terrain_global_ubo));
				renderer_end_debug_label();
			}

			// Shadow cascade begin render
			renderer_begin_rendering(renderer->renderer_state, p_frame_data, render_area, 0, 0, renderer->shadow_pass.shadow_tex, p);
			renderer_shader_use(renderer->renderer_state, renderer->shadow_pass.staticmesh_shader, VERTEX_LAYOUT_INDEX_STATIC);
			set_render_state_defaults(render_area);

			// Don't cull for the shadow pass
			renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

			// Viewport - the shadow pass requires a special one that matches the texture size. It needs flipping on the Y axis, though.
			rect_2di viewport_rect = {render_area.x, render_area.height, render_area.width, -render_area.height};
			renderer_viewport_set(viewport_rect);
			// Scissor also needs to match
			renderer_scissor_set(render_area);

			// Ensure valid depth state - this must be done for every pass.
			renderer_set_depth_test_enabled(true);
			renderer_set_depth_write_enabled(true);

			// Apply the global binding set.
			kshader_apply_binding_set(renderer->shadow_pass.staticmesh_shader, 0, renderer->shadow_pass.sm_set0_instance_id);

			// Transparent geometries - each material grouping.
			for (u32 i = 0, group_arr_idx = 1; i < render_data->shadow_data.transparent_geometries_by_material_count; ++i) {
				kmaterial_render_data *material = &render_data->shadow_data.transparent_geometries_by_material[i];

				// Default to the default_instance_id, unless transparent.
				ktexture base_colour_texture = renderer->shadow_pass.default_base_colour;
				// NOTE: Ensure there are enough group ids reserved. If not, change the value in kforward_renderer_create().
				KASSERT_DEBUG(group_arr_idx < renderer->shadow_pass.sm_set1_max_instances);

				u32 instance_id = renderer->shadow_pass.sm_set1_instance_ids[group_arr_idx];
				// Use the material's texture instead of the default one unless it is not loaded.
				base_colour_texture = kmaterial_texture_get(renderer->material_system, material->base_material, KMATERIAL_TEXTURE_INPUT_BASE_COLOUR);
				if (!texture_is_loaded(base_colour_texture)) {
					// Failsafe in case the given material doesn't have a base colour texture.
					base_colour_texture = renderer->shadow_pass.default_base_colour;
				}
				group_arr_idx++;

				if (!draw_shadow_material_geometries(renderer, p, instance_id, base_colour_texture, material->geometry_count, material->geometries)) {
					return false;
				}
			}

			// Opaque geometries
			{
				// Default to the default_group_id, unless transparent.
				u32 instance_id = renderer->shadow_pass.sm_default_instance_id;
				ktexture base_colour_texture = renderer->shadow_pass.default_base_colour;

				b8 result = draw_shadow_material_geometries(renderer, p, instance_id, base_colour_texture, render_data->shadow_data.opaque_geometry_count, render_data->shadow_data.opaque_geometries);
				if (!result) {
					return false;
				}
			}

			// HF Terrain
			{
				kshader shader = renderer->shadow_pass.hft_shader;
				kshader_system_use(shader, VERTEX_LAYOUT_INDEX_STATIC);
				renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

				// Apply the global binding set.
				kshader_apply_binding_set(shader, 0, renderer->shadow_pass.hft_set0_instance_id);
				for (u32 b = 0; b < render_data->shadow_data.hf_terrain_data.block_count; ++b) {
					hf_terrain_block_render_data *block_data = &render_data->shadow_data.hf_terrain_data.blocks[b];

					for (u32 c = 0; c < block_data->chunk_count; ++c) {
						hf_terrain_chunk_render_data *chunk_data = &block_data->chunks[c];

						// Immediate data.
						shadow_hf_terrain_immediate_data immediate = {.cascade_index = p};
						kshader_set_immediate_data(shader, &immediate, sizeof(shadow_hf_terrain_immediate_data));

						// Draw it.
						if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, chunk_data->vertex_buffer_offset, chunk_data->vertex_count, 0, true)) {
							KERROR("Shadow Renderer HF Terrain: failed to draw vertex buffer.");
							return false;
						}
						if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, render_data->shadow_data.hf_terrain_data.index_buffer_offset, render_data->shadow_data.hf_terrain_data.index_count, 0, false)) {
							KERROR("Shadow Renderer HF Terrain: failed to draw index buffer.");
							return false;
						}
					}
				}
			}

			// Heightmap Terrain - use the terrain shadowmap shader.
			{
				kshader_system_use(renderer->shadow_pass.hmt_shader, VERTEX_LAYOUT_INDEX_STATIC);
				renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

				// Apply the global binding set.
				kshader_apply_binding_set(renderer->shadow_pass.staticmesh_shader, 0, renderer->shadow_pass.sm_set0_instance_id);

				for (u32 i = 0; i < render_data->shadow_data.terrain_count; ++i) {
					hm_terrain_render_data *t = &render_data->shadow_data.terrains[i];
					kmatrix_id m = ktransform_kmatrixid_get(t->transform);

					for (u32 c = 0; c < t->chunk_count; ++c) {
						hm_terrain_chunk_render_data *chunk = &t->chunks[c];

						// Set immediate data.
						shadow_staticmesh_immediate_data immediate_data = {
							.mat_ssbo_tran_offset = kmatrix_system_get_offset_by_type(renderer->matrix_system, KMATRIX_TYPE_TRANSFORM),
							.transform_index = UNPACK_U32_U16_AT(m, 1),
							.cascade_index = p};

						kshader_set_immediate_data(renderer->shadow_pass.staticmesh_shader, &immediate_data, sizeof(shadow_staticmesh_immediate_data));

						// Draw it.
						// NOTE: heightmap terrain chunks always include index data.
						if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, chunk->vertex_offset, chunk->vertex_count, 0, true)) {
							KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
							return false;
						}
						if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, chunk->index_offset, chunk->index_count, 0, false)) {
							KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
							return false;
						}
					}
				}
			}

			// End the cascade pass
			renderer_end_rendering(renderer->renderer_state, p_frame_data);

			renderer_end_debug_label();

		} // End cascade pass

		// Prepare the image to be sampled from.
		ktexture_flag_bits flags = texture_flags_get(renderer->shadow_pass.shadow_tex);
		renderer_texture_prepare_for_sampling(renderer->renderer_state, renderer->shadow_pass.shadow_tex, flags);

		renderer_end_debug_label();
	} // End shadow pass

	// Forward pass
	if (render_data->forward_data.do_pass) {

		renderer_begin_debug_label("Forward pass", (vec3){1.0f, 0.5f, 0});

		// FIXME: If render mode is not 'regular', there is no need to perform the reflect/refract passes.

		// Reflect/refract passes on all water planes first.
		for (u32 w = 0; w < render_data->forward_data.water_plane_count; ++w) {
			{
				char label_text[14] = "water_plane_0";
				label_text[12] = ('0' + w);
				renderer_begin_debug_label(label_text, (vec3){0.0f, 0.3f, 0.8f - (w * 0.1f)});
			}

			kforward_pass_water_plane_render_data *plane = &render_data->forward_data.water_planes[w];

			ktexture refraction_colour = kmaterial_texture_get(renderer->material_system, plane->plane_render_data.material.base_material, KMATERIAL_TEXTURE_INPUT_REFRACTION);
			ktexture refraction_depth = kmaterial_texture_get(renderer->material_system, plane->plane_render_data.material.base_material, KMATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH);

			ktexture reflection_colour = kmaterial_texture_get(renderer->material_system, plane->plane_render_data.material.base_material, KMATERIAL_TEXTURE_INPUT_REFLECTION);
			ktexture reflection_depth = kmaterial_texture_get(renderer->material_system, plane->plane_render_data.material.base_material, KMATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH);

			ktexture_flag_bits refraction_colour_flags = texture_flags_get(refraction_colour);
			ktexture_flag_bits refraction_depth_flags = texture_flags_get(refraction_depth);
			ktexture_flag_bits reflection_colour_flags = texture_flags_get(reflection_colour);
			/* ktexture_flag_bits reflection_depth_flags = texture_flags_get(reflection_depth); */

			// Refract pass (draw everything minus planes)
			{

				// Viewport
				rect_2di vp_rect = {0};
				if (!texture_dimensions_get(refraction_colour, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
					return false;
				}

				// TODO: clipping plane should be based on position/orientation of water plane.
				vec4 refract_clipping_plane = (vec4){0, -1, 0, 0 + 1.0f}; // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.

				{
					char label_text[22] = "water_plane_0_refract";
					label_text[12] = '0' + w;
					renderer_begin_debug_label(label_text, (vec3){0.3f, 0.3f, 0.8f - (w * 0.1f)});
				}
				scene_pass(
					renderer,
					p_frame_data,
					render_data->forward_data.dir_light,
					vp_rect,
					render_data->forward_data.projection_id,
					render_data->forward_data.view_id, // Use the 'normal' view matrix for refraction.
					render_data->forward_data.near_clip,
					render_data->forward_data.far_clip,
					refraction_colour,
					refraction_depth,
					refract_clipping_plane,
					render_data->forward_data.irradiance_cubemap_texture_count,
					render_data->forward_data.irradiance_cubemap_textures,
					&render_data->forward_data.skybox,
					&plane->refraction_pass,
					0, // water_plane_count
					0,
					false,
					render_data->forward_data.render_mode); // water_planes

				renderer_end_debug_label();
			} // end refract

			// Reflect pass (draw everything minus planes) (NOTE: Done same as above, but with different props)
			{

				// Viewport
				rect_2di vp_rect = {0};
				if (!texture_dimensions_get(reflection_colour, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
					return false;
				}

				// TODO: clipping plane should be based on position/orientation of water plane.
				vec4 reflect_clipping_plane = (vec4){0, 1, 0, 0}; // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.
				{
					char label_text[22] = "water_plane_0_reflect";
					label_text[12] = '0' + w;
					renderer_begin_debug_label(label_text, (vec3){0.3f, 0.3f, 0.8f - (w * 0.1f)});
				}
				scene_pass(
					renderer,
					p_frame_data,
					render_data->forward_data.dir_light,
					vp_rect,
					render_data->forward_data.projection_id,
					plane->reflection_pass.view_id, // Use the 'inverted' view matrix for this water plane's reflection pass.
					render_data->forward_data.near_clip,
					render_data->forward_data.far_clip,
					reflection_colour,
					reflection_depth,
					reflect_clipping_plane,
					render_data->forward_data.irradiance_cubemap_texture_count,
					render_data->forward_data.irradiance_cubemap_textures,
					&render_data->forward_data.skybox,
					&plane->reflection_pass,
					0,
					0,
					false,
					render_data->forward_data.render_mode);

				renderer_end_debug_label();
			} // end reflect

			// Prepare the textures to be sampled from.
			renderer_texture_prepare_for_sampling(renderer->renderer_state, reflection_colour, reflection_colour_flags);
			renderer_texture_prepare_for_sampling(renderer->renderer_state, refraction_colour, refraction_colour_flags);
			renderer_texture_prepare_for_sampling(renderer->renderer_state, refraction_depth, refraction_depth_flags);

			renderer_end_debug_label();
		} // end water plane passes

		// "Standard" pass (draw planes before transparent objects) (NOTE: Done same as above, but with water planes drawn between opaque and transparent geos)
		{
			rect_2di vp_rect = {0};
			if (!texture_dimensions_get(renderer->colour_buffer, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
				return false;
			}

			// Finally, draw the scene normally with no clipping. Include the water plane rendering. Uses bound camera.
			vec4 clipping_plane = vec4_zero(); // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.

			renderer_begin_debug_label("standard scene pass", (vec3){1.0f, 0.5f, 1.0f});
			scene_pass(
				renderer,
				p_frame_data,
				render_data->forward_data.dir_light,
				vp_rect,
				render_data->forward_data.projection_id,
				render_data->forward_data.view_id, // Use the 'normal' view matrix for standard.
				render_data->forward_data.near_clip,
				render_data->forward_data.far_clip,
				renderer->colour_buffer,
				renderer->depth_stencil_buffer,
				clipping_plane,
				render_data->forward_data.irradiance_cubemap_texture_count,
				render_data->forward_data.irradiance_cubemap_textures,
				&render_data->forward_data.skybox,
				&render_data->forward_data.standard_pass,
				render_data->forward_data.water_plane_count,
				render_data->forward_data.water_planes,
				true,
				render_data->forward_data.render_mode);

			renderer_end_debug_label();
		} // end 'standard' pass

		renderer_end_debug_label();
	}

#if KOHI_DEBUG
	// NOTE: World debug pass only included in debug builds.
	if (render_data->world_debug_data.do_pass) {

		if (render_data->world_debug_data.geometry_count > 0) {
			renderer_begin_debug_label("world debug pass", (vec3){0.5f, 1.0f, 0});

			// World debug begin render
			rect_2di vp_rect = {0};
			if (!texture_dimensions_get(renderer->colour_buffer, (u32 *)&vp_rect.width, (u32 *)&vp_rect.height)) {
				return false;
			}

			renderer_begin_rendering(renderer->renderer_state, p_frame_data, vp_rect, 1, &renderer->colour_buffer, renderer->depth_stencil_buffer, 0);
			set_render_state_defaults(vp_rect);

			// Enable depth state.
			renderer_set_depth_test_enabled(true);
			renderer_set_depth_write_enabled(true);
			renderer_set_stencil_test_enabled(false);

			kshader_system_use_with_topology(renderer->world_debug_pass.debug_shader, PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT, VERTEX_LAYOUT_INDEX_STATIC);

			// Global UBO data
			debug_shader_global_ubo_data global_ubo_data = {
				.view = render_data->world_debug_data.view,
				.projection = render_data->world_debug_data.projection};
			kshader_set_binding_data(renderer->world_debug_pass.debug_shader, 0, renderer->world_debug_pass.debug_set0_instance_id, 0, 0, &global_ubo_data, sizeof(global_ubo_data));
			kshader_apply_binding_set(renderer->world_debug_pass.debug_shader, 0, renderer->world_debug_pass.debug_set0_instance_id);

			for (u32 i = 0; i < render_data->world_debug_data.geometry_count; ++i) {
				kdebug_geometry_render_data *geo = &render_data->world_debug_data.geometries[i];

				debug_shader_immediate_data immediate_data = {
					.model = geo->model,
					.colour = geo->colour};
				kshader_set_immediate_data(renderer->world_debug_pass.debug_shader, &immediate_data, sizeof(immediate_data));

				// Draw it.
				b8 includes_index_data = geo->geo.index_count > 0;

				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, geo->geo.vertex_offset, geo->geo.vertex_count, 0, includes_index_data)) {
					KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
					return false;
				}
				if (includes_index_data) {
					if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, geo->geo.index_offset, geo->geo.index_count, 0, !includes_index_data)) {
						KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
						return false;
					}
				}
			}

			// Render the grid, but using the colour shader.
			if (render_data->world_debug_data.draw_grid) {
				kshader_system_use_with_topology(renderer->world_debug_pass.colour_shader, PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT, VERTEX_LAYOUT_INDEX_STATIC);
				renderer_cull_mode_set(RENDERER_CULL_MODE_NONE);

				// Global UBO data
				colour_3d_global_ubo global_ubo_data = {
					.view = render_data->world_debug_data.view,
					.projection = render_data->world_debug_data.projection};
				kshader_set_binding_data(renderer->world_debug_pass.colour_shader, 0, renderer->world_debug_pass.colour_set0_instance_id, 0, 0, &global_ubo_data, sizeof(colour_3d_global_ubo));
				kshader_apply_binding_set(renderer->world_debug_pass.colour_shader, 0, renderer->world_debug_pass.colour_set0_instance_id);

				kdebug_geometry_render_data *g = &render_data->world_debug_data.grid_geometry;

				// FIXME: Hook up transform ssbo to editor shader
				mat4 model = mat4_identity();

				colour_3d_immediate_data immediate_data = {.model = model};
				kshader_set_immediate_data(renderer->world_debug_pass.colour_shader, &immediate_data, sizeof(colour_3d_immediate_data));

				// Draw it.
				b8 includes_index_data = g->geo.index_count > 0;

				if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->standard_vertex_buffer, g->geo.vertex_offset, g->geo.vertex_count, 0, includes_index_data)) {
					KERROR("renderer_renderbuffer_draw failed to draw vertex buffer;");
					return false;
				}
				if (includes_index_data) {
					if (!renderer_renderbuffer_draw(renderer->renderer_state, renderer->index_buffer, g->geo.index_offset, g->geo.index_count, 0, !includes_index_data)) {
						KERROR("renderer_renderbuffer_draw failed to draw index buffer;");
						return false;
					}
				}
			}

			// World debug end render
			renderer_end_rendering(renderer->renderer_state, p_frame_data);
			renderer_end_debug_label();
		}
	}
#endif

	// Frame_end
	{
		renderer_begin_debug_label("kforward_renderer frame_end", (vec3){0.75f, 0.75f, 0.75f});
		// NOTE: This is a no-op intentionally for now.
		renderer_end_debug_label();
	}

	return true;
}
