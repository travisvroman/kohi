/**
 * @file renderer_frontend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The renderer frontend, which is the only thing the rest of the engine sees.
 * This is responsible for transferring any data to and from the renderer backend in an
 * agnostic way.
 * @version 1.0
 * @date 2026-01-06
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 * The lifecycle of a single frame (including mult. monitors) should look something like this:
 *
 * frame_prepare - Increments renderer frame number
 *
 * frame_prepare_window_surface - verifies swapchain, gets image index, etc.
 * frame_commands_begin - begins command list/buffer
 * <insert renderpasses, draws, etc. here>
 * frame_commands_end - ends command list/buffer
 * frame_submit - submits command list/buffer for execution.
 * frame_present - once frame execution is complete, presents swapchain image.
 *
 *
 *
 */

#pragma once

#include <defines.h>
#include <math/geometry.h>
#include <strings/kname.h>

#include "core/frame_data.h"
#include "core_render_types.h"
#include "math/math_types.h"
#include "renderer_types.h"

struct shader_uniform;
struct frame_data;
struct viewport;

typedef struct renderer_system_config {
	const char *application_name;
	const char *backend_plugin_name;
	b8 vsync;
	b8 enable_validation;
	b8 power_saving;
	b8 triple_buffering_enabled;

	// The max number of shaders that can be held. Must match the shader system's count.
	u16 max_shader_count;
	u16 max_texture_count;

	b8 require_discrete_gpu;
} renderer_system_config;

struct renderer_system_state;
struct kwindow;

b8 renderer_system_deserialize_config (const char *config_str, renderer_system_config *out_config);
void renderer_system_destroy_config (renderer_system_config *config);

/**
 * @brief Initializes the renderer frontend/system. Should be called twice - once
 * to obtain the memory requirement (passing state=0), and a second time passing
 * allocated memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A block of memory to hold state data, or 0 if obtaining memory requirement.
 * @param config A constant pointer to the configuration for the renderer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_system_initialize (u64 *memory_requirement, struct renderer_system_state *state, const renderer_system_config *config);

/**
 * @brief Shuts the renderer system/frontend down.
 *
 * @param state A pointer to the state block of memory.
 */
KAPI void renderer_system_shutdown (struct renderer_system_state *state);

KAPI u16 renderer_system_frame_number_get (struct renderer_system_state *state);

KAPI b8 renderer_on_window_created (struct renderer_system_state *state, struct kwindow *window);
KAPI void renderer_on_window_destroyed (struct renderer_system_state *state, struct kwindow *window);

/**
 * @brief Handles window resize events.
 *
 * @param state A pointer to the state block of memory.
 * @param window A const pointer to the window that was resized.
 */
KAPI void renderer_on_window_resized (struct renderer_system_state *state, const struct kwindow *window);

/**
 * @brief Begins the marking of a section of commands, listed under a given name and
 * colour. Becomes a no-op in non-debug builds.
 * NOTE: Each renderer backend will have different or possibly non-existant implementations of this.
 *
 * @param label_text The text to be used for the label.
 * @param colour The colour to be used for the label.
 */
KAPI void renderer_begin_debug_label (const char *label_text, vec3 colour);

/**
 * @brief Ends the last debug section of commands. Becomes a no-op in non-debug builds.
 * NOTE: Each renderer backend will have different or possibly non-existant implementations of this.
 */
KAPI void renderer_end_debug_label (void);

