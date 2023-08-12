/**
 * @file vulkan_utils.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A collection of Vulkan-specific utility functions.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once
#include "vulkan_types.h"

/**
 * @brief Returns the string representation of result.
 *
 * @param result The result to get the string for.
 * @param get_extended Indicates whether to also return an extended result.
 * @returns The error code and/or extended error message in string form. Defaults to success for unknown result types.
 */
const char* vulkan_result_string(VkResult result, b8 get_extended);

/**
 * @brief Inticates if the passed result is a success or an error as defined by the Vulkan spec.
 *
 * @returns True if success; otherwise false. Defaults to true for unknown result types.
 */
b8 vulkan_result_is_success(VkResult result);

#if defined(_DEBUG)
void vulkan_set_debug_object_name(vulkan_context* context, VkObjectType object_type, void* object_handle, const char* object_name);
void vulkan_set_debug_object_tag(vulkan_context* context, VkObjectType object_type, void* object_handle, u64 tag_size, const void* tag_data);
void vulkan_begin_label(vulkan_context* context, VkCommandBuffer buffer, const char* label_name, vec4 colour);
void vulkan_end_label(vulkan_context* context, VkCommandBuffer buffer);

#define VK_SET_DEBUG_OBJECT_NAME(context, object_type, object_handle, object_name) vulkan_set_debug_object_name(context, object_type, object_handle, object_name)
#define VK_SET_DEBUG_OBJECT_TAG(context, object_type, object_handle, tag_size, tag_data) vulkan_set_debug_object_tag(context, object_type, object_handle, tag_size, tag_data)
#define VK_BEGIN_DEBUG_LABEL(context, command_buffer, label_name, colour) vulkan_begin_label(context, command_buffer, label_name, colour)
#define VK_END_DEBUG_LABEL(context, command_buffer) vulkan_end_label(context, command_buffer)
#else
// Does nothing in non-debug builds.
#define VK_SET_DEBUG_OBJECT_NAME(context, object_type, object_handle, object_name)
// Does nothing in non-debug builds.
#define VK_SET_DEBUG_OBJECT_TAG(context, object_type, object_handle, tag_size, tag_data)
// Does nothing in non-debug builds.
#define VK_BEGIN_DEBUG_LABEL(context, command_buffer, label_name, colour)
// Does nothing in non-debug builds.
#define VK_END_DEBUG_LABEL(context, command_buffer)
#endif
