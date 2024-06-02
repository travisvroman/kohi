#include <platform/platform.h>
// Windows platform layer.
#if KPLATFORM_WINDOWS

#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#    include <vulkan/vulkan.h>
#    include <vulkan/vulkan_win32.h>

#    include <containers/darray.h>
#    include <logger.h>
#    include <memory/kmemory.h>
#    include <platform/platform.h>

#    include "platform/vulkan_platform.h"
#    include "vulkan_types.h"

typedef struct win32_handle_info {
    HINSTANCE h_instance;
    HWND hwnd;
} win32_handle_info;

void vulkan_platform_get_required_extension_names(const char*** names_darray) {
    darray_push(*names_darray, &"VK_KHR_win32_surface");
}

// Surface creation for Vulkan
b8 vulkan_platform_create_vulkan_surface(vulkan_context* context, struct kwindow* window) {
    u64 size = 0;
    platform_get_handle_info(&size, 0);
    void* block = kallocate(size, MEMORY_TAG_RENDERER);
    platform_get_handle_info(&size, block);

    win32_handle_info* handle = (win32_handle_info*)block;

    if (!handle) {
        return false;
    }
    VkWin32SurfaceCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    create_info.hinstance = handle->h_instance;
    create_info.hwnd = handle->hwnd;

    VkResult result = vkCreateWin32SurfaceKHR(
        context->instance,
        &create_info,
        context->allocator,
        &window->renderer_state->backend_state->surface);
    if (result != VK_SUCCESS) {
        KFATAL("Vulkan surface creation failed.");
        return false;
    }

    return true;
}

#endif
