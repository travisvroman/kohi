#pragma once

#include "renderer/renderer_backend.h"
#include "resources/resource_types.h"

b8 vulkan_renderer_backend_initialize(renderer_backend* backend, const char* application_name);
void vulkan_renderer_backend_shutdown(renderer_backend* backend);

void vulkan_renderer_backend_on_resized(renderer_backend* backend, u16 width, u16 height);

b8 vulkan_renderer_backend_begin_frame(renderer_backend* backend, f32 delta_time);
void vulkan_renderer_update_global_state(mat4 projection, mat4 view, vec3 view_position, vec4 ambient_colour, i32 mode);
b8 vulkan_renderer_backend_end_frame(renderer_backend* backend, f32 delta_time);

void vulkan_backend_update_object(geometry_render_data data);

void vulkan_renderer_create_texture(const u8* pixels, texture* texture);
void vulkan_renderer_destroy_texture(texture* texture);

b8 vulkan_renderer_create_material(struct material* material);
void vulkan_renderer_destroy_material(struct material* material);