
// Windows platform layer.
#if KPLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <containers/darray.h>
#include <platform/platform.h>
#include <core/kmemory.h>
#include <core/logger.h>

#include "renderer/vulkan/vulkan_types.inl"
#include "renderer/vulkan/platform/vulkan_platform.h"

typedef struct win32_handle_info {
    HINSTANCE h_instance;
    HWND hwnd;
} win32_handle_info;

void platform_get_required_extension_names(const char ***names_darray) {
    darray_push(*names_darray, &"VK_KHR_win32_surface");
}

// Surface creation for Vulkan
b8 platform_create_vulkan_surface(vulkan_context *context) {
    u64 size = 0;
    platform_get_handle_info(&size, 0);
    void *block = kallocate(size, MEMORY_TAG_RENDERER);
    platform_get_handle_info(&size, block);

    win32_handle_info *handle = (win32_handle_info *)block;

    if (!handle) {
        return false;
    }
    VkWin32SurfaceCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    create_info.hinstance = handle->h_instance;
    create_info.hwnd = handle->hwnd;

    VkResult result = vkCreateWin32SurfaceKHR(context->instance, &create_info, context->allocator, &context->surface);
    if (result != VK_SUCCESS) {
        KFATAL("Vulkan surface creation failed.");
        return false;
    }

    return true;
}

#endif