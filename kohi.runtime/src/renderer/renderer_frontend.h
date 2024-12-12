/**
 * @file renderer_frontend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The renderer frontend, which is the only thing the rest of the engine sees.
 * This is responsible for transferring any data to and from the renderer backend in an
 * agnostic way.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
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
#include <identifiers/khandle.h>
#include <kresources/kresource_types.h>
#include <strings/kname.h>

#include "core/frame_data.h"
#include "core_render_types.h"
#include "renderer_types.h"
#include "resources/resource_types.h"

struct shader_uniform;
struct frame_data;
struct viewport;

typedef struct renderer_system_config {
    const char* application_name;
    const char* backend_plugin_name;
    b8 vsync;
    b8 enable_validation;
    b8 power_saving;
} renderer_system_config;

struct renderer_system_state;
struct kwindow;

b8 renderer_system_deserialize_config(const char* config_str, renderer_system_config* out_config);

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
KAPI b8 renderer_system_initialize(u64* memory_requirement, struct renderer_system_state* state, const renderer_system_config* config);

/**
 * @brief Shuts the renderer system/frontend down.
 *
 * @param state A pointer to the state block of memory.
 */
KAPI void renderer_system_shutdown(struct renderer_system_state* state);

KAPI u16 renderer_system_frame_number_get(struct renderer_system_state* state);

KAPI b8 renderer_on_window_created(struct renderer_system_state* state, struct kwindow* window);
KAPI void renderer_on_window_destroyed(struct renderer_system_state* state, struct kwindow* window);

/**
 * @brief Handles window resize events.
 *
 * @param state A pointer to the state block of memory.
 * @param window A const pointer to the window that was resized.
 */
KAPI void renderer_on_window_resized(struct renderer_system_state* state, const struct kwindow* window);

/**
 * @brief Begins the marking of a section of commands, listed under a given name and
 * colour. Becomes a no-op in non-debug builds.
 * NOTE: Each renderer backend will have different or possibly non-existant implementations of this.
 *
 * @param label_text The text to be used for the label.
 * @param colour The colour to be used for the label.
 */
KAPI void renderer_begin_debug_label(const char* label_text, vec3 colour);

/**
 * @brief Ends the last debug section of commands. Becomes a no-op in non-debug builds.
 * NOTE: Each renderer backend will have different or possibly non-existant implementations of this.
 */
KAPI void renderer_end_debug_label(void);

/**
 * @brief Performs setup routines required at the start of a frame.
 * @note A false result does not necessarily indicate failure. It can also specify that
 * the backend is simply not in a state capable of drawing a frame at the moment, and
 * that it should be attempted again on the next loop. End frame does not need to (and
 * should not) be called if this is the case.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_prepare(struct renderer_system_state* state, struct frame_data* p_frame_data);

/**
 * @brief Prepares a window's surface for drawing.
 * @param p_frame_data A pointer to the current frame's data.
 d @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_prepare_window_surface(struct renderer_system_state* state, struct kwindow* window, struct frame_data* p_frame_data);

/**
 * @brief Begins a render. There must be at least one of these and a matching end per frame.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_command_list_begin(struct renderer_system_state* state, struct frame_data* p_frame_data);

/**
 * @brief Ends a render.
 * @param p_frame_data A pointer to the current frame's data.
 * @return True if successful; otherwise false.
 */
KAPI b8 renderer_frame_command_list_end(struct renderer_system_state* state, struct frame_data* p_frame_data);

KAPI b8 renderer_frame_submit(struct renderer_system_state* state, struct frame_data* p_frame_data);

/**
 * @brief Performs routines required to draw a frame, such as presentation. Should only be called
 * after a successful return of begin_frame.
 *
 * @param p_frame_data A constant pointer to the current frame's data.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_frame_present(struct renderer_system_state* state, struct kwindow* window, struct frame_data* p_frame_data);

/**
 * @brief Sets the renderer viewport to the given rectangle.
 *
 * @param rect The viewport rectangle to be set.
 */
