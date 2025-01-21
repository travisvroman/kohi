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

#include <vulkan/vulkan_core.h>

#include <defines.h>
#include <platform/platform.h>

struct kwindow;
struct vulkan_context;
struct VkPhysicalDevice_T;

#define RHI_VULKAN_DECL(name) PFN_##name k##name

// The Vulkan Render Hardware Interface.
typedef struct krhi_vulkan {
    dynamic_library vulkan_lib;

    VkInstance instance;
    VkDevice device;

    // Core functions

    RHI_VULKAN_DECL(vkGetInstanceProcAddr);

    // Instance functions

    RHI_VULKAN_DECL(vkEnumerateInstanceExtensionProperties);
    RHI_VULKAN_DECL(vkEnumerateInstanceVersion);
    RHI_VULKAN_DECL(vkEnumerateInstanceLayerProperties);
    RHI_VULKAN_DECL(vkCreateInstance);
    RHI_VULKAN_DECL(vkDestroyInstance);
    RHI_VULKAN_DECL(vkEnumeratePhysicalDevices);
    RHI_VULKAN_DECL(vkGetDeviceProcAddr);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceProperties);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceProperties2);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceFeatures);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceFeatures2);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceMemoryProperties);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceQueueFamilyProperties);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceFormatProperties);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceSurfaceFormatsKHR);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    RHI_VULKAN_DECL(vkGetPhysicalDeviceSurfacePresentModesKHR);
    RHI_VULKAN_DECL(vkEnumerateDeviceExtensionProperties);
    RHI_VULKAN_DECL(vkCreateDevice);
    RHI_VULKAN_DECL(vkDestroySurfaceKHR);

    // Device functions.
    RHI_VULKAN_DECL(vkGetDeviceQueue);
    RHI_VULKAN_DECL(vkDeviceWaitIdle);
    RHI_VULKAN_DECL(vkCreateCommandPool);
    RHI_VULKAN_DECL(vkDestroyCommandPool);
    RHI_VULKAN_DECL(vkDestroyDevice);
    RHI_VULKAN_DECL(vkCreateSwapchainKHR);
    RHI_VULKAN_DECL(vkDestroySwapchainKHR);
    RHI_VULKAN_DECL(vkGetSwapchainImagesKHR);
    RHI_VULKAN_DECL(vkCreateImage);
    RHI_VULKAN_DECL(vkCreateImageView);
    RHI_VULKAN_DECL(vkDestroyImage);
    RHI_VULKAN_DECL(vkDestroyImageView);
    RHI_VULKAN_DECL(vkGetImageMemoryRequirements);
    RHI_VULKAN_DECL(vkAllocateMemory);
    RHI_VULKAN_DECL(vkFreeMemory);
    RHI_VULKAN_DECL(vkAllocateCommandBuffers);
    RHI_VULKAN_DECL(vkFreeCommandBuffers);
    RHI_VULKAN_DECL(vkBeginCommandBuffer);
    RHI_VULKAN_DECL(vkEndCommandBuffer);
    RHI_VULKAN_DECL(vkBindImageMemory);
    RHI_VULKAN_DECL(vkCreateSemaphore);
    RHI_VULKAN_DECL(vkDestroySemaphore);
    RHI_VULKAN_DECL(vkCreateFence);
    RHI_VULKAN_DECL(vkDestroyFence);
    RHI_VULKAN_DECL(vkWaitForFences);
    RHI_VULKAN_DECL(vkAcquireNextImageKHR);
    RHI_VULKAN_DECL(vkResetFences);
    RHI_VULKAN_DECL(vkCreateDescriptorSetLayout);
    RHI_VULKAN_DECL(vkDestroyDescriptorSetLayout);
    RHI_VULKAN_DECL(vkCreateDescriptorPool);
    RHI_VULKAN_DECL(vkDestroyDescriptorPool);
    RHI_VULKAN_DECL(vkCreateShaderModule);
    RHI_VULKAN_DECL(vkDestroyShaderModule);
    RHI_VULKAN_DECL(vkCreateSampler);
    RHI_VULKAN_DECL(vkDestroySampler);
    RHI_VULKAN_DECL(vkCreateBuffer);
    RHI_VULKAN_DECL(vkDestroyBuffer);
    RHI_VULKAN_DECL(vkGetBufferMemoryRequirements);
    RHI_VULKAN_DECL(vkBindBufferMemory);
    RHI_VULKAN_DECL(vkMapMemory);
    RHI_VULKAN_DECL(vkUnmapMemory);
    RHI_VULKAN_DECL(vkFlushMappedMemoryRanges);
    RHI_VULKAN_DECL(vkCreatePipelineLayout);
    RHI_VULKAN_DECL(vkDestroyPipelineLayout);
    RHI_VULKAN_DECL(vkCreateGraphicsPipelines);
    RHI_VULKAN_DECL(vkDestroyPipeline);
    RHI_VULKAN_DECL(vkAllocateDescriptorSets);
    RHI_VULKAN_DECL(vkFreeDescriptorSets);
    RHI_VULKAN_DECL(vkUpdateDescriptorSets);

    RHI_VULKAN_DECL(vkCmdBindPipeline);
    RHI_VULKAN_DECL(vkCmdPipelineBarrier);
    RHI_VULKAN_DECL(vkCmdBlitImage);
    RHI_VULKAN_DECL(vkCmdCopyBuffer);
    RHI_VULKAN_DECL(vkCmdCopyBufferToImage);
    RHI_VULKAN_DECL(vkCmdCopyImageToBuffer);
    RHI_VULKAN_DECL(vkCmdExecuteCommands);
    RHI_VULKAN_DECL(vkCmdSetViewport);
    RHI_VULKAN_DECL(vkCmdSetScissor);
    RHI_VULKAN_DECL(vkCmdSetFrontFace);
    RHI_VULKAN_DECL(vkCmdSetCullMode);
    RHI_VULKAN_DECL(vkCmdSetStencilTestEnable);
    RHI_VULKAN_DECL(vkCmdSetDepthTestEnable);
    RHI_VULKAN_DECL(vkCmdSetDepthWriteEnable);
    RHI_VULKAN_DECL(vkCmdSetStencilReference);
    RHI_VULKAN_DECL(vkCmdSetStencilOp);
    RHI_VULKAN_DECL(vkCmdBeginRendering);
    RHI_VULKAN_DECL(vkCmdEndRendering);
    RHI_VULKAN_DECL(vkCmdSetStencilCompareMask);
    RHI_VULKAN_DECL(vkCmdSetStencilWriteMask);
    RHI_VULKAN_DECL(vkCmdClearColorImage);
    RHI_VULKAN_DECL(vkCmdClearDepthStencilImage);
    RHI_VULKAN_DECL(vkCmdSetPrimitiveTopology);
    RHI_VULKAN_DECL(vkCmdPushConstants);
    RHI_VULKAN_DECL(vkCmdBindVertexBuffers);
    RHI_VULKAN_DECL(vkCmdBindIndexBuffer);
    RHI_VULKAN_DECL(vkCmdDraw);
    RHI_VULKAN_DECL(vkCmdDrawIndexed);
    RHI_VULKAN_DECL(vkCmdBindDescriptorSets);

    RHI_VULKAN_DECL(vkQueueSubmit);
    RHI_VULKAN_DECL(vkQueueWaitIdle);
    RHI_VULKAN_DECL(vkQueuePresentKHR);

} krhi_vulkan;

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

b8 vulkan_platform_initialize(krhi_vulkan* rhi);
