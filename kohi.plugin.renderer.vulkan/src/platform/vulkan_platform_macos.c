#include "platform/platform.h"

#if defined(KPLATFORM_APPLE)

#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>

#include <containers/darray.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/platform.h>

#include "platform/vulkan_platform.h"
#include "vulkan_types.h"

// Forward declarations for Obj-C "classes" in the platform implementation.
typedef struct ContentView ContentView;
typedef struct WindowDelegate WindowDelegate;
typedef struct NSWindow NSWindow;

typedef struct macos_handle_info {
    u32 dummy;
} macos_handle_info;

typedef struct kwindow_platform_state {
    WindowDelegate* wnd_delegate;
    NSWindow* handle;
    ContentView* view;
    CAMetalLayer* layer;
    f32 device_pixel_ratio;
} kwindow_platform_state;

void vulkan_platform_get_required_extension_names(const char*** names_darray) {
    darray_push(*names_darray, &"VK_EXT_metal_surface");
    // Required for macos
    darray_push(*names_darray, &VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
}

b8 vulkan_platform_create_vulkan_surface(vulkan_context* context, struct kwindow* window) {

    VkMetalSurfaceCreateInfoEXT create_info = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    create_info.pLayer = window->platform_state->layer;

    VkResult result = vkCreateMetalSurfaceEXT(
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

b8 vulkan_platform_presentation_support(vulkan_context* context, VkPhysicalDevice physical_device, u32 queue_family_index) {
    // NOTE: According to the Vulkan spec this must always be supported for all devices.
    // https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_querying_for_wsi_support
    // 34.4.10. macOS Platform
    // On macOS, all physical devices and queue families must be capable of presentation with any layer. As a result there is no macOS-specific query for these capabilities.
    return true;
}

#endif