KAPI void renderer_viewport_set(vec4 rect);

/**
 * @brief Resets the viewport to the default, which matches the application window.
 */
KAPI void renderer_viewport_reset(void);

/**
 * @brief Sets the renderer scissor to the given rectangle.
 *
 * @param rect The scissor rectangle to be set.
 */
KAPI void renderer_scissor_set(vec4 rect);

/**
 * @brief Resets the scissor to the default, which matches the application window.
 */
KAPI void renderer_scissor_reset(void);

/**
 * @brief Set the renderer to use the given winding direction.
 *
 * @param winding The winding direction.
 */
KAPI void renderer_winding_set(renderer_winding winding);

/**
 * @brief Set stencil testing enabled/disabled.
 *
 * @param enabled Indicates if stencil testing should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_stencil_test_enabled(b8 enabled);

/**
 * @brief Set the stencil reference for testing.
 *
 * @param reference The reference to use when stencil testing/writing.
 */
KAPI void renderer_set_stencil_reference(u32 reference);

/**
 * @brief Set depth testing enabled/disabled.
 *
 * @param enabled Indicates if depth testing should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_depth_test_enabled(b8 enabled);

/**
 * @brief Set depth write enabled/disabled.
 *
 * @param enabled Indicates if depth write should be enabled/disabled for subsequent draws.
 */
KAPI void renderer_set_depth_write_enabled(b8 enabled);

/**
 * @brief Set stencil operation.
 *
 * @param fail_op Specifys the action performed on samples that fail the stencil test.
 * @param pass_op Specifys the action performed on samples that pass both the depth and stencil tests.
 * @param depth_fail_op Specifys the action performed on samples that pass the stencil test and fail the depth test.
 * @param compare_op Specifys the comparison operator used in the stencil test.
 */
KAPI void renderer_set_stencil_op(renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);

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
KAPI void renderer_begin_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data, rect_2d render_area, u32 colour_target_count, khandle* colour_targets, khandle depth_stencil_target, u32 depth_stencil_layer);

/**
 *
 * @param state A pointer to the renderer system state.
 * @param p_frame_data A pointer to the current frame data.
 */
KAPI void renderer_end_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data);

/**
 * @brief Set stencil compare mask.
 *
 * @param compare_mask The new value to use as the stencil compare mask.
 */
KAPI void renderer_set_stencil_compare_mask(u32 compare_mask);

/**
 * @brief Set stencil write mask.
 *
 * @param write_mask The new value to use as the stencil write mask.
 */
KAPI void renderer_set_stencil_write_mask(u32 write_mask);

/**
 * Attempts to acquire renderer-specific resources to back a texture.
 *
 * @param state A pointer to the renderer system state.
 * @param name The name of the texture.
 * @param type The type of texture.
 * @param width The texture width in pixels.
 * @param height The texture height in pixels.
 * @param channel_count The number of channels in the texture (i.e. RGBA = 4)
 * @param mip_levels The number of mip maps the internal texture has. Must always be at least 1.
 * @param array_size For arrayed textures, how many "layers" there are. Otherwise this is 1.
 * @param flags Various property flags to be used in creating this texture.
 * @param out_renderer_texture_handle A pointer to hold the renderer texture handle, which points to the backing resource(s) of the texture.
 * @returns True on success, otherwise false;
 */
KAPI b8 renderer_kresource_texture_resources_acquire(struct renderer_system_state* state, kname name, kresource_texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, kresource_texture_flag_bits flags, khandle* out_renderer_texture_handle);

/**
 * Releases backing renderer-specific resources for the given renderer_texture_id.
 *
 * @param state A pointer to the renderer system state.
 * @param handle A pointer to the handle of the renderer texture whose resources are to be released. Handle is automatically invalidated.
 */
KAPI void renderer_texture_resources_release(struct renderer_system_state* state, khandle* handle);

