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

struct kwindow;
struct renderer_backend_interface;

b8 vulkan_swapchain_create(
    struct renderer_backend_interface* backend,
    struct kwindow* window,
    renderer_config_flags flags,
    vulkan_swapchain* out_swapchain);

b8 vulkan_swapchain_recreate(
    struct renderer_backend_interface* backend,
    struct kwindow* window,
    vulkan_swapchain* swapchain);

void vulkan_swapchain_destroy(struct renderer_backend_interface* backend, vulkan_swapchain* swapchain);
