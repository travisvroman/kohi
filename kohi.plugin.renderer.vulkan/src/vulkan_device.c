#include "vulkan_device.h"

#include "containers/darray.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/platform.h"
#include "platform/vulkan_platform.h"
#include "strings/kstring.h"
#include "vulkan/vulkan_core.h"
#include "vulkan_types.h"
#include "vulkan_utils.h"

typedef struct vulkan_physical_device_requirements {
    b8 graphics;
    b8 present;
    b8 compute;
    b8 transfer;
    // darray
    const char** device_extension_names;
    b8 sampler_anisotropy;
    b8 discrete_gpu;
} vulkan_physical_device_requirements;

typedef struct vulkan_physical_device_queue_family_info {
    i32 graphics_family_index;
    i32 present_family_index;
    i32 compute_family_index;
    i32 transfer_family_index;
} vulkan_physical_device_queue_family_info;

static b8 select_physical_device(vulkan_context* context);
static b8 physical_device_meets_requirements(
    vulkan_context* context,
    VkPhysicalDevice device,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_family_info,
    vulkan_swapchain_support_info* out_swapchain_support);

b8 vulkan_device_create(vulkan_context* context) {
    if (!select_physical_device(context)) {
        return false;
    }

    KINFO("Creating logical device...");
    // NOTE: Do not create additional queues for shared indices.
    b8 present_shares_graphics_queue = context->device.graphics_queue_index == context->device.present_queue_index;
    b8 transfer_shares_graphics_queue = context->device.graphics_queue_index == context->device.transfer_queue_index;
    b8 present_must_share_graphics = false;
    u32 index_count = 1;
    if (!present_shares_graphics_queue) {
        index_count++;
    }
    if (!transfer_shares_graphics_queue) {
        index_count++;
    }
    i32 indices[32];
    u8 index = 0;
    indices[index++] = context->device.graphics_queue_index;
    if (!present_shares_graphics_queue) {
        indices[index++] = context->device.present_queue_index;
    }
    if (!transfer_shares_graphics_queue) {
        indices[index++] = context->device.transfer_queue_index;
    }

    VkDeviceQueueCreateInfo queue_create_infos[32];
    f32 queue_priorities[2] = {0.9f, 1.0f};

    VkQueueFamilyProperties props[32];
    u32 prop_count;
    vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical_device, &prop_count, 0);
    vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical_device, &prop_count, props);

    for (u32 i = 0; i < index_count; ++i) {
        queue_create_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[i].queueFamilyIndex = indices[i];
        queue_create_infos[i].queueCount = 1;

        if (present_shares_graphics_queue && indices[i] == context->device.present_queue_index) {
            if (props[context->device.present_queue_index].queueCount > 1) {
                // If the same family is shared between graphic and presentation,
                // pull from the second index instead of the first for a unique queue.
                queue_create_infos[i].queueCount = 2;
            } else {
                // Don't have available queues, just share them.
                present_must_share_graphics = true;
            }
        }

        // TODO: Enable this for a future enhancement.
        // if (indices[i] == context->device.graphics_queue_index) {
        //     queue_create_infos[i].queueCount = 2;
        // }
        queue_create_infos[i].flags = 0;
        queue_create_infos[i].pNext = 0;
        queue_create_infos[i].pQueuePriorities = queue_priorities;
    }

    b8 portability_required = false;
    u32 available_extension_count = 0;
    VkExtensionProperties* available_extensions = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(context->device.physical_device, 0, &available_extension_count, 0));
    if (available_extension_count != 0) {
        available_extensions = kallocate(sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
        VK_CHECK(vkEnumerateDeviceExtensionProperties(context->device.physical_device, 0, &available_extension_count, available_extensions));
        for (u32 i = 0; i < available_extension_count; ++i) {
            if (strings_equal(available_extensions[i].extensionName, "VK_KHR_portability_subset")) {
                KINFO("Adding required extension 'VK_KHR_portability_subset'.");
                portability_required = true;
                break;
            }
        }
    }
    kfree(available_extensions, sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);

    // Setup an array large enough to hold all, even if we don't use them all.
    const char* extension_names[6];
    u32 ext_idx = 0;
    extension_names[ext_idx] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    ext_idx++;

    // Dynamic indexing. NOTE: not needed for 1.2+
    /* extension_names[ext_idx] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
    ext_idx++; */

    // If portability is required (i.e. mac), add it.
    if (portability_required) {
        extension_names[ext_idx] = "VK_KHR_portability_subset";
        ext_idx++;
    }

    // If dynamic topology isn't supported natively but *is* supported via extension,
    // include the extension.
    if (
        ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) == 0) &&
        ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) != 0)) {
        extension_names[ext_idx] = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
        ext_idx++;
        extension_names[ext_idx] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
        ext_idx++;
    }
    // If smooth lines are supported, load the extension.
    if ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT)) {
        extension_names[ext_idx] = VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME;
        ext_idx++;
    }

    // Request supported device features.
    VkPhysicalDeviceFeatures device_features = {};
    device_features.samplerAnisotropy = context->device.features.samplerAnisotropy; // Request anistrophy
    device_features.fillModeNonSolid = context->device.features.fillModeNonSolid;

    // VK_EXT_descriptor_indexing
    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT};
    // Partial binding is required for descriptor aliasing.
    descriptor_indexing_features.descriptorBindingPartiallyBound = VK_TRUE; // TODO: Check if supported?

