#pragma once

#include "platform/vulkan_platform.h"

b8 vulkan_loader_initialize(krhi_vulkan* rhi);
b8 vulkan_loader_load_core(krhi_vulkan* rhi);
b8 vulkan_loader_load_instance(krhi_vulkan* rhi, VkInstance instance);
b8 vulkan_loader_load_device(krhi_vulkan* rhi, VkDevice device);