/**
 * @brief Performs setup routines required at the start of a frame.
 * @note A false result does not necessarily indicate failure. It can also specify that
 * the backend is simply not in a state capable of drawing a frame at the moment, and
 * that it should be attempted again on the next loop. End frame does not need to (and
 * should not) be called if this is the case.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_prepare (struct renderer_system_state *state, struct frame_data *p_frame_data);

/**
 * @brief Prepares a window's surface for drawing.
 * @param p_frame_data A pointer to the current frame's data.
 d @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_prepare_window_surface (struct renderer_system_state *state, struct kwindow *window, struct frame_data *p_frame_data);

/**
 * @brief Begins a render. There must be at least one of these and a matching end per frame.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_command_list_begin (struct renderer_system_state *state, struct frame_data *p_frame_data);

/**
 * @brief Ends a render.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_command_list_end (struct renderer_system_state *state, struct frame_data *p_frame_data);

KAPI b8 renderer_frame_submit (struct renderer_system_state *state, struct frame_data *p_frame_data);

/**
 * @brief Performs routines required to draw a frame, such as presentation. Should only be called
 * after a successful return of begin_frame.
 *
 * @param p_frame_data A constant pointer to the current frame's data.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_frame_present (struct renderer_system_state *state, struct kwindow *window, struct frame_data *p_frame_data);

/**
 * @brief Sets the renderer viewport to the given rectangle.
 *
 * @param rect The viewport rectangle to be set.
 */
KAPI void renderer_viewport_set (rect_2di rect);

/**
 * @brief Resets the viewport to the default, which matches the application window.
 */
KAPI void renderer_viewport_reset (void);

/**
 * @brief Sets the renderer scissor to the given rectangle.
 *
 * @param rect The scissor rectangle to be set.
 */
KAPI void renderer_scissor_set (rect_2di rect);

/**
 * @brief Resets the scissor to the default, which matches the application window.
 */
KAPI void renderer_scissor_reset (void);

/**
 * @brief Set the renderer to use the given winding direction.
 *
 * @param winding The winding direction.
 */
KAPI void renderer_winding_set (renderer_winding winding);

/**
 * @brief Set the renderer to use the given cull mode.
 *
 * @param cull_mode The cull mode.
 */
KAPI void renderer_cull_mode_set (renderer_cull_mode cull_mode);

/**
 * @brief Set stencil testing enabled/disabled.
 *
 * @param enabled Indicates if stencil testing should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_stencil_test_enabled (b8 enabled);

/**
 * @brief Set the stencil reference for testing.
 *
 * @param reference The reference to use when stencil testing/writing.
 */
KAPI void renderer_set_stencil_reference (u32 reference);

/**
 * @brief Set depth testing enabled/disabled.
 *
 * @param enabled Indicates if depth testing should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_depth_test_enabled (b8 enabled);

/**
 * @brief Set depth write enabled/disabled.
 *
 * @param enabled Indicates if depth write should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_depth_write_enabled (b8 enabled);

/**
 * @brief Sets depth bias factors and clamp dynamically.
 *
 * @param constant_factor A scalar factor controlling the constant depth value added to each fragment.
 * @param clamp The maximum (or minimum) depth bias of a fragment.
 * @param slope_factor A scalar factor applied to a fragment’s slope in depth bias calculations.
 */
KAPI void renderer_set_depth_bias (f32 constant_factor, f32 clamp, f32 slope_factor);

/**
 * @brief Enables/disables depth bias.
 *
 * @param enabled True to enable; otherwise disable.
 */
KAPI void renderer_set_depth_bias_enabled (b8 enabled);

/**
 * @brief Set stencil operation.
 *
 * @param fail_op Specifys the action performed on samples that fail the stencil test.
 * @param pass_op Specifys the action performed on samples that pass both the depth and stencil tests.
 * @param depth_fail_op Specifys the action performed on samples that pass the stencil test and fail the depth test.
 * @param compare_op Specifys the comparison operator used in the stencil test.
 */
KAPI void renderer_set_stencil_op (renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);

/**
 * @brief Begins rendering against the given targets.
 *
 * @param state A pointer to the renderer system state.
 * @param p_frame_data A pointer to the current frame data.
 * @param render_area A rectangle representing the area of the attachments to render to.
 * @param colour_target_count The number of colour targets to be drawn to.
 * @param colour_targets An array of handles to colour targets. Required unless colour_target_count is 0.
 * @param depth_stencil_target A handle to a depth stencil target to render to.
 * @param depth_stencil_layer For layered depth targets, the layer index to render to. Ignored otherwise.
 */