#if defined(VK_USE_PLATFORM_MACOS_MVK)
    // NOTE: On macOS set environment variable to configure MoltenVK for using Metal argument buffers (needed for descriptor indexing).
    //     - MoltenVK supports Metal argument buffers on macOS, iOS possible in future (see https://github.com/KhronosGroup/MoltenVK/issues/1651)
    setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif

    // VK_EXT_extended_dynamic_state
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    extended_dynamic_state.extendedDynamicState = VK_TRUE;
    descriptor_indexing_features.pNext = &extended_dynamic_state;

    // Smooth line rasterisation, if supported.
    VkPhysicalDeviceLineRasterizationFeaturesEXT line_rasterization_ext = {0};
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) {
        line_rasterization_ext.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
        line_rasterization_ext.smoothLines = VK_TRUE;
        extended_dynamic_state.pNext = &line_rasterization_ext;
    }

    // Dynamic rendering.
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dynamic_rendering_ext.dynamicRendering = true;
    line_rasterization_ext.pNext = &dynamic_rendering_ext;

    VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.queueCreateInfoCount = index_count;
    device_create_info.pQueueCreateInfos = queue_create_infos;
    device_create_info.pEnabledFeatures = &device_features;
    device_create_info.enabledExtensionCount = ext_idx;
    device_create_info.ppEnabledExtensionNames = extension_names;

    // Deprecated and ignored, so pass nothing.
    device_create_info.enabledLayerCount = 0;
    device_create_info.ppEnabledLayerNames = 0;
    device_create_info.pNext = &descriptor_indexing_features;

    // Create the device.
    VK_CHECK(vkCreateDevice(
        context->device.physical_device,
        &device_create_info,
        context->allocator,
        &context->device.logical_device));

    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DEVICE, context->device.logical_device, "Vulkan Logical Device");

    KINFO("Logical device created.");

    // Examine dynamic state support and load function pointer if need be.
    if (
        !(context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) &&
        (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT)) {
        KINFO("Vulkan device doesn't support native dynamic state, but does via extension. Using extension.");

        // Dynamic primitive topology.
        context->vkCmdSetPrimitiveTopologyEXT = (PFN_vkCmdSetPrimitiveTopologyEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetPrimitiveTopologyEXT");

        // Dynamic front-cace
        context->vkCmdSetFrontFaceEXT = (PFN_vkCmdSetFrontFaceEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetFrontFaceEXT");

        // Dynamic depth/stencil state
        context->vkCmdSetStencilOpEXT = (PFN_vkCmdSetStencilOpEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetStencilOpEXT");
        context->vkCmdSetStencilTestEnableEXT = (PFN_vkCmdSetStencilTestEnableEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetStencilTestEnableEXT");
        context->vkCmdSetDepthTestEnableEXT = (PFN_vkCmdSetDepthTestEnableEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetDepthTestEnableEXT");
        context->vkCmdSetDepthWriteEnableEXT = (PFN_vkCmdSetDepthWriteEnableEXT)vkGetInstanceProcAddr(context->instance, "vkCmdSetDepthWriteEnableEXT");

        // Dynamic rendering
        context->vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetInstanceProcAddr(context->instance, "vkCmdBeginRenderingKHR");
        context->vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetInstanceProcAddr(context->instance, "vkCmdEndRenderingKHR");
    } else {
        if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
            KINFO("Vulkan device supports native dynamic state and dynamic rendering.");
        } else {
            KWARN("Vulkan device does not support native or extension dynamic state. This may cause issues with the renderer.");
        }
    }

    // Get queues.
    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.graphics_queue_index,
        0,
        &context->device.graphics_queue);

    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.present_queue_index,
        // If the same family is shared between graphic and presentation,
        // pull from the second index instead of the first for a unique queue.
        present_must_share_graphics ? 0 : (context->device.graphics_queue_index == context->device.present_queue_index) ? 1
                                                                                                                        : 0,
        &context->device.present_queue);

    vkGetDeviceQueue(
        context->device.logical_device,
        context->device.transfer_queue_index,
        0,
        &context->device.transfer_queue);
    KINFO("Queues obtained.");

    // Create command pool for graphics queue.
    VkCommandPoolCreateInfo pool_create_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_create_info.queueFamilyIndex = context->device.graphics_queue_index;
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(
        context->device.logical_device,
        &pool_create_info,
        context->allocator,
        &context->device.graphics_command_pool));
    KINFO("Graphics command pool created.");

    return true;
}

