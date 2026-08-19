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

#include "core_render_types.h"
#include "renderer/renderer_types.h"

struct shader_uniform;
struct frame_data;
struct kwindow;

b8 vulkan_renderer_backend_initialize (renderer_backend_interface *backend, const renderer_backend_config *config);
void vulkan_renderer_backend_shutdown (renderer_backend_interface *backend);

b8 vulkan_renderer_on_window_created (renderer_backend_interface *backend, struct kwindow *window);
void vulkan_renderer_on_window_destroyed (renderer_backend_interface *backend, struct kwindow *window);
void vulkan_renderer_backend_on_window_resized (renderer_backend_interface *backend, const struct kwindow *window);

void vulkan_renderer_begin_debug_label (renderer_backend_interface *backend, const char *label_text, vec4 colour);
void vulkan_renderer_end_debug_label (renderer_backend_interface *backend);
b8 vulkan_renderer_frame_prepare (renderer_backend_interface *backend, struct frame_data *p_frame_data);

b8 vulkan_renderer_frame_prepare_window_surface (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data);
b8 vulkan_renderer_frame_command_list_begin (renderer_backend_interface *backend, struct frame_data *p_frame_data);
b8 vulkan_renderer_frame_command_list_end (renderer_backend_interface *backend, struct frame_data *p_frame_data);
b8 vulkan_renderer_frame_submit (struct renderer_backend_interface *backend, struct frame_data *p_frame_data);
b8 vulkan_renderer_frame_present (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data);

b8 vulkan_renderer_begin (renderer_backend_interface *backend, struct frame_data *p_frame_data);
b8 vulkan_renderer_end (renderer_backend_interface *backend, struct frame_data *p_frame_data);
void vulkan_renderer_viewport_set (renderer_backend_interface *backend, rect_2di rect);
void vulkan_renderer_viewport_reset (renderer_backend_interface *backend);
void vulkan_renderer_scissor_set (renderer_backend_interface *backend, rect_2di rect);
void vulkan_renderer_scissor_reset (renderer_backend_interface *backend);

void vulkan_renderer_winding_set (struct renderer_backend_interface *backend, renderer_winding winding);
void vulkan_renderer_cull_mode_set (struct renderer_backend_interface *backend, renderer_cull_mode cull_mode);
void vulkan_renderer_set_stencil_test_enabled (struct renderer_backend_interface *backend, b8 enabled);
void vulkan_renderer_set_depth_test_enabled (struct renderer_backend_interface *backend, b8 enabled);
void vulkan_renderer_set_depth_write_enabled (struct renderer_backend_interface *backend, b8 enabled);
void vulkan_renderer_set_depth_bias (struct renderer_backend_interface *backend, f32 constant_factor, f32 clamp, f32 slope_factor);
void vulkan_renderer_set_depth_bias_enabled (struct renderer_backend_interface *backend, b8 enabled);
void vulkan_renderer_set_stencil_reference (struct renderer_backend_interface *backend, u32 reference);
void vulkan_renderer_set_stencil_op (struct renderer_backend_interface *backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);

void vulkan_renderer_begin_rendering (struct renderer_backend_interface *backend, struct frame_data *p_frame_data, rect_2di render_area, u32 colour_target_count, ktexture *colour_targets, ktexture depth_stencil_target, u32 depth_stencil_layer);
void vulkan_renderer_end_rendering (struct renderer_backend_interface *backend, struct frame_data *p_frame_data);

void vulkan_renderer_set_stencil_compare_mask (struct renderer_backend_interface *backend, u32 compare_mask);
void vulkan_renderer_set_stencil_write_mask (struct renderer_backend_interface *backend, u32 write_mask);

void vulkan_renderer_clear_colour_set (renderer_backend_interface *backend, vec4 clear_colour);
void vulkan_renderer_clear_depth_set (renderer_backend_interface *backend, f32 depth);
void vulkan_renderer_clear_stencil_set (renderer_backend_interface *backend, u32 stencil);
void vulkan_renderer_clear_colour_texture (renderer_backend_interface *backend, ktexture t);
void vulkan_renderer_clear_depth_stencil (renderer_backend_interface *backend, ktexture t);
void vulkan_renderer_colour_texture_prepare_for_present (renderer_backend_interface *backend, ktexture t);
void vulkan_renderer_texture_prepare_for_sampling (renderer_backend_interface *backend, ktexture t, ktexture_flag_bits flags);

b8 vulkan_renderer_texture_resources_acquire (renderer_backend_interface *backend, ktexture t, const char *name, ktexture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, ktexture_flag_bits flags);
void vulkan_renderer_texture_resources_release (renderer_backend_interface *backend, ktexture t);

b8 vulkan_renderer_texture_resize (renderer_backend_interface *backend, ktexture t, u32 new_width, u32 new_height);
b8 vulkan_renderer_texture_write_data (renderer_backend_interface *backend, ktexture t, u32 bpp, u32 px_x, u32 px_y, i32 layer, u32 width, u32 height, const u8 *pixels, b8 defer_to_next_frame);
b8 vulkan_renderer_texture_read_data (renderer_backend_interface *backend, ktexture t, u32 offset, u32 size, u8 **out_pixels);
b8 vulkan_renderer_texture_read_pixel (renderer_backend_interface *backend, ktexture t, u32 x, u32 y, u8 **out_rgba);

