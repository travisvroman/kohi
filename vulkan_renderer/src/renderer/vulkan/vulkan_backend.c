#include "vulkan_backend.h"

#include <renderer/renderer_types.h>
#include <vulkan/vulkan_core.h>

#include "containers/darray.h"
#include "core/event.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "platform/platform.h"
#include "platform/vulkan_platform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_utils.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "systems/material_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_image.h"
#include "vulkan_pipeline.h"
#include "vulkan_swapchain.h"
#include "vulkan_types.h"
#include "vulkan_utils.h"

// For runtime shader compilation.
#include <shaderc/shaderc.h>
#include <shaderc/status.h>
// NOTE: If wanting to trace allocations, uncomment this.
// #ifndef KVULKAN_ALLOCATOR_TRACE
// #define KVULKAN_ALLOCATOR_TRACE 1
// #endif

// NOTE: To disable the custom allocator, comment this out or set to 0.
#ifndef KVULKAN_USE_CUSTOM_ALLOCATOR
#define KVULKAN_USE_CUSTOM_ALLOCATOR 1
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

static i32 find_memory_index(vulkan_context *context, u32 type_filter,
                             u32 property_flags);

static void create_command_buffers(vulkan_context *context);
static b8 recreate_swapchain(vulkan_context *context);
static b8 create_shader_module(vulkan_context *context, shader *s, shader_stage_config *config, vulkan_shader_stage *out_stage);
static b8 vulkan_buffer_copy_range_internal(vulkan_context *context,
                                            VkBuffer source, u64 source_offset,
                                            VkBuffer dest, u64 dest_offset,
                                            u64 size);

#if KVULKAN_USE_CUSTOM_ALLOCATOR == 1
/**
 * @brief Implementation of PFN_vkAllocationFunction.
 * @link
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/PFN_vkAllocationFunction.html
 *
 * @param user_data User data specified in the allocator by the application.
 * @param size The size in bytes of the requested allocation.
 * @param alignment The requested alignment of the allocation in bytes. Must be
 * a power of two.
 * @param allocationScope The allocation scope and lifetime.
 * @return A memory block if successful; otherwise 0.
 */
void *vulkan_alloc_allocation(void *user_data, size_t size, size_t alignment,
                              VkSystemAllocationScope allocation_scope) {
    // Null MUST be returned if this fails.
    if (size == 0) {
        return 0;
    }

    void *result = kallocate_aligned(size, (u16)alignment, MEMORY_TAG_VULKAN);
#ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Allocated block %p. Size=%llu, Alignment=%llu", result, size,
           alignment);
#endif
    return result;
}

/**
 * @brief Implementation of PFN_vkFreeFunction.
 * @link
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/PFN_vkFreeFunction.html
 *
 * @param user_data User data specified in the allocator by the application.
 * @param memory The allocation to be freed.
 */
void vulkan_alloc_free(void *user_data, void *memory) {
    if (!memory) {
#ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Block is null, nothing to free: %p", memory);
#endif
        return;
    }

#ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Attempting to free block %p...", memory);
#endif
    u64 size;
    u16 alignment;
    b8 result = kmemory_get_size_alignment(memory, &size, &alignment);
    if (result) {
#ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE(
            "Block %p found with size/alignment: %llu/%u. Freeing aligned block...",
            memory, size, alignment);
#endif
        kfree_aligned(memory, size, alignment, MEMORY_TAG_VULKAN);
    } else {
        KERROR("vulkan_alloc_free failed to get alignment lookup for block %p.",
               memory);
    }
}

/**
 * @brief Implementation of PFN_vkReallocationFunction.
 * @link
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/PFN_vkReallocationFunction.html
 *
 * @param user_data User data specified in the allocator by the application.
 * @param original Either NULL or a pointer previously returned by
 * vulkan_alloc_allocation.
 * @param size The size in bytes of the requested allocation.
 * @param alignment The requested alignment of the allocation in bytes. Must be
 * a power of two.
 * @param allocation_scope The scope and lifetime of the allocation.
 * @return A memory block if successful; otherwise 0.
 */
void *vulkan_alloc_reallocation(void *user_data, void *original, size_t size,
                                size_t alignment,
                                VkSystemAllocationScope allocation_scope) {
    if (!original) {
        return vulkan_alloc_allocation(user_data, size, alignment,
                                       allocation_scope);
    }

    if (size == 0) {
        vulkan_alloc_free(user_data, original);
        return 0;
    }

    // NOTE: if pOriginal is not null, the same alignment must be used for the new
    // allocation as original.
    u64 alloc_size;
    u16 alloc_alignment;
    b8 is_aligned =
        kmemory_get_size_alignment(original, &alloc_size, &alloc_alignment);
    if (!is_aligned) {
        KERROR("vulkan_alloc_reallocation of unaligned block %p", original);
        return 0;
    }

    if (alloc_alignment != alignment) {
        KERROR(
            "Attempted realloc using a different alignment of %llu than the "
            "original of %hu.",
            alignment, alloc_alignment);
        return 0;
    }

#ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Attempting to realloc block %p...", original);
#endif

    void *result = vulkan_alloc_allocation(user_data, size, alloc_alignment,
                                           allocation_scope);
    if (result) {
#ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Block %p reallocated to %p, copying data...", original, result);
#endif

        // Copy over the original memory.
        kcopy_memory(result, original, alloc_size);
#ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Freeing original aligned block %p...", original);
#endif
        // Free the original memory only if the new allocation was successful.
        kfree_aligned(original, alloc_size, alloc_alignment, MEMORY_TAG_VULKAN);
    } else {
#ifdef KVULKAN_ALLOCATOR_TRACE
        KERROR("Failed to realloc %p.", original);
#endif
    }

    return result;
}

/**
 * @brief Implementation of PFN_vkInternalAllocationNotification.
 * Purely informational, nothing can really be done with this except to track
 * it.
 * @link
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/PFN_vkInternalAllocationNotification.html
 *
 * @param pUserData User data specified in the allocator by the application.
 * @param size The size of the allocation in bytes.
 * @param allocationType The type of internal allocation.
 * @param allocationScope The scope and lifetime of the allocation.
 */
void vulkan_alloc_internal_alloc(void *pUserData, size_t size,
                                 VkInternalAllocationType allocationType,
                                 VkSystemAllocationScope allocationScope) {
#ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("External allocation of size: %llu", size);
#endif
    kallocate_report((u64)size, MEMORY_TAG_VULKAN_EXT);
}

/**
 * @brief Implementation of PFN_vkInternalFreeNotification.
 * Purely informational, nothing can really be done with this except to track
 * it.
 * @link
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/PFN_vkInternalFreeNotification.html
 *
 * @param pUserData User data specified in the allocator by the application.
 * @param size The size of the allocation to be freed in bytes.
 * @param allocationType The type of internal allocation.
 * @param allocationScope The scope and lifetime of the allocation.
 */
void vulkan_alloc_internal_free(void *pUserData, size_t size,
                                VkInternalAllocationType allocationType,
                                VkSystemAllocationScope allocationScope) {
#ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("External free of size: %llu", size);
#endif
    kfree_report((u64)size, MEMORY_TAG_VULKAN_EXT);
}

/**
 * @brief Create a vulkan allocator object, filling out the function pointers
 * in the provided struct.
 *
 * @param callbacks A pointer to the allocation callbacks structure to be filled
 * out.
 * @return b8 True on success; otherwise false.
 */
b8 create_vulkan_allocator(vulkan_context *context,
                           VkAllocationCallbacks *callbacks) {
    if (callbacks) {
        callbacks->pfnAllocation = vulkan_alloc_allocation;
        callbacks->pfnReallocation = vulkan_alloc_reallocation;
        callbacks->pfnFree = vulkan_alloc_free;
        callbacks->pfnInternalAllocation = vulkan_alloc_internal_alloc;
        callbacks->pfnInternalFree = vulkan_alloc_internal_free;
        callbacks->pUserData = context;
        return true;
    }

    return false;
}
#endif  // KVULKAN_USE_CUSTOM_ALLOCATOR == 1

b8 vulkan_renderer_backend_initialize(renderer_plugin *plugin,
                                      const renderer_backend_config *config,
                                      u8 *out_window_render_target_count) {
    plugin->internal_context_size = sizeof(vulkan_context);
    plugin->internal_context =
        kallocate(plugin->internal_context_size, MEMORY_TAG_RENDERER);
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;

    // Function pointers
    context->find_memory_index = find_memory_index;
    context->render_flag_changed = false;

    // NOTE: Custom allocator.
#if KVULKAN_USE_CUSTOM_ALLOCATOR == 1
    context->allocator =
        kallocate(sizeof(VkAllocationCallbacks), MEMORY_TAG_RENDERER);
    if (!create_vulkan_allocator(context, context->allocator)) {
        // If this fails, gracefully fall back to the default allocator.
        KFATAL(
            "Failed to create custom Vulkan allocator. Continuing using the "
            "driver's default allocator.");
        kfree(context->allocator, sizeof(VkAllocationCallbacks),
              MEMORY_TAG_RENDERER);
        context->allocator = 0;
    }
#else
    context->allocator = 0;
#endif

    // Just set some default values for the framebuffer for now.
    // It doesn't really matyer what these are because they will be
    // overridden, but are needed for swapchain creation.
    context->framebuffer_width = 800;
    context->framebuffer_height = 600;

    // Get the currently-installed instance version. Not necessarily what the device
    // uses, though. Use this to create the instance though.
    u32 api_version = 0;
    vkEnumerateInstanceVersion(&api_version);
    context->api_major = VK_VERSION_MAJOR(api_version);
    context->api_minor = VK_VERSION_MINOR(api_version);
    context->api_patch = VK_VERSION_PATCH(api_version);

    // Setup Vulkan instance.
    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.apiVersion = VK_MAKE_API_VERSION(0, context->api_major, context->api_minor, context->api_patch);
    app_info.pApplicationName = config->application_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Kohi Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;

    // Obtain a list of required extensions
    const char **required_extensions = darray_create(const char *);
    darray_push(required_extensions,
                &VK_KHR_SURFACE_EXTENSION_NAME);  // Generic surface extension
    platform_get_required_extension_names(
        &required_extensions);  // Platform-specific extension(s)
    u32 required_extension_count = 0;
#if defined(_DEBUG)
    darray_push(required_extensions,
                &VK_EXT_DEBUG_UTILS_EXTENSION_NAME);  // debug utilities

    KDEBUG("Required extensions:");
    required_extension_count = darray_length(required_extensions);
    for (u32 i = 0; i < required_extension_count; ++i) {
        KDEBUG(required_extensions[i]);
    }
#endif

    create_info.enabledExtensionCount = darray_length(required_extensions);
    create_info.ppEnabledExtensionNames = required_extensions;

    u32 available_extension_count = 0;
    vkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0);
    VkExtensionProperties *available_extensions =
        darray_reserve(VkExtensionProperties, available_extension_count);
    vkEnumerateInstanceExtensionProperties(0, &available_extension_count,
                                           available_extensions);

    // Verify required extensions are available.
    for (u32 i = 0; i < required_extension_count; ++i) {
        b8 found = false;
        for (u32 j = 0; j < available_extension_count; ++j) {
            if (strings_equal(required_extensions[i],
                              available_extensions[j].extensionName)) {
                found = true;
                KINFO("Required exension found: %s...", required_extensions[i]);
                break;
            }
        }

        if (!found) {
            KFATAL("Required extension is missing: %s", required_extensions[i]);
            return false;
        }
    }

    // Validation layers.
    const char **required_validation_layer_names = 0;
    u32 required_validation_layer_count = 0;

// If validation should be done, get a list of the required validation layert
// names and make sure they exist. Validation layers should only be enabled on
// non-release builds.
#if defined(_DEBUG)
    KINFO("Validation layers enabled. Enumerating...");

    // The list of validation layers required.
    required_validation_layer_names = darray_create(const char *);
    darray_push(required_validation_layer_names, &"VK_LAYER_KHRONOS_validation");
    // NOTE: enable this when needed for debugging.
    // darray_push(required_validation_layer_names, &"VK_LAYER_LUNARG_api_dump");
    required_validation_layer_count =
        darray_length(required_validation_layer_names);

    // Obtain a list of available validation layers
    u32 available_layer_count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, 0));
    VkLayerProperties *available_layers =
        darray_reserve(VkLayerProperties, available_layer_count);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count,
                                                available_layers));

    // Verify all required layers are available.
    for (u32 i = 0; i < required_validation_layer_count; ++i) {
        b8 found = false;
        for (u32 j = 0; j < available_layer_count; ++j) {
            if (strings_equal(required_validation_layer_names[i],
                              available_layers[j].layerName)) {
                found = true;
                KINFO("Found validation layer: %s...",
                      required_validation_layer_names[i]);
                break;
            }
        }

        if (!found) {
            KFATAL("Required validation layer is missing: %s",
                   required_validation_layer_names[i]);
            return false;
        }
    }

    // Clean up.
    darray_destroy(available_extensions);
    darray_destroy(available_layers);

    KINFO("All required validation layers are present.");
#endif

    create_info.enabledLayerCount = required_validation_layer_count;
    create_info.ppEnabledLayerNames = required_validation_layer_names;

#if KPLATFORM_APPLE == 1
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult instance_result =
        vkCreateInstance(&create_info, context->allocator, &context->instance);
    if (!vulkan_result_is_success(instance_result)) {
        const char *result_string = vulkan_result_string(instance_result, true);
        KFATAL("Vulkan instance creation failed with result: '%s'", result_string);
        return false;
    }

    darray_destroy(required_extensions);

    KINFO("Vulkan Instance created.");

    // Clean up
    if (required_validation_layer_names) {
        darray_destroy(required_validation_layer_names);
    }

    // TODO: implement multi-threading.
    context->multithreading_enabled = false;

    // Debugger
#if defined(_DEBUG)
    KDEBUG("Creating Vulkan debugger...");
    u32 log_severity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_create_info.messageSeverity = log_severity;
    debug_create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
    debug_create_info.pfnUserCallback = vk_debug_callback;

    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkCreateDebugUtilsMessengerEXT");
    KASSERT_MSG(func, "Failed to create debug messenger!");
    VK_CHECK(func(context->instance, &debug_create_info, context->allocator,
                  &context->debug_messenger));
    KDEBUG("Vulkan debugger created.");

    // Load up debug function pointers.
    context->pfnSetDebugUtilsObjectNameEXT =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(
            context->instance, "vkSetDebugUtilsObjectNameEXT");
    if (!context->pfnSetDebugUtilsObjectNameEXT) {
        KWARN(
            "Unable to load function pointer for vkSetDebugUtilsObjectNameEXT. "
            "Debug functions associated with this will not work.");
    }
    context->pfnSetDebugUtilsObjectTagEXT =
        (PFN_vkSetDebugUtilsObjectTagEXT)vkGetInstanceProcAddr(
            context->instance, "vkSetDebugUtilsObjectTagEXT");
    if (!context->pfnSetDebugUtilsObjectTagEXT) {
        KWARN(
            "Unable to load function pointer for vkSetDebugUtilsObjectTagEXT. "
            "Debug functions associated with this will not work.");
    }

    context->pfnCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(
            context->instance, "vkCmdBeginDebugUtilsLabelEXT");
    if (!context->pfnCmdBeginDebugUtilsLabelEXT) {
        KWARN(
            "Unable to load function pointer for vkCmdBeginDebugUtilsLabelEXT. "
            "Debug functions associated with this will not work.");
    }

    context->pfnCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(
            context->instance, "vkCmdEndDebugUtilsLabelEXT");
    if (!context->pfnCmdEndDebugUtilsLabelEXT) {
        KWARN(
            "Unable to load function pointer for vkCmdEndDebugUtilsLabelEXT. "
            "Debug functions associated with this will not work.");
    }