void vulkan_device_destroy(vulkan_context* context) {
    // Unset queues
    context->device.graphics_queue = 0;
    context->device.present_queue = 0;
    context->device.transfer_queue = 0;

    KINFO("Destroying command pools...");
    vkDestroyCommandPool(
        context->device.logical_device,
        context->device.graphics_command_pool,
        context->allocator);

    // Destroy logical device
    KINFO("Destroying logical device...");
    if (context->device.logical_device) {
        vkDestroyDevice(context->device.logical_device, context->allocator);
        context->device.logical_device = 0;
    }

    // Physical devices are not destroyed.
    KINFO("Releasing physical device resources...");
    context->device.physical_device = 0;

    if (context->device.swapchain_support.formats) {
        kfree(
            context->device.swapchain_support.formats,
            sizeof(VkSurfaceFormatKHR) * context->device.swapchain_support.format_count,
            MEMORY_TAG_RENDERER);
        context->device.swapchain_support.formats = 0;
        context->device.swapchain_support.format_count = 0;
    }

    if (context->device.swapchain_support.present_modes) {
        kfree(
            context->device.swapchain_support.present_modes,
            sizeof(VkPresentModeKHR) * context->device.swapchain_support.present_mode_count,
            MEMORY_TAG_RENDERER);
        context->device.swapchain_support.present_modes = 0;
        context->device.swapchain_support.present_mode_count = 0;
    }

    kzero_memory(
        &context->device.swapchain_support.capabilities,
        sizeof(context->device.swapchain_support.capabilities));

    context->device.graphics_queue_index = -1;
    context->device.present_queue_index = -1;
    context->device.transfer_queue_index = -1;
}

