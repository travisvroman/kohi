/**
 * @file vulkan_backend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the Vulkan implementation of the renderer backend.
 * All Vulkan calls are made behind this facade to keep the rest of the engine
 * unaware about the inner workings of Vulkan.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "renderer/renderer_backend.h"
#include "resources/resource_types.h"

struct shader;
struct shader_uniform;

/**
 * @brief Initializes the Vulkan backend.
 *
 * @param backend A pointer to the generic backend interface.
 * @param application_name The name of the application.
 * @return True if initialized successfully; otherwise false.
 */
b8 vulkan_renderer_backend_initialize(renderer_backend* backend, const char* application_name);

/**
 * @brief Shuts the Vulkan renderer backend down.
 *
 * @param backend A pointer to the generic backend interface.
 */
void vulkan_renderer_backend_shutdown(renderer_backend* backend);

/**
 * @brief Handles window resizes.
 *
 * @param backend A pointer to the generic backend interface.
 * @param width The new window width.
 * @param height The new window height.
 */
void vulkan_renderer_backend_on_resized(renderer_backend* backend, u16 width, u16 height);

/**
 * @brief Performs setup routines required at the start of a frame.
 * @note A false result does not necessarily indicate failure. It can also specify that
 * the backend is simply not in a state capable of drawing a frame at the moment, and
 * that it should be attempted again on the next loop. End frame does not need to (and
 * should not) be called if this is the case.
 * @param backend A pointer to the generic backend interface.
 * @param delta_time The time in seconds since the last frame.
 * @return True if successful; otherwise false.
 */
b8 vulkan_renderer_backend_begin_frame(renderer_backend* backend, f32 delta_time);

/**
 * @brief Performs routines required to draw a frame, such as presentation. Should only be called
 * after a successful return of begin_frame.
 *
 * @param backend A pointer to the generic backend interface.
 * @param delta_time The time in seconds since the last frame.
 * @return True on success; otherwise false.
 */
b8 vulkan_renderer_backend_end_frame(renderer_backend* backend, f32 delta_time);

/**
 * @brief Begins a renderpass with the given id.
 *
 * @param backend A pointer to the generic backend interface.
 * @param renderpass_id The identifier of the renderpass to begin.
 * @return True on success; otherwise false.
 */
b8 vulkan_renderer_begin_renderpass(struct renderer_backend* backend, u8 renderpass_id);

/**
 * @brief Ends a renderpass with the given id.
 *
 * @param backend A pointer to the generic backend interface.
 * @param renderpass_id The identifier of the renderpass to end.
 * @return True on success; otherwise false.
 */
b8 vulkan_renderer_end_renderpass(struct renderer_backend* backend, u8 renderpass_id);

/**
 * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
 *
 * @param data The render data of the geometry to be drawn.
 */
void vulkan_renderer_draw_geometry(geometry_render_data data);

/**
 * @brief Creates a Vulkan-specific texture, acquiring internal resources as needed.
 *
 * @param pixels The raw image data used for the texture.
 * @param texture A pointer to the texture to hold the resources.
 */
void vulkan_renderer_texture_create(const u8* pixels, texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
void vulkan_renderer_texture_destroy(texture* texture);

/**
 * @brief Creates a new writeable texture with no data written to it.
 * 
 * @param t A pointer to the texture to hold the resources.
 */
void vulkan_renderer_texture_create_writeable(texture* t);

/**
 * @brief Resizes a texture. There is no check at this level to see if the
 * texture is writeable. Internal resources are destroyed and re-created at
 * the new resolution. Data is lost and would need to be reloaded.
 * 
 * @param t A pointer to the texture to be resized.
 * @param new_width The new width in pixels.
 * @param new_height The new height in pixels.
 */
void vulkan_renderer_texture_resize(texture* t, u32 new_width, u32 new_height);

/**
 * @brief Writes the given data to the provided texture.
 * NOTE: At this level, this can either be a writeable or non-writeable texture because
 * this also handles the initial texture load. The texture system itself should be
 * responsible for blocking write requests to non-writeable textures.
 * 
 * @param t A pointer to the texture to be written to. 
 * @param offset The offset in bytes from the beginning of the data to be written.
 * @param size The number of bytes to be written.
 * @param pixels The raw image data to be written.
 */
void vulkan_renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels);

/**
 * @brief Creates Vulkan-specific internal resources for the given geometry using
 * the data provided.
 *
 * @param geometry A pointer to the geometry to be created.
 * @param vertex_size The size of a single vertex.
 * @param vertex_count The total number of vertices.
 * @param vertices An array of vertices.
 * @param index_size The size of an individual index.
 * @param index_count The total number of indices.
 * @param indices An array of indices.
 * @return True on success; otherwise false.
 */
b8 vulkan_renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

/**
 * @brief Destroys the given geometry, releasing internal resources.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
void vulkan_renderer_destroy_geometry(geometry* geometry);

b8 vulkan_renderer_shader_create(struct shader* shader, u8 renderpass_id, u8 stage_count, const char** stage_filenames, shader_stage* stages);
void vulkan_renderer_shader_destroy(struct shader* shader);

b8 vulkan_renderer_shader_initialize(struct shader* shader);
b8 vulkan_renderer_shader_use(struct shader* shader);
b8 vulkan_renderer_shader_bind_globals(struct shader* s);
b8 vulkan_renderer_shader_bind_instance(struct shader* s, u32 instance_id);
b8 vulkan_renderer_shader_apply_globals(struct shader* s);
b8 vulkan_renderer_shader_apply_instance(struct shader* s, b8 needs_update);
b8 vulkan_renderer_shader_acquire_instance_resources(struct shader* s, texture_map** maps, u32* out_instance_id);
b8 vulkan_renderer_shader_release_instance_resources(struct shader* s, u32 instance_id);
b8 vulkan_renderer_set_uniform(struct shader* frontend_shader, struct shader_uniform* uniform, const void* value);

b8 vulkan_renderer_texture_map_acquire_resources(texture_map* map);
void vulkan_renderer_texture_map_release_resources(texture_map* map);