#endif

    // Surface
    KDEBUG("Creating Vulkan surface...");
    if (!platform_create_vulkan_surface(context)) {
        KERROR("Failed to create platform surface!");
        return false;
    }
    KDEBUG("Vulkan surface created.");

    // Device creation
    if (!vulkan_device_create(context)) {
        KERROR("Failed to create device!");
        return false;
    }

    // Swapchain
    vulkan_swapchain_create(context, context->framebuffer_width,
                            context->framebuffer_height, config->flags,
                            &context->swapchain);

    // Save off the number of images we have as the number of render targets
    // needed.
    *out_window_render_target_count = context->swapchain.image_count;

    // Create command buffers.
    create_command_buffers(context);

    // Create sync objects.
    context->image_available_semaphores =
        darray_reserve(VkSemaphore, context->swapchain.max_frames_in_flight);
    context->queue_complete_semaphores =
        darray_reserve(VkSemaphore, context->swapchain.max_frames_in_flight);

    for (u8 i = 0; i < context->swapchain.max_frames_in_flight; ++i) {
        VkSemaphoreCreateInfo semaphore_create_info = {
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context->device.logical_device, &semaphore_create_info,
                          context->allocator,
                          &context->image_available_semaphores[i]);
        vkCreateSemaphore(context->device.logical_device, &semaphore_create_info,
                          context->allocator,
                          &context->queue_complete_semaphores[i]);

        // Create the fence in a signaled state, indicating that the first frame has
        // already been "rendered". This will prevent the application from waiting
        // indefinitely for the first frame to render since it cannot be rendered
        // until a frame is "rendered" before it.
        VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(context->device.logical_device, &fence_create_info,
                               context->allocator, &context->in_flight_fences[i]));
    }

    // Samplers array.
    context->samplers = darray_create(VkSampler);

    // Staging buffer.
    const u64 staging_buffer_size = 256 * 1000 * 1000;
    if (!renderer_renderbuffer_create("staging", RENDERBUFFER_TYPE_STAGING, staging_buffer_size, RENDERBUFFER_TRACK_TYPE_LINEAR, &context->staging)) {
        KERROR("Failed to create staging buffer.");
        return false;
    }
    renderer_renderbuffer_bind(&context->staging, 0);

    // Create a shader compiler to be used.
    context->shader_compiler = shaderc_compiler_initialize();

    KINFO("Vulkan renderer initialized successfully.");
    return true;
}

void vulkan_renderer_backend_shutdown(renderer_plugin *plugin) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vkDeviceWaitIdle(context->device.logical_device);

    // Destroy the runtime shader compiler.
    if (context->shader_compiler) {
        shaderc_compiler_release(context->shader_compiler);
        context->shader_compiler = 0;
    }

    // Destroy in the opposite order of creation.
    // Destroy buffers
    renderer_renderbuffer_destroy(&context->staging);

    // Sync objects
    for (u8 i = 0; i < context->swapchain.max_frames_in_flight; ++i) {
        if (context->image_available_semaphores[i]) {
            vkDestroySemaphore(context->device.logical_device,
                               context->image_available_semaphores[i],
                               context->allocator);
            context->image_available_semaphores[i] = 0;
        }
        if (context->queue_complete_semaphores[i]) {
            vkDestroySemaphore(context->device.logical_device,
                               context->queue_complete_semaphores[i],
                               context->allocator);
            context->queue_complete_semaphores[i] = 0;
        }
        vkDestroyFence(context->device.logical_device, context->in_flight_fences[i],
                       context->allocator);
    }
    darray_destroy(context->image_available_semaphores);
    context->image_available_semaphores = 0;

    darray_destroy(context->queue_complete_semaphores);
    context->queue_complete_semaphores = 0;

    // Command buffers
    for (u32 i = 0; i < context->swapchain.image_count; ++i) {
        if (context->graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(context, context->device.graphics_command_pool,
                                       &context->graphics_command_buffers[i]);
            context->graphics_command_buffers[i].handle = 0;
        }
    }
    darray_destroy(context->graphics_command_buffers);
    context->graphics_command_buffers = 0;

    // Swapchain
    vulkan_swapchain_destroy(context, &context->swapchain);

    KDEBUG("Destroying Vulkan device...");
    vulkan_device_destroy(context);

    KDEBUG("Destroying Vulkan surface...");
    if (context->surface) {
        vkDestroySurfaceKHR(context->instance, context->surface,
                            context->allocator);
        context->surface = 0;
    }

    if (plugin->internal_context) {
        kfree(plugin->internal_context, plugin->internal_context_size,
              MEMORY_TAG_RENDERER);
        plugin->internal_context_size = 0;
    }

#if defined(_DEBUG)
    KDEBUG("Destroying Vulkan debugger...");
    if (context->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                context->instance, "vkDestroyDebugUtilsMessengerEXT");
        func(context->instance, context->debug_messenger, context->allocator);
    }
#endif

    KDEBUG("Destroying Vulkan instance...");
    vkDestroyInstance(context->instance, context->allocator);

    // Destroy the allocator callbacks if set.
    if (context->allocator) {
        kfree(context->allocator, sizeof(VkAllocationCallbacks),
              MEMORY_TAG_RENDERER);
        context->allocator = 0;
    }
}

void vulkan_renderer_backend_on_resized(renderer_plugin *plugin, u16 width,
                                        u16 height) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Update the "framebuffer size generation", a counter which indicates when
    // the framebuffer size has been updated.
    context->framebuffer_width = width;
    context->framebuffer_height = height;
    context->framebuffer_size_generation++;

    KINFO("Vulkan renderer plugin->resized: w/h/gen: %i/%i/%llu", width, height,
          context->framebuffer_size_generation);
}

b8 vulkan_renderer_frame_prepare(renderer_plugin *plugin, struct frame_data *p_frame_data) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_device *device = &context->device;

    // Check if recreating swap chain and boot out.
    if (context->recreating_swapchain) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (1) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }
        KINFO("Recreating swapchain, booting.");
        return false;
    }

    // Check if the framebuffer has been resized. If so, a new swapchain must be
    // created. Also include a vsync changed check.
    if (context->framebuffer_size_generation != context->framebuffer_size_last_generation ||
        context->render_flag_changed) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (2) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }

        if (context->render_flag_changed) {
            context->render_flag_changed = false;
        }

        // If the swapchain recreation failed (because, for example, the window was
        // minimized), boot out before unsetting the flag.
        if (!recreate_swapchain(context)) {
            return false;
        }

        KINFO("Resized, booting.");
        return false;
    }

    // Reset staging buffer.
    if (!renderer_renderbuffer_clear(&context->staging, false)) {
        KERROR("Failed to clear staging buffer.");
        return false;
    }

    // Wait for the execution of the current frame to complete. The fence being
    // free will allow this one to move on.
    VkResult result = vkWaitForFences(
        context->device.logical_device, 1,
        &context->in_flight_fences[context->current_frame], true, UINT64_MAX);
    if (!vulkan_result_is_success(result)) {
        KFATAL("In-flight fence wait failure! error: %s", vulkan_result_string(result, true));
        return false;
    }

    // Acquire the next image from the swap chain. Pass along the semaphore that
    // should signaled when this completes. This same semaphore will later be
    // waited on by the queue submission to ensure this image is available.
    result = vkAcquireNextImageKHR(
        context->device.logical_device,
        context->swapchain.handle,
        UINT64_MAX,
        context->image_available_semaphores[context->current_frame],
        0,
        &context->image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Trigger swapchain recreation, then boot out of the render loop.
        vulkan_swapchain_recreate(context, context->framebuffer_width, context->framebuffer_height, &context->swapchain);
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        KFATAL("Failed to acquire swapchain image!");
        return false;
    }

    // Reset the fence for use on the next frame
    VK_CHECK(vkResetFences(context->device.logical_device, 1, &context->in_flight_fences[context->current_frame]));

    return true;
}

b8 vulkan_renderer_begin(renderer_plugin *plugin, struct frame_data *p_frame_data) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Begin recording commands.
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    vulkan_command_buffer_reset(command_buffer);
    vulkan_command_buffer_begin(command_buffer, false, false, false);

    // Dynamic state

    vulkan_renderer_winding_set(plugin, RENDERER_WINDING_COUNTER_CLOCKWISE);

    vulkan_renderer_set_stencil_reference(plugin, 0);
    vulkan_renderer_set_stencil_compare_mask(plugin, 0xFF);
    vulkan_renderer_set_stencil_op(
        plugin,
        RENDERER_STENCIL_OP_KEEP,
        RENDERER_STENCIL_OP_REPLACE,
        RENDERER_STENCIL_OP_KEEP,
        RENDERER_COMPARE_OP_ALWAYS);
    vulkan_renderer_set_stencil_test_enabled(plugin, false);
    vulkan_renderer_set_depth_test_enabled(plugin, true);
    // Disable stencil writing.
    vulkan_renderer_set_stencil_write_mask(plugin, 0x00);
    return true;
}

b8 vulkan_renderer_end(renderer_plugin *plugin, struct frame_data *p_frame_data) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    vulkan_command_buffer_end(command_buffer);

    // Submit the queue and wait for the operation to complete.
    // Begin queue submission
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};

    // Command buffer(s) to be executed.
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer->handle;

    // The semaphore(s) to be signaled when the queue is complete.
    if (plugin->draw_index == 0) {
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &context->queue_complete_semaphores[context->current_frame];
    } else {
        submit_info.signalSemaphoreCount = 0;
    }

    // Wait semaphore ensures that the operation cannot begin until the image is
    // available.
    if (plugin->draw_index == 0) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &context->image_available_semaphores[context->current_frame];
    } else {
        submit_info.waitSemaphoreCount = 0;
    }

    // Each semaphore waits on the corresponding pipeline stage to complete. 1:1
    // ratio. VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT prevents subsequent
    // colour attachment writes from executing until the semaphore signals (i.e.
    // one frame is presented at a time)
    VkPipelineStageFlags flags[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = flags;

    VkResult result =
        vkQueueSubmit(context->device.graphics_queue, 1, &submit_info,
                      context->in_flight_fences[context->current_frame]);
    if (result != VK_SUCCESS) {
        KERROR("vkQueueSubmit failed with result: %s",
               vulkan_result_string(result, true));
        return false;
    }

    vulkan_command_buffer_update_submitted(command_buffer);
    // End queue submission

    // For timing purposes, wait for the queue to complete.
    // This gives an accurate picture of how long the render takes, including the
    // work submitted to the actual queue.
    vkWaitForFences(context->device.logical_device, 1, &context->in_flight_fences[context->current_frame], true, UINT64_MAX);

    return true;
}

b8 vulkan_renderer_present(renderer_plugin *plugin, struct frame_data *p_frame_data) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;

    // Return the image to the swapchain for presentation.
    VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &context->queue_complete_semaphores[context->current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &context->swapchain.handle;
    present_info.pImageIndices = &context->image_index;
    present_info.pResults = 0;

    // HACK: By waiting on the transfer queue, we avoid a segfault here for some reason. This shouldn't
    // be needed since it _should_ be waiting on the pWaitSemaphores, which _should_ be
    // signaled by the queue's completion after submission. And strangely, it's specifically the
    // _transfer_ queue, even though the one being used for presentation here is the present queue.
    // TODO: Need to dive a bit deeper on this to figure it out.
    vkQueueWaitIdle(context->device.transfer_queue);
    VkResult result = vkQueuePresentKHR(context->device.present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain is out of date, suboptimal or a framebuffer resize has occurred. Trigger swapchain recreation.
        vulkan_swapchain_recreate(context, context->framebuffer_width, context->framebuffer_height, &context->swapchain);
        KDEBUG("Swapchain recreated because swapchain returned out of date or suboptimal.");
    } else if (result != VK_SUCCESS) {
        KFATAL("Failed to present swap chain image!");
    }

    // Increment (and loop) the index.
    context->current_frame = (context->current_frame + 1) % context->swapchain.max_frames_in_flight;

    return true;
}

void vulkan_renderer_viewport_set(renderer_plugin *plugin, vec4 rect) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Dynamic state
    VkViewport viewport;
    viewport.x = rect.x;
    viewport.y = rect.y;
    viewport.width = rect.z;
    viewport.height = rect.w;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vulkan_command_buffer *command_buffer =
        &context->graphics_command_buffers[context->image_index];

    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
}

void vulkan_renderer_viewport_reset(renderer_plugin *plugin) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Just set the current viewport rect.
    vulkan_renderer_viewport_set(plugin, context->viewport_rect);
}

void vulkan_renderer_scissor_set(renderer_plugin *plugin, vec4 rect) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    VkRect2D scissor;
    scissor.offset.x = rect.x;
    scissor.offset.y = rect.y;
    scissor.extent.width = rect.z;
    scissor.extent.height = rect.w;

    vulkan_command_buffer *command_buffer =
        &context->graphics_command_buffers[context->image_index];

    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
}

void vulkan_renderer_scissor_reset(renderer_plugin *plugin) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Just set the current scissor rect.
    vulkan_renderer_scissor_set(plugin, context->scissor_rect);
}

void vulkan_renderer_winding_set(struct renderer_plugin *plugin, renderer_winding winding) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    VkFrontFace vk_winding = winding == RENDERER_WINDING_COUNTER_CLOCKWISE ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetFrontFace(command_buffer->handle, vk_winding);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetFrontFaceEXT(command_buffer->handle, vk_winding);
    } else {
        KFATAL("renderer_winding_set cannot be used on a device without dynamic state support.");
    }
}

static VkStencilOp vulkan_renderer_get_stencil_op(renderer_stencil_op op) {
    switch (op) {
        case RENDERER_STENCIL_OP_KEEP:
            return VK_STENCIL_OP_KEEP;
        case RENDERER_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case RENDERER_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case RENDERER_STENCIL_OP_INCREMENT_AND_CLAMP:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case RENDERER_STENCIL_OP_DECREMENT_AND_CLAMP:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case RENDERER_STENCIL_OP_INCREMENT_AND_WRAP:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case RENDERER_STENCIL_OP_DECREMENT_AND_WRAP:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case RENDERER_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        default:
            KWARN("Unsupported stencil op, defaulting to keep.");
            return VK_STENCIL_OP_KEEP;
    }
}

static VkCompareOp vulkan_renderer_get_compare_op(renderer_compare_op op) {
    switch (op) {
        case RENDERER_COMPARE_OP_NEVER:
            return VK_COMPARE_OP_NEVER;
        case RENDERER_COMPARE_OP_LESS:
            return VK_COMPARE_OP_LESS;
        case RENDERER_COMPARE_OP_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case RENDERER_COMPARE_OP_LESS_OR_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RENDERER_COMPARE_OP_GREATER:
            return VK_COMPARE_OP_GREATER;
        case RENDERER_COMPARE_OP_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case RENDERER_COMPARE_OP_GREATER_OR_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case RENDERER_COMPARE_OP_ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
        default:
            KWARN("Unsupported compare op, using always.");
            return VK_COMPARE_OP_ALWAYS;
    }
}

void vulkan_renderer_set_stencil_test_enabled(struct renderer_plugin *plugin, b8 enabled) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetStencilTestEnable(command_buffer->handle, (VkBool32)enabled);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetStencilTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
    } else {
        KFATAL("renderer_set_stencil_test_enabled cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_depth_test_enabled(struct renderer_plugin *plugin, b8 enabled) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetDepthTestEnable(command_buffer->handle, (VkBool32)enabled);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetDepthTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
    } else {
        KFATAL("renderer_set_depth_test_enabled cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_stencil_reference(struct renderer_plugin *plugin, u32 reference) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    vkCmdSetStencilReference(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void vulkan_renderer_set_stencil_op(struct renderer_plugin *plugin, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetStencilOp(
            command_buffer->handle,
            VK_STENCIL_FACE_FRONT_AND_BACK,
            vulkan_renderer_get_stencil_op(fail_op),
            vulkan_renderer_get_stencil_op(pass_op),
            vulkan_renderer_get_stencil_op(depth_fail_op),
            vulkan_renderer_get_compare_op(compare_op));
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetStencilOpEXT(
            command_buffer->handle,
            VK_STENCIL_FACE_FRONT_AND_BACK,
            vulkan_renderer_get_stencil_op(fail_op),
            vulkan_renderer_get_stencil_op(pass_op),
            vulkan_renderer_get_stencil_op(depth_fail_op),
            vulkan_renderer_get_compare_op(compare_op));
    } else {
        KFATAL("renderer_set_stencil_op cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_stencil_compare_mask(struct renderer_plugin *plugin, u32 compare_mask) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    // Supported as of vulkan 1.0, so no need to check for dynamic state support.
    vkCmdSetStencilCompareMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, compare_mask);
}

void vulkan_renderer_set_stencil_write_mask(struct renderer_plugin *plugin, u32 write_mask) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    // Supported as of vulkan 1.0, so no need to check for dynamic state support.
    vkCmdSetStencilWriteMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, write_mask);
}

