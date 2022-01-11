/**
 * @file vulkan_image.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The implementation of the Vulkan image, which can be thought of as a texture.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief Creates a new Vulkan image.
 * 
 * @param context A pointer to the Vulkan context.
 * @param image_type The type of image.
 * @param width The width of the image.
 * @param height The height of the image.
 * @param format The format of the image.
 * @param tiling The image tiling mode.
 * @param usage The image usage.
 * @param memory_flags Memory flags for the memory used by the image.
 * @param create_view Indicates if a view should be created with the image.
 * @param view_aspect_flags Aspect flags to be used when creating the view, if applicable.
 * @param out_image A pointer to hold the newly-created image.
 */
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

/**
 * @brief Creates a view for the given image.
 * 
 * @param context A pointer to the Vulkan context.
 * @param format The image format.
 * @param image A pointer to the image to associate the view with.
 * @param aspect_flags Aspect flags to be used when creating the view, if applicable.
 */
void vulkan_image_view_create(
    vulkan_context* context,
    VkFormat format,
    vulkan_image* image,
    VkImageAspectFlags aspect_flags);

/**
 * @brief Transitions the provided image from old_layout to new_layout.
 * 
 * @param context A pointer to the Vulkan context.
 * @param command_buffer A pointer to the command buffer to be used.
 * @param image A pointer to the image whose layout will be transitioned.
 * @param format The image format.
 * @param old_layout The old layout.
 * @param new_layout The new layout.
 */
void vulkan_image_transition_layout(
    vulkan_context* context,
    vulkan_command_buffer* command_buffer,
    vulkan_image* image,
    VkFormat format,
    VkImageLayout old_layout,
    VkImageLayout new_layout);

/**
 * @brief Copies data in buffer to provided image.
 * @param context The Vulkan context.
 * @param image The image to copy the buffer's data to.
 * @param buffer The buffer whose data will be copied.
 */
void vulkan_image_copy_from_buffer(
    vulkan_context* context,
    vulkan_image* image,
    VkBuffer buffer,
    vulkan_command_buffer* command_buffer);

/**
 * @brief Destroys the given image.
 * 
 * @param context A pointer to the Vulkan context.
 * @param image A pointer to the image to be destroyed.
 */
void vulkan_image_destroy(vulkan_context* context, vulkan_image* image);
