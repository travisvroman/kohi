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

/**
 * @brief Initializes the renderer frontend/system. Should be called twice - once
 * to obtain the memory requirement (passing state=0), and a second time passing
 * allocated memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A block of memory to hold state data, or 0 if obtaining memory requirement.
 * @param application_name The name of the application.
 * @return True on success; otherwise false.
 */
b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name);

/**
 * @brief Shuts the renderer system/frontend down.
 *
 * @param state A pointer to the state block of memory.
 */
void renderer_system_shutdown(void* state);

/**
 * @brief Handles resize events.
 *
 * @param width The new window width.
 * @param height The new window height.
 */
void renderer_on_resized(u16 width, u16 height);

/**
 * @brief Draws the next frame using the data provided in the render packet.
 *
 * @param packet A pointer to the render packet, which contains data on what should be rendered.
 * @return True on success; otherwise false.
 */
b8 renderer_draw_frame(render_packet* packet);

/**
 * @brief Creates a new texture.
 *
 * @param pixels The raw image data to be uploaded to the GPU.
 * @param texture A pointer to the texture to be loaded.
 */
void renderer_texture_create(const u8* pixels, struct texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources from the GPU.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
void renderer_texture_destroy(struct texture* texture);

/**
 * @brief Creates a new writeable texture with no data written to it.
 *
 * @param t A pointer to the texture to hold the resources.
 */
void renderer_texture_create_writeable(texture* t);

/**
 * @brief Resizes a texture. There is no check at this level to see if the
 * texture is writeable. Internal resources are destroyed and re-created at
 * the new resolution. Data is lost and would need to be reloaded.
 *
 * @param t A pointer to the texture to be resized.
 * @param new_width The new width in pixels.
 * @param new_height The new height in pixels.
 */
void renderer_texture_resize(texture* t, u32 new_width, u32 new_height);

/**
 * @brief Writes the given data to the provided texture.
 *
 * @param t A pointer to the texture to be written to. NOTE: Must be a writeable texture.
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param pixels The raw image data to be written.
 */
void renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels);

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
b8 renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
void renderer_destroy_geometry(geometry* geometry);

/**
 * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
 *
 * @param data The render data of the geometry to be drawn.
 */
void renderer_draw_geometry(geometry_render_data* data);

/**
 * @brief Begins the given renderpass.
 *
 * @param pass A pointer to the renderpass to begin.
 * @param target A pointer to the render target to be used.
 * @return True on success; otherwise false.
 */
b8 renderer_renderpass_begin(renderpass* pass, render_target* target);

/**
 * @brief Ends the given renderpass.
 *
 * @param pass A pointer to the renderpass to end.
 * @return True on success; otherwise false.
 */
b8 renderer_renderpass_end(renderpass* pass);

/**
 * @brief Obtains a pointer to the renderpass with the given name.
 *
 * @param name The name of the renderpass whose identifier to obtain.
 * @return A pointer to a renderpass if found; otherwise 0.
 */
renderpass* renderer_renderpass_get(const char* name);

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
b8 renderer_shader_create(struct shader* s, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 * @param s A pointer to the shader to be destroyed.
 */
void renderer_shader_destroy(struct shader* s);

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * Must be done after vulkan_shader_create().
 *
 * @param s A pointer to the shader to be initialized.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_initialize(struct shader* s);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 *
 * @param s A pointer to the shader to be used.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_use(struct shader* s);

/**
 * @brief Binds global resources for use and updating.
 *
 * @param s A pointer to the shader whose globals are to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_globals(struct shader* s);

/**
 * @brief Binds instance resources for use and updating.
 *
 * @param s A pointer to the shader whose instance resources are to be bound.
 * @param instance_id The identifier of the instance to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_instance(struct shader* s, u32 instance_id);

/**
 * @brief Applies global data to the uniform buffer.
 *
 * @param s A pointer to the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_globals(struct shader* s);

/**
 * @brief Applies data for the currently bound instance.
 *
 * @param s A pointer to the shader to apply the instance data for.
 * @param needs_update Indicates if the shader uniforms need to be updated or just bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_instance(struct shader* s, b8 needs_update);

/**
 * @brief Acquires internal instance-level resources and provides an instance id.
 *
 * @param s A pointer to the shader to acquire resources from.
 * @param maps An array of texture map pointers. Must be one per texture in the instance.
 * @param out_instance_id A pointer to hold the new instance identifier.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_acquire_instance_resources(struct shader* s, texture_map** maps, u32* out_instance_id);

/**
 * @brief Releases internal instance-level resources for the given instance id.
 *
 * @param s A pointer to the shader to release resources from.
 * @param instance_id The instance identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_release_instance_resources(struct shader* s, u32 instance_id);

/**
 * @brief Sets the uniform of the given shader to the provided value.
 *
 * @param s A ponter to the shader.
 * @param uniform A constant pointer to the uniform.
 * @param value A pointer to the value to be set.
 * @return b8 True on success; otherwise false.
 */
b8 renderer_set_uniform(struct shader* s, struct shader_uniform* uniform, const void* value);

/**
 * @brief Acquires internal resources for the given texture map.
 *
 * @param map A pointer to the texture map to obtain resources for.
 * @return True on success; otherwise false.
 */
b8 renderer_texture_map_acquire_resources(struct texture_map* map);

/**
 * @brief Releases internal resources for the given texture map.
 *
 * @param map A pointer to the texture map to release resources from.
 */
void renderer_texture_map_release_resources(struct texture_map* map);

/**
 * @brief Creates a new render target using the provided data.
 *
 * @param attachment_count The number of attachments (texture pointers).
 * @param attachments An array of attachments (texture pointers).
 * @param renderpass A pointer to the renderpass the render target is associated with.
 * @param width The width of the render target in pixels.
 * @param height The height of the render target in pixels.
 * @param out_target A pointer to hold the newly created render target.
 */
void renderer_render_target_create(u8 attachment_count, texture** attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);

/**
 * @brief Destroys the provided render target.
 *
 * @param target A pointer to the render target to be destroyed.
 * @param free_internal_memory Indicates if internal memory should be freed.
 */
void renderer_render_target_destroy(render_target* target, b8 free_internal_memory);

/**
 * @brief Creates a new renderpass.
 *
 * @param out_renderpass A pointer to the generic renderpass.
 * @param depth The depth clear amount.
 * @param stencil The stencil clear value.
 * @param clear_flags The combined clear flags indicating what kind of clear should take place.
 * @param has_prev_pass Indicates if there is a previous renderpass.
 * @param has_next_pass Indicates if there is a next renderpass.
 */
void renderer_renderpass_create(renderpass* out_renderpass, f32 depth, u32 stencil, b8 has_prev_pass, b8 has_next_pass);

/**
 * @brief Destroys the given renderpass.
 *
 * @param pass A pointer to the renderpass to be destroyed.
 */
void renderer_renderpass_destroy(renderpass* pass);

/**
 * @brief Indicates if the renderer is capable of multi-threading.
 */
b8 renderer_is_multithreaded();
