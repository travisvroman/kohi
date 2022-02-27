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
void vulkan_renderer_create_texture(const u8* pixels, texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
void vulkan_renderer_destroy_texture(texture* texture);

/**
 * @brief Creates a material, acquiring required internal resources.
 *
 * @param material A pointer to the material to hold the resources.
 * @return True on success; otherwise false.
 */
b8 vulkan_renderer_create_material(struct material* material);

/**
 * @brief Destroys a texture, releasing required internal resouces.
 *
 * @param material A pointer to the material whose resources should be released.
 */
void vulkan_renderer_destroy_material(struct material* material);

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

b8 vulkan_renderer_shader_create(const char* name, u8 renderpass_id, u32 stages, b8 use_instances, b8 use_local, u32* out_shader_id);
void vulkan_renderer_shader_destroy(u32 shader_id);
b8 vulkan_renderer_shader_add_attribute(u32 shader_id, const char* name, shader_attribute_type type);
b8 vulkan_renderer_shader_add_sampler(u32 shader_id, const char* sampler_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_i8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_i16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_i32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_u8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_u16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_u32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_f32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_vec2(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_vec3(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_vec4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_mat4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_add_uniform_custom(u32 shader_id, const char* uniform_name, u32 size, shader_scope scope, u32* out_location);
b8 vulkan_renderer_shader_initialize(u32 shader_id);
b8 vulkan_renderer_shader_use(u32 shader_id);
b8 vulkan_renderer_shader_bind_globals(u32 shader_id);
b8 vulkan_renderer_shader_bind_instance(u32 shader_id, u32 instance_id);
b8 vulkan_renderer_shader_apply_globals(u32 shader_id);
b8 vulkan_renderer_shader_apply_instance(u32 shader_id);
b8 vulkan_renderer_shader_acquire_instance_resources(u32 shader_id, u32* out_instance_id);
b8 vulkan_renderer_shader_release_instance_resources(u32 shader_id, u32 instance_id);
u32 vulkan_renderer_shader_uniform_location(u32 shader_id, const char* uniform_name);
b8 vulkan_renderer_shader_set_sampler(u32 shader_id, u32 location, texture* t);
b8 vulkan_renderer_shader_set_uniform_i8(u32 shader_id, u32 location, i8 value);
b8 vulkan_renderer_shader_set_uniform_i16(u32 shader_id, u32 location, i16 value);
b8 vulkan_renderer_shader_set_uniform_i32(u32 shader_id, u32 location, i32 value);
b8 vulkan_renderer_shader_set_uniform_u8(u32 shader_id, u32 location, u8 value);
b8 vulkan_renderer_shader_set_uniform_u16(u32 shader_id, u32 location, u16 value);
b8 vulkan_renderer_shader_set_uniform_u32(u32 shader_id, u32 location, u32 value);
b8 vulkan_renderer_shader_set_uniform_f32(u32 shader_id, u32 location, f32 value);
b8 vulkan_renderer_shader_set_uniform_vec2(u32 shader_id, u32 location, vec2 value);
b8 vulkan_renderer_shader_set_uniform_vec2f(u32 shader_id, u32 location, f32 value_0, f32 value_1);
b8 vulkan_renderer_shader_set_uniform_vec3(u32 shader_id, u32 location, vec3 value);
b8 vulkan_renderer_shader_set_uniform_vec3f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2);
b8 vulkan_renderer_shader_set_uniform_vec4(u32 shader_id, u32 location, vec4 value);
b8 vulkan_renderer_shader_set_uniform_vec4f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3);
b8 vulkan_renderer_shader_set_uniform_mat4(u32 shader_id, u32 location, mat4 value);
b8 vulkan_renderer_shader_set_uniform_custom(u32 shader_id, u32 location, void* value);