void vulkan_device_query_swapchain_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    vulkan_swapchain_support_info* out_support_info) {
    // Surface capabilities
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &out_support_info->capabilities);
    if (!vulkan_result_is_success(result)) {
        KFATAL("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with message: %s", vulkan_result_string(result, true));
    }

    // Surface formats
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device,
        surface,
        &out_support_info->format_count,
        0));

    if (out_support_info->format_count != 0) {
        if (!out_support_info->formats) {
            out_support_info->formats = kallocate(sizeof(VkSurfaceFormatKHR) * out_support_info->format_count, MEMORY_TAG_RENDERER);
        }
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical_device,
            surface,
            &out_support_info->format_count,
            out_support_info->formats));
    }

    // Present modes
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device,
        surface,
        &out_support_info->present_mode_count,
        0));
    if (out_support_info->present_mode_count != 0) {
        if (!out_support_info->present_modes) {
            out_support_info->present_modes = kallocate(sizeof(VkPresentModeKHR) * out_support_info->present_mode_count, MEMORY_TAG_RENDERER);
        }
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical_device,
            surface,
            &out_support_info->present_mode_count,
            out_support_info->present_modes));
    }
}

b8 vulkan_device_detect_depth_format(vulkan_device* device) {
    // Format candidates
    const u64 candidate_count = 2;
    VkFormat candidates[2] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT};

    u8 sizes[2] = {
        4,
        3};

    u32 flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (u64 i = 0; i < candidate_count; ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(device->physical_device, candidates[i], &properties);

        if ((properties.linearTilingFeatures & flags) == flags) {
            device->depth_format = candidates[i];
            device->depth_channel_count = sizes[i];
            return true;
        } else if ((properties.optimalTilingFeatures & flags) == flags) {
            device->depth_format = candidates[i];
            device->depth_channel_count = sizes[i];
            return true;
        }
    }

    return false;
}

static b8 select_physical_device(vulkan_context* context) {
    u32 physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(context->instance, &physical_device_count, 0));
    if (physical_device_count == 0) {
        KFATAL("No devices which support Vulkan were found.");
        return false;
    }

    // Setup requirements
    // TODO: These requirements should probably be driven by engine
    // configuration.
    vulkan_physical_device_requirements requirements = {};
    requirements.graphics = true;
    requirements.present = true;
    requirements.transfer = true;
    // NOTE: Enable this if compute will be required.
    // requirements.compute = true;
    requirements.sampler_anisotropy = true;
#if KPLATFORM_APPLE
    requirements.discrete_gpu = false;
#else
    requirements.discrete_gpu = true;