KAPI void renderer_begin_rendering (struct renderer_system_state *state, struct frame_data *p_frame_data, rect_2di render_area, u32 colour_target_count, ktexture *colour_targets, ktexture depth_stencil_target, u32 depth_stencil_layer);

/**
 *
 * @param state A pointer to the renderer system state.
 * @param p_frame_data A pointer to the current frame data.
 */
KAPI void renderer_end_rendering (struct renderer_system_state *state, struct frame_data *p_frame_data);

/**
 * @brief Set stencil compare mask.
 *
 * @param compare_mask The new value to use as the stencil compare mask.
 */
KAPI void renderer_set_stencil_compare_mask (u32 compare_mask);

/**
 * @brief Set stencil write mask.
 *
 * @param write_mask The new value to use as the stencil write mask.
 */
KAPI void renderer_set_stencil_write_mask (u32 write_mask);

/**
 * Attempts to acquire renderer-specific resources to back a texture.
 *
 * @param state A pointer to the renderer system state.
 * @param t The texture handle which also points to the backing resource(s) of the texture.
 * @param name The name of the texture.
 * @param type The type of texture.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @param channel_count The number of channels in the texture (i.e. RGBA = 4)
 * @param mip_levels The number of mip maps the internal texture has. Must always be at least 1.
 * @param array_size For arrayed textures, how many "layers" there are. Otherwise this is 1.
 * @param flags Various property flags to be used in creating this texture.
 * @returns True on success, otherwise false;
 */
KAPI b8 renderer_texture_resources_acquire (struct renderer_system_state *state, ktexture t, kname name, ktexture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, ktexture_flag_bits flags);

/**
 * Releases backing renderer-specific resources for the given texture.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the renderer texture whose resources are to be released.
 */
KAPI void renderer_texture_resources_release (struct renderer_system_state *state, ktexture t);

/**
 * @brief Resizes a texture. There is no check at this level to see if the
 * texture is writeable. Internal resources are destroyed and re-created at
 * the new resolution. Data is lost and would need to be reloaded.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to be resized.
 * @param new_width The new width in pixels.
 * @param new_height The new height in pixels.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_resize (struct renderer_system_state *state, ktexture t, u32 new_width, u32 new_height);

/**
 * @brief Writes the given data to the provided texture.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to be written to. NOTE: Must be a writeable texture.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param pixels The raw image data to be written.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_write_data (struct renderer_system_state *state, ktexture t, u32 bpp, u32 px_x, u32 px_y, i32 layer, u32 width, u32 height, const u8 *pixels, b8 defer_to_next_frame);

/**
 * @brief Reads the given data from the provided texture.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to be read from.
 * @param offset The offset in bytes from the beginning of the data to be read.
 * @param size The number of bytes to be read.
 * @param out_pixelshader A handle to a block of memory to write the read data to.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_read_data (struct renderer_system_state *state, ktexture t, u32 offset, u32 size, u8 **out_memory);

/**
 * @brief Reads a pixel from the provided texture at the given x/y coordinate.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to be read from.
 * @param x The pixel x-coordinate.
 * @param y The pixel y-coordinate.
 * @param out_rgba A pointer to an array of u8s to hold the pixel data (should be sizeof(u8) * 4)
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_read_pixel (struct renderer_system_state *state, ktexture t, u32 x, u32 y, u8 **out_rgba);

/**
 * @brief Registers a texture with the given handle to the default texture slot specified.
 *
 * @param state A pointer to the renderer system state.
 * @param default_texture The texture slot to register to.
 * @param t A handle to the texture to be registered.
 */
KAPI void renderer_default_texture_register (struct renderer_system_state *state, renderer_default_texture default_texture, ktexture t);

/**
 * @brief Gets a texture handle with the default texture slot specified.
 *
 * @param state A pointer to the renderer system state.
 * @param default_texture The texture slot to register to.
 * @returns A handle to the default texture.
 */
