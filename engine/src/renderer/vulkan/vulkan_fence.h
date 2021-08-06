#pragma once

#include "vulkan_types.inl"

void vulkan_fence_create(
    vulkan_context* context,
    b8 create_signaled,
    vulkan_fence* out_fence);

void vulkan_fence_destroy(vulkan_context* context, vulkan_fence* fence);

b8 vulkan_fence_wait(vulkan_context* context, vulkan_fence* fence, u64 timeout_ns);

void vulkan_fence_reset(vulkan_context* context, vulkan_fence* fence);