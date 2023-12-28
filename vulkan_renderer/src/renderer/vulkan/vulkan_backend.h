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

#include "renderer/renderer_types.h"
#include "resources/resource_types.h"
#include "vulkan_renderer_plugin_main.h"

struct shader;
struct shader_uniform;
struct frame_data;

b8 vulkan_renderer_backend_initialize(renderer_plugin* backend, const renderer_backend_config* config, u8* out_window_render_target_count);
void vulkan_renderer_backend_shutdown(renderer_plugin* backend);
void vulkan_renderer_backend_on_resized(renderer_plugin* backend, u16 width, u16 height);
b8 vulkan_renderer_frame_prepare(renderer_plugin* backend, struct frame_data* p_frame_data);
b8 vulkan_renderer_begin(renderer_plugin* plugin, struct frame_data* p_frame_data);
b8 vulkan_renderer_end(renderer_plugin* plugin, struct frame_data* p_frame_data);
b8 vulkan_renderer_present(renderer_plugin* backend, struct frame_data* p_frame_data);
void vulkan_renderer_viewport_set(renderer_plugin* backend, vec4 rect);
void vulkan_renderer_viewport_reset(renderer_plugin* backend);
void vulkan_renderer_scissor_set(renderer_plugin* backend, vec4 rect);
void vulkan_renderer_scissor_reset(renderer_plugin* backend);

void vulkan_renderer_winding_set(struct renderer_plugin* plugin, renderer_winding winding);
void vulkan_renderer_set_stencil_test_enabled(struct renderer_plugin* plugin, b8 enabled);
void vulkan_renderer_set_depth_test_enabled(struct renderer_plugin* plugin, b8 enabled);
void vulkan_renderer_set_stencil_reference(struct renderer_plugin* plugin, u32 reference);
void vulkan_renderer_set_stencil_op(struct renderer_plugin* plugin, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);
void vulkan_renderer_set_stencil_compare_mask(struct renderer_plugin* plugin, u32 compare_mask);
void vulkan_renderer_set_stencil_write_mask(struct renderer_plugin* plugin, u32 write_mask);

b8 vulkan_renderer_renderpass_begin(renderer_plugin* backend, renderpass* pass, render_target* target);
b8 vulkan_renderer_renderpass_end(renderer_plugin* backend, renderpass* pass);

void vulkan_renderer_texture_create(renderer_plugin* backend, const u8* pixels, texture* texture);
void vulkan_renderer_texture_destroy(renderer_plugin* backend, texture* texture);
void vulkan_renderer_texture_create_writeable(renderer_plugin* backend, texture* t);
void vulkan_renderer_texture_resize(renderer_plugin* backend, texture* t, u32 new_width, u32 new_height);
void vulkan_renderer_texture_write_data(renderer_plugin* backend, texture* t, u32 offset, u32 size, const u8* pixels);
void vulkan_renderer_texture_read_data(renderer_plugin* backend, texture* t, u32 offset, u32 size, void** out_memory);
void vulkan_renderer_texture_read_pixel(renderer_plugin* backend, texture* t, u32 x, u32 y, u8** out_rgba);

b8 vulkan_renderer_shader_create(renderer_plugin* backend, struct shader* shader, const shader_config* config, renderpass* pass);
void vulkan_renderer_shader_destroy(renderer_plugin* backend, struct shader* shader);

b8 vulkan_renderer_shader_initialize(renderer_plugin* backend, struct shader* shader);
b8 vulkan_renderer_shader_use(renderer_plugin* backend, struct shader* shader);
b8 vulkan_renderer_shader_bind_globals(renderer_plugin* backend, struct shader* s);
b8 vulkan_renderer_shader_bind_instance(renderer_plugin* backend, struct shader* s, u32 instance_id);
b8 vulkan_renderer_shader_bind_local(renderer_plugin* backend, struct shader* s);
b8 vulkan_renderer_shader_apply_globals(renderer_plugin* backend, struct shader* s, b8 needs_update);
b8 vulkan_renderer_shader_apply_instance(renderer_plugin* backend, struct shader* s, b8 needs_update);
b8 vulkan_renderer_shader_instance_resources_acquire(renderer_plugin* backend, struct shader* s, const shader_instance_resource_config* config, u32* out_instance_id);
b8 vulkan_renderer_shader_instance_resources_release(renderer_plugin* backend, struct shader* s, u32 instance_id);
b8 vulkan_renderer_uniform_set(renderer_plugin* backend, struct shader* frontend_shader, struct shader_uniform* uniform, u32 array_index, const void* value);
b8 vulkan_renderer_shader_apply_local(renderer_plugin* plugin, struct shader* s);

b8 vulkan_renderer_texture_map_resources_acquire(renderer_plugin* backend, texture_map* map);
void vulkan_renderer_texture_map_resources_release(renderer_plugin* backend, texture_map* map);
b8 vulkan_renderer_texture_map_resources_refresh(renderer_plugin* plugin, texture_map* map);

b8 vulkan_renderpass_create(renderer_plugin* backend, const renderpass_config* config, renderpass* out_renderpass);
void vulkan_renderpass_destroy(renderer_plugin* backend, renderpass* pass);

b8 vulkan_renderer_render_target_create(renderer_plugin* backend, u8 attachment_count, render_target_attachment* attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);
void vulkan_renderer_render_target_destroy(renderer_plugin* backend, render_target* target, b8 free_internal_memory);

texture* vulkan_renderer_window_attachment_get(renderer_plugin* backend, u8 index);
texture* vulkan_renderer_depth_attachment_get(renderer_plugin* backend, u8 index);
u8 vulkan_renderer_window_attachment_index_get(renderer_plugin* backend);
u8 vulkan_renderer_window_attachment_count_get(renderer_plugin* backend);

b8 vulkan_renderer_is_multithreaded(renderer_plugin* backend);

b8 vulkan_renderer_flag_enabled_get(renderer_plugin* backend, renderer_config_flags flag);
void vulkan_renderer_flag_enabled_set(renderer_plugin* backend, renderer_config_flags flag, b8 enabled);

b8 vulkan_buffer_create_internal(renderer_plugin* backend, renderbuffer* buffer);
void vulkan_buffer_destroy_internal(renderer_plugin* backend, renderbuffer* buffer);
b8 vulkan_buffer_resize(renderer_plugin* backend, renderbuffer* buffer, u64 new_size);
b8 vulkan_buffer_bind(renderer_plugin* backend, renderbuffer* buffer, u64 offset);
b8 vulkan_buffer_unbind(renderer_plugin* backend, renderbuffer* buffer);
void* vulkan_buffer_map_memory(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u64 size);
void vulkan_buffer_unmap_memory(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u64 size);
b8 vulkan_buffer_flush(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u64 size);
b8 vulkan_buffer_read(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u64 size, void** out_memory);
b8 vulkan_buffer_load_range(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u64 size, const void* data);
b8 vulkan_buffer_copy_range(renderer_plugin* backend, renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size);
b8 vulkan_buffer_draw(renderer_plugin* backend, renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only);