KAPI ktexture renderer_default_texture_get (struct renderer_system_state *state, renderer_default_texture default_texture);

/**
 * @brief Acquires GPU resources and uploads geometry data.
 *
 * @param geometry A pointer to the geometry to upload.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_geometry_upload (kgeometry *geometry);

/**
 * @brief Updates vertex data in the given geometry with the provided data in the given range.
 *
 * @param g A pointer to the geometry to be created.
 * @param offset The offset in bytes to update. 0 if updating from the beginning.
 * @param vertex_count The number of vertices which will be updated.
 * @param vertices The vertex data.
 */
KAPI void renderer_geometry_vertex_update (kgeometry *g, u32 offset, u32 vertex_count, void *vertices, b8 include_in_frame_workload);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 * NOTE: Does NOT release index and vertex data arrays. Call geometry_destroy() to do that.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
KAPI void renderer_geometry_destroy (kgeometry *geometry);

/**
 * @brief Draws the given geometry.
 *
 * @param data The render data of the geometry to be drawn.
 */
KAPI void renderer_geometry_draw (geometry_render_data *data);

/**
 * @brief Sets the value to be used on the colour buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param colour the RGBA colour to be used for the next clear operation. Each element is clamped to [0-1]
 */
KAPI void renderer_clear_colour_set (struct renderer_system_state *state, vec4 colour);

/**
 * @brief Sets the value to be used on the depth buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param depth The depth value to be used for the next clear operation. Clamped to [0-1].
 */
KAPI void renderer_clear_depth_set (struct renderer_system_state *state, f32 depth);

/**
 * @brief Sets the value to be used on the stencil buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param stencil The depth value to be used for the next clear operation.
 */
KAPI void renderer_clear_stencil_set (struct renderer_system_state *state, u32 stencil);

/**
 * @brief Clears the colour buffer using the previously set clear colour.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to clear.
 * @returns True if successful; otherwise false.
 */
KAPI b8 renderer_clear_colour (struct renderer_system_state *state, ktexture t);

/**
 * @brief Clears the depth/stencil buffer using the previously set clear values.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to clear.
 * @returns True if successful; otherwise false.
 */
KAPI b8 renderer_clear_depth_stencil (struct renderer_system_state *state, ktexture t);

/**
 * @brief Performs operations required on the supplied colour texture before presentation.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to prepare for presentation.
 */
KAPI void renderer_colour_texture_prepare_for_present (struct renderer_system_state *state, ktexture t);

/**
 * @brief Performs operations required on the supplied texture before being used for sampling.
 *
 * @param state A pointer to the renderer system state.
 * @param t A handle to the texture to prepare for sampling.
 * @param flags Texture flags from the texture itself, used to determine format/layout, etc.
 */
KAPI void renderer_texture_prepare_for_sampling (struct renderer_system_state *state, ktexture t, ktexture_flag_bits flags);

/**
 * @brief Creates internal shader resources using the provided parameters.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader.
 * @param shader_asset A constant pointer to the shader asset.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 renderer_shader_create (
	struct renderer_system_state *state,
	kshader shader,
	kname name,
	shader_flags flags,
	primitive_topology_type_bits topology_types,
	primitive_topology_type default_topology,
	u8 colour_attachment_count,
	kpixel_format *colour_attachment_formats,
	kpixel_format depth_attachment_format,
	kpixel_format stencil_attachment_format,
	u8 pipeline_count,
	shader_pipeline_config *pipelines,
	u8 binding_set_count,
	const shader_binding_set_config *binding_sets);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be destroyed.
 */
KAPI void renderer_shader_destroy (struct renderer_system_state *state, kshader shader);