/**
 * @brief Resizes a texture. There is no check at this level to see if the
 * texture is writeable. Internal resources are destroyed and re-created at
 * the new resolution. Data is lost and would need to be reloaded.
 *
 * @param state A pointer to the renderer system state.
 * @param renderer_texture_handle A handle to the texture to be resized.
 * @param new_width The new width in pixels.
 * @param new_height The new height in pixels.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_resize(struct renderer_system_state* state, khandle renderer_texture_handle, u32 new_width, u32 new_height);

/**
 * @brief Writes the given data to the provided texture.
 *
 * @param state A pointer to the renderer system state.
 * @param renderer_texture_handle A handle to the texture to be written to. NOTE: Must be a writeable texture.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param pixels The raw image data to be written.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_write_data(struct renderer_system_state* state, khandle renderer_texture_handle, u32 offset, u32 size, const u8* pixels);

/**
 * @brief Reads the given data from the provided texture.
 *
 * @param state A pointer to the renderer system state.
 * @param renderer_texture_handle A handle to the texture to be read from.
 * @param offset The offset in bytes from the beginning of the data to be read.
 * @param size The number of bytes to be read.
 * @param out_pixelshader A handle to a block of memory to write the read data to.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_read_data(struct renderer_system_state* state, khandle renderer_texture_handle, u32 offset, u32 size, u8** out_memory);

/**
 * @brief Reads a pixel from the provided texture at the given x/y coordinate.
 *
 * @param state A pointer to the renderer system state.
 * @param renderer_texture_handle A handle to the texture to be read from.
 * @param x The pixel x-coordinate.
 * @param y The pixel y-coordinate.
 * @param out_rgba A pointer to an array of u8s to hold the pixel data (should be sizeof(u8) * 4)
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_texture_read_pixel(struct renderer_system_state* state, khandle renderer_texture_handle, u32 x, u32 y, u8** out_rgba);

/**
 * @brief Registers a texture with the given handle to the default texture slot specified.
 *
 * @param state A pointer to the renderer system state.
 * @param default_texture The texture slot to register to.
 * @param renderer_texture_handle A handle to the texture to be registered.
 */
KAPI void renderer_default_texture_register(struct renderer_system_state* state, renderer_default_texture default_texture, khandle renderer_texture_handle);

/**
 * @brief Gets a texture handle with the default texture slot specified.
 *
 * @param state A pointer to the renderer system state.
 * @param default_texture The texture slot to register to.
 * @returns A handle to the default texture.
 */
KAPI khandle renderer_default_texture_get(struct renderer_system_state* state, renderer_default_texture default_texture);

/**
 * @brief Attempts retrieve the renderer's internal buffer of the given type.
 * @param type The type of buffer to retrieve.
 * @returnshader A handle to the buffer on success; otherwise 0/null.
 */
KAPI renderbuffer* renderer_renderbuffer_get(renderbuffer_type type);

/**
 * @brief Acquires GPU resources and uploads geometry data.
 *
 * @param geometry A pointer to the geometry to upload.
 * @return True on success; otherwise false.
 */
KDEPRECATED("The renderer frontend geometry functions will be removed in a future pass. Upload directly to renderbuffers instead.")
KAPI b8 renderer_geometry_upload(kgeometry* geometry);

/**
 * @brief Updates vertex data in the given geometry with the provided data in the given range.
 *
 * @param g A pointer to the geometry to be created.
 * @param offset The offset in bytes to update. 0 if updating from the beginning.
 * @param vertex_count The number of vertices which will be updated.
 * @param vertices The vertex data.
 */
KDEPRECATED("The renderer frontend geometry functions will be removed in a future pass. Upload directly to renderbuffers instead.")
KAPI void renderer_geometry_vertex_update(kgeometry* g, u32 offset, u32 vertex_count, void* vertices, b8 include_in_frame_workload);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
KDEPRECATED("The renderer frontend geometry functions will be removed in a future pass. Upload directly to renderbuffers instead.")
KAPI void renderer_geometry_destroy(kgeometry* geometry);

