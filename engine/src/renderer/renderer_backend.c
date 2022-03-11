#include "renderer_backend.h"

#include "vulkan/vulkan_backend.h"
#include "core/kmemory.h"

b8 renderer_backend_create(renderer_backend_type type, renderer_backend* out_renderer_backend) {
    if (type == RENDERER_BACKEND_TYPE_VULKAN) {
        out_renderer_backend->initialize = vulkan_renderer_backend_initialize;
        out_renderer_backend->shutdown = vulkan_renderer_backend_shutdown;
        out_renderer_backend->begin_frame = vulkan_renderer_backend_begin_frame;
        out_renderer_backend->end_frame = vulkan_renderer_backend_end_frame;
        out_renderer_backend->begin_renderpass = vulkan_renderer_begin_renderpass;
        out_renderer_backend->end_renderpass = vulkan_renderer_end_renderpass;
        out_renderer_backend->resized = vulkan_renderer_backend_on_resized;
        out_renderer_backend->draw_geometry = vulkan_renderer_draw_geometry;
        out_renderer_backend->create_texture = vulkan_renderer_create_texture;
        out_renderer_backend->destroy_texture = vulkan_renderer_destroy_texture;
        out_renderer_backend->create_geometry = vulkan_renderer_create_geometry;
        out_renderer_backend->destroy_geometry = vulkan_renderer_destroy_geometry;

        out_renderer_backend->shader_create = vulkan_renderer_shader_create;
        out_renderer_backend->shader_destroy = vulkan_renderer_shader_destroy;
        out_renderer_backend->shader_set_uniform = vulkan_renderer_set_uniform;
        out_renderer_backend->shader_initialize = vulkan_renderer_shader_initialize;
        out_renderer_backend->shader_use = vulkan_renderer_shader_use;
        out_renderer_backend->shader_bind_globals = vulkan_renderer_shader_bind_globals;
        out_renderer_backend->shader_bind_instance = vulkan_renderer_shader_bind_instance;
        
        out_renderer_backend->shader_apply_globals = vulkan_renderer_shader_apply_globals;
        out_renderer_backend->shader_apply_instance = vulkan_renderer_shader_apply_instance;
        out_renderer_backend->shader_acquire_instance_resources = vulkan_renderer_shader_acquire_instance_resources;
        out_renderer_backend->shader_release_instance_resources = vulkan_renderer_shader_release_instance_resources;
        
        return true;
    }

    return false;
}

void renderer_backend_destroy(renderer_backend* renderer_backend) {
    kzero_memory(renderer_backend, sizeof(renderer_backend));
}