/**
 * @brief Reloads the internals of the given shader.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be reloaded.
 * @param shader_stage_count The number of shader stages.
 * @param shader_stages An array of shader stages configs.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_reload (
	struct renderer_system_state *state,
	kshader shader,
	u8 pipeline_count,
	shader_pipeline_config *pipelines);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls. Uses the default topology configured by the shader.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_use (struct renderer_system_state *state, kshader shader, u8 vertex_layout_index);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls using the provided topology.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param type The primitive topology type to use.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_use_with_topology (struct renderer_system_state *state, kshader shader, primitive_topology_type type, u8 vertex_layout_index);

/**
 * @brief Determines if the given shader supports wireframe mode.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_supports_wireframe (struct renderer_system_state *state, kshader shader);

/**
 * @brief Indicates if the given shader flag is set.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param flag The flag to check.
 * @return True if set; otherwise false.
 */
KAPI b8 renderer_shader_flag_get (struct renderer_system_state *state, kshader shader, shader_flags flag);

/**
 * @brief Sets the given shader flag.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param flag The flag to set.
 * @param enabled Indicates whether the flag should be set or unset.
 */
KAPI void renderer_shader_flag_set (struct renderer_system_state *state, kshader shader, shader_flags flag, b8 enabled);

KAPI void renderer_shader_set_immediate_data (struct renderer_system_state *state, kshader shader, const void *data, u8 size);
KAPI void renderer_shader_set_binding_data (struct renderer_system_state *state, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size);
KAPI void renderer_shader_set_binding_texture (struct renderer_system_state *state, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture texture);
KAPI void renderer_shader_set_binding_sampler (struct renderer_system_state *state, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler);
KAPI b8 renderer_shader_apply_binding_set (struct renderer_system_state *state, kshader shader, u8 binding_set, u32 instance_id);
KAPI u32 renderer_shader_acquire_binding_set_instance (struct renderer_system_state *state, kshader shader, u8 binding_set);
KAPI void renderer_shader_release_binding_set_instance (struct renderer_system_state *state, kshader shader, u8 binding_set, u32 instance_id);
KAPI u32 renderer_shader_binding_set_get_max_instance_count (struct renderer_system_state *state, kshader shader, u8 binding_set);

/**
 * @brief Gets a handle to a generic sampler of the given type.
 *
 * @param state A pointer to the renderer state.
 * @param sampler The shader sampler to get a handle to.
 * @returns A handle to a generic sampler of the given type.
 */
KAPI ksampler_backend renderer_generic_sampler_get (struct renderer_system_state *state, shader_generic_sampler sampler);

/**
 * @brief Acquires a internal sampler and returns a handle to it.
 *
 * @param state A pointer to the renderer state.
 * @param name The name of the sampler.
 * @param filter The min/mag filter.
 * @param repeat The repeat mode.
 * @param anisotropy The anisotropy level, if needed; otherwise 0.
 * @return A handle to the sampler on success; otherwise an invalid handle.
 */
KAPI ksampler_backend renderer_sampler_acquire (struct renderer_system_state *state, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy);

/**
 * @brief Releases the internal sampler for the given handle.
 *
 * @param state A pointer to the renderer state.
 * @param map A pointer to the handle whose sampler is to be released. Handle is invalidated upon release.
 */
KAPI void renderer_sampler_release (struct renderer_system_state *state, ksampler_backend *sampler);

/**
 * @brief Recreates the internal sampler pointed to by the given handle. Modifies the handle.
 *
 * @param state A pointer to the renderer state.
 * @param sampler A pointer to the handle of the sampler to be refreshed.
 * @param filter The min/mag filter.
 * @param repeat The repeat mode.
 * @param anisotropy The anisotropy level, if needed; otherwise 0.
 * @param mip_levels The mip levels, if used; otherwise 0.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_sampler_refresh (struct renderer_system_state *state, ksampler_backend *sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels);

/**
 * @brief Attempts to obtain the name of a sampler with the given handle. Returns INVALID_KNAME if not found.
 *
 * @param state A pointer to the renderer state.
 * @param sampler A handle to the sampler whose name to get.
 * @return The name of the sampler on success; otherwise INVALID_KNAME.
 */