/**
 * @brief Draws the given geometry.
 *
 * @param data The render data of the geometry to be drawn.
 */
KDEPRECATED("The renderer frontend geometry functions will be removed in a future pass. Upload directly to renderbuffers instead.")
KAPI void renderer_geometry_draw(geometry_render_data* data);

/**
 * @brief Sets the value to be used on the colour buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param colour the RGBA colour to be used for the next clear operation. Each element is clamped to [0-1]
 */
KAPI void renderer_clear_colour_set(struct renderer_system_state* state, vec4 colour);

/**
 * @brief Sets the value to be used on the depth buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param depth The depth value to be used for the next clear operation. Clamped to [0-1].
 */
KAPI void renderer_clear_depth_set(struct renderer_system_state* state, f32 depth);

/**
 * @brief Sets the value to be used on the stencil buffer clear.
 *
 * @param state A pointer to the renderer system state.
 * @param stencil The depth value to be used for the next clear operation.
 */
KAPI void renderer_clear_stencil_set(struct renderer_system_state* state, u32 stencil);

/**
 * @brief Clears the colour buffer using the previously set clear colour.
 *
 * @param state A pointer to the renderer system state.
 * @param texture_handle A handle to the texture to clear.
 * @returns True if successful; otherwise false.
 */
KAPI b8 renderer_clear_colour(struct renderer_system_state* state, khandle texture_handle);

/**
 * @brief Clears the depth/stencil buffer using the previously set clear values.
 *
 * @param state A pointer to the renderer system state.
 * @param texture_handle A handle to the texture to clear.
 * @returns True if successful; otherwise false.
 */
KAPI b8 renderer_clear_depth_stencil(struct renderer_system_state* state, khandle texture_handle);

/**
 * @brief Performs operations required on the supplied colour texture before presentation.
 *
 * @param state A pointer to the renderer system state.
 * @param texture_handle A handle to the texture to prepare for presentation.
 */
KAPI void renderer_colour_texture_prepare_for_present(struct renderer_system_state* state, khandle texture_handle);

/**
 * @brief Performs operations required on the supplied texture before being used for sampling.
 *
 * @param state A pointer to the renderer system state.
 * @param texture_handle A handle to the texture to prepare for sampling.
 * @param flags Texture flags from the texture itself, used to determine format/layout, etc.
 */
KAPI void renderer_texture_prepare_for_sampling(struct renderer_system_state* state, khandle texture_handle, texture_flag_bits flags);

/**
 * @brief Creates internal shader resources using the provided parameters.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader.
 * @param shader_resource A constant pointer to the shader shader_resource.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 renderer_shader_create(struct renderer_system_state* state, khandle shader, const kresource_shader* shader_resource);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be destroyed.
 */
KAPI void renderer_shader_destroy(struct renderer_system_state* state, khandle shader);

/**
 * @brief Reloads the internals of the given shader.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be reloaded.
 * @param shader_stage_count The number of shader stages.
 * @param shader_stages An array of shader stages configs.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_reload(struct renderer_system_state* state, khandle shader, u32 shader_stage_count, shader_stage_config* shader_stages);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_use(struct renderer_system_state* state, khandle shader);

/**
 * @brief Determines if the given shader supports wireframe mode.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_supports_wireframe(struct renderer_system_state* state, khandle shader);

/**
 * @brief Indicates if the given shader flag is set.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param flag The flag to check.
 * @return True if set; otherwise false.
 */
KAPI b8 renderer_shader_flag_get(struct renderer_system_state* state, khandle shader, shader_flags flag);

/**
 * @brief Sets the given shader flag.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param flag The flag to set.
 * @param enabled Indicates whether the flag should be set or unset.
 */
KAPI void renderer_shader_flag_set(struct renderer_system_state* state, khandle shader, shader_flags flag, b8 enabled);