b8 vulkan_renderer_renderpass_begin(renderer_plugin *plugin, renderpass *pass, render_target *target) {
    // Cold-cast the context
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];

    // Begin the render pass.
    vulkan_renderpass *internal_data = pass->internal_data;

    viewport *v = renderer_active_viewport_get();

    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = internal_data->handle;
    begin_info.framebuffer = target->internal_framebuffer;
    begin_info.renderArea.offset.x = v->rect.x;
    begin_info.renderArea.offset.y = v->rect.y;
    begin_info.renderArea.extent.width = v->rect.width;
    begin_info.renderArea.extent.height = v->rect.height;

    // KTRACE("Renderpass '%s' is using framebuffer at 0x%x", pass->name, target->internal_framebuffer);

    begin_info.clearValueCount = 0;
    begin_info.pClearValues = 0;

    VkClearValue clear_values[2];
    kzero_memory(clear_values, sizeof(VkClearValue) * 2);
    b8 do_clear_colour = (pass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;
    if (do_clear_colour) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, pass->clear_colour.elements, sizeof(f32) * 4);
        begin_info.clearValueCount++;
    } else {
        // Still add it anyway, but don't bother copying data since it will be
        // ignored.
        begin_info.clearValueCount++;
    }

    b8 do_clear_depth = (pass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
    b8 do_clear_stencil = (pass->clear_flags & RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG) != 0;
    if (do_clear_depth || do_clear_stencil) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, pass->clear_colour.elements, sizeof(f32) * 4);
        clear_values[begin_info.clearValueCount].depthStencil.depth = internal_data->depth;

        clear_values[begin_info.clearValueCount].depthStencil.stencil = do_clear_stencil ? internal_data->stencil : 0;
        begin_info.clearValueCount++;
    } else {
        for (u32 i = 0; i < target->attachment_count; ++i) {
            if (target->attachments[i].type & RENDER_TARGET_ATTACHMENT_TYPE_DEPTH || target->attachments[i].type & RENDER_TARGET_ATTACHMENT_TYPE_STENCIL) {
                // If there is a depth/stencil attachment, make sure to add the clear count, but
                // don't bother copying the data.
                begin_info.clearValueCount++;
            }
        }
    }

    begin_info.pClearValues = begin_info.clearValueCount > 0 ? clear_values : 0;

    vkCmdBeginRenderPass(command_buffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    command_buffer->state = COMMAND_BUFFER_STATE_IN_RENDER_PASS;

#ifdef _DEBUG
    f32 r = kfrandom_in_range(0.0f, 1.0f);
    f32 g = kfrandom_in_range(0.0f, 1.0f);
    f32 b = kfrandom_in_range(0.0f, 1.0f);
    vec4 colour = (vec4){r, g, b, 1.0f};
#endif
    VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, pass->name, colour);
    return true;
}

b8 vulkan_renderer_renderpass_end(renderer_plugin *plugin, renderpass *pass) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer =
        &context->graphics_command_buffers[context->image_index];
    // End the renderpass.
    vkCmdEndRenderPass(command_buffer->handle);
    VK_END_DEBUG_LABEL(context, command_buffer->handle);

    command_buffer->state = COMMAND_BUFFER_STATE_RECORDING;
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                  VkDebugUtilsMessageTypeFlagsEXT message_types,
                  const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                  void *user_data) {
    switch (message_severity) {
        default:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            KERROR(callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            KWARN(callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            KINFO(callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            KTRACE(callback_data->pMessage);
            break;
    }
    return VK_FALSE;
}

static i32 find_memory_index(vulkan_context *context, u32 type_filter,
                             u32 property_flags) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(context->device.physical_device,
                                        &memory_properties);

    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        // Check each memory type to see if its bit is set to 1.
        if (type_filter & (1 << i) &&
            (memory_properties.memoryTypes[i].propertyFlags & property_flags) ==
                property_flags) {
            return i;
        }
    }

    KWARN("Unable to find suitable memory type!");
    return -1;
}

static void create_command_buffers(vulkan_context *context) {
    if (!context->graphics_command_buffers) {
        context->graphics_command_buffers =
            darray_reserve(vulkan_command_buffer, context->swapchain.image_count);
        for (u32 i = 0; i < context->swapchain.image_count; ++i) {
            kzero_memory(&context->graphics_command_buffers[i],
                         sizeof(vulkan_command_buffer));
        }
    }

    for (u32 i = 0; i < context->swapchain.image_count; ++i) {
        if (context->graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(context, context->device.graphics_command_pool,
                                       &context->graphics_command_buffers[i]);
        }
        kzero_memory(&context->graphics_command_buffers[i],
                     sizeof(vulkan_command_buffer));
        vulkan_command_buffer_allocate(context,
                                       context->device.graphics_command_pool, true,
                                       &context->graphics_command_buffers[i]);
    }

    KDEBUG("Vulkan command buffers created.");
}

static b8 recreate_swapchain(vulkan_context *context) {
    // If already being recreated, do not try again.
    if (context->recreating_swapchain) {
        KDEBUG("recreate_swapchain called when already recreating. Booting.");
        return false;
    }

    // Detect if the window is too small to be drawn to
    if (context->framebuffer_width == 0 || context->framebuffer_height == 0) {
        KDEBUG(
            "recreate_swapchain called when window is < 1 in a dimension. "
            "Booting.");
        return false;
    }

    // Mark as recreating if the dimensions are valid.
    context->recreating_swapchain = true;

    // Wait for any operations to complete.
    vkDeviceWaitIdle(context->device.logical_device);

    // Requery support
    vulkan_device_query_swapchain_support(context->device.physical_device,
                                          context->surface,
                                          &context->device.swapchain_support);
    vulkan_device_detect_depth_format(&context->device);

    vulkan_swapchain_recreate(context, context->framebuffer_width,
                              context->framebuffer_height, &context->swapchain);

    // Update framebuffer size generation.
    context->framebuffer_size_last_generation =
        context->framebuffer_size_generation;

    // cleanup swapchain
    for (u32 i = 0; i < context->swapchain.image_count; ++i) {
        vulkan_command_buffer_free(context, context->device.graphics_command_pool,
                                   &context->graphics_command_buffers[i]);
    }

    // Indicate to listeners that a render target refresh is required.
    event_context event_context = {0};
    event_fire(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, 0,
               event_context);

    create_command_buffers(context);

    // Clear the recreating flag.
    context->recreating_swapchain = false;

    return true;
}

void vulkan_renderer_texture_create(renderer_plugin *plugin, const u8 *pixels,
                                    texture *t) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Internal data creation.
    // TODO: Use an allocator for this.
    t->internal_data =
        (vulkan_image *)kallocate(sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    vulkan_image *image = (vulkan_image *)t->internal_data;
    u32 size = t->width * t->height * t->channel_count *
               (t->type == TEXTURE_TYPE_CUBE ? 6 : 1);

    // NOTE: Assumes 8 bits per channel.
    VkFormat image_format = VK_FORMAT_R8G8B8A8_UNORM;

    // NOTE: Lots of assumptions here, different texture types will require
    // different options here.
    vulkan_image_create(
        context, t->type, t->width, t->height, t->array_size, image_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, VK_IMAGE_ASPECT_COLOR_BIT,
        t->name, t->mip_levels, image);

    // Load the data.
    vulkan_renderer_texture_write_data(plugin, t, 0, size, pixels);

    t->generation++;
}

void vulkan_renderer_texture_destroy(renderer_plugin *plugin,
                                     struct texture *texture) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vkDeviceWaitIdle(context->device.logical_device);

    vulkan_image *image = (vulkan_image *)texture->internal_data;
    if (image) {
        vulkan_image_destroy(context, image);
        kzero_memory(image, sizeof(vulkan_image));

        kfree(texture->internal_data, sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    }
    kzero_memory(texture, sizeof(struct texture));
}

static VkFormat channel_count_to_format(u8 channel_count,
                                        VkFormat default_format) {
    switch (channel_count) {
        case 1:
            return VK_FORMAT_R8_UNORM;
        case 2:
            return VK_FORMAT_R8G8_UNORM;
        case 3:
            return VK_FORMAT_R8G8B8_UNORM;
        case 4:
            return VK_FORMAT_R8G8B8A8_UNORM;
        default:
            return default_format;
    }
}

void vulkan_renderer_texture_create_writeable(renderer_plugin *plugin, texture *t) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Internal data creation.
    t->internal_data = (vulkan_image *)kallocate(sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    vulkan_image *image = (vulkan_image *)t->internal_data;

    VkImageUsageFlagBits usage;
    VkImageAspectFlagBits aspect;
    VkFormat image_format;
    if (t->flags & TEXTURE_FLAG_DEPTH) {
        usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        image_format = context->device.depth_format;
    } else {
        usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        image_format = channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);
    }

    vulkan_image_create(context, t->type, t->width, t->height, t->array_size, image_format,
                        VK_IMAGE_TILING_OPTIMAL, usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, aspect,
                        t->name, t->mip_levels, image);

    t->generation++;
}

void vulkan_renderer_texture_resize(renderer_plugin *plugin, texture *t,
                                    u32 new_width, u32 new_height) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (t && t->internal_data) {
        // Resizing is really just destroying the old image and creating a new one.
        // Data is not preserved because there's no reliable way to map the old data
        // to the new since the amount of data differs.
        vulkan_image *image = (vulkan_image *)t->internal_data;
        vulkan_image_destroy(context, image);

        VkFormat image_format =
            channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

        // Recalculate mip levels if anything other than 1.
        if (t->mip_levels > 1) {
            // Recalculate the number of levels.
            // The number of mip levels is calculated by first taking the largest dimension
            // (either width or height), figuring out how many times that number can be divided
            // by 2, taking the floor value (rounding down) and adding 1 to represent the
            // base level. This always leaves a value of at least 1.
            t->mip_levels = (u32)(kfloor(klog2(KMAX(new_width, new_height))) + 1);
        }

        // TODO: Lots of assumptions here, different texture types will require
        // different options here.
        vulkan_image_create(
            context, t->type, new_width, new_height, t->array_size, image_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, VK_IMAGE_ASPECT_COLOR_BIT,
            t->name, t->mip_levels, image);

        t->generation++;
    }
}

