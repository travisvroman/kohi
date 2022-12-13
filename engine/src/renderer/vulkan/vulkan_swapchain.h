/**
 * @file vulkan_swapchain.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The Vulkan swapchain, which works with the framebuffer/attachments and
 * the surface to present an image to the screen.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief Creates a new swapchain.
 * 
 * @param context A pointer to the Vulkan context.
 * @param width The initial width of the surface area.
 * @param height The initial height of the surface area.
 * @param vsync Indicates if the swapchain should use vsync.
 * @param out_swapchain A pointer to the newly-created swapchain.
 */
void vulkan_swapchain_create(
    vulkan_context* context,
    u32 width,
    u32 height,
    b8 vsync,
    vulkan_swapchain* out_swapchain);

/**
 * @brief Recreates the given swapchain with the given width and
 * height, replacing the internal swapchain with the newly-created
 * one.
 * 
 * @param context A pointer to the Vulkan context.
 * @param width The new width of the surface area.
 * @param height The new width of the surface area.
 * @param swapchain A pointer to the swapchain to be recreated.
 */
void vulkan_swapchain_recreate(
    vulkan_context* context,
    u32 width,
    u32 height,
    vulkan_swapchain* swapchain);

/**
 * @brief Destroys the given swapchain.
 * 
 * @param context A pointer to the Vulkan context.
 * @param swapchain A pointer to the swapchain to be destroyed.
 */
void vulkan_swapchain_destroy(
    vulkan_context* context,
    vulkan_swapchain* swapchain);

/**
 * @brief Acquires the index of the next image to be rendered to.
 * 
 * @param context A pointer to the Vulkan context.
 * @param swapchain A pointer to the swapchain to acquire the image index from.
 * @param timeout_ns The maximum amount of time that can pass before the operation is considered failed.
 * @param image_available_semaphore A semaphore that will be signaled when this completes.
 * @param fence A fence that will be signaled when this completes.
 * @param out_image_index A pointer to hold the image index.
 * @return True on success; otherwise false.
 */
b8 vulkan_swapchain_acquire_next_image_index(
    vulkan_context* context,
    vulkan_swapchain* swapchain,
    u64 timeout_ns,
    VkSemaphore image_available_semaphore,
    VkFence fence,
    u32* out_image_index);

/**
 * @brief Presents the swapchain's current image to the surface.
 * 
 * @param context A pointer to the Vulkan context.
 * @param swapchain A pointer to the swapchain to present.
 * @param present_queue The presentation queue used for presentation.
 * @param render_complete_semaphore The semaphore that will be signaled when the presentation is complete.
 * @param present_image_index The image index to present.
 */
void vulkan_swapchain_present(
    vulkan_context* context,
    vulkan_swapchain* swapchain,
    VkQueue present_queue,
    VkSemaphore render_complete_semaphore,
    u32 present_image_index);
