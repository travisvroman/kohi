/**
 * @file vulkan_pipeline.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains a Vulkan pipeline, which is responsible for combining
 * items such as the shader modules, attributes, uniforms/descriptors, viewport/scissor,
 * etc. 
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief Creates a new Vulkan pipeline.
 * 
 * @param context A pointer to the Vulkan context.
 * @param renderpass A pointer to the renderpass to associate with the pipeline.
 * @param stride The stride of the vertex data to be used (ex: sizeof(vertex_3d))
 * @param attribute_count The number of attributes.
 * @param attributes An array of attributes.
 * @param descriptor_set_layout_count The number of descriptor set layouts.
 * @param descriptor_set_layouts An array of descriptor set layouts.
 * @param stage_count The number of stages (vertex, fragment, etc).
 * @param stages An array of stages.
 * @param viewport The viewport configuration.
 * @param scissor The scissor configuration.
 * @param cull_mode The face cull mode.
 * @param is_wireframe Indicates if this pipeline should use wireframe mode.
 * @param depth_test_enabled Indicates if depth testing is enabled for this pipeline/
 * @param out_pipeline A pointer to hold the newly-created pipeline.
 * @return True on success; otherwise false.
 */
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
    face_cull_mode cull_mode,
    b8 is_wireframe,
    b8 depth_test_enabled,
    u32 push_constant_range_count,
    range* push_constant_ranges,
    vulkan_pipeline* out_pipeline);

/**
 * @brief Destroys the given pipeline.
 * 
 * @param context A pointer to the Vulkan context.
 * @param pipeline A pointer to the pipeline to be destroyed.
 */
void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline);

/**
 * @brief Binds the given pipeline for use. This must be done within a renderpass.
 * 
 * @param command_buffer The command buffer to assign the bind command to.
 * @param bind_point The pipeline bind point (typically bind_point_graphics)
 * @param pipeline A pointer to the pipeline to be bound.
 */
void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline);