void vulkan_renderer_texture_write_data(renderer_plugin *plugin, texture *t,
                                        u32 offset, u32 size,
                                        const u8 *pixels) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_image *image = (vulkan_image *)t->internal_data;

    VkFormat image_format =
        channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

    // Staging buffer.
    u64 staging_offset = 0;
    renderer_renderbuffer_allocate(&context->staging, size, &staging_offset);
    vulkan_buffer_load_range(plugin, &context->staging, staging_offset, size, pixels);

    vulkan_command_buffer temp_command_buffer;
    VkCommandPool pool = context->device.graphics_command_pool;
    VkQueue queue = context->device.graphics_queue;
    vulkan_command_buffer_allocate_and_begin_single_use(context, pool, &temp_command_buffer);

    // Transition the layout from whatever it is currently to optimal for
    // recieving data.
    vulkan_image_transition_layout(context, t->type, &temp_command_buffer, image,
                                   image_format, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy the data from the buffer.
    vulkan_image_copy_from_buffer(context, t->type, image, ((vulkan_buffer *)context->staging.internal_data)->handle, staging_offset, &temp_command_buffer);

    if (t->mip_levels <= 1 || !vulkan_image_mipmaps_generate(context, image, &temp_command_buffer)) {
        // If mip generation isn't needed or fails, fall back to ordinary transition.
        // Transition from optimal for data reciept to shader-read-only optimal layout.
        vulkan_image_transition_layout(context, t->type, &temp_command_buffer, image,
                                       image_format,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    vulkan_command_buffer_end_single_use(context, pool, &temp_command_buffer, queue);

    t->generation++;
}

void vulkan_renderer_texture_read_data(renderer_plugin *plugin, texture *t,
                                       u32 offset, u32 size,
                                       void **out_memory) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_image *image = (vulkan_image *)t->internal_data;

    VkFormat image_format =
        channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

    // Create a staging buffer and load data into it.
    // TODO: global read buffer w/freelist (like staging), but for reading.
    renderbuffer staging;
    char bufname[256];
    kzero_memory(bufname, 256);
    string_format(bufname, "renderbuffer_texture_read_staging");
    if (!renderer_renderbuffer_create(bufname, RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, &staging)) {
        KERROR("Failed to create staging buffer for texture read.");
        return;
    }
    renderer_renderbuffer_bind(&staging, 0);

    vulkan_command_buffer temp_buffer;
    VkCommandPool pool = context->device.graphics_command_pool;
    VkQueue queue = context->device.graphics_queue;
    vulkan_command_buffer_allocate_and_begin_single_use(context, pool,
                                                        &temp_buffer);

    // NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    // Transition the layout from whatever it is currently to optimal for handing
    // out data.
    vulkan_image_transition_layout(context, t->type, &temp_buffer, image,
                                   image_format, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Copy the data to the buffer.
    vulkan_image_copy_to_buffer(context, t->type, image,
                                ((vulkan_buffer *)staging.internal_data)->handle,
                                &temp_buffer);

    // Transition from optimal for data reading to shader-read-only optimal
    // layout.
    vulkan_image_transition_layout(context, t->type, &temp_buffer, image,
                                   image_format,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vulkan_command_buffer_end_single_use(context, pool, &temp_buffer, queue);

    if (!vulkan_buffer_read(plugin, &staging, offset, size, out_memory)) {
        KERROR("vulkan_buffer_read failed.");
    }

    renderer_renderbuffer_unbind(&staging);
    renderer_renderbuffer_destroy(&staging);
}

void vulkan_renderer_texture_read_pixel(renderer_plugin *plugin, texture *t,
                                        u32 x, u32 y, u8 **out_rgba) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_image *image = (vulkan_image *)t->internal_data;

    VkFormat image_format =
        channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

    // TODO: creating a buffer every time isn't great. Could optimize this by
    // creating a buffer once and just reusing it.

    // Create a staging buffer and load data into it.
    renderbuffer staging;
    char bufname[256];
    kzero_memory(bufname, 256);
    string_format(bufname, "renderbuffer_texture_read_pixel_staging");
    if (!renderer_renderbuffer_create(bufname, RENDERBUFFER_TYPE_READ, sizeof(u8) * 4, RENDERBUFFER_TRACK_TYPE_NONE, &staging)) {
        KERROR("Failed to create staging buffer for texture pixel read.");
        return;
    }
    renderer_renderbuffer_bind(&staging, 0);

    vulkan_command_buffer temp_buffer;
    VkCommandPool pool = context->device.graphics_command_pool;
    VkQueue queue = context->device.graphics_queue;
    vulkan_command_buffer_allocate_and_begin_single_use(context, pool,
                                                        &temp_buffer);

    // NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    // Transition the layout from whatever it is currently to optimal for handing
    // out data.
    vulkan_image_transition_layout(context, t->type, &temp_buffer, image,
                                   image_format, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Copy the data to the buffer.
    // vulkan_image_copy_to_buffer(context, t->type, image,
    // ((vulkan_buffer*)staging.internal_data)->handle, &temp_buffer);
    vulkan_image_copy_pixel_to_buffer(
        context, t->type, image, ((vulkan_buffer *)staging.internal_data)->handle,
        x, y, &temp_buffer);

    // Transition from optimal for data reading to shader-read-only optimal
    // layout.
    vulkan_image_transition_layout(context, t->type, &temp_buffer, image,
                                   image_format,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vulkan_command_buffer_end_single_use(context, pool, &temp_buffer, queue);

    if (!vulkan_buffer_read(plugin, &staging, 0, sizeof(u8) * 4,
                            (void **)out_rgba)) {
        KERROR("vulkan_buffer_read failed.");
    }

    renderer_renderbuffer_unbind(&staging);
    renderer_renderbuffer_destroy(&staging);
}

b8 vulkan_renderer_shader_create(renderer_plugin *plugin, shader *s, const shader_config *config, renderpass *pass) {
    // Verify stage support.
    for (u8 i = 0; i < config->stage_count; ++i) {
        switch (config->stage_configs[i].stage) {
            case SHADER_STAGE_FRAGMENT:
            case SHADER_STAGE_VERTEX:
                break;
            case SHADER_STAGE_GEOMETRY:
                KWARN("vulkan_renderer_shader_create: VK_SHADER_STAGE_GEOMETRY_BIT is set but not yet supported.");
                break;
            case SHADER_STAGE_COMPUTE:
                KWARN("vulkan_renderer_shader_create: SHADER_STAGE_COMPUTE is set but not yet supported.");
                break;
            default:
                KERROR("Unsupported stage type: %d", config->stage_configs[i].name);
                break;
        }
    }

    s->internal_data = kallocate(sizeof(vulkan_shader), MEMORY_TAG_RENDERER);
    vulkan_context *context = (vulkan_context *)plugin->internal_context;

    // Setup the internal shader.
    vulkan_shader *internal_shader = (vulkan_shader *)s->internal_data;
    internal_shader->renderpass = pass->internal_data;
    internal_shader->local_push_constant_block = kallocate(128, MEMORY_TAG_RENDERER);

    internal_shader->stage_count = config->stage_count;

    // Need a max of 2 descriptor sets, one for global and one for instance.
    // Note that this can mean that only one (or potentially none) exist as well.
    internal_shader->descriptor_set_count = 0;
    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    b8 has_instance = s->instance_uniform_count > 0 || s->instance_uniform_sampler_count > 0;
    kzero_memory(internal_shader->descriptor_sets, sizeof(vulkan_descriptor_set_config) * 2);
    u8 set_count = 0;
    if (has_global) {
        internal_shader->descriptor_sets[set_count].sampler_binding_index_start = INVALID_ID_U8;
        set_count++;
    }
    if (has_instance) {
        internal_shader->descriptor_sets[set_count].sampler_binding_index_start = INVALID_ID_U8;
        set_count++;
    }

    // Attributes array.
    kzero_memory(internal_shader->attributes, sizeof(VkVertexInputAttributeDescription) * VULKAN_SHADER_MAX_ATTRIBUTES);

    // Calculate the total number of descriptors needed.
    u32 frame_count = context->swapchain.image_count;
    // 1 set of globals * framecount + x samplers per instance, per frame.
    u32 max_sampler_count = (s->global_uniform_sampler_count * frame_count) + (config->max_instances * s->instance_uniform_sampler_count * frame_count);
    // 1 global (1*framecount) + 1 per instance, per frame.
    u32 max_ubo_count = frame_count + (config->max_instances * frame_count);
    // Total number of descriptors needed.
    u32 max_descriptor_allocate_count = max_ubo_count + max_sampler_count;

    internal_shader->max_descriptor_set_count = max_descriptor_allocate_count;
    internal_shader->max_instances = config->max_instances;

    // For now, shaders will only ever have these 2 types of descriptor pools.
    internal_shader->pool_size_count = 0;
    if (max_ubo_count > 0) {
        internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_ubo_count};
        internal_shader->pool_size_count++;
    }
    if (max_sampler_count > 0) {
        internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sampler_count};
        internal_shader->pool_size_count++;
    }

    // Global descriptor set config.
    if (has_global) {
        // Global descriptor set config.
        vulkan_descriptor_set_config *set_config = &internal_shader->descriptor_sets[internal_shader->descriptor_set_count];

        // Total bindings are 1 UBO for global (if needed), plus global sampler count.
        // This is dynamically allocated now.
        u32 ubo_count = s->global_uniform_count ? 1 : 0;
        set_config->binding_count = ubo_count + s->global_uniform_sampler_count;
        set_config->bindings = kallocate(sizeof(VkDescriptorSetLayoutBinding) * set_config->binding_count, MEMORY_TAG_ARRAY);

        // Global UBO binding is first, if present.
        u8 global_binding_index = 0;
        if (s->global_uniform_count > 0) {
            set_config->bindings[global_binding_index].binding = global_binding_index;
            set_config->bindings[global_binding_index].descriptorCount = 1;  // NOTE: the whole UBO is one binding.
            set_config->bindings[global_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set_config->bindings[global_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
            global_binding_index++;
        }

        // Set the index where the sampler bindings start. This will be used later to figure out what
        // index to begin binding sampler descriptors at.
        set_config->sampler_binding_index_start = s->global_uniform_count ? 1 : 0;

        // Add a binding for each configured sampler.
        if (s->global_uniform_sampler_count > 0) {
            for (u32 i = 0; i < s->global_uniform_sampler_count; ++i) {
                // Look up by the sampler indices collected above.
                shader_uniform_config *u = &config->uniforms[s->global_sampler_indices[i]];
                set_config->bindings[global_binding_index].binding = global_binding_index;
                set_config->bindings[global_binding_index].descriptorCount = KMAX(u->array_length, 1);  // Either treat as an array or a single texture, depending on what is passed in.
                set_config->bindings[global_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                set_config->bindings[global_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
                global_binding_index++;
            }
        }

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // If using instance uniforms, add a UBO descriptor set.
    if (has_instance) {
        // In that set, add a binding for UBO if used.
        vulkan_descriptor_set_config *set_config = &internal_shader->descriptor_sets[internal_shader->descriptor_set_count];

        // Total bindings are 1 UBO for instance (if needed), plus instance sampler count.
        // This is dynamically allocated now.
        u32 ubo_count = s->instance_uniform_count ? 1 : 0;
        set_config->binding_count = ubo_count + s->instance_uniform_sampler_count;
        set_config->bindings = kallocate(sizeof(VkDescriptorSetLayoutBinding) * set_config->binding_count, MEMORY_TAG_ARRAY);

        // Instance UBO binding is first, if present.
        u8 instance_binding_index = 0;
        if (s->instance_uniform_count > 0) {
            set_config->bindings[instance_binding_index].binding = instance_binding_index;
            set_config->bindings[instance_binding_index].descriptorCount = 1;
            set_config->bindings[instance_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set_config->bindings[instance_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
            instance_binding_index++;
        }

        // Set the index where the sampler bindings start. This will be used later to figure out what
        // index to begin binding sampler descriptors at.
        set_config->sampler_binding_index_start = s->instance_uniform_count ? 1 : 0;

        // Add a binding for each configured sampler.
        if (s->instance_uniform_sampler_count > 0) {
            for (u32 i = 0; i < s->instance_uniform_sampler_count; ++i) {
                // Look up by the sampler indices collected above.
                shader_uniform_config *u = &config->uniforms[s->instance_sampler_indices[i]];
                set_config->bindings[instance_binding_index].binding = instance_binding_index;
                set_config->bindings[instance_binding_index].descriptorCount = KMAX(u->array_length, 1);  // Either treat as an array or a single texture, depending on what is passed in.
                set_config->bindings[instance_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                set_config->bindings[instance_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
                instance_binding_index++;
            }
        }

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // Invalidate all instance states.
    internal_shader->instance_states = kallocate(sizeof(vulkan_shader_instance_state) * internal_shader->max_instances, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < internal_shader->max_instances; ++i) {
        internal_shader->instance_states[i].id = INVALID_ID;
    }

    // Keep a copy of the cull mode.
    internal_shader->cull_mode = config->cull_mode;

    // Keep a copy of the topology types.
    s->topology_types = config->topology_types;

    return true;
}

void vulkan_renderer_shader_destroy(renderer_plugin *plugin, shader *s) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (s && s->internal_data) {
        vulkan_shader *shader = s->internal_data;
        if (!shader) {
            KERROR(
                "vulkan_renderer_shader_destroy requires a valid pointer to a "
                "shader.");
            return;
        }

        VkDevice logical_device = context->device.logical_device;
        VkAllocationCallbacks *vk_allocator = context->allocator;

        // Descriptor set layouts.
        for (u32 i = 0; i < shader->descriptor_set_count; ++i) {
            if (shader->descriptor_set_layouts[i]) {
                kfree(shader->descriptor_sets[i].bindings, sizeof(VkDescriptorSetLayoutBinding) * shader->descriptor_sets[i].binding_count, MEMORY_TAG_ARRAY);
                vkDestroyDescriptorSetLayout(logical_device, shader->descriptor_set_layouts[i], vk_allocator);
                shader->descriptor_set_layouts[i] = 0;
            }
        }

        // Descriptor pool
        if (shader->descriptor_pool) {
            vkDestroyDescriptorPool(logical_device, shader->descriptor_pool, vk_allocator);
        }

        // Nuke the instance states.
        kfree(shader->instance_states, sizeof(vulkan_shader_instance_state) * shader->max_instances, MEMORY_TAG_ARRAY);

        // Uniform buffer.
        vulkan_buffer_unmap_memory(plugin, &shader->uniform_buffer, 0, VK_WHOLE_SIZE);
        shader->mapped_uniform_buffer_block = 0;
        renderer_renderbuffer_destroy(&shader->uniform_buffer);

        // Pipelines
        for (u32 i = 0; i < VULKAN_TOPOLOGY_CLASS_MAX; ++i) {
            if (shader->pipelines[i]) {
                vulkan_pipeline_destroy(context, shader->pipelines[i]);
            }
        }

        // Shader modules
        for (u32 i = 0; i < shader->stage_count; ++i) {
            vkDestroyShaderModule(context->device.logical_device, shader->stages[i].handle, context->allocator);
        }

        // Free the internal data memory.
        kfree(s->internal_data, sizeof(vulkan_shader), MEMORY_TAG_RENDERER);
        s->internal_data = 0;
    }
}

b8 vulkan_renderer_shader_initialize(renderer_plugin *plugin, shader *s) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    VkDevice logical_device = context->device.logical_device;
    VkAllocationCallbacks *vk_allocator = context->allocator;
    vulkan_shader *internal_shader = (vulkan_shader *)s->internal_data;

    // Create a module for each stage.
    kzero_memory(internal_shader->stages, sizeof(vulkan_shader_stage) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        if (!create_shader_module(context, s, &s->stage_configs[i], &internal_shader->stages[i])) {
            KERROR("Unable to create %s shader module for '%s'. Shader will be destroyed.", s->stage_configs[i].filename, s->name);
            return false;
        }
    }

    // Static lookup table for our types->Vulkan ones.
    static VkFormat *types = 0;
    static VkFormat t[11];
    if (!types) {
        t[SHADER_ATTRIB_TYPE_FLOAT32] = VK_FORMAT_R32_SFLOAT;
        t[SHADER_ATTRIB_TYPE_FLOAT32_2] = VK_FORMAT_R32G32_SFLOAT;
        t[SHADER_ATTRIB_TYPE_FLOAT32_3] = VK_FORMAT_R32G32B32_SFLOAT;
        t[SHADER_ATTRIB_TYPE_FLOAT32_4] = VK_FORMAT_R32G32B32A32_SFLOAT;
        t[SHADER_ATTRIB_TYPE_INT8] = VK_FORMAT_R8_SINT;
        t[SHADER_ATTRIB_TYPE_UINT8] = VK_FORMAT_R8_UINT;
        t[SHADER_ATTRIB_TYPE_INT16] = VK_FORMAT_R16_SINT;
        t[SHADER_ATTRIB_TYPE_UINT16] = VK_FORMAT_R16_UINT;
        t[SHADER_ATTRIB_TYPE_INT32] = VK_FORMAT_R32_SINT;
        t[SHADER_ATTRIB_TYPE_UINT32] = VK_FORMAT_R32_UINT;
        types = t;
    }

    // Process attributes
    u32 attribute_count = darray_length(s->attributes);
    u32 offset = 0;
    for (u32 i = 0; i < attribute_count; ++i) {
        // Setup the new attribute.
        VkVertexInputAttributeDescription attribute;
        attribute.location = i;
        attribute.binding = 0;
        attribute.offset = offset;
        attribute.format = types[s->attributes[i].type];

        // Push into the config's attribute collection and add to the stride.
        internal_shader->attributes[i] = attribute;

        offset += s->attributes[i].size;
    }

    // Descriptor pool.
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.poolSizeCount = internal_shader->pool_size_count;
    pool_info.pPoolSizes = internal_shader->pool_sizes;
    pool_info.maxSets = internal_shader->max_descriptor_set_count;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
#if defined(VK_USE_PLATFORM_MACOS_MVK)
    // NOTE: increase the per-stage descriptor samplers limit on macOS (maxPerStageDescriptorUpdateAfterBindSamplers > maxPerStageDescriptorSamplers)
    pool_info.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
#endif
    // Create descriptor pool.
    VkResult result = vkCreateDescriptorPool(logical_device, &pool_info, vk_allocator, &internal_shader->descriptor_pool);
    if (!vulkan_result_is_success(result)) {
        KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
        return false;
    }

    // Create descriptor set layouts.
    kzero_memory(internal_shader->descriptor_set_layouts, internal_shader->descriptor_set_count);
    for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = internal_shader->descriptor_sets[i].binding_count;
        layout_info.pBindings = internal_shader->descriptor_sets[i].bindings;

        result = vkCreateDescriptorSetLayout(logical_device, &layout_info, vk_allocator, &internal_shader->descriptor_set_layouts[i]);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_shader_initialize failed descriptor set layout: '%s'", vulkan_result_string(result, true));
            return false;
        }
    }

    // Default viewport/scissor, can be dynamically overidden.
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)context->framebuffer_height;
    viewport.width = (f32)context->framebuffer_width;
    viewport.height = -(f32)context->framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = context->framebuffer_width;
    scissor.extent.height = context->framebuffer_height;

    VkPipelineShaderStageCreateInfo stage_create_infos[VULKAN_SHADER_MAX_STAGES];
    kzero_memory(stage_create_infos, sizeof(VkPipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        stage_create_infos[i] = internal_shader->stages[i].shader_stage_create_info;
    }

    // Only dynamic topology is supported. Create one pipeline per topology class.
    // If this isn't supported, perhaps a different backend should be used.
    u32 pipeline_count = 3;

    // Create an array of pointers to pipelines, one per topology class. Null means not supported for this shader.
    internal_shader->pipelines = kallocate(sizeof(vulkan_pipeline *) * pipeline_count, MEMORY_TAG_ARRAY);

    // Create one pipeline per topology class.
    // Point class.
    if (s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST;
    }

    // Line class.
    if (s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST || s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP;
    }

    // Triangle class.
    if (s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST ||
        s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP ||
        s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN;
    }

    // Loop through and config/create one pipeline per class. Null entries are skipped.
    for (u32 i = 0; i < pipeline_count; ++i) {
        if (!internal_shader->pipelines[i]) {
            continue;
        }

        vulkan_pipeline_config pipeline_config = {0};
        pipeline_config.renderpass = internal_shader->renderpass;
        pipeline_config.stride = s->attribute_stride;
        pipeline_config.attribute_count = darray_length(s->attributes);
        pipeline_config.attributes = internal_shader->attributes;
        pipeline_config.descriptor_set_layout_count = internal_shader->descriptor_set_count;
        pipeline_config.descriptor_set_layouts = internal_shader->descriptor_set_layouts;
        pipeline_config.stage_count = internal_shader->stage_count;
        pipeline_config.stages = stage_create_infos;
        pipeline_config.viewport = viewport;
        pipeline_config.scissor = scissor;
        pipeline_config.cull_mode = internal_shader->cull_mode;
        pipeline_config.shader_flags = s->flags;
        // NOTE: Always one block for the push constant.
        pipeline_config.push_constant_range_count = 1;
        range push_constant_range;
        push_constant_range.offset = 0;
        push_constant_range.size = s->local_ubo_stride;
        pipeline_config.push_constant_ranges = &push_constant_range;
        pipeline_config.name = string_duplicate(s->name);
        pipeline_config.topology_types = s->topology_types;

        b8 pipeline_result = vulkan_graphics_pipeline_create(context, &pipeline_config, internal_shader->pipelines[i]);

        kfree(pipeline_config.name, string_length(pipeline_config.name) + 1, MEMORY_TAG_STRING);

        if (!pipeline_result) {
            KERROR("Failed to load graphics pipeline for shader: '%s'.", s->name);
            return false;
        }
    }

    // TODO: Figure out what the default should be here.
    internal_shader->bound_pipeline_index = 0;
    b8 pipeline_found = false;
    for (u32 i = 0; i < pipeline_count; ++i) {
        if (internal_shader->pipelines[i]) {
            internal_shader->bound_pipeline_index = i;

            // Extract the first type from the pipeline
            for (u32 j = 1; j < PRIMITIVE_TOPOLOGY_TYPE_MAX; j = j << 1) {
                if (internal_shader->pipelines[i]->supported_topology_types & j) {
                    switch (j) {
                        case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                            break;
                        case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                            break;
                        case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                            break;
                        case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                            break;
                        case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                            break;
                        case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN:
                            internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                            break;
                        default:
                            KWARN("primitive topology '%u' not supported. Skipping.", j);
                            break;
                    }

                    // Break out here and just assume the first one for now. This can be overidden by
                    // whatever is using the shader if need be.
                    break;
                }
            }
            pipeline_found = true;
            break;
        }
    }

    if (!pipeline_found) {
        // Getting here means that all of the pipelines are null, which they definitely should not be.
        // This is an extra failsafe to ensure configuration is at least somewhat sane.
        KERROR("No available topology classes are available, so a pipeline cannot be bound. Check shader configuration.");
        return false;
    }

    // Grab the UBO alignment requirement from the device.
    s->required_ubo_alignment = context->device.properties.limits.minUniformBufferOffsetAlignment;

    // Make sure the UBO is aligned according to device requirements.
    s->global_ubo_stride = get_aligned(s->global_ubo_size, s->required_ubo_alignment);
    s->ubo_stride = get_aligned(s->ubo_size, s->required_ubo_alignment);

    // Uniform  buffer.
    u64 total_buffer_size = s->global_ubo_stride + (s->ubo_stride * internal_shader->max_instances);
    char bufname[256];
    kzero_memory(bufname, 256);
    string_format(bufname, "renderbuffer_global_uniform");
    if (!renderer_renderbuffer_create(bufname, RENDERBUFFER_TYPE_UNIFORM, total_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST, &internal_shader->uniform_buffer)) {
        KERROR("Vulkan buffer creation failed for object shader.");
        return false;
    }
    renderer_renderbuffer_bind(&internal_shader->uniform_buffer, 0);

    // Map the entire buffer's memory.
    internal_shader->mapped_uniform_buffer_block = vulkan_buffer_map_memory(plugin, &internal_shader->uniform_buffer, 0, VK_WHOLE_SIZE);

    // NOTE: All of this below is only allocated if actually needed.
    //
    //  Allocate space for the global UBO, whcih should occupy the _stride_ space,
    //  _not_ the actual size used.
    if (s->global_ubo_size > 0 && s->global_ubo_stride > 0) {
        if (!renderer_renderbuffer_allocate(&internal_shader->uniform_buffer, s->global_ubo_stride, &s->global_ubo_offset)) {
            KERROR("Failed to allocate space for the uniform buffer!");
            return false;
        }

        // Allocate global descriptor sets, one per frame. Global is always the first set.
        // TODO: this should be dynamic based off the number of swapchain images.
        VkDescriptorSetLayout global_layouts[3] = {
            internal_shader->descriptor_set_layouts[0],
            internal_shader->descriptor_set_layouts[0],
            internal_shader->descriptor_set_layouts[0]};

        VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc_info.descriptorPool = internal_shader->descriptor_pool;
        // TODO: this should be dynamic based off the number of swapchain images.
        alloc_info.descriptorSetCount = 3;
        alloc_info.pSetLayouts = global_layouts;
        VK_CHECK(vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, internal_shader->global_descriptor_sets));

        for (u32 i = 0; i < 3; ++i) {
            char desc_set_object_name[512] = {0};
            string_format(desc_set_object_name, "desc_set_shader_%s_global_frame_%u", s->name, i);
            vulkan_set_debug_object_name(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, internal_shader->global_descriptor_sets[i], desc_set_object_name);
        }
    }

    return true;
}

b8 vulkan_renderer_shader_use(renderer_plugin *plugin, shader *shader) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_shader *s = shader->internal_data;
    vulkan_command_buffer *command_buffer = &context->graphics_command_buffers[context->image_index];
    vulkan_pipeline_bind(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipelines[s->bound_pipeline_index]);

    context->bound_shader = shader;
    // Make sure to use the current bound type as well.
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetPrimitiveTopology(command_buffer->handle, s->current_topology);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetPrimitiveTopologyEXT(command_buffer->handle, s->current_topology);
    }
    return true;
}

b8 vulkan_renderer_shader_bind_globals(renderer_plugin *plugin, shader *s) {
    if (!s) {
        return false;
    }

    // Global UBO is always at the beginning, but use this anyway.
    s->bound_ubo_offset = s->global_ubo_offset;
    return true;
}

b8 vulkan_renderer_shader_bind_instance(renderer_plugin *plugin, shader *s, u32 instance_id) {
    if (!s) {
        KERROR("vulkan_shader_bind_instance requires a valid pointer to a shader.");
        return false;
    }
    if (instance_id == INVALID_ID) {
        KERROR("Cannot bind instance INVALID_ID.");
        return false;
    }
    vulkan_shader *internal = s->internal_data;

    s->bound_instance_id = instance_id;
    vulkan_shader_instance_state *object_state = &internal->instance_states[instance_id];
    s->bound_ubo_offset = object_state->offset;
    return true;
}

b8 vulkan_renderer_shader_bind_local(renderer_plugin *plugin, shader *s) {
    if (!s) {
        return false;
    }

    // NOTE: This is intentionally blank, and does nothing. Other APIs may need to act here.
    return true;
}

b8 vulkan_renderer_shader_apply_globals(renderer_plugin *plugin, shader *s, b8 needs_update) {
    // Don't do anything if there are no updatable globals.
    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    if (!has_global) {
        return true;
    }
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    u32 image_index = context->image_index;
    vulkan_shader *internal = s->internal_data;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;
    VkDescriptorSet global_descriptor_set = internal->global_descriptor_sets[image_index];
    if (needs_update) {
        VkWriteDescriptorSet descriptor_writes[1 + VULKAN_SHADER_MAX_GLOBAL_TEXTURES];

        u32 descriptor_write_count = 0;
        u32 binding_index = 0;

        // Only update if there is actually a UBO
        if (s->global_uniform_count > 0) {
            // Apply UBO first
            VkDescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = ((vulkan_buffer *)internal->uniform_buffer.internal_data)->handle;
            bufferInfo.offset = s->global_ubo_offset;
            bufferInfo.range = s->global_ubo_stride;

            // Update descriptor sets.
            VkWriteDescriptorSet ubo_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            ubo_write.dstSet = internal->global_descriptor_sets[image_index];
            ubo_write.dstBinding = binding_index;
            ubo_write.dstArrayElement = 0;
            ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubo_write.descriptorCount = 1;
            ubo_write.pBufferInfo = &bufferInfo;
            descriptor_writes[binding_index] = ubo_write;
            descriptor_write_count++;
            binding_index++;
        }

        // Iterate samplers.
        if (s->global_uniform_sampler_count > 0) {
            vulkan_descriptor_set_config set_config = internal->descriptor_sets[0];

            // Iterate each sampler binding.
            for (u32 sb = 0; sb < s->global_uniform_sampler_count; ++sb) {
                vulkan_uniform_sampler_state *binding_sampler_state = &internal->global_sampler_uniforms[sb];

                u32 binding_descriptor_count = set_config.bindings[binding_index].descriptorCount;

                u32 update_sampler_count = 0;
                VkDescriptorImageInfo image_infos[VULKAN_SHADER_MAX_GLOBAL_TEXTURES];
                // Each sampler descriptor within the binding.
                for (u32 d = 0; d < binding_descriptor_count; ++d) {
                    // TODO: only update in the list if actually needing an update.
                    //
                    // Instead of a flat list of texture maps, the instance state should have a list of
                    // uniform samplers, each with their own list of texture maps associated with them.
                    // This will make for fast lookups/assignments here.
                    texture_map *map = binding_sampler_state->uniform_texture_maps[d];
                    // if (!map) {
                    //     continue;
                    // }
                    texture *t = map->texture;

                    // Ensure the texture is valid.
                    if (t->generation == INVALID_ID) {
                        // Texture generations are always invalid for default textures, so
                        // check first if already using one.
                        if (!texture_system_is_default_texture(t)) {
                            // If not using one, grab the default. This is only here as a failsafe
                            // and to be used while assets are loading.
                            t = texture_system_get_default_texture();
                        }
                        // If using the default texture, invalidate the map's generation so it's updated next run.
                        map->generation = INVALID_ID;
                    } else {
                        // If valid, ensure the texture map's generation matches the texture's.
                        // If not, the texture map resources should be regenerated.
                        if (t->generation != map->generation) {
                            b8 refresh_required = t->mip_levels != map->mip_levels;
                            KTRACE("A sampler refresh is%s required. Tex/map mips: %u/%u", refresh_required ? "" : " not", t->mip_levels, map->mip_levels);
                            if (refresh_required && !vulkan_renderer_texture_map_resources_refresh(plugin, map)) {
                                KWARN("Failed to refresh texture map resources. This means the sampler settings could be out of date.");
                            } else {
                                // Sync the generations.
                                map->generation = t->generation;
                            }
                        }
                    }

                    vulkan_image *image = (vulkan_image *)t->internal_data;
                    image_infos[d].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image_infos[d].imageView = image->view;
                    image_infos[d].sampler = context->samplers[map->internal_id];

                    // TODO: change up descriptor state to handle this properly.
                    // Sync frame generation if not using a default texture.
                    // if (t->generation != INVALID_ID) {
                    //     *descriptor_generation = t->generation;
                    //     *descriptor_id = t->id;
                    // }

                    update_sampler_count++;
                }

                VkWriteDescriptorSet sampler_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                sampler_descriptor.dstSet = global_descriptor_set;
                sampler_descriptor.dstBinding = binding_index;
                sampler_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                sampler_descriptor.descriptorCount = update_sampler_count;
                sampler_descriptor.pImageInfo = image_infos;

                descriptor_writes[descriptor_write_count] = sampler_descriptor;
                descriptor_write_count++;

                binding_index++;
            }
        }

        if (descriptor_write_count > 0) {
            vkUpdateDescriptorSets(context->device.logical_device, descriptor_write_count, descriptor_writes, 0, 0);
        }
    }

    // Bind the global descriptor set to be updated.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            internal->pipelines[internal->bound_pipeline_index]->pipeline_layout, 0, 1,
                            &global_descriptor_set, 0, 0);
    return true;
}

b8 vulkan_renderer_shader_apply_instance(renderer_plugin *plugin, shader *s, b8 needs_update) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_shader *internal = s->internal_data;
    if (s->instance_uniform_count < 1 && s->instance_uniform_sampler_count < 1) {
        KERROR("This shader does not use instances.");
        return false;
    }
    u32 image_index = context->image_index;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[image_index].handle;

    // Obtain instance data.
    vulkan_shader_instance_state *instance_state = &internal->instance_states[s->bound_instance_id];
    VkDescriptorSet instance_descriptor_set = instance_state->descriptor_sets[image_index];

    if (needs_update) {
        // Allocate enough descriptor writes to handle the max allowed textures per instance.
        VkWriteDescriptorSet descriptor_writes[1 + VULKAN_SHADER_MAX_INSTANCE_TEXTURES];
        kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * (1 + VULKAN_SHADER_MAX_INSTANCE_TEXTURES));

        u32 descriptor_write_count = 0;
        u32 binding_index = 0;

        // Descriptor 0 - Uniform buffer
        if (s->instance_uniform_count > 0) {
            // Only do this if the descriptor has not yet been updated.
            u8 *instance_ubo_generation = &(instance_state->ubo_descriptor_state.generations[image_index]);
            if (*instance_ubo_generation == INVALID_ID_U8) {
                VkDescriptorBufferInfo buffer_info;
                buffer_info.buffer = ((vulkan_buffer *)internal->uniform_buffer.internal_data)->handle;
                buffer_info.offset = instance_state->offset;
                buffer_info.range = s->ubo_stride;

                VkWriteDescriptorSet ubo_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                ubo_descriptor.dstSet = instance_descriptor_set;
                ubo_descriptor.dstBinding = binding_index;
                ubo_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                ubo_descriptor.descriptorCount = 1;
                ubo_descriptor.pBufferInfo = &buffer_info;

                descriptor_writes[descriptor_write_count] = ubo_descriptor;
                descriptor_write_count++;

                // Update the frame generation. In this case it is only needed once since this is a buffer.
                *instance_ubo_generation = 1;
            }
            binding_index++;
        }

        // Iterate samplers.
        if (s->instance_uniform_sampler_count > 0) {
            b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
            u8 instance_desc_set_index = has_global ? 1 : 0;
            vulkan_descriptor_set_config set_config = internal->descriptor_sets[instance_desc_set_index];

            // Iterate each sampler binding.
            for (u32 sb = 0; sb < s->instance_uniform_sampler_count; ++sb) {
                vulkan_uniform_sampler_state *binding_sampler_state = &instance_state->sampler_uniforms[sb];

                u32 binding_descriptor_count = set_config.bindings[binding_index].descriptorCount;

                u32 update_sampler_count = 0;
                VkDescriptorImageInfo image_infos[VULKAN_SHADER_MAX_INSTANCE_TEXTURES];
                // Each sampler descriptor within the binding.
                for (u32 d = 0; d < binding_descriptor_count; ++d) {
                    // TODO: only update in the list if actually needing an update.
                    //
                    // Instead of a flat list of texture maps, the instance state should have a list of
                    // uniform samplers, each with their own list of texture maps associated with them.
                    // This will make for fast lookups/assignments here.
                    texture_map *map = binding_sampler_state->uniform_texture_maps[d];
                    // if (!map) {
                    //     continue;
                    // }
                    texture *t = map->texture;

                    // Ensure the texture is valid.
                    if (t->generation == INVALID_ID) {
                        // Texture generations are always invalid for default textures, so
                        // check first if already using one.
                        if (!texture_system_is_default_texture(t)) {
                            // If not using one, grab the default. This is only here as a failsafe
                            // and to be used while assets are loading.
                            t = texture_system_get_default_texture();
                        }
                        // If using the default texture, invalidate the map's generation so it's updated next run.
                        map->generation = INVALID_ID;
                    } else {
                        // If valid, ensure the texture map's generation matches the texture's.
                        // If not, the texture map resources should be regenerated.
                        if (t->generation != map->generation) {
                            b8 refresh_required = t->mip_levels != map->mip_levels;
                            KTRACE("A sampler refresh is%s required. Tex/map mips: %u/%u", refresh_required ? "" : " not", t->mip_levels, map->mip_levels);
                            if (refresh_required && !vulkan_renderer_texture_map_resources_refresh(plugin, map)) {
                                KWARN("Failed to refresh texture map resources. This means the sampler settings could be out of date.");
                            } else {
                                // Sync the generations.
                                map->generation = t->generation;
                            }
                        }
                    }

                    vulkan_image *image = (vulkan_image *)t->internal_data;
                    image_infos[d].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image_infos[d].imageView = image->view;
                    image_infos[d].sampler = context->samplers[map->internal_id];

                    // TODO: change up descriptor state to handle this properly.
                    // Sync frame generation if not using a default texture.
                    // if (t->generation != INVALID_ID) {
                    //     *descriptor_generation = t->generation;
                    //     *descriptor_id = t->id;
                    // }

                    update_sampler_count++;
                }

                VkWriteDescriptorSet sampler_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                sampler_descriptor.dstSet = instance_descriptor_set;
                sampler_descriptor.dstBinding = binding_index;
                sampler_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                sampler_descriptor.descriptorCount = update_sampler_count;
                sampler_descriptor.pImageInfo = image_infos;

                descriptor_writes[descriptor_write_count] = sampler_descriptor;
                descriptor_write_count++;

                binding_index++;
            }
        }

        if (descriptor_write_count > 0) {
            vkUpdateDescriptorSets(context->device.logical_device, descriptor_write_count, descriptor_writes, 0, 0);
        }
    }

    // Determine the descriptor set index which will be first. If there are no globals, for example,
    // this will be 0. If there are globals, this will be 1.
    u32 first_set = 1;
    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    if (!has_global) {
        first_set = 0;
    }
    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            internal->pipelines[internal->bound_pipeline_index]->pipeline_layout, first_set, 1,
                            &instance_descriptor_set, 0, 0);
    return true;
}

static VkSamplerAddressMode convert_repeat_type(const char *axis,
                                                texture_repeat repeat) {
    switch (repeat) {
        case TEXTURE_REPEAT_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case TEXTURE_REPEAT_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case TEXTURE_REPEAT_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case TEXTURE_REPEAT_CLAMP_TO_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:
            KWARN(
                "convert_repeat_type(axis='%s') Type '%x' not supported, defaulting "
                "to repeat.",
                axis, repeat);
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkFilter convert_filter_type(const char *op, texture_filter filter) {
    switch (filter) {
        case TEXTURE_FILTER_MODE_NEAREST:
            return VK_FILTER_NEAREST;
        case TEXTURE_FILTER_MODE_LINEAR:
            return VK_FILTER_LINEAR;
        default:
            KWARN(
                "convert_filter_type(op='%s'): Unsupported filter type '%x', "
                "defaulting to linear.",
                op, filter);
            return VK_FILTER_LINEAR;
    }
}

static b8 create_sampler(vulkan_context *context, texture_map *map, VkSampler *sampler) {
    // Create a sampler for the texture
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    // Sync the mip levels with that of the assigned texture.
    map->mip_levels = map->texture->mip_levels;

    sampler_info.minFilter = convert_filter_type("min", map->filter_minify);
    sampler_info.magFilter = convert_filter_type("mag", map->filter_magnify);

    sampler_info.addressModeU = convert_repeat_type("U", map->repeat_u);
    sampler_info.addressModeV = convert_repeat_type("V", map->repeat_v);
    sampler_info.addressModeW = convert_repeat_type("W", map->repeat_w);

    // TODO: Configurable
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 16;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    // sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    // Use the full range of mips available.
    sampler_info.minLod = 0.0f;
    // NOTE: Uncomment the following line to test the lowest mip level.
    /* sampler_info.minLod = map->texture->mip_levels > 1 ? map->texture->mip_levels : 0.0f; */
    sampler_info.maxLod = map->texture->mip_levels;

    VkResult result = vkCreateSampler(context->device.logical_device, &sampler_info, context->allocator, sampler);
    if (!vulkan_result_is_success(VK_SUCCESS)) {
        KERROR("Error creating texture sampler: %s", vulkan_result_string(result, true));
        return false;
    }

    return true;
}

b8 vulkan_renderer_texture_map_resources_acquire(renderer_plugin *plugin, texture_map *map) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Find a free sampler.
    u32 sampler_count = darray_length(context->samplers);
    u32 selected_id = INVALID_ID;
    for (u32 i = 0; i < sampler_count; ++i) {
        if (context->samplers[i] == 0) {
            selected_id = i;
            break;
        }
    }
    if (selected_id == INVALID_ID) {
        // Push an empty entry into the array.
        darray_push(context->samplers, 0);
        selected_id = sampler_count;
    }
    if (!create_sampler(context, map, &context->samplers[selected_id])) {
        return false;
    }

    char formatted_name[TEXTURE_NAME_MAX_LENGTH] = {0};
    string_format(formatted_name, "%s_texmap_sampler", map->texture->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_SAMPLER, context->samplers[selected_id], formatted_name);
    map->internal_id = selected_id;

    return true;
}

void vulkan_renderer_texture_map_resources_release(renderer_plugin *plugin, texture_map *map) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (map && map->internal_id != INVALID_ID) {
        // Make sure there's no way this is in use.
        vkDeviceWaitIdle(context->device.logical_device);
        vkDestroySampler(context->device.logical_device, context->samplers[map->internal_id], context->allocator);
        context->samplers[map->internal_id] = 0;
        map->internal_id = INVALID_ID;
    }
}

b8 vulkan_renderer_texture_map_resources_refresh(renderer_plugin *plugin, texture_map *map) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (map && map->internal_id != INVALID_ID) {
        // Create a new sampler first.
        VkSampler new_sampler = 0;
        if (!create_sampler(context, map, &new_sampler)) {
            return false;
        }

        // Take a pointer to the current sampler.
        VkSampler old_sampler = context->samplers[map->internal_id];

        // Make sure there's no way this is in use.
        vkDeviceWaitIdle(context->device.logical_device);
        // Assign the new.
        context->samplers[map->internal_id] = new_sampler;
        // Destroy the old.
        vkDestroySampler(context->device.logical_device, old_sampler, context->allocator);
    }
    return true;
}