/**
 * @brief Binds the per-frame frequency.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_shader_bind_per_frame(struct renderer_system_state* state, khandle shader);

/**
 * @brief Binds the given per-group frequency id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param group_id The per-group frequency id.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_shader_bind_per_group(struct renderer_system_state* state, khandle shader, u32 group_id);

/**
 * @brief Binds the given per-draw frequency id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to be used.
 * @param draw_id The per-draw frequency id.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_shader_bind_per_draw(struct renderer_system_state* state, khandle shader, u32 draw_id);

/**
 * @brief Applies per-frame data to the uniform buffer.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_apply_per_frame(struct renderer_system_state* state, khandle shader);

/**
 * @brief Applies data for the currently bound group.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to apply the instance data for.
 * @param generation The data generation for this frequency.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_apply_per_group(struct renderer_system_state* state, khandle shader, u16 generation);

/**
 * @brief Triggers the upload of per-draw uniform data to the GPU.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader.
 * @param generation The data generation for this frequency.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_apply_per_draw(struct renderer_system_state* state, khandle shader, u16 generation);

/**
 * @brief Acquires internal per-group resources and provides a group id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to acquire resources from.
 * @param config A constant pointer to the configuration of the group to be used while acquiring resources.
 * @param out_group_id A pointer to hold the new per-group identifier.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_per_group_resources_acquire(struct renderer_system_state* state, khandle shader, u32* out_group_id);

/**
 * @brief Releases internal per-group resources for the given group id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to release resources from.
 * @param group_id The per-group identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_per_group_resources_release(struct renderer_system_state* state, khandle shader, u32 group_id);

/**
 * @brief Acquires internal per-draw resources and provides a per-draw id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to acquire resources from.
 * @param texture_map_count The number of texture maps used.
 * @param maps An array of pointers to texture maps. Must be one map for each per-group texture.
 * @param out_draw_id A pointer to hold the new per-draw identifier.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_per_draw_resources_acquire(struct renderer_system_state* state, khandle shader, u32* out_draw_id);

/**
 * @brief Releases internal per-draw resources for the given per-draw id.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader to release resources from.
 * @param draw_id The per-draw identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_per_draw_resources_release(struct renderer_system_state* state, khandle shader, u32 draw_id);

/**
 * @brief Sets the uniform of the given shader to the provided value.
 *
 * @param state A pointer to the renderer state.
 * @param shader A handle to the shader.
 * @param uniform A constant pointer to the uniform.
 * @param array_index The index of the uniform array to be set, if it is an array. For non-array types, this value is ignored.
 * @param value A pointer to the value to be set.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_uniform_set(struct renderer_system_state* state, khandle shader, struct shader_uniform* uniform, u32 array_index, const void* value);

/**
 * @brief Gets a handle to a generic sampler of the given type.
 *
 * @param state A pointer to the renderer state.
 * @param sampler The shader sampler to get a handle to.
 * @returns A handle to a generic sampler of the given type.
 */
KAPI khandle renderer_generic_sampler_get(struct renderer_system_state* state, shader_generic_sampler sampler);

/**
 * @brief Acquires a internal sampler and returns a handle to it.
 *
 * @param state A pointer to the renderer state.
 * @param filter The min/mag filter.
 * @param repeat The repeat mode.
 * @param anisotropy The anisotropy level, if needed; otherwise 0.
 * @return A handle to the sampler on success; otherwise an invalid handle.
 */
KAPI khandle renderer_sampler_acquire(struct renderer_system_state* state, texture_filter filter, texture_repeat repeat, f32 anisotropy);

/**
 * @brief Releases the internal sampler for the given handle.
 *
 * @param state A pointer to the renderer state.
 * @param map A pointer to the handle whose sampler is to be released. Handle is invalidated upon release.
 */
