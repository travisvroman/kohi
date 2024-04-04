#include <platform/platform.h>

// Linux platform layer.
#if KPLATFORM_LINUX

#include <xcb/xcb.h>

// For surface creation
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include <containers/darray.h>
#include <kmemory.h>
#include <logger.h>

#include "renderer/vulkan/vulkan_types.h"
#include "renderer/vulkan/platform/vulkan_platform.h"


typedef struct linux_handle_info {
    xcb_connection_t* connection;
    xcb_window_t window;
} linux_handle_info;

void platform_get_required_extension_names(const char*** names_darray) {
    darray_push(*names_darray, &"VK_KHR_xcb_surface");  // VK_KHR_xlib_surface?
}

// Surface creation for Vulkan
b8 platform_create_vulkan_surface(vulkan_context* context) {
    u64 size = 0;
    platform_get_handle_info(&size, 0);
    void *block = kallocate(size, MEMORY_TAG_RENDERER);
    platform_get_handle_info(&size, block);

    linux_handle_info* handle = (linux_handle_info*)block;

    VkXcbSurfaceCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
    create_info.connection = handle->connection;
    create_info.window = handle->window;

    VkResult result = vkCreateXcbSurfaceKHR(
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