b8 vulkan_renderer_shader_instance_resources_acquire(renderer_plugin *plugin, struct shader *s, const shader_instance_resource_config *config, u32 *out_instance_id) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_shader *internal = s->internal_data;

    *out_instance_id = INVALID_ID;
    for (u32 i = 0; i < internal->max_instances; ++i) {
        if (internal->instance_states[i].id == INVALID_ID) {
            internal->instance_states[i].id = i;
            *out_instance_id = i;
            break;
        }
    }
    if (*out_instance_id == INVALID_ID) {
        KERROR("vulkan_shader_acquire_instance_resources failed to acquire new id for shader '%s', max instances=%u", s->name, internal->max_instances);
        return false;
    }

    texture *default_texture = texture_system_get_default_texture();

    // Map texture maps in the config to the correct uniforms
    vulkan_shader_instance_state *instance_state = &internal->instance_states[*out_instance_id];
    // Only setup if the shader actually requires it.
    if (s->instance_texture_count > 0) {
        instance_state->sampler_uniforms = kallocate(sizeof(vulkan_uniform_sampler_state) * s->instance_uniform_sampler_count, MEMORY_TAG_ARRAY);

        // Assign uniforms to each of the sampler states.
        for (u32 ii = 0; ii < s->instance_uniform_sampler_count; ++ii) {
            vulkan_uniform_sampler_state *sampler_state = &instance_state->sampler_uniforms[ii];
            sampler_state->uniform = &s->uniforms[s->instance_sampler_indices[ii]];

            // Grab the uniform texture config as well.
            shader_instance_uniform_texture_config *tc = &config->uniform_configs[ii];

            u32 array_length = KMAX(sampler_state->uniform->array_length, 1);
            // Setup the array for the sampler texture maps.
            sampler_state->uniform_texture_maps = kallocate(sizeof(texture_map *) * array_length, MEMORY_TAG_ARRAY);
            // Setup descriptor states
            sampler_state->descriptor_states = kallocate(sizeof(vulkan_descriptor_state) * array_length, MEMORY_TAG_ARRAY);
            // Per descriptor
            for (u32 d = 0; d < array_length; ++d) {
                sampler_state->uniform_texture_maps[d] = tc->texture_maps[d];
                // Make sure it has a texture map assigned. Use default if not.
                if (!sampler_state->uniform_texture_maps[d]->texture) {
                    sampler_state->uniform_texture_maps[d]->texture = default_texture;
                }
                // Per frame
                // TODO: handle different frame counts.
                for (u32 j = 0; j < 3; ++j) {
                    sampler_state->descriptor_states[d].generations[j] = INVALID_ID_U8;
                    sampler_state->descriptor_states[d].ids[j] = INVALID_ID;
                }
            }
        }
    }

    // Allocate some space in the UBO - by the stride, not the size.
    u64 size = s->ubo_stride;
    if (size > 0) {
        if (!renderer_renderbuffer_allocate(&internal->uniform_buffer, size, &instance_state->offset)) {
            KERROR("vulkan_material_shader_acquire_resources failed to acquire ubo space");
            return false;
        }
    }

    // UBO binding. NOTE: really only matters where there are instance uniforms, but set them anyway.
    for (u32 j = 0; j < 3; ++j) {
        instance_state->ubo_descriptor_state.generations[j] = INVALID_ID_U8;
        instance_state->ubo_descriptor_state.ids[j] = INVALID_ID_U8;
    }

    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    u8 instance_desc_set_index = has_global ? 1 : 0;
    // TODO: handle different frame counts.
    // Allocate 3 descriptor sets (one per frame).
    VkDescriptorSetLayout layouts[3] = {
        internal->descriptor_set_layouts[instance_desc_set_index],
        internal->descriptor_set_layouts[instance_desc_set_index],
        internal->descriptor_set_layouts[instance_desc_set_index]};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = internal->descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, instance_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating instance descriptor sets in shader: '%s'.", vulkan_result_string(result, true));
        return false;
    }

    for (u32 i = 0; i < 3; ++i) {
        char desc_set_object_name[512] = {0};
        string_format(desc_set_object_name, "desc_set_shader_%s_instance_frame_%u", s->name, i);
        vulkan_set_debug_object_name(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, instance_state->descriptor_sets[i], desc_set_object_name);
    }

    return true;
}

