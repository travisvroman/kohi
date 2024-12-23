#include "vulkan_swapchain.h"

#include <vulkan/vulkan_core.h>

#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/resource_types.h>
#include <strings/kstring.h>
#include <vulkan_device.h>
#include <vulkan_types.h>
#include <vulkan_utils.h>

static b8 create(renderer_backend_interface* backend, kwindow* window, renderer_config_flags flags, vulkan_swapchain* swapchain);
static void destroy(renderer_backend_interface* backend, vulkan_swapchain* swapchain);

b8 vulkan_swapchain_create(
    struct renderer_backend_interface* backend,
    kwindow* window,
    renderer_config_flags flags,
    vulkan_swapchain* out_swapchain) {
    // Simply create a new one.
    return create(backend, window, flags, out_swapchain);
}

b8 vulkan_swapchain_recreate(
    struct renderer_backend_interface* backend,
    kwindow* window,
    vulkan_swapchain* swapchain) {
    // Destroy the old and create a new one.
    destroy(backend, swapchain);
    return create(backend, window, swapchain->flags, swapchain);
}

void vulkan_swapchain_destroy(renderer_backend_interface* backend, vulkan_swapchain* swapchain) {
    destroy(backend, swapchain);
}

static b8 create(renderer_backend_interface* backend, kwindow* window, renderer_config_flags flags, vulkan_swapchain* swapchain) {
    vulkan_context* context = backend->internal_context;
    kwindow_renderer_state* window_internal = window->renderer_state;
    kwindow_renderer_backend_state* window_backend = window_internal->backend_state;

    VkExtent2D swapchain_extent = {window->width, window->height};

    // Requery swapchain support.
    vulkan_device_query_swapchain_support(
        context->device.physical_device,
        window_backend->surface,
        &context->device.swapchain_support);

    // Choose a swap surface format.
    b8 found = false;
    for (u32 i = 0; i < context->device.swapchain_support.format_count; ++i) {
        VkSurfaceFormatKHR format = context->device.swapchain_support.formats[i];
        // Preferred formats
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchain->image_format = format;
            found = true;
            break;
        }
    }

    if (!found) {
        swapchain->image_format = context->device.swapchain_support.formats[0];
    }

    // Query swapchain image format properties to see if it can be a src/destination for blitting.
    VkFormatProperties format_properties = {0};
    vkGetPhysicalDeviceFormatProperties(context->device.physical_device, swapchain->image_format.format, &format_properties);
    swapchain->supports_blit_dest = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
    swapchain->supports_blit_src = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
    KDEBUG("Swapchain image format %s be a blit destination.", swapchain->supports_blit_dest ? "CAN" : "CANNOT");
    KDEBUG("Swapchain image format %s be a blit source.", swapchain->supports_blit_src ? "CAN" : "CANNOT");

    // FIFO and MAILBOX support vsync, IMMEDIATE does not.
    // TODO: vsync seems to hold up the game update for some reason.
    // It theoretically should be post-update and pre-render where that happens.
    swapchain->flags = flags;
    VkPresentModeKHR present_mode;
    if (flags & RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        // Only try for mailbox mode if not in power-saving mode.
        if ((flags & RENDERER_CONFIG_FLAG_POWER_SAVING_BIT) == 0) {
            for (u32 i = 0; i < context->device.swapchain_support.present_mode_count; ++i) {
                VkPresentModeKHR mode = context->device.swapchain_support.present_modes[i];
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    present_mode = mode;
                    break;
                }
            }
        }
    } else {
        present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    vulkan_swapchain_support_info* swapchain_support = &context->device.swapchain_support;
    if (swapchain_support->format_count < 1 || swapchain_support->present_mode_count < 1) {
        if (swapchain_support->formats) {
            kfree(swapchain_support->formats, sizeof(VkSurfaceFormatKHR) * swapchain_support->format_count, MEMORY_TAG_RENDERER);
        }
        if (swapchain_support->present_modes) {
            kfree(swapchain_support->present_modes, sizeof(VkPresentModeKHR) * swapchain_support->present_mode_count, MEMORY_TAG_RENDERER);
        }
        KINFO("Required swapchain support not present, skipping device.");
        return false;
    }

    // Swapchain extent
    if (context->device.swapchain_support.capabilities.currentExtent.width != U32_MAX) {
        swapchain_extent = context->device.swapchain_support.capabilities.currentExtent;
    }

    // Clamp to the value allowed by the GPU.
    VkExtent2D min = context->device.swapchain_support.capabilities.minImageExtent;
    VkExtent2D max = context->device.swapchain_support.capabilities.maxImageExtent;
    swapchain_extent.width = KCLAMP(swapchain_extent.width, min.width, max.width);
    swapchain_extent.height = KCLAMP(swapchain_extent.height, min.height, max.height);

    u32 image_count = context->device.swapchain_support.capabilities.minImageCount + 1;
    if (context->device.swapchain_support.capabilities.maxImageCount > 0 && image_count > context->device.swapchain_support.capabilities.maxImageCount) {
        image_count = context->device.swapchain_support.capabilities.maxImageCount;
    }

    swapchain->max_frames_in_flight = image_count - 1;

    // Swapchain create info
    VkSwapchainCreateInfoKHR swapchain_create_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_create_info.surface = window_backend->surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = swapchain->image_format.format;
    swapchain_create_info.imageColorSpace = swapchain->image_format.colorSpace;
    swapchain_create_info.imageExtent = swapchain_extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Setup the queue family indices
    if (context->device.graphics_queue_index != context->device.present_queue_index) {
        u32 queueFamilyIndices[] = {
            (u32)context->device.graphics_queue_index,
            (u32)context->device.present_queue_index};
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices = 0;
    }

    swapchain_create_info.preTransform = context->device.swapchain_support.capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = 0;

    // Verify the swapchain creation.
    VkResult result = vkCreateSwapchainKHR(context->device.logical_device, &swapchain_create_info, context->allocator, &swapchain->handle);
    if (!vulkan_result_is_success(result)) {
        const char* result_str = vulkan_result_string(result, true);
        KFATAL("Failed to create Vulkan swapchain with the error: '%s'.", result_str);
        return false;
    }

    // Start with a zero frame index.
    window_backend->current_frame = 0;

    // Get image count from swapchain.
    swapchain->image_count = 0;
    result = vkGetSwapchainImagesKHR(context->device.logical_device, swapchain->handle, &swapchain->image_count, 0);
    if (!vulkan_result_is_success(result)) {
        const char* result_str = vulkan_result_string(result, true);
        KFATAL("Failed to obtain image count from Vulkan swapchain with the error: '%s'.", result_str);
        return false;
    }

    // Get the actual images from swapchain.
    VkImage swapchain_images[32];
    result = vkGetSwapchainImagesKHR(context->device.logical_device, swapchain->handle, &swapchain->image_count, swapchain_images);
    if (!vulkan_result_is_success(result)) {
        const char* result_str = vulkan_result_string(result, true);
        KFATAL("Failed to obtain images from Vulkan swapchain with the error: '%s'.", result_str);
        return false;
    }

    // Swapchain images are stored in the backend data of the window.colourbuffer.
    if (khandle_is_invalid(window_internal->colourbuffer->renderer_texture_handle)) {
        // If invalid, then a new one needs to be created. This does not reach out to the
        // texture system to create this, but handles it internally instead. This is because
        // the process for this varies greatly between backends.
        if (!renderer_kresource_texture_resources_acquire(
                backend->frontend_state,
                kname_create("__window_colourbuffer_texture__"),
                KRESOURCE_TEXTURE_TYPE_2D,
                swapchain_extent.width,
                swapchain_extent.height,
                4,
                1,
                1,
                // NOTE: This should be a wrapped texture, so the frontend does not try to
                // acquire the resources we already have here.
                TEXTURE_FLAG_IS_WRAPPED | TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING,
                &window_internal->colourbuffer->renderer_texture_handle)) {

            KFATAL("Failed to acquire internal texture resources for window.colourbuffer");
            return false;
        }
    }

    // Get the texture_internal_data based on the existing or newly-created handle above.
    // Use that to setup the internal images/views for the colourbuffer texture.
    vulkan_texture_handle_data* texture_data = &context->textures[window_internal->colourbuffer->renderer_texture_handle.handle_index];
    if (!texture_data) {
        KFATAL("Unable to get internal data for colourbuffer image. Swapchain creation failed.");
        return false;
    }

    // Name is meaningless here, but might be useful for debugging.
    if (window_internal->colourbuffer->base.name == INVALID_KNAME) {
        window_internal->colourbuffer->base.name = kname_create("__window_colourbuffer_texture__");
    }

    texture_data->image_count = swapchain->image_count;
    // Create the array if it doesn't exist.
    if (!texture_data->images) {
        // Also have to setup the internal data.
        texture_data->images = kallocate(sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);

        // Set initial parameters for each.
        for (u32 i = 0; i < texture_data->image_count; ++i) {
            vulkan_image* image = &texture_data->images[i];

            // Construct a unique name for each image.
            char tex_name[38] = "__internal_vulkan_swapchain_image_0__";
            tex_name[34] = '0' + (char)i;
            image->name = string_duplicate(tex_name);

            // Set initial parameters for each.
            image->memory_flags = 0; // Doesn't really apply anyway/not needed.
            image->mip_levels = 1;
            image->format = swapchain->image_format.format;
            image->layer_count = 1;
            image->layer_views = 0;
        }
    }

    // Update the parameters and setup a view for each image.
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        vulkan_image* image = &texture_data->images[i];

        // Update the internal handle and dimensions.
        image->handle = swapchain_images[i];
        image->width = swapchain_extent.width;
        image->height = swapchain_extent.height;
        image->format = swapchain->image_format.format; // Technically possible that the format changes...
        // Setup a debug name for the image.
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_IMAGE, image->handle, image->name);

        // Create the view for this image.
        VkImageViewCreateInfo view_create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_create_info.image = image->handle;
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = swapchain->image_format.format;
        image->view_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // | VK_IMAGE_USAGE_SAMPLED_BIT;
        image->view_subresource_range.baseMipLevel = 0;
        image->view_subresource_range.levelCount = 1;
        image->view_subresource_range.baseArrayLayer = 0;
        image->view_subresource_range.layerCount = 1;
        view_create_info.subresourceRange = image->view_subresource_range;

        VK_CHECK(vkCreateImageView(context->device.logical_device, &view_create_info, context->allocator, &image->view));
    }

    // Make sure to set the owning window.
    swapchain->owning_window = window;

    KINFO("Swapchain created successfully.");
    return true;
}

static void destroy(renderer_backend_interface* backend, vulkan_swapchain* swapchain) {
    vulkan_context* context = backend->internal_context;

    kwindow* window = swapchain->owning_window;
    kwindow_renderer_state* window_internal = window->renderer_state;

    vulkan_texture_handle_data* texture_data = &context->textures[window_internal->colourbuffer->renderer_texture_handle.handle_index];
    if (!texture_data) {
        KFATAL("Unable to get internal data for colourbuffer image. Swapchain destruction failed.");
        return;
    }

    vkDeviceWaitIdle(context->device.logical_device);

    // Only destroy the colourbuffer views, not the images, since those are owned by the swapchain and are thus
    // destroyed when it is.
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        vulkan_image* image = &texture_data->images[i];
        vkDestroyImageView(context->device.logical_device, image->view, context->allocator);
    }

    vkDestroySwapchainKHR(context->device.logical_device, swapchain->handle, context->allocator);
}