KAPI kname renderer_sampler_name_get (struct renderer_system_state *state, ksampler_backend sampler);

/**
 * @brief Indicates if the renderer is capable of multi-threading.
 */
KAPI b8 renderer_is_multithreaded (void);

/**
 * @brief Indicates if the provided renderer flag is enabled. If multiple
 * flags are passed, all must be set for this to return true.
 *
 * @param flag The flag to be checked.
 * @return True if the flag(s) set; otherwise false.
 */
KAPI b8 renderer_flag_enabled_get (renderer_config_flags flag);
/**
 * @brief Sets whether the included flag(s) are enabled or not. If multiple flags
 * are passed, multiple are set at once.
 *
 * @param flag The flag to be checked.
 * @param enabled Indicates whether or not to enable the flag(s).
 */
KAPI void renderer_flag_enabled_set (renderer_config_flags flag, b8 enabled);

/**
 * @brief Obtains the max anisotropy level available from the renderer. 0 means not available.
 */
KAPI f32 renderer_max_anisotropy_get (void);

/**
 * @brief Creates a new renderbuffer to hold data for a given purpose/use. Backed by a
 * renderer-backend-specific buffer resource.
 *
 * @param state A pointer to the renderer state.
 * @param name The name of the renderbuffer.
 * @param type The type of buffer, indicating it's use (i.e. vertex/index data, uniforms, etc.)
 * @param total_size The total size in bytes of the buffer.
 * @param track_type Indicates what type of allocation tracking should be used.
 * @return out_buffer A handle to hold the newly created buffer.
 */
KAPI krenderbuffer renderer_renderbuffer_create (struct renderer_system_state *state, kname name, renderbuffer_type type, u64 total_size, renderbuffer_track_type track_type, renderbuffer_flags);

/**
 * @brief Destroys the given renderbuffer.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be destroyed.
 */
KAPI void renderer_renderbuffer_destroy (struct renderer_system_state *state, krenderbuffer buffer);

/**
 * @brief Binds the given buffer at the provided offset.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to bind.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param binding_index The index of which to bind the buffer. Unless using multiple buffers of the same time, pass 0 here.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_bind (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u32 binding_index);

/**
 * @brief Unbinds the given buffer.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be unbound.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_unbind (struct renderer_system_state *state, krenderbuffer buffer);

/**
 * @brief Maps memory from the given buffer in the provided range to a block of memory and returns it.
 * This memory should be considered invalid once unmapped.
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to map.
 * @param offset The number of bytes from the beginning of the buffer to map.
 * @param size The amount of memory in the buffer to map. Use KWHOLE_SIZE to map the entire buffer.
 */
KAPI void renderer_renderbuffer_map_memory (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u64 size);

/**
 * @brief Unmaps memory from the given buffer in the provided range to a block of memory.
 * This memory should be considered invalid once unmapped.
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to unmap.
 * @param offset The number of bytes from the beginning of the buffer to unmap.
 * @param size The amount of memory in the buffer to unmap.
 */
KAPI void renderer_renderbuffer_unmap_memory (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u64 size);

/**
 * @brief Obtains the mapped memory of the buffer, if mapped.
 * This memory should be considered invalid once unmapped.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer whose mapped memory is to be obtained.
 * @return A pointer to the mapped memory, if mapped. Otherwise KNULL.
 */
KAPI void *renderer_renderbuffer_get_mapped_memory (struct renderer_system_state *state, krenderbuffer buffer);

