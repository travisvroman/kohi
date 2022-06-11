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

b8 vulkan_renderer_backend_initialize(renderer_backend* backend, const renderer_backend_config* config, u8* out_window_render_target_count);
void vulkan_renderer_backend_shutdown(renderer_backend* backend);
void vulkan_renderer_backend_on_resized(renderer_backend* backend, u16 width, u16 height);
b8 vulkan_renderer_backend_begin_frame(renderer_backend* backend, f32 delta_time);
b8 vulkan_renderer_backend_end_frame(renderer_backend* backend, f32 delta_time);
b8 vulkan_renderer_renderpass_begin(renderpass* pass, render_target* target);
b8 vulkan_renderer_renderpass_end(renderpass* pass);
renderpass* vulkan_renderer_renderpass_get(const char* name);

void vulkan_renderer_draw_geometry(geometry_render_data* data);
void vulkan_renderer_texture_create(const u8* pixels, texture* texture);
void vulkan_renderer_texture_destroy(texture* texture);
void vulkan_renderer_texture_create_writeable(texture* t);
void vulkan_renderer_texture_resize(texture* t, u32 new_width, u32 new_height);
void vulkan_renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels);
b8 vulkan_renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);
void vulkan_renderer_destroy_geometry(geometry* geometry);

b8 vulkan_renderer_shader_create(struct shader* shader, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages);
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

void vulkan_renderpass_create(renderpass* out_renderpass, f32 depth, u32 stencil, b8 has_prev_pass, b8 has_next_pass);
void vulkan_renderpass_destroy(renderpass* pass);

void vulkan_renderer_render_target_create(u8 attachment_count, texture** attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);
void vulkan_renderer_render_target_destroy(render_target* target, b8 free_internal_memory);

texture* vulkan_renderer_window_attachment_get(u8 index);
texture* vulkan_renderer_depth_attachment_get();
u8 vulkan_renderer_window_attachment_index_get();

b8 vulkan_renderer_is_multithreaded();
