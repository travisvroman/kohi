#include "platform/platform.h"

#if defined(KPLATFORM_APPLE)

#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>

#include <containers/darray.h>
#include <platform/platform.h>
#include <core/kmemory.h>
#include <core/logger.h>

#include "renderer/vulkan/vulkan_types.h"
#include "renderer/vulkan/platform/vulkan_platform.h"

typedef struct macos_handle_info {
    CAMetalLayer* layer;
} macos_handle_info;


void platform_get_required_extension_names(const char ***names_darray) {
    darray_push(*names_darray, &"VK_EXT_metal_surface");
    // Required for macos
    darray_push(*names_darray, &VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
}

b8 platform_create_vulkan_surface(vulkan_context *context) {
    u64 size = 0;
    platform_get_handle_info(&size, 0);
    void *block = kallocate(size, MEMORY_TAG_RENDERER);
    platform_get_handle_info(&size, block);


    VkMetalSurfaceCreateInfoEXT create_info = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    create_info.pLayer = ((macos_handle_info*)block)->layer;

    VkResult result = vkCreateMetalSurfaceEXT(
        context->instance, 
        &create_info,
        context->allocator,
        &context->surface);
    if (result != VK_SUCCESS) {
        KFATAL("Vulkan surface creation failed.");
        return false;
    }

    return true;
}

#endif