b8 vulkan_renderer_shader_create (
	renderer_backend_interface *backend,
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
void vulkan_renderer_shader_destroy (renderer_backend_interface *backend, kshader shader);

b8 vulkan_renderer_shader_reload (renderer_backend_interface *backend, kshader shader, u8 pipeline_count, shader_pipeline_config *pipelines);
b8 vulkan_renderer_shader_use (renderer_backend_interface *backend, kshader shader, u8 vertex_layout_index);
b8 vulkan_renderer_shader_use_with_topology (renderer_backend_interface *backend, kshader shader, primitive_topology_type type, u8 vertex_layout_index);
b8 vulkan_renderer_shader_supports_wireframe (const renderer_backend_interface *backend, const kshader shader);
b8 vulkan_renderer_shader_flag_get (const renderer_backend_interface *backend, kshader shader, shader_flags flag);
void vulkan_renderer_shader_flag_set (renderer_backend_interface *backend, kshader shader, shader_flags flag, b8 enabled);
b8 vulkan_renderer_shader_set_immediate_data (renderer_backend_interface *backend, kshader shader, const void *data, u8 size);
b8 vulkan_renderer_shader_set_binding_data (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size);
b8 vulkan_renderer_shader_set_binding_texture (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture t);
b8 vulkan_renderer_shader_set_binding_sampler (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler);
b8 vulkan_renderer_shader_apply_binding_set (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id);

u32 vulkan_renderer_shader_acquire_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set);
void vulkan_renderer_shader_release_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id);
u32 vulkan_renderer_shader_binding_set_get_max_instance_count (renderer_backend_interface *backend, kshader shader, u8 binding_set);

ksampler_backend vulkan_renderer_sampler_acquire (renderer_backend_interface *backend, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy);
void vulkan_renderer_sampler_release (renderer_backend_interface *backend, ksampler_backend *sampler);
b8 vulkan_renderer_sampler_refresh (renderer_backend_interface *backend, ksampler_backend *sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels);
kname vulkan_renderer_sampler_name_get (renderer_backend_interface *backend, ksampler_backend sampler);

b8 vulkan_renderer_is_multithreaded (renderer_backend_interface *backend);

b8 vulkan_renderer_flag_enabled_get (renderer_backend_interface *backend, renderer_config_flags flag);
void vulkan_renderer_flag_enabled_set (renderer_backend_interface *backend, renderer_config_flags flag, b8 enabled);

f32 vulkan_renderer_max_anisotropy_get (renderer_backend_interface *backend);

b8 vulkan_renderbuffer_create (renderer_backend_interface *backend, kname name, u64 size, renderbuffer_type type, renderbuffer_flags flags, krenderbuffer handle);
void vulkan_renderbuffer_destroy (renderer_backend_interface *backend, krenderbuffer handle);
b8 vulkan_buffer_resize (renderer_backend_interface *backend, krenderbuffer handle, u64 new_size);
b8 vulkan_buffer_bind (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 binding_index);
b8 vulkan_buffer_unbind (renderer_backend_interface *backend, krenderbuffer handle);
void vulkan_buffer_map_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size);
void vulkan_buffer_unmap_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size);
void *vulkan_renderbuffer_get_mapped_memory (renderer_backend_interface *backend, krenderbuffer handle);
b8 vulkan_buffer_flush (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size);
b8 vulkan_buffer_read (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size, void **out_memory);
b8 vulkan_buffer_load_range (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size, const void *data, b8 include_in_frame_workload);
b8 vulkan_buffer_copy_range (renderer_backend_interface *backend, krenderbuffer source, u64 source_offset, krenderbuffer dest, u64 dest_offset, u64 size, b8 include_in_frame_workload);
b8 vulkan_buffer_draw (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 element_count, u32 binding_index, b8 bind_only);

void vulkan_renderer_wait_for_idle (renderer_backend_interface *backend);

void vulkan_renderer_gpu_profiler_initialize (renderer_backend_interface *backend, kgpu_profiler *profiler);
void vulkan_renderer_gpu_profiler_destroy (renderer_backend_interface *backend, kgpu_profiler *profiler);
void vulkan_renderer_gpu_profiler_begin_frame (renderer_backend_interface *backend, kgpu_profiler *profiler);
void vulkan_renderer_gpu_profiler_end_frame (renderer_backend_interface *backend, kgpu_profiler *profiler);
void vulkan_renderer_gpu_profiler_begin_event (renderer_backend_interface *backend, kgpu_profiler_eventid id);
void vulkan_renderer_gpu_profiler_end_event (renderer_backend_interface *backend, kgpu_profiler_eventid id);
void vulkan_renderer_gpu_profiler_query_timestamps (renderer_backend_interface *backend, u32 begin_id, u64 *out_start, u64 *out_end);

#if KOHI_DEBUG
void vulkan_renderer_debug_pump_brakes (renderer_backend_interface *backend);
#endif
