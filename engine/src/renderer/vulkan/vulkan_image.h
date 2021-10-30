#pragma once

#include "vulkan_types.inl"

void vulkan_image_create(
    vulkan_context* context,
    VkImageType image_type,
    u32 width,
    u32 height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memory_flags,
    b32 create_view,
    VkImageAspectFlags view_aspect_flags,
    vulkan_image* out_image);

void vulkan_image_view_create(
    vulkan_context* context,
    VkFormat format,
    vulkan_image* image,
    VkImageAspectFlags aspect_flags);

/**
 * Transitions the provided image from old_layout to new_layout.
 */
void vulkan_image_transition_layout(
    vulkan_context* context,
    vulkan_command_buffer* command_buffer,
    vulkan_image* image,
    VkFormat format,
    VkImageLayout old_layout,
    VkImageLayout new_layout);

/**
 * Copies data in buffer to provided image.
 * @param context The Vulkan context.
 * @param image The image to copy the buffer's data to.
 * @param buffer The buffer whose data will be copied.
 */
void vulkan_image_copy_from_buffer(
    vulkan_context* context,
    vulkan_image* image,
    VkBuffer buffer,
    vulkan_command_buffer* command_buffer);

void vulkan_image_destroy(vulkan_context* context, vulkan_image* image);
