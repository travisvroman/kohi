#include "vulkan_renderer_plugin_main.h"

#include <logger.h>
#include <memory/kmemory.h>

#include "kohi.plugin.renderer.vulkan_version.h"
#include "plugins/plugin_types.h"
#include "renderer/renderer_types.h"
#include "vulkan_backend.h"

b8 kplugin_create(kruntime_plugin* out_plugin) {
    out_plugin->plugin_state_size = sizeof(renderer_backend_interface);
    out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_RENDERER);

    renderer_backend_interface* backend = out_plugin->plugin_state;

    backend->initialize = vulkan_renderer_backend_initialize;
    backend->shutdown = vulkan_renderer_backend_shutdown;
    backend->begin_debug_label = vulkan_renderer_begin_debug_label;
    backend->end_debug_label = vulkan_renderer_end_debug_label;
    backend->window_create = vulkan_renderer_on_window_created;
    backend->window_destroy = vulkan_renderer_on_window_destroyed;
    backend->window_resized = vulkan_renderer_backend_on_window_resized;
    backend->frame_prepare = vulkan_renderer_frame_prepare;
    backend->frame_prepare_window_surface = vulkan_renderer_frame_prepare_window_surface;
    backend->frame_commands_begin = vulkan_renderer_frame_command_list_begin;
    backend->frame_commands_end = vulkan_renderer_frame_command_list_end;
    backend->frame_submit = vulkan_renderer_frame_submit;
    backend->frame_present = vulkan_renderer_frame_present;

    backend->viewport_set = vulkan_renderer_viewport_set;
    backend->viewport_reset = vulkan_renderer_viewport_reset;
    backend->scissor_set = vulkan_renderer_scissor_set;
    backend->scissor_reset = vulkan_renderer_scissor_reset;

    backend->clear_depth_set = vulkan_renderer_clear_depth_set;
    backend->clear_colour_set = vulkan_renderer_clear_colour_set;
    backend->clear_stencil_set = vulkan_renderer_clear_stencil_set;
    backend->clear_colour = vulkan_renderer_clear_colour_texture;
    backend->clear_depth_stencil = vulkan_renderer_clear_depth_stencil;
    backend->colour_texture_prepare_for_present = vulkan_renderer_colour_texture_prepare_for_present;
    backend->texture_prepare_for_sampling = vulkan_renderer_texture_prepare_for_sampling;

    backend->winding_set = vulkan_renderer_winding_set;
    backend->set_stencil_test_enabled = vulkan_renderer_set_stencil_test_enabled;
    backend->set_depth_test_enabled = vulkan_renderer_set_depth_test_enabled;
    backend->set_depth_write_enabled = vulkan_renderer_set_depth_write_enabled;
    backend->set_stencil_reference = vulkan_renderer_set_stencil_reference;
    backend->set_stencil_op = vulkan_renderer_set_stencil_op;

    backend->begin_rendering = vulkan_renderer_begin_rendering;
    backend->end_rendering = vulkan_renderer_end_rendering;

    backend->set_stencil_compare_mask = vulkan_renderer_set_stencil_compare_mask;
    backend->set_stencil_write_mask = vulkan_renderer_set_stencil_write_mask;

    backend->texture_resources_acquire = vulkan_renderer_texture_resources_acquire;
    backend->texture_resources_release = vulkan_renderer_texture_resources_release;

    backend->texture_resize = vulkan_renderer_texture_resize;
    backend->texture_write_data = vulkan_renderer_texture_write_data;
    backend->texture_read_data = vulkan_renderer_texture_read_data;
    backend->texture_read_pixel = vulkan_renderer_texture_read_pixel;

    backend->shader_create = vulkan_renderer_shader_create;
    backend->shader_destroy = vulkan_renderer_shader_destroy;
    backend->shader_uniform_set = vulkan_renderer_uniform_set;
    backend->shader_initialize = vulkan_renderer_shader_initialize;
    backend->shader_reload = vulkan_renderer_shader_reload;
    backend->shader_use = vulkan_renderer_shader_use;
    backend->shader_supports_wireframe = vulkan_renderer_shader_supports_wireframe;

    backend->shader_apply_globals = vulkan_renderer_shader_apply_globals;
    backend->shader_apply_instance = vulkan_renderer_shader_apply_instance;
    backend->shader_apply_local = vulkan_renderer_shader_apply_local;
    backend->shader_instance_resources_acquire = vulkan_renderer_shader_instance_resources_acquire;
    backend->shader_instance_resources_release = vulkan_renderer_shader_instance_resources_release;
    backend->shader_uniform_set = vulkan_renderer_uniform_set;

    // NOTE: old
    backend->texture_map_resources_acquire = vulkan_renderer_texture_map_resources_acquire;
    backend->texture_map_resources_release = vulkan_renderer_texture_map_resources_release;

    // NOTE: new
    backend->kresource_texture_map_resources_acquire = vulkan_renderer_kresource_texture_map_resources_acquire;
    backend->kresource_texture_map_resources_release = vulkan_renderer_kresource_texture_map_resources_release;

    backend->is_multithreaded = vulkan_renderer_is_multithreaded;
    backend->flag_enabled_get = vulkan_renderer_flag_enabled_get;
    backend->flag_enabled_set = vulkan_renderer_flag_enabled_set;

    backend->renderbuffer_internal_create = vulkan_buffer_create_internal;
    backend->renderbuffer_internal_destroy = vulkan_buffer_destroy_internal;
    backend->renderbuffer_bind = vulkan_buffer_bind;
    backend->renderbuffer_unbind = vulkan_buffer_unbind;
    backend->renderbuffer_map_memory = vulkan_buffer_map_memory;
    backend->renderbuffer_unmap_memory = vulkan_buffer_unmap_memory;
    backend->renderbuffer_flush = vulkan_buffer_flush;
    backend->renderbuffer_read = vulkan_buffer_read;
    backend->renderbuffer_resize = vulkan_buffer_resize;
    backend->renderbuffer_load_range = vulkan_buffer_load_range;
    backend->renderbuffer_copy_range = vulkan_buffer_copy_range;
    backend->renderbuffer_draw = vulkan_buffer_draw;
    backend->wait_for_idle = vulkan_renderer_wait_for_idle;

    KINFO("Vulkan Renderer Plugin Creation successful (%s).", KVERSION);

    return true;
}

void kplugin_destroy(kruntime_plugin* plugin) {
    // NOTE: this is taken care of internally
    // if (plugin && plugin->plugin_state) {
    //     kfree(plugin->plugin_state, plugin->plugin_state_size, MEMORY_TAG_RENDERER);
    // }
    // kzero_memory(plugin, sizeof(kruntime_plugin));
}