#endif
    requirements.device_extension_names = darray_create(const char*);
    darray_push(requirements.device_extension_names, &VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Iterate physical devices to find one that fits the bill.
    VkPhysicalDevice physical_devices[32];
    VK_CHECK(vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices));
    for (u32 i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDeviceDriverProperties driverProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        properties2.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(physical_devices[i], &properties2);
        VkPhysicalDeviceProperties properties = properties2.properties;

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);

        VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        // Check for dynamic topology support via extension.
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        features2.pNext = &dynamic_state_next;
        // Check for smooth line rasterisation support via extension.
        VkPhysicalDeviceLineRasterizationFeaturesEXT smooth_line_next = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT};
        dynamic_state_next.pNext = &smooth_line_next;
        // Perform the query.
        vkGetPhysicalDeviceFeatures2(physical_devices[i], &features2);

        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(physical_devices[i], &memory);

        KINFO("Evaluating device: '%s', index %u.", properties.deviceName, i);

        // Check if device supports local/host visible combo
        b8 supports_device_local_host_visible = false;
        for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
            // Check each memory type to see if its bit is set to 1.
            if (
                ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) &&
                ((memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)) {
                supports_device_local_host_visible = true;
                break;
            }
        }

        vulkan_physical_device_queue_family_info queue_info = {};
        b8 result = physical_device_meets_requirements(
            context,
            physical_devices[i],
            &properties,
            &features,
            &requirements,
            &queue_info,
            &context->device.swapchain_support);

        if (result) {
            KINFO("Selected device: '%s'.", properties.deviceName);
            // GPU type, etc.
            switch (properties.deviceType) {
            default:
            case VK_PHYSICAL_DEVICE_TYPE_OTHER:
                KINFO("GPU type is Unknown.");
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                KINFO("GPU type is Integrated.");
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                KINFO("GPU type is Descrete.");
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                KINFO("GPU type is Virtual.");
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                KINFO("GPU type is CPU.");
                break;
            }

            KINFO("GPU Driver version: %s", driverProperties.driverInfo);

            // Save off the device-supported API version.
            context->device.api_major = VK_VERSION_MAJOR(properties.apiVersion);
            context->device.api_minor = VK_VERSION_MINOR(properties.apiVersion);
            context->device.api_patch = VK_VERSION_PATCH(properties.apiVersion);

            // Vulkan API version.
            KINFO(
                "Vulkan API version: %d.%d.%d",
                context->device.api_major,
                context->device.api_minor,
                context->device.api_minor);

            // Memory information
            for (u32 j = 0; j < memory.memoryHeapCount; ++j) {
                f32 memory_size_gib = (((f32)memory.memoryHeaps[j].size) / 1024.0f / 1024.0f / 1024.0f);
                if (memory.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                    KINFO("Local GPU memory: %.2f GiB", memory_size_gib);
                } else {
                    KINFO("Shared System memory: %.2f GiB", memory_size_gib);
                }
            }

            context->device.physical_device = physical_devices[i];
            context->device.graphics_queue_index = queue_info.graphics_family_index;
            context->device.present_queue_index = queue_info.present_family_index;
            context->device.transfer_queue_index = queue_info.transfer_family_index;
            // NOTE: set compute index here if needed.

            // Keep a copy of properties, features and memory info for later use.
            context->device.properties = properties;
            context->device.features = features;
            context->device.memory = memory;
            context->device.supports_device_local_host_visible = supports_device_local_host_visible;

            // The device may or may not support dynamic state, so save that here.
            if (context->device.api_major >= 1 && context->device.api_minor > 2) {
                context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT;
            }
            // If not supported natively, it might be supported via extension.
            if (dynamic_state_next.extendedDynamicState) {
                context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT;
            }
            // Check for smooth line rasterization support.
            if (smooth_line_next.smoothLines) {
                context->device.support_flags |= VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT;
            }
            break;
        }
    }

    // Clean up requirements.
    darray_destroy(requirements.device_extension_names);

    // Ensure a device was selected
    if (!context->device.physical_device) {
        KERROR("No physical devices were found which meet the requirements.");
        return false;
    }

    KINFO("Physical device selected.");
    return true;
}