/**
 * @brief Flushes buffer memory at the given range. Should be done after a write.
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to unmap.
 * @param offset The number of bytes from the beginning of the buffer to flush.
 * @param size The amount of memory in the buffer to flush.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_flush (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u64 size);

/**
 * @brief Reads memory from the provided buffer at the given range to the output variable.
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to read from.
 * @param offset The number of bytes from the beginning of the buffer to read.
 * @param size The amount of memory in the buffer to read.
 * @param out_memory A pointer to a block of memory to read to. Must be of appropriate size.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_read (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u64 size, void **out_memory);

/**
 * @brief Resizes the given buffer to new_total_size. new_total_size must be
 * greater than the current buffer size. Data from the old internal buffer is copied
 * over.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be resized.
 * @param new_total_size The new size in bytes. Must be larger than the current size.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_resize (struct renderer_system_state *state, krenderbuffer buffer, u64 new_total_size);

/**
 * @brief Attempts to allocate memory from the given buffer. Should only be used on
 * buffers that were created with use_freelist = true.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be allocated from.
 * @param size The size in bytes to allocate.
 * @param out_offset A pointer to hold the offset in bytes of the allocation from the beginning of the buffer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_allocate (struct renderer_system_state *state, krenderbuffer buffer, u64 size, u64 *out_offset);

/**
 * @brief Frees memory from the given buffer.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be freed from.
 * @param size The size in bytes to free.
 * @param offset The offset in bytes from the beginning of the buffer to free.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_free (struct renderer_system_state *state, krenderbuffer buffer, u64 size, u64 offset);

/**
 * @brief Clears the given buffer. Internally, resets the free list if one is used.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be freed from.
 * @param zero_memory True if memory should be zeroed; otherwise false. NOTE: this can be an expensive operation on large sums of memory.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_clear (struct renderer_system_state *state, krenderbuffer buffer, b8 zero_memory);

/**
 * @brief Loads provided data into the specified rage of the given buffer.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to load data into.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param size The size of the data in bytes to be loaded.
 * @param data The data to be loaded.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_load_range (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u64 size, const void *data, b8 include_in_frame_workload);

/**
 * @brief Copies data in the specified rage fron the source to the destination buffer.
 *
 * @param state A pointer to the renderer state.
 * @param source A handle to the source buffer to copy data from.
 * @param source_offset The offset in bytes from the beginning of the source buffer.
 * @param dest A pointer to the destination buffer to copy data to.
 * @param dest_offset The offset in bytes from the beginning of the destination buffer.
 * @param size The size of the data in bytes to be copied.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_copy_range (struct renderer_system_state *state, krenderbuffer source, u64 source_offset, krenderbuffer dest, u64 dest_offset, u64 size, b8 include_in_frame_workload);

/**
 * @brief Attempts to draw the contents of the provided buffer at the given offset
 * and element count. Only meant to be used with vertex and index buffers.
 *
 * @param state A pointer to the renderer state.
 * @param buffer A handle to the buffer to be drawn.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param element_count The number of elements to be drawn.
 * @param binding_index The index of which to bind the buffer. Unless using multiple buffers of the same time, pass 0 here.
 * @param bind_only Only bind the buffer, but don't draw.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_draw (struct renderer_system_state *state, krenderbuffer buffer, u64 offset, u32 element_count, u32 binding_index, b8 bind_only);

/**
 * @brief Attempts retrieve the renderer's internal buffer of the given name.
 * @param state A pointer to the renderer state.
 * @param type The name of buffer to retrieve.
 * @returnshader A handle to the buffer on success; otherwise 0/null.
 */
KAPI krenderbuffer renderer_renderbuffer_get (struct renderer_system_state *state, kname name);

/**
 * Waits for the renderer backend to be completely idle of work before returning.
 * NOTE: This incurs a lot of overhead/waits, and should be used sparingly.
 */
KAPI void renderer_wait_for_idle (void);

#if KOHI_DEBUG
KAPI void renderer_debug_pump_brakes (void);
#endif

/**
 * Indicates if PCF filtering is enabled for shadow maps.
 */
KAPI b8 renderer_pcf_enabled (struct renderer_system_state *state);

/**
 * @brief Returns the max number of textures that can be bound at once for a single draw call.
 */
KAPI u16 renderer_max_bound_texture_count_get (struct renderer_system_state *state);

/**
 * @brief Returns the max number of samplers that can be bound at once for a single draw call.
 */
KAPI u16 renderer_max_bound_sampler_count_get (struct renderer_system_state *state);