KAPI void renderer_sampler_release(struct renderer_system_state* state, khandle* sampler);

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
KAPI b8 renderer_sampler_refresh(struct renderer_system_state* state, khandle* sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels);

/**
 * @brief Attempts to obtain the name of a sampler with the given handle. Returns INVALID_KNAME if not found.
 *
 * @param state A pointer to the renderer state.
 * @param sampler A handle to the sampler whose name to get.
 * @return The name of the sampler on success; otherwise INVALID_KNAME.
 */
KAPI kname renderer_sampler_name_get(struct renderer_system_state* state, khandle sampler);

/**
 * @brief Indicates if the renderer is capable of multi-threading.
 */
KAPI b8 renderer_is_multithreaded(void);

/**
 * @brief Indicates if the provided renderer flag is enabled. If multiple
 * flags are passed, all must be set for this to return true.
 *
 * @param flag The flag to be checked.
 * @return True if the flag(s) set; otherwise false.
 */
KAPI b8 renderer_flag_enabled_get(renderer_config_flags flag);
/**
 * @brief Sets whether the included flag(s) are enabled or not. If multiple flags
 * are passed, multiple are set at once.
 *
 * @param flag The flag to be checked.
 * @param enabled Indicates whether or not to enable the flag(s).
 */
KAPI void renderer_flag_enabled_set(renderer_config_flags flag, b8 enabled);

/**
 * @brief Obtains the max anisotropy level available from the renderer. 0 means not available.
 */
KAPI f32 renderer_max_anisotropy_get(void);

/**
 * @brief Creates a new renderbuffer to hold data for a given purpose/use. Backed by a
 * renderer-backend-specific buffer resource.
 *
 * @param name The name of the renderbuffer, used for debugging purposes.
 * @param type The type of buffer, indicating it's use (i.e. vertex/index data, uniforms, etc.)
 * @param total_size The total size in bytes of the buffer.
 * @param track_type Indicates what type of allocation tracking should be used.
 * @param out_buffer A pointer to hold the newly created buffer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_create(const char* name, renderbuffer_type type, u64 total_size, renderbuffer_track_type track_type, renderbuffer* out_buffer);

/**
 * @brief Destroys the given renderbuffer.
 *
 * @param buffer A pointer to the buffer to be destroyed.
 */
KAPI void renderer_renderbuffer_destroy(renderbuffer* buffer);

/**
 * @brief Binds the given buffer at the provided offset.
 *
 * @param buffer A pointer to the buffer to bind.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_bind(renderbuffer* buffer, u64 offset);

/**
 * @brief Unbinds the given buffer.
 *
 * @param buffer A pointer to the buffer to be unbound.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_unbind(renderbuffer* buffer);

/**
 * @brief Maps memory from the given buffer in the provided range to a block of memory and returns it.
 * This memory should be considered invalid once unmapped.
 * @param buffer A pointer to the buffer to map.
 * @param offset The number of bytes from the beginning of the buffer to map.
 * @param size The amount of memory in the buffer to map.
 * @returns A mapped block of memory. Freed and invalid once unmapped.
 */
KAPI void* renderer_renderbuffer_map_memory(renderbuffer* buffer, u64 offset, u64 size);

/**
 * @brief Unmaps memory from the given buffer in the provided range to a block of memory.
 * This memory should be considered invalid once unmapped.
 * @param buffer A pointer to the buffer to unmap.
 * @param offset The number of bytes from the beginning of the buffer to unmap.
 * @param size The amount of memory in the buffer to unmap.
 */
KAPI void renderer_renderbuffer_unmap_memory(renderbuffer* buffer, u64 offset, u64 size);

