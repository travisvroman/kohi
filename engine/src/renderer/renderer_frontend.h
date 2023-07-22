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
 */

#pragma once

#include "renderer_types.inl"

struct shader;
struct shader_uniform;
struct frame_data;

typedef struct renderer_system_config {
    char* application_name;
    renderer_plugin plugin;
} renderer_system_config;

/**
 * @brief Initializes the renderer frontend/system. Should be called twice - once
 * to obtain the memory requirement (passing state=0), and a second time passing
 * allocated memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A block of memory to hold state data, or 0 if obtaining memory requirement.
 * @param config The configuration (renderer_system_config) for the renderer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts the renderer system/frontend down.
 *
 * @param state A pointer to the state block of memory.
 */
KAPI void renderer_system_shutdown(void* state);

/**
 * @brief Handles resize events.
 *
 * @param width The new window width.
 * @param height The new window height.
 */
KAPI void renderer_on_resized(u16 width, u16 height);

/**
 * @brief Draws the next frame using the data provided in the render packet.
 *
 * @param packet A pointer to the render packet, which contains data on what should be rendered.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_draw_frame(render_packet* packet, const struct frame_data* p_frame_data);

/**
 * @brief Sets the renderer viewport to the given rectangle. Must be done within a renderpass.
 *
 * @param rect The viewport rectangle to be set.
 */
KAPI void renderer_viewport_set(vec4 rect);

/**
 * @brief Resets the viewport to the default, which matches the application window.
 * Must be done within a renderpass.
 */
KAPI void renderer_viewport_reset(void);

/**
 * @brief Sets the renderer scissor to the given rectangle. Must be done within a renderpass.
 *
 * @param rect The scissor rectangle to be set.
 */
KAPI void renderer_scissor_set(vec4 rect);

/**
 * @brief Resets the scissor to the default, which matches the application window.
 * Must be done within a renderpass.
 */
KAPI void renderer_scissor_reset(void);

/**
 * @brief Creates a new texture.
 *
 * @param pixels The raw image data to be uploaded to the GPU.
 * @param texture A pointer to the texture to be loaded.
 */
KAPI void renderer_texture_create(const u8* pixels, struct texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources from the GPU.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
KAPI void renderer_texture_destroy(struct texture* texture);

/**
 * @brief Creates a new writeable texture with no data written to it.
 *
 * @param t A pointer to the texture to hold the resources.
 */
KAPI void renderer_texture_create_writeable(texture* t);

/**
 * @brief Resizes a texture. There is no check at this level to see if the
 * texture is writeable. Internal resources are destroyed and re-created at
 * the new resolution. Data is lost and would need to be reloaded.
 *
 * @param t A pointer to the texture to be resized.
 * @param new_width The new width in pixels.
 * @param new_height The new height in pixels.
 */
KAPI void renderer_texture_resize(texture* t, u32 new_width, u32 new_height);

/**
 * @brief Writes the given data to the provided texture.
 *
 * @param t A pointer to the texture to be written to. NOTE: Must be a writeable texture.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param pixels The raw image data to be written.
 */
KAPI void renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels);

/**
 * @brief Reads the given data from the provided texture.
 *
 * @param t A pointer to the texture to be read from.
 * @param offset The offset in bytes from the beginning of the data to be read.
 * @param size The number of bytes to be read.
 * @param out_memory A pointer to a block of memory to write the read data to.
 */
KAPI void renderer_texture_read_data(texture* t, u32 offset, u32 size, void** out_memory);

/**
 * @brief Reads a pixel from the provided texture at the given x/y coordinate.
 *
 * @param t A pointer to the texture to be read from.
 * @param x The pixel x-coordinate.
 * @param y The pixel y-coordinate.
 * @param out_rgba A pointer to an array of u8s to hold the pixel data (should be sizeof(u8) * 4)
 */
KAPI void renderer_texture_read_pixel(texture* t, u32 x, u32 y, u8** out_rgba);

/**
 * @brief Acquiores GPU resources and uploads geometry data.
 *
 * @param geometry A pointer to the geometry to acquire resources for.
 * @param vertex_size The size of each vertex.
 * @param vertex_count The number of vertices.
 * @param vertices The vertex array.
 * @param index_size The size of each index.
 * @param index_count The number of indices.
 * @param indices The index array.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_geometry_create(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

/**
 * @brief Updates vertex data in the given geometry with the provided data in the given range.
 *
 * @param g A pointer to the geometry to be created.
 * @param offset The offset in bytes to update. 0 if updating from the beginning.
 * @param vertex_count The number of vertices which will be updated.
 * @param vertices The vertex data.
 */
KAPI void renderer_geometry_vertex_update(geometry* g, u32 offset, u32 vertex_count, void* vertices);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
KAPI void renderer_geometry_destroy(geometry* geometry);

/**
 * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
 *
 * @param data The render data of the geometry to be drawn.
 */
KAPI void renderer_geometry_draw(geometry_render_data* data);

/**
 * @brief Begins the given renderpass.
 *
 * @param pass A pointer to the renderpass to begin.
 * @param target A pointer to the render target to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderpass_begin(renderpass* pass, render_target* target);

/**
 * @brief Ends the given renderpass.
 *
 * @param pass A pointer to the renderpass to end.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderpass_end(renderpass* pass);

/**
 * @brief Creates internal shader resources using the provided parameters.
 *
 * @param s A pointer to the shader.
 * @param config A constant pointer to the shader config.
 * @param pass A pointer to the renderpass to be associated with the shader.
 * @param stage_count The total number of stages.
 * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
 * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 renderer_shader_create(struct shader* s, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 * @param s A pointer to the shader to be destroyed.
 */
