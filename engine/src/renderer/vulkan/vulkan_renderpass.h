#pragma once

#include "vulkan_types.inl"

typedef enum renderpass_clear_flag {
    RENDERPASS_CLEAR_NONE_FLAG = 0x0,
    RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG = 0x1,
    RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG = 0x2,
    RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG = 0x4
} renderpass_clear_flag;

void vulkan_renderpass_create(
    vulkan_context* context, 
    vulkan_renderpass* out_renderpass,
    vec4 render_area,
    vec4 clear_colour,
    f32 depth,
    u32 stencil,
    u8 clear_flags,
    b8 has_prev_pass,
    b8 has_next_pass);

void vulkan_renderpass_destroy(vulkan_context* context, vulkan_renderpass* renderpass);

void vulkan_renderpass_begin(
    vulkan_command_buffer* command_buffer, 
    vulkan_renderpass* renderpass,
    VkFramebuffer frame_buffer);

void vulkan_renderpass_end(vulkan_command_buffer* command_buffer, vulkan_renderpass* renderpass);