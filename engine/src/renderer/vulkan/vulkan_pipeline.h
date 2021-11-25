#pragma once

#include "vulkan_types.inl"

b8 vulkan_graphics_pipeline_create(
    vulkan_context* context,
    vulkan_renderpass* renderpass,
    u32 stride,
    u32 attribute_count,
    VkVertexInputAttributeDescription* attributes,
    u32 descriptor_set_layout_count,
    VkDescriptorSetLayout* descriptor_set_layouts,
    u32 stage_count,
    VkPipelineShaderStageCreateInfo* stages,
    VkViewport viewport,
    VkRect2D scissor,
    b8 is_wireframe,
    b8 depth_test_enabled,
    vulkan_pipeline* out_pipeline);

void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline);

void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline);