b8 vulkan_renderer_shader_instance_resources_release(renderer_plugin *plugin, shader *s, u32 instance_id) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_shader *internal = s->internal_data;
    vulkan_shader_instance_state *instance_state = &internal->instance_states[instance_id];

    // Wait for any pending operations using the descriptor set to finish.
    vkDeviceWaitIdle(context->device.logical_device);

    // Free 3 descriptor sets (one per frame)
    VkResult result = vkFreeDescriptorSets(context->device.logical_device, internal->descriptor_pool, 3, instance_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error freeing object shader descriptor sets!");
    }

    // Invalidate UBO descriptor state.
    for (u32 j = 0; j < 3; ++j) {
        instance_state->ubo_descriptor_state.generations[j] = INVALID_ID_U8;
        instance_state->ubo_descriptor_state.ids[j] = INVALID_ID_U8;
    }

    // Destroy bindings and their descriptor states/uniforms.
    for (u32 a = 0; a < s->instance_uniform_sampler_count; ++a) {
        vulkan_uniform_sampler_state *sampler_state = &instance_state->sampler_uniforms[a];
        u32 array_length = KMAX(sampler_state->uniform->array_length, 1);
        kfree(sampler_state->descriptor_states, sizeof(vulkan_descriptor_state) * array_length, MEMORY_TAG_ARRAY);
        sampler_state->descriptor_states = 0;
        kfree(sampler_state->uniform_texture_maps, sizeof(texture_map *) * array_length, MEMORY_TAG_ARRAY);
        sampler_state->uniform_texture_maps = 0;
    }

    if (s->ubo_stride != 0) {
        if (!renderer_renderbuffer_free(&internal->uniform_buffer, s->ubo_stride, instance_state->offset)) {
            KERROR("vulkan_renderer_shader_release_instance_resources failed to free range from renderbuffer.");
        }
    }
    instance_state->offset = INVALID_ID;
    instance_state->id = INVALID_ID;

    return true;
}

static b8 sampler_state_try_set(vulkan_uniform_sampler_state *sampler_uniforms, u32 sampler_count, u16 uniform_location, u32 array_index, texture_map *map) {
    // Find the sampler uniform state to update.
    for (u32 i = 0; i < sampler_count; ++i) {
        vulkan_uniform_sampler_state *su = &sampler_uniforms[i];
        if (su->uniform->location == uniform_location) {
            if (su->uniform->array_length > 1) {
                if (array_index >= su->uniform->array_length) {
                    KERROR("vulkan_renderer_uniform_set error: array_index (%u) is out of range (0-%u)", array_index, su->uniform->array_length);
                    return false;
                }
                su->uniform_texture_maps[array_index] = map;
            } else {
                su->uniform_texture_maps[0] = map;
            }
            return true;
        }
    }
    KERROR("sampler_state_try_set: Unable to find uniform location %u. Sampler uniform not set.", uniform_location);
    return false;
}

b8 vulkan_renderer_uniform_set(renderer_plugin *plugin, shader *s, shader_uniform *uniform, u32 array_index, const void *value) {
    vulkan_shader *internal = s->internal_data;
    if (uniform_type_is_sampler(uniform->type)) {
        // Samplers can only be assigned at the instance or global level.
        texture_map *map = (texture_map *)value;
        if (uniform->scope == SHADER_SCOPE_GLOBAL) {
            return sampler_state_try_set(internal->global_sampler_uniforms, s->global_uniform_sampler_count, uniform->location, array_index, map);
        } else {
            vulkan_shader_instance_state *instance_state = &internal->instance_states[s->bound_instance_id];
            return sampler_state_try_set(instance_state->sampler_uniforms, s->instance_uniform_sampler_count, uniform->location, array_index, map);
        }
    } else {
        if (uniform->scope == SHADER_SCOPE_LOCAL) {
            u64 addr = (u64)internal->local_push_constant_block;
            addr += uniform->offset + (uniform->size * array_index);
            kcopy_memory((void *)addr, value, uniform->size);
        } else {
            // Map the appropriate memory location and copy the data over.
            u64 addr = (u64)internal->mapped_uniform_buffer_block;
            addr += s->bound_ubo_offset + uniform->offset + (uniform->size * array_index);
            kcopy_memory((void *)addr, value, uniform->size);
        }
    }
    return true;
}

b8 vulkan_renderer_shader_apply_local(renderer_plugin *plugin, shader *s) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_shader *internal = s->internal_data;
    VkCommandBuffer command_buffer = context->graphics_command_buffers[context->image_index].handle;
    vkCmdPushConstants(
        command_buffer,
        internal->pipelines[internal->bound_pipeline_index]->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, 128, internal->local_push_constant_block);
    return true;
}

