#include "vulkan_renderer_plugin_main.h"

#include "renderer/vulkan/vulkan_backend.h"
#include <core/kmemory.h>

b8 plugin_create(renderer_plugin* out_plugin) {
    out_plugin->initialize = vulkan_renderer_backend_initialize;
    out_plugin->shutdown = vulkan_renderer_backend_shutdown;
    out_plugin->frame_begin = vulkan_renderer_backend_frame_begin;
    out_plugin->frame_end = vulkan_renderer_backend_frame_end;
    out_plugin->viewport_set = vulkan_renderer_viewport_set;
    out_plugin->viewport_reset = vulkan_renderer_viewport_reset;
    out_plugin->scissor_set = vulkan_renderer_scissor_set;
    out_plugin->scissor_reset = vulkan_renderer_scissor_reset;
    out_plugin->renderpass_begin = vulkan_renderer_renderpass_begin;
    out_plugin->renderpass_end = vulkan_renderer_renderpass_end;
    out_plugin->resized = vulkan_renderer_backend_on_resized;
    out_plugin->geometry_draw = vulkan_renderer_geometry_draw;
    out_plugin->texture_create = vulkan_renderer_texture_create;
    out_plugin->texture_destroy = vulkan_renderer_texture_destroy;
    out_plugin->texture_create_writeable = vulkan_renderer_texture_create_writeable;
    out_plugin->texture_resize = vulkan_renderer_texture_resize;
    out_plugin->texture_write_data = vulkan_renderer_texture_write_data;
    out_plugin->texture_read_data = vulkan_renderer_texture_read_data;
    out_plugin->texture_read_pixel = vulkan_renderer_texture_read_pixel;
    out_plugin->geometry_create = vulkan_renderer_geometry_create;
    out_plugin->geometry_destroy = vulkan_renderer_geometry_destroy;

    out_plugin->shader_create = vulkan_renderer_shader_create;
    out_plugin->shader_destroy = vulkan_renderer_shader_destroy;
    out_plugin->shader_uniform_set = vulkan_renderer_uniform_set;
    out_plugin->shader_initialize = vulkan_renderer_shader_initialize;
    out_plugin->shader_use = vulkan_renderer_shader_use;
    out_plugin->shader_bind_globals = vulkan_renderer_shader_bind_globals;
    out_plugin->shader_bind_instance = vulkan_renderer_shader_bind_instance;

    out_plugin->shader_apply_globals = vulkan_renderer_shader_apply_globals;
    out_plugin->shader_apply_instance = vulkan_renderer_shader_apply_instance;
    out_plugin->shader_instance_resources_acquire = vulkan_renderer_shader_instance_resources_acquire;
    out_plugin->shader_instance_resources_release = vulkan_renderer_shader_instance_resources_release;

    out_plugin->texture_map_resources_acquire = vulkan_renderer_texture_map_resources_acquire;
    out_plugin->texture_map_resources_release = vulkan_renderer_texture_map_resources_release;

    out_plugin->render_target_create = vulkan_renderer_render_target_create;
    out_plugin->render_target_destroy = vulkan_renderer_render_target_destroy;

    out_plugin->renderpass_create = vulkan_renderpass_create;
    out_plugin->renderpass_destroy = vulkan_renderpass_destroy;
    out_plugin->window_attachment_get = vulkan_renderer_window_attachment_get;
    out_plugin->depth_attachment_get = vulkan_renderer_depth_attachment_get;
    out_plugin->window_attachment_index_get = vulkan_renderer_window_attachment_index_get;
    out_plugin->window_attachment_count_get = vulkan_renderer_window_attachment_count_get;
    out_plugin->is_multithreaded = vulkan_renderer_is_multithreaded;
    out_plugin->flag_enabled_get = vulkan_renderer_flag_enabled_get;
    out_plugin->flag_enabled_set = vulkan_renderer_flag_enabled_set;

    out_plugin->renderbuffer_internal_create = vulkan_buffer_create_internal;
    out_plugin->renderbuffer_internal_destroy = vulkan_buffer_destroy_internal;
    out_plugin->renderbuffer_bind = vulkan_buffer_bind;
    out_plugin->renderbuffer_unbind = vulkan_buffer_unbind;
    out_plugin->renderbuffer_map_memory = vulkan_buffer_map_memory;
    out_plugin->renderbuffer_unmap_memory = vulkan_buffer_unmap_memory;
    out_plugin->renderbuffer_flush = vulkan_buffer_flush;
    out_plugin->renderbuffer_read = vulkan_buffer_read;
    out_plugin->renderbuffer_resize = vulkan_buffer_resize;
    out_plugin->renderbuffer_load_range = vulkan_buffer_load_range;
    out_plugin->renderbuffer_copy_range = vulkan_buffer_copy_range;
    out_plugin->renderbuffer_draw = vulkan_buffer_draw;

    return true;
}

void plugin_destroy(renderer_plugin* plugin) {
    kzero_memory(plugin, sizeof(renderer_plugin));
}
