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
 * @brief Sets the view matrix in the renderer. NOTE: exposed to public API.
 *
 * @deprecated HACK: this should not be exposed outside the engine.
 * @param view The view matrix to be set.
 */
KAPI void renderer_set_view(mat4 view);

/**
 * @brief Creates a new texture.
 *
 * @param pixels The raw image data to be uploaded to the GPU.
 * @param texture A pointer to the texture to be loaded.
 */
void renderer_create_texture(const u8* pixels, struct texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources from the GPU.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
void renderer_destroy_texture(struct texture* texture);

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
 * @brief Obtains the identifier of the renderpass with the given name.
 *
 * @param name The name of the renderpass whose identifier to obtain.
 * @param out_renderpass_id A pointer to hold the renderpass id.
 * @return True if found; otherwise false.
 */
b8 renderer_renderpass_id(const char* name, u8* out_renderpass_id);

/**
 * @brief Creates a new shader using the provided parameters.
 * @param name The name of the shader.
 * @param renderpass_id The identifier of the renderpass to be associated with the shader.
 * @param stage_count The total number of stages.
 * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
 * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
 * @param use_instances Indicates if instances will be used with the shader.
 * @param use_local Indicates if local uniforms will be used with the shader.
 * @param out_shader A pointer to hold the identifier of the newly-created shader.
 * @returns True on success; otherwise false.
 */
b8 renderer_shader_create(struct shader* shader, u8 renderpass_id, u8 stage_count, const char** stage_filenames, shader_stage* stages);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 * @param shader_id The identifier of the shader to be destroyed.
 */
void renderer_shader_destroy(struct shader* shader);

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * Must be done after vulkan_shader_create().
 *
 * @param shader_id The identifier of the shader to be initialized.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_initialize(struct shader* s);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 *
 * @param shader_id The identifier of the shader to be used.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_use(struct shader* s);

/**
 * @brief Binds global resources for use and updating.
 *
 * @param shader_id The identifier of the shader whose globals are to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_globals(struct shader* s);

/**
 * @brief Binds instance resources for use and updating.
 *
 * @param shader_id The identifier of the shader whose instance resources are to be bound.
 * @param instance_id The identifier of the instance to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_instance(struct shader* s, u32 instance_id);

/**
 * @brief Applies global data to the uniform buffer.
 *
 * @param shader_id The identifier of the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_globals(struct shader* s);

/**
 * @brief Applies data for the currently bound instance.
 *
 * @param shader_id The identifier of the shader to apply the instance data for.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_instance(struct shader* s);

/**
 * @brief Acquires internal instance-level resources and provides an instance id.
 *
 * @param shader_id The identifier of the shader to acquire resources from.
 * @param out_instance_id A pointer to hold the new instance identifier.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_acquire_instance_resources(struct shader* s, u32* out_instance_id);

/**
 * @brief Releases internal instance-level resources for the given instance id.
 *
 * @param shader_id The identifier of the shader to release resources from.
 * @param instance_id The instance identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_release_instance_resources(struct shader* s, u32 instance_id);

b8 renderer_set_uniform(struct shader* frontend_shader, struct shader_uniform* uniform, void* value);