static b8 create_shader_module(vulkan_context *context, shader *s, shader_stage_config *config, vulkan_shader_stage *out_stage) {
    shaderc_shader_kind shader_kind;
    char *shader_type_str = 0;
    VkShaderStageFlagBits stage;
    switch (config->stage) {
        case SHADER_STAGE_VERTEX:
            shader_kind = shaderc_glsl_default_vertex_shader;
            shader_type_str = "vertex";
            stage = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case SHADER_STAGE_FRAGMENT:
            shader_kind = shaderc_glsl_default_fragment_shader;
            shader_type_str = "fragment";
            stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case SHADER_STAGE_COMPUTE:
            shader_kind = shaderc_glsl_default_compute_shader;
            shader_type_str = "compute";
            stage = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        case SHADER_STAGE_GEOMETRY:
            shader_kind = shaderc_glsl_default_geometry_shader;
            shader_type_str = "geometry";
            stage = VK_SHADER_STAGE_GEOMETRY_BIT;
            break;
        default:
            KERROR("Unsupported shader kind. Unable to create module.");
            return false;
    }

    KDEBUG("Compiling stage '%s' for shader '%s'...", shader_type_str, s->name);

    // Attempt to compile the shader.
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        context->shader_compiler,
        config->source,
        config->source_length,
        shader_kind,
        config->filename,
        "main",
        0);

    if (!compilation_result) {
        KERROR("An unknown error occurred while trying to compile the shader. Unable to process futher.");
        return false;
    }
    shaderc_compilation_status status = shaderc_result_get_compilation_status(compilation_result);

    // Handle errors, if any.
    if (status != shaderc_compilation_status_success) {
        const char *error_message = shaderc_result_get_error_message(compilation_result);
        u64 error_count = shaderc_result_get_num_errors(compilation_result);
        KERROR("Error compiling shader with %llu errors.", error_count);
        KERROR("Error(s):\n%s", error_message);
        shaderc_result_release(compilation_result);
        return false;
    }

    KDEBUG("Shader compiled successfully.");

    // Output warnings if there are any.
    u64 warning_count = shaderc_result_get_num_warnings(compilation_result);
    if (warning_count) {
        // NOTE: Not sure this it the correct way to obtain warnings.
        KWARN("%llu warnings were generated during shader compilation:\n%s", warning_count, shaderc_result_get_error_message(compilation_result));
    }

    // Extract the data from the result.
    const char *bytes = shaderc_result_get_bytes(compilation_result);
    size_t result_length = shaderc_result_get_length(compilation_result);
    // Take a copy of the result data and cast it to a u32* as is required by Vulkan.
    u32 *code = kallocate(result_length, MEMORY_TAG_RENDERER);
    kcopy_memory(code, bytes, result_length);

    // Release the compilation result.
    shaderc_result_release(compilation_result);

    kzero_memory(&out_stage->create_info, sizeof(VkShaderModuleCreateInfo));
    out_stage->create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    out_stage->create_info.codeSize = result_length;
    out_stage->create_info.pCode = code;

    VK_CHECK(vkCreateShaderModule(context->device.logical_device, &out_stage->create_info, context->allocator, &out_stage->handle));

    // Release the copy of the code.
    kfree(code, result_length, MEMORY_TAG_RENDERER);

    // Shader stage info
    kzero_memory(&out_stage->shader_stage_create_info, sizeof(VkPipelineShaderStageCreateInfo));
    out_stage->shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    out_stage->shader_stage_create_info.stage = stage;
    out_stage->shader_stage_create_info.module = out_stage->handle;
    out_stage->shader_stage_create_info.pName = "main";

    return true;
}

b8 vulkan_renderpass_create(renderer_plugin *plugin, const renderpass_config *config, renderpass *out_renderpass) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    out_renderpass->internal_data = kallocate(sizeof(vulkan_renderpass), MEMORY_TAG_RENDERER);
    vulkan_renderpass *internal_data = (vulkan_renderpass *)out_renderpass->internal_data;

    internal_data->depth = config->depth;
    internal_data->stencil = config->stencil;

    // Main subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Attachments.
    VkAttachmentDescription *attachment_descriptions = darray_create(VkAttachmentDescription);
    VkAttachmentDescription *colour_attachment_descs = darray_create(VkAttachmentDescription);
    VkAttachmentDescription *depth_attachment_descs = darray_create(VkAttachmentDescription);

    // Can always just look at the first target since they are all the same (one
    // per frame). render_target* target = &out_renderpass->targets[0];
    for (u32 i = 0; i < config->target.attachment_count; ++i) {
        render_target_attachment_config *attachment_config = &config->target.attachments[i];

        VkAttachmentDescription attachment_desc = {};
        if (attachment_config->type == RENDER_TARGET_ATTACHMENT_TYPE_COLOUR) {
            // Colour attachment.
            b8 do_clear_colour = (out_renderpass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;

            if (attachment_config->source == RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT) {
                attachment_desc.format = context->swapchain.image_format.format;
            } else {
                // TODO: configurable format?
                attachment_desc.format = VK_FORMAT_R8G8B8A8_UNORM;
            }

            attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
            // attachment_desc.loadOp = do_clear_colour ? VK_ATTACHMENT_LOAD_OP_CLEAR
            // : VK_ATTACHMENT_LOAD_OP_LOAD;

            // Determine which load operation to use.
            if (attachment_config->load_operation == RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE) {
                // If we don't care, the only other thing that needs checking is if the
                // attachment is being cleared.
                attachment_desc.loadOp = do_clear_colour ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            } else {
                // If we are loading, check if we are also clearing. This combination
                // doesn't make sense, and should be warned about.
                if (attachment_config->load_operation == RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD) {
                    if (do_clear_colour) {
                        KWARN(
                            "Colour attachment load operation set to load, but is also "
                            "set to clear. This combination is invalid, and will err "
                            "toward clearing. Verify attachment configuration.");
                        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    } else {
                        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                } else {
                    KFATAL(
                        "Invalid and unsupported combination of load operation (0x%x) "
                        "and clear flags (0x%x) for colour attachment.",
                        attachment_desc.loadOp, out_renderpass->clear_flags);
                    return false;
                }
            }

            // Determine which store operation to use.
            if (attachment_config->store_operation == RENDER_TARGET_ATTACHMENT_STORE_OPERATION_DONT_CARE) {
                attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            } else if (attachment_config->store_operation == RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE) {
                attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            } else {
                KFATAL("Invalid store operation (0x%x) set for colour attachment. Check configuration.", attachment_config->store_operation);
                return false;
            }

            // NOTE: these will never be used on a colour attachment.
            attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // If loading, that means coming from another pass, meaning the format
            // should be VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. Otherwise it is
            // undefined.
            attachment_desc.initialLayout =
                attachment_config->load_operation ==
                        RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD
                    ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_UNDEFINED;

            // If this is the last pass writing to this attachment, present after
            // should be set to true.
            attachment_desc.finalLayout =
                attachment_config->present_after
                    ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // Transitioned to
                                                                 // after the render
                                                                 // pass
            attachment_desc.flags = 0;

            // Push to colour attachments array.
            darray_push(colour_attachment_descs, attachment_desc);
        } else if (attachment_config->type & RENDER_TARGET_ATTACHMENT_TYPE_DEPTH || attachment_config->type & RENDER_TARGET_ATTACHMENT_TYPE_STENCIL) {
            // Depth attachment.
            b8 do_clear_depth = (out_renderpass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
            b8 do_clear_stencil = (out_renderpass->clear_flags & RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG) != 0;

            if (attachment_config->source == RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT) {
                attachment_desc.format = context->device.depth_format;
            } else {
                // TODO: There may be a more optimal format to use when not the default
                // depth target.
                attachment_desc.format = context->device.depth_format;
            }

            attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
            // Determine which load operation to use.
            if (attachment_config->load_operation == RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE) {
                // If we don't care, the only other thing that needs checking is if the
                // attachment is being cleared.
                attachment_desc.loadOp = do_clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment_desc.stencilLoadOp = do_clear_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            } else {
                // If we are loading, check if we are also clearing. This combination
                // doesn't make sense, and should be warned about.
                if (attachment_config->load_operation == RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD) {
                    // Depth
                    if (do_clear_depth) {
                        KWARN(
                            "Depth attachment load operation set to load, but is also "
                            "set to clear. This combination is invalid, and will err "
                            "toward clearing. Verify attachment configuration.");
                        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    } else {
                        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                    // Stencil
                    if (do_clear_stencil) {
                        KWARN(
                            "Stencil attachment load operation set to load, but is also "
                            "set to clear. This combination is invalid, and will err "
                            "toward clearing. Verify attachment configuration.");
                        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    } else {
                        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                } else {
                    KFATAL(
                        "Invalid and unsupported combination of load operation (0x%x) "
                        "and clear flags (0x%x) for depth attachment.",
                        attachment_desc.loadOp, out_renderpass->clear_flags);
                    return false;
                }
            }

            // Determine which store operation to use.
            if (attachment_config->store_operation == RENDER_TARGET_ATTACHMENT_STORE_OPERATION_DONT_CARE) {
                attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            } else if (attachment_config->store_operation == RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE) {
                attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            } else {
                KFATAL("Invalid store operation (0x%x) set for depth attachment. Check configuration.", attachment_config->store_operation);
                return false;
            }

            // If coming from a previous pass, should already be
            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL. Otherwise undefined.
            attachment_desc.initialLayout =
                attachment_config->load_operation == RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_UNDEFINED;
            // Final layout for depth stencil attachments is always this.
            attachment_desc.finalLayout =
                attachment_config->present_after ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // Push to colour attachments array.
            darray_push(depth_attachment_descs, attachment_desc);
        }
        // Push to general array.
        darray_push(attachment_descriptions, attachment_desc);
    }

    // Setup the attachment references.
    u32 attachments_added = 0;

    // Colour attachment reference.
    VkAttachmentReference *colour_attachment_references = 0;
    u32 colour_attachment_count = darray_length(colour_attachment_descs);
    if (colour_attachment_count > 0) {
        colour_attachment_references = kallocate(sizeof(VkAttachmentReference) * colour_attachment_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < colour_attachment_count; ++i) {
            colour_attachment_references[i].attachment = attachments_added;  // Attachment description array index
            colour_attachment_references[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments_added++;
        }

        subpass.colorAttachmentCount = colour_attachment_count;
        subpass.pColorAttachments = colour_attachment_references;
    } else {
        subpass.colorAttachmentCount = 0;
        subpass.pColorAttachments = 0;
    }

    // Depth attachment reference.
    VkAttachmentReference *depth_attachment_references = 0;
    u32 depth_attachment_count = darray_length(depth_attachment_descs);
    if (depth_attachment_count > 0) {
        KASSERT_MSG(depth_attachment_count == 1, "Multiple depth/stencil attachments not supported.");
        depth_attachment_references = kallocate(sizeof(VkAttachmentReference) * depth_attachment_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < depth_attachment_count; ++i) {
            depth_attachment_references[i].attachment = attachments_added;  // Attachment description array index
            depth_attachment_references[i].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments_added++;
        }

        // Depth stencil data.
        subpass.pDepthStencilAttachment = depth_attachment_references;
    } else {
        subpass.pDepthStencilAttachment = 0;
    }

    // Input from a shader
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = 0;

    // Attachments used for multisampling colour attachments
    subpass.pResolveAttachments = 0;

    // Attachments not used in this subpass, but must be preserved for the next.
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = 0;

    // Render pass dependencies. TODO: make this configurable.
    VkSubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    // Render pass create.
    VkRenderPassCreateInfo render_pass_create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_create_info.attachmentCount = darray_length(attachment_descriptions);

    render_pass_create_info.pAttachments = attachment_descriptions;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;
    render_pass_create_info.pNext = 0;
    render_pass_create_info.flags = 0;

    VK_CHECK(vkCreateRenderPass(context->device.logical_device,
                                &render_pass_create_info, context->allocator,
                                &internal_data->handle));

    // Cleanup
    if (attachment_descriptions) {
        darray_destroy(attachment_descriptions);
    }

    if (colour_attachment_descs) {
        darray_destroy(colour_attachment_descs);
    }
    if (colour_attachment_references) {
        kfree(colour_attachment_references,
              sizeof(VkAttachmentReference) * colour_attachment_count,
              MEMORY_TAG_ARRAY);
    }

    if (depth_attachment_descs) {
        darray_destroy(depth_attachment_descs);
    }
    if (depth_attachment_references) {
        kfree(depth_attachment_references, sizeof(VkAttachmentReference) * depth_attachment_count, MEMORY_TAG_ARRAY);
    }

    return true;
}

void vulkan_renderpass_destroy(renderer_plugin *plugin, renderpass *pass) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (pass && pass->internal_data) {
        vulkan_renderpass *internal_data = pass->internal_data;
        vkDestroyRenderPass(context->device.logical_device, internal_data->handle,
                            context->allocator);
        internal_data->handle = 0;
        kfree(internal_data, sizeof(vulkan_renderpass), MEMORY_TAG_RENDERER);
        pass->internal_data = 0;
    }
}

b8 vulkan_renderer_render_target_create(renderer_plugin *plugin,
                                        u8 attachment_count,
                                        render_target_attachment *attachments,
                                        renderpass *pass, u32 width, u32 height,
                                        u16 layer_index,
                                        render_target *out_target) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    // Max number of attachments
    VkImageView attachment_views[32] = {0};
    for (u32 i = 0; i < attachment_count; ++i) {
        vulkan_image *internal = (vulkan_image *)attachments[i].texture->internal_data;
        if (internal->layer_views) {
            attachment_views[i] = internal->layer_views[layer_index];
        } else {
            attachment_views[i] = internal->view;
        }
    }
    kcopy_memory(out_target->attachments, attachments, sizeof(render_target_attachment) * attachment_count);

    VkFramebufferCreateInfo framebuffer_create_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_create_info.renderPass = ((vulkan_renderpass *)pass->internal_data)->handle;
    framebuffer_create_info.attachmentCount = attachment_count;
    framebuffer_create_info.pAttachments = attachment_views;
    framebuffer_create_info.width = width;
    framebuffer_create_info.height = height;
    framebuffer_create_info.layers = 1;

    VK_CHECK(vkCreateFramebuffer(
        context->device.logical_device, &framebuffer_create_info,
        context->allocator, (VkFramebuffer *)&out_target->internal_framebuffer));

    char formatted_name[512] = {0};
    string_format(formatted_name, "pass_%s_framebuffer_%u_x_%u", pass->name, width, height);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_FRAMEBUFFER, out_target->internal_framebuffer, formatted_name);
    KTRACE("Created framebuffer ' %s' at 0x%x.", formatted_name, out_target->internal_framebuffer);
    return true;
}

void vulkan_renderer_render_target_destroy(renderer_plugin *plugin,
                                           render_target *target,
                                           b8 free_internal_memory) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (target && target->internal_framebuffer) {
        vkDestroyFramebuffer(context->device.logical_device,
                             (VkFramebuffer)target->internal_framebuffer,
                             context->allocator);
        target->internal_framebuffer = 0;
        if (free_internal_memory) {
            kfree(target->attachments,
                  sizeof(render_target_attachment) * target->attachment_count,
                  MEMORY_TAG_ARRAY);
            target->attachments = 0;
            target->attachment_count = 0;
        }
    }
}

texture *vulkan_renderer_window_attachment_get(renderer_plugin *plugin,
                                               u8 index) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (index >= context->swapchain.image_count) {
        KFATAL(
            "Attempting to get colour attachment index out of range: %d. "
            "Attachment count: %d",
            index, context->swapchain.image_count);
        return 0;
    }

    return &context->swapchain.render_textures[index];
}
texture *vulkan_renderer_depth_attachment_get(renderer_plugin *plugin,
                                              u8 index) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (index >= context->swapchain.image_count) {
        KFATAL(
            "Attempting to get depth attachment index out of range: %d. "
            "Attachment count: %d",
            index, context->swapchain.image_count);
        return 0;
    }

    return &context->swapchain.depth_textures[index];
}
u8 vulkan_renderer_window_attachment_index_get(renderer_plugin *plugin) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    return (u8)context->image_index;
}

u8 vulkan_renderer_window_attachment_count_get(renderer_plugin *plugin) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    return (u8)context->swapchain.image_count;
}

b8 vulkan_renderer_is_multithreaded(renderer_plugin *plugin) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    return context->multithreading_enabled;
}

b8 vulkan_renderer_flag_enabled_get(renderer_plugin *plugin,
                                    renderer_config_flags flag) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    return (context->swapchain.flags & flag);
}

void vulkan_renderer_flag_enabled_set(renderer_plugin *plugin,
                                      renderer_config_flags flag, b8 enabled) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    context->swapchain.flags = (enabled ? (context->swapchain.flags | flag)
                                        : (context->swapchain.flags & ~flag));
    context->render_flag_changed = true;
}

