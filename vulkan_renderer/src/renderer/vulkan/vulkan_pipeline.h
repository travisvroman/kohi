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
 * @param config A constant pointer to configuration to be used in creating the pipeline.
 * @param out_pipeline A pointer to hold the newly-created pipeline.
 * @return True on success; otherwise false.
 */
b8 vulkan_graphics_pipeline_create(vulkan_context* context, const vulkan_pipeline_config* config, vulkan_pipeline* out_pipeline);

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
