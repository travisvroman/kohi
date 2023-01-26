/**
 * @file vulkan_device.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains a unified Vulkan device, which holds state and pointers
 * to both the Vulkan physical and logical devices, as well as other information such
 * as the swapchain.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "vulkan_types.inl"

/**
 * @brief Creates a new Vulkan device and assigns it to the given context.
 * 
 * @param context A pointer to the Vulkan context.
 * @return True on success; otherwise false.
 */
b8 vulkan_device_create(vulkan_context* context);

/**
 * @brief Destroys the device present in the given context.
 * 
 * @param context A pointer to the Vulkan context.
 */
void vulkan_device_destroy(vulkan_context* context);

/**
 * @brief Queries for swapchain support data for the given physical device and surface.
 * 
 * @param physical_device The Vulkan physical device.
 * @param surface The Vulkan surface.
 * @param out_support_info A pointer to hold the support info.
 */
void vulkan_device_query_swapchain_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    vulkan_swapchain_support_info* out_support_info);

/**
 * @brief Detects and assigns the depth format for the given device.
 * 
 * @param device A pointer to the device.
 * @return True if successful; otherwise false.
 */
b8 vulkan_device_detect_depth_format(vulkan_device* device);