static b8 physical_device_meets_requirements(
    vulkan_context* context,
    VkPhysicalDevice device,
    const VkPhysicalDeviceProperties* properties,
    const VkPhysicalDeviceFeatures* features,
    const vulkan_physical_device_requirements* requirements,
    vulkan_physical_device_queue_family_info* out_queue_info,
    vulkan_swapchain_support_info* out_swapchain_support) {
    // Evaluate device properties to determine if it meets the needs of our applcation.
    out_queue_info->graphics_family_index = -1;
    out_queue_info->present_family_index = -1;
    out_queue_info->compute_family_index = -1;
    out_queue_info->transfer_family_index = -1;

    // Discrete GPU?
    if (requirements->discrete_gpu) {
        if (properties->deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            KINFO("Device is not a discrete GPU, and one is required. Skipping.");
            return false;
        }
    }

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0);
    VkQueueFamilyProperties queue_families[32];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    // Look at each queue and see what queues it supports
    KINFO("Graphics | Present | Compute | Transfer | Name");
    u8 min_transfer_score = 255;
    for (u32 i = 0; i < queue_family_count; ++i) {
        u8 current_transfer_score = 0;

        // Graphics queue?
        if (out_queue_info->graphics_family_index == -1 && queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out_queue_info->graphics_family_index = i;
            ++current_transfer_score;

            // If also a present queue, this prioritizes grouping of the 2.
            b8 supports_present = vulkan_platform_presentation_support(context, device, i);
            if (supports_present) {
                out_queue_info->present_family_index = i;
                ++current_transfer_score;
            }
        }

        // Compute queue?
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            out_queue_info->compute_family_index = i;
            ++current_transfer_score;
        }

        // Transfer queue?
        if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            // Take the index if it is the current lowest. This increases the
            // liklihood that it is a dedicated transfer queue.
            if (current_transfer_score <= min_transfer_score) {
                min_transfer_score = current_transfer_score;
                out_queue_info->transfer_family_index = i;
            }
        }
    }

    // If a present queue hasn't been found, iterate again and take the first one.
    // This should only happen if there is a queue that supports graphics but NOT
    // present.
    if (out_queue_info->present_family_index == -1) {
        for (u32 i = 0; i < queue_family_count; ++i) {
            b8 supports_present = vulkan_platform_presentation_support(context, device, i);
            if (supports_present) {
                out_queue_info->present_family_index = i;

                // If they differ, bleat about it and move on. This is just here for troubleshooting
                // purposes.
                if (out_queue_info->present_family_index != out_queue_info->graphics_family_index) {
                    KWARN("Warning: Different queue index used for present vs graphics: %u.", i);
                }
                break;
            }
        }
    }

    // Print out some info about the device
    KINFO("       %d |       %d |       %d |        %d | %s",
          out_queue_info->graphics_family_index != -1,
          out_queue_info->present_family_index != -1,
          out_queue_info->compute_family_index != -1,
          out_queue_info->transfer_family_index != -1,
          properties->deviceName);

    if (
        (!requirements->graphics || (requirements->graphics && out_queue_info->graphics_family_index != -1)) &&
        (!requirements->present || (requirements->present && out_queue_info->present_family_index != -1)) &&
        (!requirements->compute || (requirements->compute && out_queue_info->compute_family_index != -1)) &&
        (!requirements->transfer || (requirements->transfer && out_queue_info->transfer_family_index != -1))) {
        KINFO("Device meets queue requirements.");
        KTRACE("Graphics Family Index: %i", out_queue_info->graphics_family_index);
        KTRACE("Present Family Index:  %i", out_queue_info->present_family_index);
        KTRACE("Transfer Family Index: %i", out_queue_info->transfer_family_index);
        KTRACE("Compute Family Index:  %i", out_queue_info->compute_family_index);

        // Device extensions.
        if (requirements->device_extension_names) {
            u32 available_extension_count = 0;
            VkExtensionProperties* available_extensions = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(
                device,
                0,
                &available_extension_count,
                0));
            if (available_extension_count != 0) {
                available_extensions = kallocate(sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
                VK_CHECK(vkEnumerateDeviceExtensionProperties(
                    device,
                    0,
                    &available_extension_count,
                    available_extensions));

                u32 required_extension_count = darray_length(requirements->device_extension_names);
                for (u32 i = 0; i < required_extension_count; ++i) {
                    b8 found = false;
                    for (u32 j = 0; j < available_extension_count; ++j) {
                        if (strings_equal(requirements->device_extension_names[i], available_extensions[j].extensionName)) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        KINFO("Required extension not found: '%s', skipping device.", requirements->device_extension_names[i]);
                        kfree(available_extensions, sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
                        return false;
                    }
                }
            }
            kfree(available_extensions, sizeof(VkExtensionProperties) * available_extension_count, MEMORY_TAG_RENDERER);
        }

        // Sampler anisotropy
        if (requirements->sampler_anisotropy && !features->samplerAnisotropy) {
            KINFO("Device does not support samplerAnisotropy, skipping.");
            return false;
        }

        // Device meets all requirements.
        return true;
    }

    return false;
}
