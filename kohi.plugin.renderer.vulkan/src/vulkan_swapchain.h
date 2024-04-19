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

#include "vulkan_types.h"

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
    renderer_config_flags flags,
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
