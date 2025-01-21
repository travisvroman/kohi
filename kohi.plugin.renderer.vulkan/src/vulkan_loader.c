#include "vulkan_loader.h"
#include "platform/vulkan_platform.h"

#include <vulkan/vulkan.h>

#define RHI_FUNCTION(name) rhi->k##name = (PFN_##name)platform_dynamic_library_load_function(#name, &rhi->vulkan_lib)
#define RHI_INSTANCE_FUNCTION(name) rhi->k##name = (PFN_##name)rhi->kvkGetInstanceProcAddr(rhi->instance, #name)
#define RHI_DEVICE_FUNCTION(name) rhi->k##name = (PFN_##name)rhi->kvkGetDeviceProcAddr(rhi->device, #name)

b8 vulkan_loader_initialize(krhi_vulkan* rhi) {
    return vulkan_platform_initialize(rhi);
}

b8 vulkan_loader_load_core(krhi_vulkan* rhi) {
    if (!rhi) {
        return false;
    }

    // Core functions.
    RHI_FUNCTION(vkGetInstanceProcAddr);
    RHI_FUNCTION(vkEnumerateInstanceVersion);
    RHI_FUNCTION(vkEnumerateInstanceExtensionProperties);
    RHI_FUNCTION(vkEnumerateInstanceLayerProperties);
    RHI_FUNCTION(vkCreateInstance);

    return rhi->kvkGetInstanceProcAddr != 0;
}