// NOTE: Begin vulkan buffer.

// Indicates if the provided buffer has device-local memory.
static b8 vulkan_buffer_is_device_local(renderer_plugin *plugin,
                                        vulkan_buffer *buffer) {
    return (buffer->memory_property_flags &
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

// Indicates if the provided buffer has host-visible memory.
static b8 vulkan_buffer_is_host_visible(renderer_plugin *plugin,
                                        vulkan_buffer *buffer) {
    return (buffer->memory_property_flags &
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ==
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}

// Indicates if the provided buffer has host-coherent memory.
static b8 vulkan_buffer_is_host_coherent(renderer_plugin *plugin,
                                         vulkan_buffer *buffer) {
    return (buffer->memory_property_flags &
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ==
           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

b8 vulkan_buffer_create_internal(renderer_plugin *plugin, renderbuffer *buffer) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer) {
        KERROR("vulkan_buffer_create_internal requires a valid pointer to a buffer.");
        return false;
    }

    vulkan_buffer internal_buffer;

    switch (buffer->type) {
        case RENDERBUFFER_TYPE_VERTEX:
            internal_buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            internal_buffer.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case RENDERBUFFER_TYPE_INDEX:
            internal_buffer.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            internal_buffer.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case RENDERBUFFER_TYPE_UNIFORM: {
            u32 device_local_bits = context->device.supports_device_local_host_visible
                                        ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                        : 0;
            internal_buffer.usage =
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            internal_buffer.memory_property_flags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | device_local_bits;
        } break;
        case RENDERBUFFER_TYPE_STAGING:
            internal_buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            internal_buffer.memory_property_flags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case RENDERBUFFER_TYPE_READ:
            internal_buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            internal_buffer.memory_property_flags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case RENDERBUFFER_TYPE_STORAGE:
            KERROR("Storage buffer not yet supported.");
            return false;
        default:
            KERROR("Unsupported buffer type: %i", buffer->type);
            return false;
    }

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = buffer->total_size;
    buffer_info.usage = internal_buffer.usage;
    buffer_info.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;  // NOTE: Only used in one queue.

    VK_CHECK(vkCreateBuffer(context->device.logical_device, &buffer_info,
                            context->allocator, &internal_buffer.handle));

    // Gather memory requirements.
    vkGetBufferMemoryRequirements(context->device.logical_device,
                                  internal_buffer.handle,
                                  &internal_buffer.memory_requirements);
    internal_buffer.memory_index = context->find_memory_index(
        context, internal_buffer.memory_requirements.memoryTypeBits,
        internal_buffer.memory_property_flags);
    if (internal_buffer.memory_index == -1) {
        KERROR(
            "Unable to create vulkan buffer because the required memory type "
            "index was not found.");
        return false;
    }

    // Allocate memory info
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = internal_buffer.memory_requirements.size;
    allocate_info.memoryTypeIndex = (u32)internal_buffer.memory_index;

    // Allocate the memory.
    VkResult result = vkAllocateMemory(context->device.logical_device, &allocate_info,
                                       context->allocator, &internal_buffer.memory);
    if (!vulkan_result_is_success(result)) {
        KERROR("Failed to allocate memory for buffer with error: %s", vulkan_result_string(result, true));
        return false;
    }
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DEVICE_MEMORY, internal_buffer.memory, buffer->name);

    // Determine if memory is on a device heap.
    b8 is_device_memory = (internal_buffer.memory_property_flags &
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Report memory as in-use.
    kallocate_report(internal_buffer.memory_requirements.size,
                     is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

    if (result != VK_SUCCESS) {
        KERROR(
            "Unable to create vulkan buffer because the required memory "
            "allocation failed. Error: %i",
            result);
        return false;
    }

    // Allocate the internal state block of memory at the end once we are sure
    // everything was created successfully.
    buffer->internal_data = kallocate(sizeof(vulkan_buffer), MEMORY_TAG_VULKAN);
    *((vulkan_buffer *)buffer->internal_data) = internal_buffer;

    return true;
}

void vulkan_buffer_destroy_internal(renderer_plugin *plugin, renderbuffer *buffer) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vkDeviceWaitIdle(context->device.logical_device);
    if (buffer) {
        vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
        if (internal_buffer) {
            if (internal_buffer->memory) {
                vkFreeMemory(context->device.logical_device, internal_buffer->memory,
                             context->allocator);
                internal_buffer->memory = 0;
            }
            if (internal_buffer->handle) {
                vkDestroyBuffer(context->device.logical_device, internal_buffer->handle,
                                context->allocator);
                internal_buffer->handle = 0;
            }

            // Report the free memory.
            b8 is_device_memory = (internal_buffer->memory_property_flags &
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            kfree_report(internal_buffer->memory_requirements.size,
                         is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
            kzero_memory(&internal_buffer->memory_requirements,
                         sizeof(VkMemoryRequirements));

            internal_buffer->usage = 0;
            internal_buffer->is_locked = false;

            // Free up the internal buffer.
            kfree(buffer->internal_data, sizeof(vulkan_buffer), MEMORY_TAG_VULKAN);
            buffer->internal_data = 0;
        }
    }
}

b8 vulkan_buffer_resize(renderer_plugin *plugin, renderbuffer *buffer,
                        u64 new_size) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data) {
        return false;
    }

    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;

    // Create new buffer.
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = new_size;
    buffer_info.usage = internal_buffer->usage;
    buffer_info.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;  // NOTE: Only used in one queue.

    VkBuffer new_buffer;
    VK_CHECK(vkCreateBuffer(context->device.logical_device, &buffer_info,
                            context->allocator, &new_buffer));

    // Gather memory requirements.
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(context->device.logical_device, new_buffer,
                                  &requirements);

    // Allocate memory info
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = (u32)internal_buffer->memory_index;

    // Allocate the memory.
    VkDeviceMemory new_memory;
    VkResult result = vkAllocateMemory(context->device.logical_device, &allocate_info, context->allocator, &new_memory);
    if (result != VK_SUCCESS) {
        KERROR(
            "Unable to resize vulkan buffer because the required memory "
            "allocation failed. Error: %i",
            result);
        return false;
    }
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DEVICE_MEMORY, new_memory, buffer->name);

    // Bind the new buffer's memory
    VK_CHECK(vkBindBufferMemory(context->device.logical_device, new_buffer,
                                new_memory, 0));

    // Copy over the data.
    vulkan_buffer_copy_range_internal(context, internal_buffer->handle, 0,
                                      new_buffer, 0, buffer->total_size);

    // Make sure anything potentially using these is finished.
    // NOTE: We could use vkQueueWaitIdle here if we knew what queue this buffer
    // would be used with...
    vkDeviceWaitIdle(context->device.logical_device);

    // Destroy the old
    if (internal_buffer->memory) {
        vkFreeMemory(context->device.logical_device, internal_buffer->memory,
                     context->allocator);
        internal_buffer->memory = 0;
    }
    if (internal_buffer->handle) {
        vkDestroyBuffer(context->device.logical_device, internal_buffer->handle,
                        context->allocator);
        internal_buffer->handle = 0;
    }

    // Report free of the old, allocate of the new.
    b8 is_device_memory = (internal_buffer->memory_property_flags &
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    kfree_report(internal_buffer->memory_requirements.size,
                 is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
    internal_buffer->memory_requirements = requirements;
    kallocate_report(internal_buffer->memory_requirements.size,
                     is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

    // Set new properties
    internal_buffer->memory = new_memory;
    internal_buffer->handle = new_buffer;

    return true;
}

b8 vulkan_buffer_bind(renderer_plugin *plugin, renderbuffer *buffer,
                      u64 offset) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_bind requires valid pointer to a buffer.");
        return false;
    }
    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    VK_CHECK(vkBindBufferMemory(context->device.logical_device,
                                internal_buffer->handle, internal_buffer->memory,
                                offset));
    return true;
}

b8 vulkan_buffer_unbind(renderer_plugin *plugin, renderbuffer *buffer) {
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_unbind requires valid pointer to a buffer.");
        return false;
    }

    // NOTE: Does nothing, for now.
    return true;
}

void *vulkan_buffer_map_memory(renderer_plugin *plugin, renderbuffer *buffer,
                               u64 offset, u64 size) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_map_memory requires a valid pointer to a buffer.");
        return 0;
    }
    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    void *data;
    VK_CHECK(vkMapMemory(context->device.logical_device, internal_buffer->memory,
                         offset, size, 0, &data));
    return data;
}

void vulkan_buffer_unmap_memory(renderer_plugin *plugin, renderbuffer *buffer,
                                u64 offset, u64 size) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_unmap_memory requires a valid pointer to a buffer.");
        return;
    }
    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
}

b8 vulkan_buffer_flush(renderer_plugin *plugin, renderbuffer *buffer,
                       u64 offset, u64 size) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_flush requires a valid pointer to a buffer.");
        return false;
    }
    // NOTE: If not host-coherent, flush the mapped memory range.
    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    if (!vulkan_buffer_is_host_coherent(plugin, internal_buffer)) {
        VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = internal_buffer->memory;
        range.offset = offset;
        range.size = size;
        VK_CHECK(
            vkFlushMappedMemoryRanges(context->device.logical_device, 1, &range));
    }

    return true;
}

b8 vulkan_buffer_read(renderer_plugin *plugin, renderbuffer *buffer, u64 offset,
                      u64 size, void **out_memory) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data || !out_memory) {
        KERROR(
            "vulkan_buffer_read requires a valid pointer to a buffer and "
            "out_memory, and the size must be nonzero.");
        return false;
    }

    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    if (vulkan_buffer_is_device_local(plugin, internal_buffer) &&
        !vulkan_buffer_is_host_visible(plugin, internal_buffer)) {
        // NOTE: If a read buffer is needed (i.e.) the target buffer's memory is not
        // host visible but is device-local, create the read buffer, copy data to
        // it, then read from that buffer.

        // Create a host-visible staging buffer to copy to. Mark it as the
        // destination of the transfer.
        renderbuffer read;
        char bufname[256];
        kzero_memory(bufname, 256);
        string_format(bufname, "renderbuffer_read");
        if (!renderer_renderbuffer_create(bufname, RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, &read)) {
            KERROR("vulkan_buffer_read() - Failed to create read buffer.");
            return false;
        }
        renderer_renderbuffer_bind(&read, 0);
        vulkan_buffer *read_internal = (vulkan_buffer *)read.internal_data;

        // Perform the copy from device local to the read buffer.
        vulkan_buffer_copy_range(plugin, buffer, offset, &read, 0, size);

        // Map/copy/unmap
        void *mapped_data;
        VK_CHECK(vkMapMemory(context->device.logical_device, read_internal->memory,
                             0, size, 0, &mapped_data));
        kcopy_memory(*out_memory, mapped_data, size);
        vkUnmapMemory(context->device.logical_device, read_internal->memory);

        // Clean up the read buffer.
        renderer_renderbuffer_unbind(&read);
        renderer_renderbuffer_destroy(&read);
    } else {
        // If no staging buffer is needed, map/copy/unmap.
        void *data_ptr;
        VK_CHECK(vkMapMemory(context->device.logical_device,
                             internal_buffer->memory, offset, size, 0, &data_ptr));
        kcopy_memory(*out_memory, data_ptr, size);
        vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
    }

    return true;
}

b8 vulkan_buffer_load_range(renderer_plugin *plugin, renderbuffer *buffer,
                            u64 offset, u64 size, const void *data) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!buffer || !buffer->internal_data || !size || !data) {
        KERROR(
            "vulkan_buffer_load_range requires a valid pointer to a buffer, a "
            "nonzero size and a valid pointer to data.");
        return false;
    }

    vulkan_buffer *internal_buffer = (vulkan_buffer *)buffer->internal_data;
    if (vulkan_buffer_is_device_local(plugin, internal_buffer) &&
        !vulkan_buffer_is_host_visible(plugin, internal_buffer)) {
        // NOTE: If a staging buffer is needed (i.e.) the target buffer's memory is
        // not host visible but is device-local, create a staging buffer to load the
        // data into first. Then copy from it to the target buffer.

        // Load the data into the staging buffer.
        u64 staging_offset = 0;
        renderer_renderbuffer_allocate(&context->staging, size, &staging_offset);
        vulkan_buffer_load_range(plugin, &context->staging, staging_offset, size, data);

        // Perform the copy from staging to the device local buffer.
        vulkan_buffer_copy_range(plugin, &context->staging, staging_offset, buffer, offset, size);
    } else {
        // If no staging buffer is needed, map/copy/unmap.
        void *data_ptr;
        VK_CHECK(vkMapMemory(context->device.logical_device,
                             internal_buffer->memory, offset, size, 0, &data_ptr));
        kcopy_memory(data_ptr, data, size);
        vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
    }

    return true;
}

static b8 vulkan_buffer_copy_range_internal(vulkan_context *context,
                                            VkBuffer source, u64 source_offset,
                                            VkBuffer dest, u64 dest_offset,
                                            u64 size) {
    // TODO: Assuming queue and pool usage here. Might want dedicated queue.
    VkQueue queue = context->device.graphics_queue;
    vkQueueWaitIdle(queue);
    // Create a one-time-use command buffer.
    vulkan_command_buffer temp_command_buffer;
    vulkan_command_buffer_allocate_and_begin_single_use(
        context, context->device.graphics_command_pool, &temp_command_buffer);

    // Prepare the copy command and add it to the command buffer.
    VkBufferCopy copy_region;
    copy_region.srcOffset = source_offset;
    copy_region.dstOffset = dest_offset;
    copy_region.size = size;
    vkCmdCopyBuffer(temp_command_buffer.handle, source, dest, 1, &copy_region);

    // Submit the buffer for execution and wait for it to complete.
    vulkan_command_buffer_end_single_use(context,
                                         context->device.graphics_command_pool,
                                         &temp_command_buffer, queue);

    return true;
}

b8 vulkan_buffer_copy_range(renderer_plugin *plugin, renderbuffer *source,
                            u64 source_offset, renderbuffer *dest,
                            u64 dest_offset, u64 size) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    if (!source || !source->internal_data || !dest || !dest->internal_data ||
        !size) {
        KERROR(
            "vulkan_buffer_copy_range requires a valid pointers to source and "
            "destination buffers as well as a nonzero size.");
        return false;
    }

    return vulkan_buffer_copy_range_internal(
        context, ((vulkan_buffer *)source->internal_data)->handle, source_offset,
        ((vulkan_buffer *)dest->internal_data)->handle, dest_offset, size);
    return true;
}

b8 vulkan_buffer_draw(renderer_plugin *plugin, renderbuffer *buffer, u64 offset,
                      u32 element_count, b8 bind_only) {
    vulkan_context *context = (vulkan_context *)plugin->internal_context;
    vulkan_command_buffer *command_buffer =
        &context->graphics_command_buffers[context->image_index];

    if (buffer->type == RENDERBUFFER_TYPE_VERTEX) {
        // Bind vertex buffer at offset.
        VkDeviceSize offsets[1] = {offset};
        vkCmdBindVertexBuffers(command_buffer->handle, 0, 1,
                               &((vulkan_buffer *)buffer->internal_data)->handle,
                               offsets);
        if (!bind_only) {
            vkCmdDraw(command_buffer->handle, element_count, 1, 0, 0);
        }
        return true;
    } else if (buffer->type == RENDERBUFFER_TYPE_INDEX) {
        // Bind index buffer at offset.
        vkCmdBindIndexBuffer(command_buffer->handle,
                             ((vulkan_buffer *)buffer->internal_data)->handle,
                             offset, VK_INDEX_TYPE_UINT32);
        if (!bind_only) {
            vkCmdDrawIndexed(command_buffer->handle, element_count, 1, 0, 0, 0);
        }
        return true;
    } else {
        KERROR("Cannot draw buffer of type: %i", buffer->type);
        return false;
    }
}
