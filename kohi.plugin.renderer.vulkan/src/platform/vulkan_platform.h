/**
 * @file vulkan_platform.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the "front end interface" for where the platform
 * and Vulkan meet. The implementation for this should exist in the corresponding
 * .c file for a given platform.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */
#pragma once

#include <defines.h>

struct kwindow;
struct vulkan_context;
struct VkPhysicalDevice_T;

/**
 * @brief Creates and assigns a surface to the given context.
 *
 * @param context A pointer to the Vulkan context.
 * @return True on success; otherwise false.
 */
b8 vulkan_platform_create_vulkan_surface(struct vulkan_context* context, struct kwindow* window);

/**
 * @brief Appends the names of required extensions for this platform to
 * the names_darray, which should be created and passed in.
 * @param names_darray A pointer to the array names of required extension names. Must be a darray
 * as this function adds names to the array.
 */
void vulkan_platform_get_required_extension_names(const char*** names_darray);

/**
 * Indicates if the given device/queue family index combo supports presentation.
 */
b8 vulkan_platform_presentation_support(struct vulkan_context* context, struct VkPhysicalDevice_T* physical_device, u32 queue_family_index);
