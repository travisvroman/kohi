/**
 * @file vulkan_loader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Loader for Vulkan.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "platform/vulkan_platform.h"

b8 vulkan_loader_initialize (krhi_vulkan *rhi);
void vulkan_loader_shutdown (krhi_vulkan *rhi);
b8 vulkan_loader_load_core (krhi_vulkan *rhi);
b8 vulkan_loader_load_instance (krhi_vulkan *rhi, VkInstance instance);
b8 vulkan_loader_load_device (krhi_vulkan *rhi, VkDevice device);