/**
 * @brief Flushes buffer memory at the given range. Should be done after a write.
 * @param buffer A pointer to the buffer to unmap.
 * @param offset The number of bytes from the beginning of the buffer to flush.
 * @param size The amount of memory in the buffer to flush.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_flush(renderbuffer* buffer, u64 offset, u64 size);

/**
 * @brief Reads memory from the provided buffer at the given range to the output variable.
 * @param buffer A pointer to the buffer to read from.
 * @param offset The number of bytes from the beginning of the buffer to read.
 * @param size The amount of memory in the buffer to read.
 * @param out_memory A pointer to a block of memory to read to. Must be of appropriate size.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_read(renderbuffer* buffer, u64 offset, u64 size, void** out_memory);

/**
 * @brief Resizes the given buffer to new_total_size. new_total_size must be
 * greater than the current buffer size. Data from the old internal buffer is copied
 * over.
 *
 * @param buffer A pointer to the buffer to be resized.
 * @param new_total_size The new size in bytes. Must be larger than the current size.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_resize(renderbuffer* buffer, u64 new_total_size);

/**
 * @brief Attempts to allocate memory from the given buffer. Should only be used on
 * buffers that were created with use_freelist = true.
 *
 * @param buffer A pointer to the buffer to be allocated from.
 * @param size The size in bytes to allocate.
 * @param out_offset A pointer to hold the offset in bytes of the allocation from the beginning of the buffer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_allocate(renderbuffer* buffer, u64 size, u64* out_offset);

/**
 * @brief Frees memory from the given buffer.
 *
 * @param buffer A pointer to the buffer to be freed from.
 * @param size The size in bytes to free.
 * @param offset The offset in bytes from the beginning of the buffer to free.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_free(renderbuffer* buffer, u64 size, u64 offset);

/**
 * @brief Clears the given buffer. Internally, resets the free list if one is used.
 *
 * @param buffer A pointer to the buffer to be freed from.
 * @param zero_memory True if memory should be zeroed; otherwise false. NOTE: this can be an expensive operation on large sums of memory.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_clear(renderbuffer* buffer, b8 zero_memory);

/**
 * @brief Loads provided data into the specified rage of the given buffer.
 *
 * @param buffer A pointer to the buffer to load data into.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param size The size of the data in bytes to be loaded.
 * @param data The data to be loaded.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_load_range(renderbuffer* buffer, u64 offset, u64 size, const void* data, b8 include_in_frame_workload);

/**
 * @brief Copies data in the specified rage fron the source to the destination buffer.
 *
 * @param source A pointer to the source buffer to copy data from.
 * @param source_offset The offset in bytes from the beginning of the source buffer.
 * @param dest A pointer to the destination buffer to copy data to.
 * @param dest_offset The offset in bytes from the beginning of the destination buffer.
 * @param size The size of the data in bytes to be copied.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_copy_range(renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size, b8 include_in_frame_workload);

/**
 * @brief Attempts to draw the contents of the provided buffer at the given offset
 * and element count. Only meant to be used with vertex and index buffers.
 *
 * @param buffer A pointer to the buffer to be drawn.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param element_count The number of elements to be drawn.
 * @param bind_only Only bind the buffer, but don't draw.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_draw(renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only);

/**
 * @brief Returns a pointer to the currently active viewport.
 */
KAPI struct viewport* renderer_active_viewport_get(void);

/**
 * @brief Sets the currently active viewport.
 *
 * @param viewport A pointer to the viewport to be set.
 */
KAPI void renderer_active_viewport_set(struct viewport* v);

/**
 * Waits for the renderer backend to be completely idle of work before returning.
 * NOTE: This incurs a lot of overhead/waits, and should be used sparingly.
 */
KAPI void renderer_wait_for_idle(void);

/**
 * Indicates if PCF filtering is enabled for shadow maps.
 */
KAPI b8 renderer_pcf_enabled(struct renderer_system_state* state);

/**
 * @brief Returns the max number of textures that can be bound at once for a single draw call.
 */
KAPI u16 renderer_max_bound_texture_count_get(struct renderer_system_state* state);

/**
 * @brief Returns the max number of samplers that can be bound at once for a single draw call.
 */
KAPI u16 renderer_max_bound_sampler_count_get(struct renderer_system_state* state);