b8 vulkan_loader_load_instance(krhi_vulkan* rhi, VkInstance instance) {
    if (!rhi) {
        return false;
    }

    RHI_INSTANCE_FUNCTION(vkGetDeviceProcAddr);
    RHI_INSTANCE_FUNCTION(vkDestroyInstance);
    RHI_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties2);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures2);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);
    RHI_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR);
    RHI_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties);
    RHI_INSTANCE_FUNCTION(vkCreateDevice);
    RHI_INSTANCE_FUNCTION(vkDestroySurfaceKHR);

    return true;
}
b8 vulkan_loader_load_device(krhi_vulkan* rhi, VkDevice device) {
    if (!rhi) {
        return false;
    }

    RHI_DEVICE_FUNCTION(vkGetDeviceQueue);
    RHI_DEVICE_FUNCTION(vkDeviceWaitIdle);
    RHI_DEVICE_FUNCTION(vkCreateCommandPool);
    RHI_DEVICE_FUNCTION(vkDestroyCommandPool);
    RHI_DEVICE_FUNCTION(vkDestroyDevice);
    RHI_DEVICE_FUNCTION(vkCreateSwapchainKHR);
    RHI_DEVICE_FUNCTION(vkDestroySwapchainKHR);
    RHI_DEVICE_FUNCTION(vkGetSwapchainImagesKHR);
    RHI_DEVICE_FUNCTION(vkCreateImage);
    RHI_DEVICE_FUNCTION(vkCreateImageView);
    RHI_DEVICE_FUNCTION(vkDestroyImage);
    RHI_DEVICE_FUNCTION(vkDestroyImageView);
    RHI_DEVICE_FUNCTION(vkGetImageMemoryRequirements);
    RHI_DEVICE_FUNCTION(vkAllocateMemory);
    RHI_DEVICE_FUNCTION(vkFreeMemory);
    RHI_DEVICE_FUNCTION(vkAllocateCommandBuffers);
    RHI_DEVICE_FUNCTION(vkFreeCommandBuffers);
    RHI_DEVICE_FUNCTION(vkBeginCommandBuffer);
    RHI_DEVICE_FUNCTION(vkEndCommandBuffer);
    RHI_DEVICE_FUNCTION(vkBindImageMemory);
    RHI_DEVICE_FUNCTION(vkCreateSemaphore);
    RHI_DEVICE_FUNCTION(vkDestroySemaphore);
    RHI_DEVICE_FUNCTION(vkCreateFence);
    RHI_DEVICE_FUNCTION(vkDestroyFence);
    RHI_DEVICE_FUNCTION(vkWaitForFences);
    RHI_DEVICE_FUNCTION(vkAcquireNextImageKHR);
    RHI_DEVICE_FUNCTION(vkResetFences);
    RHI_DEVICE_FUNCTION(vkCreateDescriptorSetLayout);
    RHI_DEVICE_FUNCTION(vkDestroyDescriptorSetLayout);
    RHI_DEVICE_FUNCTION(vkCreateDescriptorPool);
    RHI_DEVICE_FUNCTION(vkDestroyDescriptorPool);
    RHI_DEVICE_FUNCTION(vkCreateShaderModule);
    RHI_DEVICE_FUNCTION(vkDestroyShaderModule);
    RHI_DEVICE_FUNCTION(vkCreateSampler);
    RHI_DEVICE_FUNCTION(vkDestroySampler);
    RHI_DEVICE_FUNCTION(vkCreateBuffer);
    RHI_DEVICE_FUNCTION(vkDestroyBuffer);
    RHI_DEVICE_FUNCTION(vkGetBufferMemoryRequirements);
    RHI_DEVICE_FUNCTION(vkBindBufferMemory);
    RHI_DEVICE_FUNCTION(vkMapMemory);
    RHI_DEVICE_FUNCTION(vkUnmapMemory);
    RHI_DEVICE_FUNCTION(vkFlushMappedMemoryRanges);
    RHI_DEVICE_FUNCTION(vkCreatePipelineLayout);
    RHI_DEVICE_FUNCTION(vkDestroyPipelineLayout);
    RHI_DEVICE_FUNCTION(vkCreateGraphicsPipelines);
    RHI_DEVICE_FUNCTION(vkDestroyPipeline);
    RHI_DEVICE_FUNCTION(vkCmdBindPipeline);
    RHI_DEVICE_FUNCTION(vkAllocateDescriptorSets);
    RHI_DEVICE_FUNCTION(vkFreeDescriptorSets);
    RHI_DEVICE_FUNCTION(vkUpdateDescriptorSets);

    RHI_DEVICE_FUNCTION(vkCmdPipelineBarrier);
    RHI_DEVICE_FUNCTION(vkCmdBlitImage);
    RHI_DEVICE_FUNCTION(vkCmdCopyBuffer);
    RHI_DEVICE_FUNCTION(vkCmdCopyBufferToImage);
    RHI_DEVICE_FUNCTION(vkCmdCopyImageToBuffer);
    RHI_DEVICE_FUNCTION(vkCmdExecuteCommands);
    RHI_DEVICE_FUNCTION(vkCmdSetViewport);
    RHI_DEVICE_FUNCTION(vkCmdSetScissor);
    RHI_DEVICE_FUNCTION(vkCmdSetFrontFace);
    RHI_DEVICE_FUNCTION(vkCmdSetCullMode);
    RHI_DEVICE_FUNCTION(vkCmdSetStencilTestEnable);
    RHI_DEVICE_FUNCTION(vkCmdSetDepthTestEnable);
    RHI_DEVICE_FUNCTION(vkCmdSetDepthWriteEnable);
    RHI_DEVICE_FUNCTION(vkCmdSetStencilReference);
    RHI_DEVICE_FUNCTION(vkCmdSetStencilOp);
    RHI_DEVICE_FUNCTION(vkCmdBeginRendering);
    RHI_DEVICE_FUNCTION(vkCmdEndRendering);
    RHI_DEVICE_FUNCTION(vkCmdSetStencilCompareMask);
    RHI_DEVICE_FUNCTION(vkCmdSetStencilWriteMask);
    RHI_DEVICE_FUNCTION(vkCmdClearColorImage);
    RHI_DEVICE_FUNCTION(vkCmdClearDepthStencilImage);
    RHI_DEVICE_FUNCTION(vkCmdSetPrimitiveTopology);
    RHI_DEVICE_FUNCTION(vkCmdPushConstants);
    RHI_DEVICE_FUNCTION(vkCmdBindVertexBuffers);
    RHI_DEVICE_FUNCTION(vkCmdBindIndexBuffer);
    RHI_DEVICE_FUNCTION(vkCmdDraw);
    RHI_DEVICE_FUNCTION(vkCmdDrawIndexed);
    RHI_DEVICE_FUNCTION(vkCmdBindDescriptorSets);

    RHI_DEVICE_FUNCTION(vkQueueSubmit);
    RHI_DEVICE_FUNCTION(vkQueueWaitIdle);
    RHI_DEVICE_FUNCTION(vkQueuePresentKHR);

    return true;
}
