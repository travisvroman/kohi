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

struct shader;
struct shader_uniform;
struct frame_data;
struct kwindow;

b8 vulkan_renderer_backend_initialize(renderer_backend_interface* backend, const renderer_backend_config* config);
void vulkan_renderer_backend_shutdown(renderer_backend_interface* backend);

b8 vulkan_renderer_on_window_created(renderer_backend_interface* backend, struct kwindow* window);
void vulkan_renderer_on_window_destroyed(renderer_backend_interface* backend, struct kwindow* window);
void vulkan_renderer_backend_on_window_resized(renderer_backend_interface* backend, const struct kwindow* window);

void vulkan_renderer_begin_debug_label(renderer_backend_interface* backend, const char* label_text, vec3 colour);
void vulkan_renderer_end_debug_label(renderer_backend_interface* backend);
b8 vulkan_renderer_frame_prepare(renderer_backend_interface* backend, struct frame_data* p_frame_data);

b8 vulkan_renderer_frame_prepare_window_surface(renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data);
b8 vulkan_renderer_frame_command_list_begin(renderer_backend_interface* backend, struct frame_data* p_frame_data);
b8 vulkan_renderer_frame_command_list_end(renderer_backend_interface* backend, struct frame_data* p_frame_data);
b8 vulkan_renderer_frame_submit(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);
b8 vulkan_renderer_frame_present(renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data);

b8 vulkan_renderer_begin(renderer_backend_interface* backend, struct frame_data* p_frame_data);
b8 vulkan_renderer_end(renderer_backend_interface* backend, struct frame_data* p_frame_data);
void vulkan_renderer_viewport_set(renderer_backend_interface* backend, vec4 rect);
void vulkan_renderer_viewport_reset(renderer_backend_interface* backend);
void vulkan_renderer_scissor_set(renderer_backend_interface* backend, vec4 rect);
void vulkan_renderer_scissor_reset(renderer_backend_interface* backend);

void vulkan_renderer_winding_set(struct renderer_backend_interface* backend, renderer_winding winding);
void vulkan_renderer_set_stencil_test_enabled(struct renderer_backend_interface* backend, b8 enabled);
void vulkan_renderer_set_depth_test_enabled(struct renderer_backend_interface* backend, b8 enabled);
void vulkan_renderer_set_depth_write_enabled(struct renderer_backend_interface* backend, b8 enabled);
void vulkan_renderer_set_stencil_reference(struct renderer_backend_interface* backend, u32 reference);
void vulkan_renderer_set_stencil_op(struct renderer_backend_interface* backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);

void vulkan_renderer_begin_rendering(struct renderer_backend_interface* backend, struct frame_data* p_frame_data, rect_2d render_area, u32 colour_target_count, struct texture_internal_data** colour_targets, struct texture_internal_data* depth_stencil_target, u32 depth_stencil_layer);
void vulkan_renderer_end_rendering(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);

void vulkan_renderer_set_stencil_compare_mask(struct renderer_backend_interface* backend, u32 compare_mask);
void vulkan_renderer_set_stencil_write_mask(struct renderer_backend_interface* backend, u32 write_mask);

void vulkan_renderer_clear_colour_set(renderer_backend_interface* backend, vec4 clear_colour);
void vulkan_renderer_clear_depth_set(renderer_backend_interface* backend, f32 depth);
void vulkan_renderer_clear_stencil_set(renderer_backend_interface* backend, u32 stencil);
void vulkan_renderer_clear_colour_texture(renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
void vulkan_renderer_clear_depth_stencil(renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
void vulkan_renderer_colour_texture_prepare_for_present(renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
void vulkan_renderer_texture_prepare_for_sampling(renderer_backend_interface* backend, struct texture_internal_data* tex_internal, texture_flag_bits flags);

b8 vulkan_renderer_texture_resources_acquire(renderer_backend_interface* backend, struct texture_internal_data* texture_data, const char* name, kresource_texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, kresource_texture_flag_bits flags);
void vulkan_renderer_texture_resources_release(renderer_backend_interface* backend, struct texture_internal_data* texture_data);

b8 vulkan_renderer_texture_resize(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 new_width, u32 new_height);
b8 vulkan_renderer_texture_write_data(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 offset, u32 size, const u8* pixels, b8 include_in_frame_workload);
b8 vulkan_renderer_texture_read_data(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 offset, u32 size, u8** out_pixels);
b8 vulkan_renderer_texture_read_pixel(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 x, u32 y, u8** out_rgba);

b8 vulkan_renderer_shader_create(renderer_backend_interface* backend, struct shader* shader, const shader_config* config);
void vulkan_renderer_shader_destroy(renderer_backend_interface* backend, struct shader* shader);

b8 vulkan_renderer_shader_initialize(renderer_backend_interface* backend, struct shader* shader);
b8 vulkan_renderer_shader_reload(renderer_backend_interface* backend, struct shader* s);
b8 vulkan_renderer_shader_use(renderer_backend_interface* backend, struct shader* shader);
b8 vulkan_renderer_shader_supports_wireframe(const renderer_backend_interface* backend, const struct shader* s);
b8 vulkan_renderer_shader_apply_globals(renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);
b8 vulkan_renderer_shader_apply_instance(renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);
b8 vulkan_renderer_shader_apply_local(renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);
b8 vulkan_renderer_shader_instance_resources_acquire(renderer_backend_interface* backend, struct shader* s, const shader_instance_resource_config* config, u32* out_instance_id);
b8 vulkan_renderer_shader_instance_resources_release(renderer_backend_interface* backend, struct shader* s, u32 instance_id);
b8 vulkan_renderer_uniform_set(renderer_backend_interface* backend, struct shader* frontend_shader, struct shader_uniform* uniform, u32 array_index, const void* value);

b8 vulkan_renderer_kresource_texture_map_resources_acquire(renderer_backend_interface* backend, kresource_texture_map* map);
void vulkan_renderer_kresource_texture_map_resources_release(renderer_backend_interface* backend, kresource_texture_map* map);
b8 vulkan_renderer_kresource_texture_map_resources_refresh(renderer_backend_interface* backend, kresource_texture_map* map);

b8 vulkan_renderer_is_multithreaded(renderer_backend_interface* backend);

b8 vulkan_renderer_flag_enabled_get(renderer_backend_interface* backend, renderer_config_flags flag);
void vulkan_renderer_flag_enabled_set(renderer_backend_interface* backend, renderer_config_flags flag, b8 enabled);

b8 vulkan_buffer_create_internal(renderer_backend_interface* backend, renderbuffer* buffer);
void vulkan_buffer_destroy_internal(renderer_backend_interface* backend, renderbuffer* buffer);
b8 vulkan_buffer_resize(renderer_backend_interface* backend, renderbuffer* buffer, u64 new_size);
b8 vulkan_buffer_bind(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset);
b8 vulkan_buffer_unbind(renderer_backend_interface* backend, renderbuffer* buffer);
void* vulkan_buffer_map_memory(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);
void vulkan_buffer_unmap_memory(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);
b8 vulkan_buffer_flush(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);
b8 vulkan_buffer_read(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size, void** out_memory);
b8 vulkan_buffer_load_range(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size, const void* data, b8 include_in_frame_workload);
b8 vulkan_buffer_copy_range(renderer_backend_interface* backend, renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size, b8 include_in_frame_workload);
b8 vulkan_buffer_draw(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only);

void vulkan_renderer_wait_for_idle(renderer_backend_interface* backend);