KAPI void renderer_shader_destroy(struct shader* s);

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * Must be done after vulkan_shader_create().
 *
 * @param s A pointer to the shader to be initialized.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_initialize(struct shader* s);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 *
 * @param s A pointer to the shader to be used.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_use(struct shader* s);

/**
 * @brief Binds global resources for use and updating.
 *
 * @param s A pointer to the shader whose globals are to be bound.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_bind_globals(struct shader* s);

/**
 * @brief Binds instance resources for use and updating.
 *
 * @param s A pointer to the shader whose instance resources are to be bound.
 * @param instance_id The identifier of the instance to be bound.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_bind_instance(struct shader* s, u32 instance_id);

/**
 * @brief Applies global data to the uniform buffer.
 *
 * @param s A pointer to the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_apply_globals(struct shader* s);

/**
 * @brief Applies data for the currently bound instance.
 *
 * @param s A pointer to the shader to apply the instance data for.
 * @param needs_update Indicates if the shader uniforms need to be updated or just bound.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_apply_instance(struct shader* s, b8 needs_update);

/**
 * @brief Acquires internal instance-level resources and provides an instance id.
 *
 * @param s A pointer to the shader to acquire resources from.
 * @param texture_map_count The number of texture maps used.
 * @param maps An array of texture map pointers. Must be one per texture in the instance.
 * @param out_instance_id A pointer to hold the new instance identifier.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_instance_resources_acquire(struct shader* s, u32 texture_map_count, texture_map** maps, u32* out_instance_id);

/**
 * @brief Releases internal instance-level resources for the given instance id.
 *
 * @param s A pointer to the shader to release resources from.
 * @param instance_id The instance identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_shader_instance_resources_release(struct shader* s, u32 instance_id);

/**
 * @brief Sets the uniform of the given shader to the provided value.
 *
 * @param s A ponter to the shader.
 * @param uniform A constant pointer to the uniform.
 * @param value A pointer to the value to be set.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 renderer_shader_uniform_set(struct shader* s, struct shader_uniform* uniform, const void* value);

/**
 * @brief Acquires internal resources for the given texture map.
 *
 * @param map A pointer to the texture map to obtain resources for.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_texture_map_resources_acquire(struct texture_map* map);

/**
 * @brief Releases internal resources for the given texture map.
 *
 * @param map A pointer to the texture map to release resources from.
 */
KAPI void renderer_texture_map_resources_release(struct texture_map* map);

/**
 * @brief Creates a new render target using the provided data.
 *
 * @param attachment_count The number of attachments.
 * @param attachments An array of attachments.
 * @param renderpass A pointer to the renderpass the render target is associated with.
 * @param width The width of the render target in pixels.
 * @param height The height of the render target in pixels.
 * @param out_target A pointer to hold the newly created render target.
 */
KAPI void renderer_render_target_create(u8 attachment_count, render_target_attachment* attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);

/**
 * @brief Destroys the provided render target.
 *
 * @param target A pointer to the render target to be destroyed.
 * @param free_internal_memory Indicates if internal memory should be freed.
 */
KAPI void renderer_render_target_destroy(render_target* target, b8 free_internal_memory);

/**
 * @brief Attempts to get the window render target at the given index.
 *
 * @param index The index of the attachment to get. Must be within the range of window render target count.
 * @return A pointer to a texture attachment if successful; otherwise 0.
 */
KAPI texture* renderer_window_attachment_get(u8 index);

/**
 * @brief Returns a pointer to the main depth texture target.
 *
 * @param index The index of the attachment to get. Must be within the range of window render target count.
 * @return A pointer to a texture attachment if successful; otherwise 0.
 */
KAPI texture* renderer_depth_attachment_get(u8 index);

/**
 * @brief Returns the current window attachment index.
 */
KAPI u8 renderer_window_attachment_index_get(void);

/**
 * @brief Returns the number of attachments required for window-based render targets.
 */
KAPI u8 renderer_window_attachment_count_get(void);

/**
 * @brief Creates a new renderpass.
 *
 * @param config A constant pointer to the configuration to be used when creating the renderpass.
 * @param out_renderpass A pointer to the generic renderpass.
 */
KAPI b8 renderer_renderpass_create(const renderpass_config* config, renderpass* out_renderpass);

/**
 * @brief Destroys the given renderpass.
 *
 * @param pass A pointer to the renderpass to be destroyed.
 */
KAPI void renderer_renderpass_destroy(renderpass* pass);

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
 * @brief Creates a new renderbuffer to hold data for a given purpose/use. Backed by a
 * renderer-backend-specific buffer resource.
 *
 * @param name The name of the renderbuffer, used for debugging purposes.
 * @param type The type of buffer, indicating it's use (i.e. vertex/index data, uniforms, etc.)
 * @param total_size The total size in bytes of the buffer.
 * @param use_freelist Indicates if the buffer should use a freelist to track allocations.
 * @param out_buffer A pointer to hold the newly created buffer.
 * @return True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_create(const char* name, renderbuffer_type type, u64 total_size, b8 use_freelist, renderbuffer* out_buffer);

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
 * @brief Loads provided data into the specified rage of the given buffer.
 *
 * @param buffer A pointer to the buffer to load data into.
 * @param offset The offset in bytes from the beginning of the buffer.
 * @param size The size of the data in bytes to be loaded.
 * @param data The data to be loaded.
 * @returns True on success; otherwise false.
 */
KAPI b8 renderer_renderbuffer_load_range(renderbuffer* buffer, u64 offset, u64 size, const void* data);

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
KAPI b8 renderer_renderbuffer_copy_range(renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size);

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
