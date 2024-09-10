#include "vulkan_backend.h"

#include <renderer/renderer_types.h>
#include <vulkan/vulkan_core.h>

#include "containers/darray.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "debug/kassert.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "platform/platform.h"
#include "platform/vulkan_platform.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_utils.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_image.h"
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
#    define KVULKAN_USE_CUSTOM_ALLOCATOR 1
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data);

static i32 find_memory_index(vulkan_context* context, u32 type_filter,
                             u32 property_flags);

static void create_command_buffers(vulkan_context* context, kwindow* window);
static b8 recreate_swapchain(renderer_backend_interface* backend, kwindow* window);
static b8 create_shader_module(vulkan_context* context, shader* s, shader_stage_config* config, vulkan_shader_stage* out_stage);
static b8 vulkan_buffer_copy_range_internal(vulkan_context* context,
                                            VkBuffer source, u64 source_offset,
                                            VkBuffer dest, u64 dest_offset,
                                            u64 size, b8 queue_wait);
static vulkan_command_buffer* get_current_command_buffer(vulkan_context* context);
static u32 get_current_image_index(vulkan_context* context);
static u32 get_current_frame_index(vulkan_context* context);

static b8 vulkan_graphics_pipeline_create(vulkan_context* context, const vulkan_pipeline_config* config, vulkan_pipeline* out_pipeline);
static void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline);
static void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline);

// FIXME: May want to have this as a configurable option instead.
// Forward declarations of custom vulkan allocator functions.
#if KVULKAN_USE_CUSTOM_ALLOCATOR == 1
static void* vulkan_alloc_allocation(void* user_data, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope);
static void vulkan_alloc_free(void* user_data, void* memory);
static void* vulkan_alloc_reallocation(void* user_data, void* original, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope);
static void vulkan_alloc_internal_alloc(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
static void vulkan_alloc_internal_free(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
static b8 create_vulkan_allocator(vulkan_context* context, VkAllocationCallbacks* callbacks);
#endif // KVULKAN_USE_CUSTOM_ALLOCATOR == 1

b8 vulkan_renderer_backend_initialize(renderer_backend_interface* backend, const renderer_backend_config* config) {
    backend->internal_context_size = sizeof(vulkan_context);
    backend->internal_context = kallocate(backend->internal_context_size, MEMORY_TAG_RENDERER);
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (config->flags & RENDERER_CONFIG_FLAG_ENABLE_VALIDATION) {
        context->validation_enabled = true;
    }
    context->flags = config->flags;

    // Note down the internal size requirements for various resources.
    backend->texture_internal_data_size = sizeof(texture_internal_data);

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
    const char** required_extensions = darray_create(const char*);
    darray_push(required_extensions,
                &VK_KHR_SURFACE_EXTENSION_NAME);                        // Generic surface extension
    vulkan_platform_get_required_extension_names(&required_extensions); // Platform-specific extension(s)
    u32 required_extension_count = 0;
#if defined(_DEBUG)
    darray_push(required_extensions,
                &VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // debug utilities

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
    VkExtensionProperties* available_extensions = darray_reserve(VkExtensionProperties, available_extension_count);
    vkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions);

    // Verify required extensions are available.
    for (u32 i = 0; i < required_extension_count; ++i) {
        b8 found = false;
        for (u32 j = 0; j < available_extension_count; ++j) {
            if (strings_equal(required_extensions[i], available_extensions[j].extensionName)) {
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
    const char** required_validation_layer_names = 0;
    u32 required_validation_layer_count = 0;

    // If validation should be done, get a list of the required validation layert
    // names and make sure they exist. Validation layers should only be enabled on
    // non-release builds.
    if (context->validation_enabled) {
        KINFO("Validation layers enabled. Enumerating...");

        // The list of validation layers required.
        required_validation_layer_names = darray_create(const char*);
        darray_push(required_validation_layer_names, &"VK_LAYER_KHRONOS_validation");
        // NOTE: enable this when needed for debugging.
        // darray_push(required_validation_layer_names, &"VK_LAYER_LUNARG_api_dump");
        required_validation_layer_count = darray_length(required_validation_layer_names);

        // Obtain a list of available validation layers
        u32 available_layer_count = 0;
        VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, 0));
        VkLayerProperties* available_layers = darray_reserve(VkLayerProperties, available_layer_count);
        VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers));

        // Verify all required layers are available.
        for (u32 i = 0; i < required_validation_layer_count; ++i) {
            b8 found = false;
            for (u32 j = 0; j < available_layer_count; ++j) {
                if (strings_equal(required_validation_layer_names[i], available_layers[j].layerName)) {
                    found = true;
                    KINFO("Found validation layer: %s...", required_validation_layer_names[i]);
                    break;
                }
            }

            if (!found) {
                KFATAL("Required validation layer is missing: %s", required_validation_layer_names[i]);
                return false;
            }
        }

        // Clean up.
        darray_destroy(available_extensions);
        darray_destroy(available_layers);

        KINFO("All required validation layers are present.");
    } else {
        KINFO("Vulkan validation layers are not enabled.");
    }

    create_info.enabledLayerCount = required_validation_layer_count;
    create_info.ppEnabledLayerNames = required_validation_layer_names;

#if KPLATFORM_APPLE == 1
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult instance_result = vkCreateInstance(&create_info, context->allocator, &context->instance);
    if (!vulkan_result_is_success(instance_result)) {
        const char* result_string = vulkan_result_string(instance_result, true);
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

    // Device creation
    if (!vulkan_device_create(context)) {
        KERROR("Failed to create device!");
        return false;
    }

    // Samplers array.
    context->samplers = darray_create(VkSampler);

    // Create a shader compiler to be used.
    context->shader_compiler = shaderc_compiler_initialize();

    KINFO("Vulkan renderer initialized successfully.");
    return true;
}

void vulkan_renderer_backend_shutdown(renderer_backend_interface* backend) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vkDeviceWaitIdle(context->device.logical_device);

    // Destroy the runtime shader compiler.
    if (context->shader_compiler) {
        shaderc_compiler_release(context->shader_compiler);
        context->shader_compiler = 0;
    }

    KDEBUG("Destroying Vulkan device...");
    vulkan_device_destroy(context);

    if (backend->internal_context) {
        kfree(backend->internal_context, backend->internal_context_size, MEMORY_TAG_RENDERER);
        backend->internal_context_size = 0;
        backend->internal_context = 0;
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
        kfree(context->allocator, sizeof(VkAllocationCallbacks), MEMORY_TAG_RENDERER);
        context->allocator = 0;
    }
}

b8 vulkan_renderer_on_window_created(renderer_backend_interface* backend, kwindow* window) {
    KASSERT(backend && window);

    vulkan_context* context = (vulkan_context*)backend->internal_context;
    kwindow_renderer_state* window_internal = window->renderer_state;

    // Setup backend-specific state for the window.
    window_internal->backend_state = kallocate(sizeof(kwindow_renderer_backend_state), MEMORY_TAG_RENDERER);
    kwindow_renderer_backend_state* window_backend = window_internal->backend_state;

    // Create the surface
    KDEBUG("Creating Vulkan surface for window '%s'...", window->name);
    if (!vulkan_platform_create_vulkan_surface(context, window)) {
        KERROR("Failed to create platform surface for window '%s'!", window->name);
        return false;
    }
    KDEBUG("Vulkan surface created for window '%s'.", window->name);

    // Create swapchain. This also handles colourbuffer creation.
    if (!vulkan_swapchain_create(backend, window, context->flags, &window_backend->swapchain)) {
        KERROR("Failed to create Vulkan swapchain during creation of window '%s'. See logs for details.", window->name);
        return false;
    }

    // Re-detect supported device depth format.
    if (!vulkan_device_detect_depth_format(&context->device)) {
        context->device.depth_format = VK_FORMAT_UNDEFINED;
        KFATAL("Failed to find a supported format!");
        return false;
    }

    // Create per-frame-in-flight resources.
    {
        u8 max_frames_in_flight = window_backend->swapchain.max_frames_in_flight;

        // Sync objects are owned by the window since they go hand-in-hand
        // with the swapchain and window resources.
        window_backend->image_available_semaphores = kallocate(sizeof(VkSemaphore) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->queue_complete_semaphores = kallocate(sizeof(VkSemaphore) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->in_flight_fences = kallocate(sizeof(VkFence) * max_frames_in_flight, MEMORY_TAG_ARRAY);

        // The staging buffer also goes here since it is tied to the frame.
        // TODO: Reduce this to a single buffer split by max_frames_in_flight.
        const u64 staging_buffer_size = 512 * 1000 * 1000;
        window_backend->staging = kallocate(sizeof(renderbuffer) * max_frames_in_flight, MEMORY_TAG_ARRAY);

        for (u8 i = 0; i < max_frames_in_flight; ++i) {
            VkSemaphoreCreateInfo semaphore_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vkCreateSemaphore(context->device.logical_device, &semaphore_create_info, context->allocator, &window_backend->image_available_semaphores[i]);
            vkCreateSemaphore(context->device.logical_device, &semaphore_create_info, context->allocator, &window_backend->queue_complete_semaphores[i]);

            // Create the fence in a signaled state, indicating that the first frame has
            // already been "rendered". This will prevent the application from waiting
            // indefinitely for the first frame to render since it cannot be rendered
            // until a frame is "rendered" before it.
            VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK(vkCreateFence(context->device.logical_device, &fence_create_info, context->allocator, &window_backend->in_flight_fences[i]));

            // Staging buffer.
            // TODO: Reduce this to a single buffer split by max_frames_in_flight.
            if (!renderer_renderbuffer_create("staging", RENDERBUFFER_TYPE_STAGING, staging_buffer_size, RENDERBUFFER_TRACK_TYPE_LINEAR, &window_backend->staging[i])) {
                KERROR("Failed to create staging buffer.");
                return false;
            }
            renderer_renderbuffer_bind(&window_backend->staging[i], 0);
        }
    }

    // Create command buffers.
    (context, window);

    // Create the depthbuffer.
    KDEBUG("Creating Vulkan depthbuffer for window '%s'...", window->name);
    if (k_handle_is_invalid(window_internal->depthbuffer.renderer_texture_handle)) {
        // If invalid, then a new one needs to be created. This does not reach out to the
        // texture system to create this, but handles it internally instead. This is because
        // the process for this varies greatly between backends.
        if (!renderer_texture_resources_acquire(
                backend->frontend_state,
                window->name,
                TEXTURE_TYPE_2D,
                window->width,
                window->height,
                4,
                1,
                1,
                // NOTE: This should be a wrapped texture, so the frontend does not try to
                // acquire the resources we already have here.
                // Also flag as a depth texture
                TEXTURE_FLAG_IS_WRAPPED | TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING | TEXTURE_FLAG_DEPTH,
                &window_internal->depthbuffer.renderer_texture_handle)) {
            KFATAL("Failed to acquire internal texture resources for window.depthbuffer");
            return false;
        }
    }

    // Get the texture_internal_data based on the existing or newly-created handle above.
    // Use that to setup the internal images/views for the colourbuffer texture.
    texture_internal_data* texture_data = renderer_texture_resources_get(backend->frontend_state, window_internal->depthbuffer.renderer_texture_handle);
    if (!texture_data) {
        KFATAL("Unable to get internal data for depthbuffer image. Window creation failed.");
        return false;
    }

    // Name is meaningless here, but might be useful for debugging.
    if (!window_internal->depthbuffer.name) {
        window_internal->depthbuffer.name = string_duplicate("__window_depthbuffer_texture__");
    }

    texture_data->image_count = window_backend->swapchain.image_count;
    // Create the array if it doesn't exist.
    if (!texture_data->images) {
        // Also have to setup the internal data.
        texture_data->images = kallocate(sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);
    }

    // Update the parameters and setup a view for each image.
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        vulkan_image* image = &texture_data->images[i];

        // Construct a unique name for each image.
        char* formatted_name = string_format("__window_%s_depth_stencil_texture_%u", window->name, i);

        // Create the actual backing image.
        vulkan_image_create(
            context,
            TEXTURE_TYPE_2D,
            window->width,
            window->height,
            1,
            context->device.depth_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            formatted_name,
            1,
            image);

        string_free(formatted_name);

        // Doesn't really do anything... but track it anyways.
        window->renderer_state->depthbuffer.channel_count = context->device.depth_channel_count;

        // Setup a debug name for the image.
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_IMAGE, image->handle, image->name);
    }

    KINFO("Vulkan depthbuffer created successfully.");

    // If there is not yet a current window, assign it now.
    if (!context->current_window) {
        context->current_window = window;
    }

    return true;
}

void vulkan_renderer_on_window_destroyed(renderer_backend_interface* backend, kwindow* window) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    kwindow_renderer_state* window_internal = window->renderer_state;
    kwindow_renderer_backend_state* window_backend = window_internal->backend_state;

    u32 max_frames_in_flight = window_backend->swapchain.max_frames_in_flight;

    // Destroy per-frame-in-flight resources.
    {
        for (u32 i = 0; i < window_backend->swapchain.max_frames_in_flight; ++i) {
            // Destroy staging buffers
            renderer_renderbuffer_destroy(&window_backend->staging[i]);

            // Sync objects
            if (window_backend->image_available_semaphores[i]) {
                vkDestroySemaphore(context->device.logical_device, window_backend->image_available_semaphores[i], context->allocator);
                window_backend->image_available_semaphores[i] = 0;
            }
            if (window_backend->queue_complete_semaphores[i]) {
                vkDestroySemaphore(context->device.logical_device, window_backend->queue_complete_semaphores[i], context->allocator);
                window_backend->queue_complete_semaphores[i] = 0;
            }

            vkDestroyFence(context->device.logical_device, window_backend->in_flight_fences[i], context->allocator);
        }
        kfree(window_backend->image_available_semaphores, sizeof(VkSemaphore) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->image_available_semaphores = 0;

        kfree(window_backend->queue_complete_semaphores, sizeof(VkSemaphore) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->queue_complete_semaphores = 0;

        kfree(window_backend->in_flight_fences, sizeof(VkFence) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->in_flight_fences = 0;

        kfree(window_backend->staging, sizeof(renderbuffer) * max_frames_in_flight, MEMORY_TAG_ARRAY);
        window_backend->staging = 0;
    }

    // Destroy per-swapchain-image resources.
    {
        for (u32 i = 0; i < window_backend->swapchain.image_count; ++i) {
            // Command buffers
            if (window_backend->graphics_command_buffers[i].handle) {
                vulkan_command_buffer_free(context, context->device.graphics_command_pool, &window_backend->graphics_command_buffers[i]);
                window_backend->graphics_command_buffers[i].handle = 0;
            }
        }
        kfree(window_backend->graphics_command_buffers, sizeof(vulkan_command_buffer) * window_backend->swapchain.image_count, MEMORY_TAG_ARRAY);
        window_backend->graphics_command_buffers = 0;

        // Destroy depthbuffer images/views.

        texture_internal_data* texture_data = renderer_texture_resources_get(backend->frontend_state, window_internal->depthbuffer.renderer_texture_handle);
        if (!texture_data) {
            KWARN("Unable to get internal data for depthbuffer image. Underlying resources may not be properly destroyed.");
        } else {
            // Free the name
            if (window_internal->depthbuffer.name) {
                string_free(window_internal->depthbuffer.name);
                window_internal->depthbuffer.name = 0;
            }

            // Destroy each backing image.
            if (texture_data->images) {
                for (u32 i = 0; i < texture_data->image_count; ++i) {
                    vulkan_image_destroy(context, &texture_data->images[i]);
                }
                // Free the internal data.
                // kfree(texture_data->images, sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);
            }

            // Releasing the resources for the default depthbuffer should destroy backing resources too.
            renderer_texture_resources_release(backend->frontend_state, &window->renderer_state->depthbuffer.renderer_texture_handle);
        }
    }

    // Swapchain
    KDEBUG("Destroying Vulkan swapchain for window '%s'...", window->name);
    vulkan_swapchain_destroy(backend, &window_backend->swapchain);

    KDEBUG("Destroying Vulkan surface for window '%s'...", window->name);
    if (window_backend->surface) {
        vkDestroySurfaceKHR(context->instance, window_backend->surface, context->allocator);
        window_backend->surface = 0;
    }

    // Free the backend state.
    kfree(window_internal->backend_state, sizeof(kwindow_renderer_backend_state), MEMORY_TAG_RENDERER);
    window_internal->backend_state = 0;
}

void vulkan_renderer_backend_on_window_resized(renderer_backend_interface* backend, const kwindow* window) {
    // Cold-cast the context
    /* vulkan_context* context = (vulkan_context*)backend->internal_context; */
    kwindow_renderer_backend_state* backend_window = window->renderer_state->backend_state;
    // Update the "framebuffer size generation", a counter which indicates when
    // the framebuffer size has been updated.
    backend_window->framebuffer_size_generation++;

    KINFO("Vulkan renderer backend->resized: w/h/gen: %i/%i/%llu", window->width, window->height, backend_window->framebuffer_size_generation);
}

void vulkan_renderer_begin_debug_label(renderer_backend_interface* backend, const char* label_text, vec3 colour) {
#ifdef _DEBUG
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    vec4 rgba = (vec4){colour.r, colour.g, colour.b, 1.0f};
#endif
    VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, label_text, rgba);
}

void vulkan_renderer_end_debug_label(renderer_backend_interface* backend) {
#ifdef _DEBUG
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
#endif
    VK_END_DEBUG_LABEL(context, command_buffer->handle);
}

b8 vulkan_renderer_frame_prepare(renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    // NOTE: this is an intentional no-op in this backend.
    return true;
}

b8 vulkan_renderer_frame_prepare_window_surface(renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_device* device = &context->device;

    kwindow_renderer_state* window_internal = window->renderer_state;
    kwindow_renderer_backend_state* window_backend = window_internal->backend_state;

    // Check if recreating swap chain and boot out.
    if (window_backend->recreating_swapchain) {
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
    if (window_backend->framebuffer_size_generation != window_backend->framebuffer_previous_size_generation || context->render_flag_changed) {
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
        if (window_backend->skip_frames == 0) {
            if (!recreate_swapchain(backend, window)) {
                return false;
            }
        }

        window_backend->skip_frames++;

        // Resize depth buffer image.
        if (window_backend->skip_frames == window_backend->swapchain.max_frames_in_flight) {
            if (!k_handle_is_invalid(window->renderer_state->depthbuffer.renderer_texture_handle)) {
                /* vkQueueWaitIdle(context->device.graphics_queue); */
                if (!renderer_texture_resize(backend->frontend_state, window->renderer_state->depthbuffer.renderer_texture_handle, window->width, window->height)) {
                    KERROR("Failed to resize depth buffer for window '%s'. See logs for details.", window->name);
                }
            }
            // Sync the framebuffer size generation.
            window_backend->framebuffer_previous_size_generation = window_backend->framebuffer_size_generation;

            window_backend->skip_frames = 0;
        }

        KINFO("Resized, booting.");
        return false;
    }

    // Wait for the execution of the current frame to complete. The fence being
    // free will allow this one to move on.
    VkResult result = vkWaitForFences(
        context->device.logical_device, 1,
        &window_backend->in_flight_fences[window_backend->current_frame], true, U64_MAX);
    if (!vulkan_result_is_success(result)) {
        KFATAL("In-flight fence wait failure! error: %s", vulkan_result_string(result, true));
        return false;
    }

    // Acquire the next image from the swap chain. Pass along the semaphore that
    // should signaled when this completes. This same semaphore will later be
    // waited on by the queue submission to ensure this image is available.
    result = vkAcquireNextImageKHR(
        context->device.logical_device,
        window_backend->swapchain.handle,
        U64_MAX,
        window_backend->image_available_semaphores[window_backend->current_frame],
        0,
        &window_backend->image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Trigger swapchain recreation, then boot out of the render loop.
        if (!vulkan_swapchain_recreate(backend, window, &window_backend->swapchain)) {
            KFATAL("Failed to recreate swapchain.");
        }
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        KFATAL("Failed to acquire swapchain image!");
        return false;
    }

    // Reset the fence for use on the next frame
    VK_CHECK(vkResetFences(context->device.logical_device, 1, &window_backend->in_flight_fences[window_backend->current_frame]));

    // Reset staging buffer.
    if (!renderer_renderbuffer_clear(&window_backend->staging[window_backend->current_frame], false)) {
        KERROR("Failed to clear staging buffer.");
        return false;
    }

    return true;
}

b8 vulkan_renderer_frame_command_list_begin(renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Begin recording commands.
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    vulkan_command_buffer_reset(command_buffer);
    vulkan_command_buffer_begin(command_buffer, false, false, false);

    // Dynamic state

    vulkan_renderer_winding_set(backend, RENDERER_WINDING_COUNTER_CLOCKWISE);

    vulkan_renderer_set_stencil_reference(backend, 0);
    vulkan_renderer_set_stencil_compare_mask(backend, 0xFF);
    vulkan_renderer_set_stencil_op(
        backend,
        RENDERER_STENCIL_OP_KEEP,
        RENDERER_STENCIL_OP_REPLACE,
        RENDERER_STENCIL_OP_KEEP,
        RENDERER_COMPARE_OP_ALWAYS);
    vulkan_renderer_set_stencil_test_enabled(backend, false);
    vulkan_renderer_set_depth_test_enabled(backend, true);
    vulkan_renderer_set_depth_write_enabled(backend, true);
    // Disable stencil writing.
    vulkan_renderer_set_stencil_write_mask(backend, 0x00);
    return true;
}

b8 vulkan_renderer_frame_command_list_end(renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Just end the command buffer.
    vulkan_command_buffer_end(command_buffer);

    return true;
}

b8 vulkan_renderer_frame_submit(struct renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Submit the queue and wait for the operation to complete.
    // Begin queue submission
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};

    // Command buffer(s) to be executed.
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer->handle;

    // The semaphore(s) to be signaled when the queue is complete.
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &window_backend->queue_complete_semaphores[window_backend->current_frame];

    // Wait semaphore ensures that the operation cannot begin until the image is
    // available.
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &window_backend->image_available_semaphores[window_backend->current_frame];

    // Each semaphore waits on the corresponding pipeline stage to complete. 1:1
    // ratio. VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT prevents subsequent
    // colour attachment writes from executing until the semaphore signals (i.e.
    // one frame is presented at a time)
    VkPipelineStageFlags flags[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = flags;

    VkResult result = vkQueueSubmit(
        context->device.graphics_queue,
        1,
        &submit_info,
        window_backend->in_flight_fences[window_backend->current_frame]);
    if (result != VK_SUCCESS) {
        KERROR("vkQueueSubmit failed with result: %s", vulkan_result_string(result, true));
        return false;
    }

    vulkan_command_buffer_update_submitted(command_buffer);
    // End queue submission

    return true;
}

b8 vulkan_renderer_frame_present(renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    /* kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state; */
    kwindow_renderer_backend_state* window_backend = window->renderer_state->backend_state;

    // Return the image to the swapchain for presentation.
    VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &window_backend->queue_complete_semaphores[window_backend->current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &window_backend->swapchain.handle;
    present_info.pImageIndices = &window_backend->image_index;
    present_info.pResults = 0;
    VkResult result = vkQueuePresentKHR(context->device.present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain is out of date, suboptimal or a framebuffer resize has occurred. Trigger swapchain recreation.
        if (!vulkan_swapchain_recreate(backend, window, &window_backend->swapchain)) {
            KFATAL("Failed to recreate swapchain after presentation");
        }
        KDEBUG("Swapchain recreated because swapchain returned out of date or suboptimal.");
    } else if (result != VK_SUCCESS) {
        KFATAL("Failed to present swap chain image!");
    }

    // Increment (and loop) the index.
    window_backend->current_frame = (window_backend->current_frame + 1) % window_backend->swapchain.max_frames_in_flight;

    return true;
}

void vulkan_renderer_viewport_set(renderer_backend_interface* backend, vec4 rect) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Dynamic state
    VkViewport viewport;
    viewport.x = rect.x;
    viewport.y = rect.y;
    viewport.width = rect.z;
    viewport.height = rect.w;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
}

void vulkan_renderer_viewport_reset(renderer_backend_interface* backend) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Just set the current viewport rect.
    vulkan_renderer_viewport_set(backend, context->viewport_rect);
}

void vulkan_renderer_scissor_set(renderer_backend_interface* backend, vec4 rect) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    VkRect2D scissor;
    scissor.offset.x = rect.x;
    scissor.offset.y = rect.y;
    scissor.extent.width = rect.z;
    scissor.extent.height = rect.w;

    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
}

void vulkan_renderer_scissor_reset(renderer_backend_interface* backend) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Just set the current scissor rect.
    vulkan_renderer_scissor_set(backend, context->scissor_rect);
}

void vulkan_renderer_winding_set(struct renderer_backend_interface* backend, renderer_winding winding) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

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

void vulkan_renderer_set_stencil_test_enabled(struct renderer_backend_interface* backend, b8 enabled) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetStencilTestEnable(command_buffer->handle, (VkBool32)enabled);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetStencilTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
    } else {
        KFATAL("renderer_set_stencil_test_enabled cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_depth_test_enabled(struct renderer_backend_interface* backend, b8 enabled) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetDepthTestEnable(command_buffer->handle, (VkBool32)enabled);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetDepthTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
    } else {
        KFATAL("renderer_set_depth_test_enabled cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_depth_write_enabled(struct renderer_backend_interface* backend, b8 enabled) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetDepthWriteEnable(command_buffer->handle, (VkBool32)enabled);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetDepthWriteEnableEXT(command_buffer->handle, (VkBool32)enabled);
    } else {
        KFATAL("renderer_set_depth_write_enabled cannot be used on a device without dynamic state support.");
    }
}

void vulkan_renderer_set_stencil_reference(struct renderer_backend_interface* backend, u32 reference) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    vkCmdSetStencilReference(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void vulkan_renderer_set_stencil_op(struct renderer_backend_interface* backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

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

void vulkan_renderer_begin_rendering(struct renderer_backend_interface* backend, frame_data* p_frame_data, u32 colour_target_count, struct texture_internal_data** colour_targets, struct texture_internal_data* depth_stencil_target, u32 depth_stencil_layer) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    u32 image_index = context->current_window->renderer_state->backend_state->image_index;

    viewport* v = renderer_active_viewport_get();
    VkRenderingInfo render_info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    render_info.renderArea.offset.x = v->rect.x;
    render_info.renderArea.offset.y = v->rect.y;
    render_info.renderArea.extent.width = v->rect.width;
    render_info.renderArea.extent.height = v->rect.height;

    // TODO: This may be a problem for layered images/cubemaps
    render_info.layerCount = 1;

    // Depth
    VkRenderingAttachmentInfoKHR depth_attachment_info = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth_stencil_target) {
        vulkan_image* image = &depth_stencil_target->images[image_index];

        // // vulkan_image_transition_layout(context, command_buffer, image, image->format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        // // Transition the layout
        // VkImageMemoryBarrier barrier = {0};
        // barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
        // barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
        // barrier.image = image->handle;
        // barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        // // Mips
        // barrier.subresourceRange.baseMipLevel = 0;
        // barrier.subresourceRange.levelCount = image->mip_levels;

        // // Transition all layers at once.
        // barrier.subresourceRange.layerCount = image->layer_count;

        // // Start at the first layer.
        // barrier.subresourceRange.baseArrayLayer = 0;

        // barrier.srcAccessMask = 0;
        // barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        // vkCmdPipelineBarrier(
        //     command_buffer->handle,
        //     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     0,
        //     0, 0,
        //     0, 0,
        //     1, &barrier);

        //     //

        depth_attachment_info.imageView = image->view;
        if (image->layer_count > 1) {
            depth_attachment_info.imageView = image->layer_views[depth_stencil_layer];
        }

        depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;    // Always load.
        depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Always store.
        depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment_info.resolveImageView = 0;
        render_info.pDepthAttachment = &depth_attachment_info;
        render_info.pStencilAttachment = &depth_attachment_info;
    } else {
        render_info.pDepthAttachment = 0;
        render_info.pStencilAttachment = 0;
    }

    render_info.colorAttachmentCount = colour_target_count;
    if (colour_target_count) {
        // NOTE: this memory won't be leaked because it uses the frame allocator, which is reset per frame.
        VkRenderingAttachmentInfo* colour_attachments = p_frame_data->allocator.allocate(sizeof(VkRenderingAttachmentInfo) * colour_target_count);
        for (u32 i = 0; i < colour_target_count; ++i) {
            VkRenderingAttachmentInfo* attachment_info = &colour_attachments[i];
            attachment_info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment_info->imageView = colour_targets[i]->images[image_index].view;
            attachment_info->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment_info->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;    // Always load.
            attachment_info->storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Always store.
            kzero_memory(attachment_info->clearValue.color.float32, sizeof(f32) * 4);
            attachment_info->resolveMode = VK_RESOLVE_MODE_NONE;
            attachment_info->resolveImageView = 0;
            attachment_info->resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment_info->pNext = 0;
        }
        render_info.pColorAttachments = colour_attachments;
    } else {
        render_info.pColorAttachments = 0;
    }

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdBeginRendering(command_buffer->handle, &render_info);
    } else {
        context->vkCmdBeginRenderingKHR(command_buffer->handle, &render_info);
    }
}

void vulkan_renderer_end_rendering(struct renderer_backend_interface* backend, frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdEndRendering(command_buffer->handle);
    } else {
        context->vkCmdEndRenderingKHR(command_buffer->handle);
    }
}

void vulkan_renderer_set_stencil_compare_mask(struct renderer_backend_interface* backend, u32 compare_mask) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Supported as of vulkan 1.0, so no need to check for dynamic state support.
    vkCmdSetStencilCompareMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, compare_mask);
}

void vulkan_renderer_set_stencil_write_mask(struct renderer_backend_interface* backend, u32 write_mask) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Supported as of vulkan 1.0, so no need to check for dynamic state support.
    vkCmdSetStencilWriteMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, write_mask);
}

void vulkan_renderer_clear_colour_set(renderer_backend_interface* backend, vec4 colour) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Clamp values.
    for (u8 i = 0; i < 4; ++i) {
        colour.elements[i] = KCLAMP(colour.elements[i], 0.0f, 1.0f);
    }

    // Cache the clear colour for the next colour clear operation.
    kcopy_memory(context->colour_clear_value.float32, colour.elements, sizeof(f32) * 4);
}

void vulkan_renderer_clear_depth_set(renderer_backend_interface* backend, f32 depth) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Ensure the value is blamped
    depth = KCLAMP(depth, 0.0f, 1.0f);
    // Cache the depth for the next depth clear operation.
    context->depth_stencil_clear_value.depth = depth;
}

void vulkan_renderer_clear_stencil_set(renderer_backend_interface* backend, u32 stencil) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Cache the depth for the next stencil clear operation.
    context->depth_stencil_clear_value.stencil = stencil;
}

void vulkan_renderer_clear_colour_texture(renderer_backend_interface* backend, texture_internal_data* tex_internal) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[get_current_image_index(context)];

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, 0,
        0, 0,
        1, &barrier);

    // Clear the image.
    vkCmdClearColorImage(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        &context->colour_clear_value,
        image->layer_count,
        image->layer_count == 1 ? &image->view_subresource_range : image->layer_view_subresource_ranges);
}

void vulkan_renderer_clear_depth_stencil(renderer_backend_interface* backend, texture_internal_data* tex_internal) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[get_current_image_index(context)];

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, 0,
        0, 0,
        1, &barrier);

    // Clear the image.
    vkCmdClearDepthStencilImage(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &context->depth_stencil_clear_value,
        image->layer_count,
        image->layer_count == 1 ? &image->view_subresource_range : image->layer_view_subresource_ranges);
}

void vulkan_renderer_colour_texture_prepare_for_present(renderer_backend_interface* backend, texture_internal_data* tex_internal) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[get_current_image_index(context)];

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, 0,
        0, 0,
        1, &barrier);
}

void vulkan_renderer_texture_prepare_for_sampling(renderer_backend_interface* backend, texture_internal_data* tex_internal, texture_flag_bits flags) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[get_current_image_index(context)];

    b8 is_depth = (flags & TEXTURE_FLAG_DEPTH) != 0;

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = is_depth ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        0,
        0, 0,
        0, 0,
        1, &barrier);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                  VkDebugUtilsMessageTypeFlagsEXT message_types,
                  const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                  void* user_data) {
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

static i32 find_memory_index(vulkan_context* context, u32 type_filter, u32 property_flags) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(context->device.physical_device, &memory_properties);

    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        // Check each memory type to see if its bit is set to 1.
        if (type_filter & (1 << i) &&
            (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags) {
            return i;
        }
    }

    KWARN("Unable to find suitable memory type!");
    return -1;
}

static void create_command_buffers(vulkan_context* context, kwindow* window) {
    kwindow_renderer_backend_state* window_backend = window->renderer_state->backend_state;

    // Create new command buffers according to the new swapchain image count.
    u32 new_image_count = window_backend->swapchain.image_count;
    window_backend->graphics_command_buffers = kallocate(sizeof(vulkan_command_buffer) * new_image_count, MEMORY_TAG_ARRAY);
    // FIX: Should work faster.
    kzero_memory(window_backend->graphics_command_buffers, sizeof(vulkan_command_buffer) * new_image_count);
    
    for (u32 i = 0; i < new_image_count; ++i) {
        // kzero_memory(&window_backend->graphics_command_buffers[i], sizeof(vulkan_command_buffer));

        // Allocate a new buffer.
        char* name = string_format("%s_command_buffer_%d", window->name, i);
        vulkan_command_buffer_allocate(context, context->device.graphics_command_pool, true, name, &window_backend->graphics_command_buffers[i]);
        string_free(name);
    }

    KDEBUG("Vulkan command buffers created.");
}

static b8 recreate_swapchain(renderer_backend_interface* backend, kwindow* window) {
    vulkan_context* context = backend->internal_context;
    kwindow_renderer_state* window_internal = window->renderer_state;
    kwindow_renderer_backend_state* window_backend = window_internal->backend_state;

    // If already being recreated, do not try again.
    if (window_backend->recreating_swapchain) {
        KDEBUG("recreate_swapchain called when already recreating. Booting.");
        return false;
    }

    // Detect if the window is too small to be drawn to
    if (window->width == 0 || window->height == 0) {
        KDEBUG("recreate_swapchain called when window is < 1 in a dimension. Booting.");
        return false;
    }

    // Mark as recreating if the dimensions are valid.
    window_backend->recreating_swapchain = true;

    // Use the old swapchain count to free swapchain-image-count related items.
    u32 old_swapchain_image_count = window_backend->swapchain.image_count;

    // Wait for any operations to complete.
    vkDeviceWaitIdle(context->device.logical_device);

    // Redetect the depth format.
    vulkan_device_detect_depth_format(&context->device);

    // Recreate the swapchain.
    if (!vulkan_swapchain_recreate(backend, window, &window_backend->swapchain)) {
        // TODO: Should this be fatal? Or keep trying?
        KERROR("Failed to recreate swapchain. See logs for details.");
        return false;
    }

    // Free old command buffers.
    if (window_backend->graphics_command_buffers) {
        // Free the old command buffers first. Use the old image count for this, if it changed.
        for (u32 i = 0; i < old_swapchain_image_count; ++i) {
            if (window_backend->graphics_command_buffers[i].handle) {
                vulkan_command_buffer_free(context, context->device.graphics_command_pool, &window_backend->graphics_command_buffers[i]);
            }
        }

        kfree(window_backend->graphics_command_buffers, sizeof(vulkan_command_buffer) * old_swapchain_image_count, MEMORY_TAG_ARRAY);
        window_backend->graphics_command_buffers = 0;
    }

    // Indicate to listeners that a render target refresh is required.
    // TODO: Might remove this.
    event_fire(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, 0, (event_context){0});

    create_command_buffers(context, window);

    // Clear the recreating flag.
    window_backend->recreating_swapchain = false;

    return true;
}

static VkFormat channel_count_to_format(u8 channel_count, VkFormat default_format) {
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

b8 vulkan_renderer_texture_resources_acquire(renderer_backend_interface* backend, texture_internal_data* texture_data, const char* name, texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, texture_flag_bits flags) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Internal data creation.
    if (flags & TEXTURE_FLAG_RENDERER_BUFFERING) {
        // Need to generate as many images as we have swapchain images.
        // FIXME: This is really only valid for the window it's attached to, unless this number is synced and
        // used across all windows. This should probably be stored and accessed elsewhere.
        texture_data->image_count = context->current_window->renderer_state->backend_state->swapchain.image_count;
    } else {
        // Only one needed.
        texture_data->image_count = 1;
    }
    texture_data->images = kallocate(sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);

    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlagBits aspect;
    VkFormat image_format;
    if (flags & TEXTURE_FLAG_DEPTH) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        image_format = context->device.depth_format;
    } else {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        image_format = channel_count_to_format(channel_count, VK_FORMAT_R8G8B8A8_UNORM);
    }

    // Create one image per swapchain image (or just one image)
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        char* image_name = string_format("%s_vkimage_%d", name, i);
        vulkan_image_create(
            context, type, width, height, array_size, image_format,
            VK_IMAGE_TILING_OPTIMAL, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, aspect,
            image_name, mip_levels, &texture_data->images[i]);
        string_free(image_name);
    }

    return true;
}

void vulkan_renderer_texture_resources_release(renderer_backend_interface* backend, texture_internal_data* texture_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (texture_data->images) {
        for (u32 i = 0; i < texture_data->image_count; ++i) {
            vulkan_image_destroy(context, &texture_data->images[i]);
        }
        kfree(texture_data->images, sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);
        texture_data->images = 0;
    }
}

b8 vulkan_renderer_texture_resize(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 new_width, u32 new_height) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (texture_data) {
        for (u32 i = 0; i < texture_data->image_count; ++i) {
            // Resizing is really just destroying the old image and creating a new one.
            // Data is not preserved because there's no reliable way to map the old data
            // to the new since the amount of data differs.
            vulkan_image* image = &texture_data->images[i];
            image->image_create_info.extent.width = new_width;
            image->image_create_info.extent.height = new_height;
            // Recalculate mip levels if anything other than 1.
            if (image->mip_levels > 1) {
                // Recalculate the number of levels.
                // The number of mip levels is calculated by first taking the largest dimension
                // (either width or height), figuring out how many times that number can be divided
                // by 2, taking the floor value (rounding down) and adding 1 to represent the
                // base level. This always leaves a value of at least 1.
                image->mip_levels = (u32)(kfloor(klog2(KMAX(new_width, new_height))) + 1);
            }

            vulkan_image_recreate(context, image);
        }

        return true;
    }

    return false;
}

b8 vulkan_renderer_texture_write_data(renderer_backend_interface* backend, struct texture_internal_data* texture_data,
                                      u32 offset, u32 size, const u8* pixels, b8 include_in_frame_workload) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // If no window, can't include in a frame workload.
    if (!context->current_window) {
        include_in_frame_workload = false;
    }
    if (texture_data) {
        renderbuffer temp;
        renderbuffer* staging = 0;
        if (include_in_frame_workload) {
            u32 current_frame = context->current_window->renderer_state->backend_state->current_frame;
            staging = &context->current_window->renderer_state->backend_state->staging[current_frame];
        } else {
            renderer_renderbuffer_create("temp_staging", RENDERBUFFER_TYPE_STAGING, size * texture_data->image_count, RENDERBUFFER_TRACK_TYPE_NONE, &temp);
            renderer_renderbuffer_bind(&temp, 0);
            staging = &temp;
        }
        for (u32 i = 0; i < texture_data->image_count; ++i) {
            vulkan_image* image = &texture_data->images[i];

            // Staging buffer.
            u64 staging_offset = 0;
            if (include_in_frame_workload) {
                renderer_renderbuffer_allocate(staging, size, &staging_offset);
            }
            vulkan_buffer_load_range(backend, staging, staging_offset, size, pixels, include_in_frame_workload);

            vulkan_command_buffer temp_command_buffer;
            VkCommandPool pool = context->device.graphics_command_pool;
            VkQueue queue = context->device.graphics_queue;
            vulkan_command_buffer_allocate_and_begin_single_use(context, pool, &temp_command_buffer);

            // Transition the layout from whatever it is currently to optimal for
            // recieving data.
            vulkan_image_transition_layout(context, &temp_command_buffer, image, image->format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Copy the data from the buffer.
            vulkan_image_copy_from_buffer(context, image, ((vulkan_buffer*)staging->internal_data)->handle, staging_offset, &temp_command_buffer);

            if (image->mip_levels <= 1 || !vulkan_image_mipmaps_generate(context, image, &temp_command_buffer)) {
                // If mip generation isn't needed or fails, fall back to ordinary transition.
                // Transition from optimal for data reciept to shader-read-only optimal layout.
                vulkan_image_transition_layout(context, &temp_command_buffer, image, image->format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            vulkan_command_buffer_end_single_use(context, pool, &temp_command_buffer, queue);
        }

        if (!include_in_frame_workload) {
            renderer_renderbuffer_destroy(&temp);
        }

        // Counts as a texture update.
        // FIXME: This internal generation isn't useful in particular.
        // Also, the texture generation here can only really be updated if we _don't_ include
        // the upload in the frame workload, since that results in a wait. If we include it in
        // the frame workload, then we must also wait until that frame's queue is complete.
        // texture_data->generation++;
        return true;
    }

    return false;
}

static b8 texture_read_offset_range(
    renderer_backend_interface* backend,
    struct texture_internal_data* texture_data,
    u32 offset,
    u32 size,
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u8** out_memory) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (texture_data) {
        // Always just use the first image for this operaton.
        vulkan_image* image = &texture_data->images[0];

        // NOTE: If offset or size are nonzero, read the entire image and select the offset and size in the range.
        if (offset || size) {
            x = y = 0;
            width = image->width;
            height = image->height;
        } else {
            // NOTE: Assuming RGBA/8bpp
            size = image->width * image->height * 4 * sizeof(u8);
        }

        // Create a staging buffer and load data into it.
        // TODO: global read buffer w/freelist (like staging), but for reading.
        renderbuffer staging;
        if (!renderer_renderbuffer_create("renderbuffer_texture_read_staging", RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, &staging)) {
            KERROR("Failed to create staging buffer for texture read.");
            return false;
        }
        renderer_renderbuffer_bind(&staging, 0);

        vulkan_command_buffer temp_buffer;
        VkCommandPool pool = context->device.graphics_command_pool;
        VkQueue queue = context->device.graphics_queue;
        vulkan_command_buffer_allocate_and_begin_single_use(context, pool, &temp_buffer);

        // NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        // Transition the layout from whatever it is currently to optimal for handing
        // out data.
        vulkan_image_transition_layout(context, &temp_buffer, image, image->format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Copy the data to the buffer.
        vulkan_image_copy_region_to_buffer(context, image, ((vulkan_buffer*)staging.internal_data)->handle, x, y, width, height, &temp_buffer);

        // Transition from optimal for data reading to shader-read-only optimal layout.
        // TODO: Should probably cache the previous layout and transfer back to that instead.
        vulkan_image_transition_layout(context, &temp_buffer, image, image->format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vulkan_command_buffer_end_single_use(context, pool, &temp_buffer, queue);

        if (!vulkan_buffer_read(backend, &staging, offset, size, (void**)out_memory)) {
            KERROR("vulkan_buffer_read failed.");
        }

        renderer_renderbuffer_unbind(&staging);
        renderer_renderbuffer_destroy(&staging);
        return true;
    }

    return false;
}

b8 vulkan_renderer_texture_read_data(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 offset, u32 size, u8** out_pixels) {
    return texture_read_offset_range(backend, texture_data, offset, size, 0, 0, 0, 0, out_pixels);
}

b8 vulkan_renderer_texture_read_pixel(renderer_backend_interface* backend, struct texture_internal_data* texture_data, u32 x, u32 y, u8** out_rgba) {
    return texture_read_offset_range(backend, texture_data, 0, 0, x, y, 1, 1, out_rgba);
}

b8 vulkan_renderer_shader_create(renderer_backend_interface* backend, shader* s, const shader_config* config) {
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
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Setup the internal shader.
    vulkan_shader* internal_shader = (vulkan_shader*)s->internal_data;
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
    // FIXME: This is really only valid for the window it's attached to, unless this number is synced and
    // used across all windows. This should probably be stored and accessed elsewhere.
    u32 image_count = context->current_window->renderer_state->backend_state->swapchain.image_count;
    // 1 set of globals * framecount + x samplers per instance, per frame.
    u32 max_sampler_count = (s->global_uniform_sampler_count * image_count) + (config->max_instances * s->instance_uniform_sampler_count * image_count);
    // 1 global (1*framecount) + 1 per instance, per frame.
    u32 max_ubo_count = image_count + (config->max_instances * image_count);
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
        vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_sets[internal_shader->descriptor_set_count];

        // Total bindings are 1 UBO for global (if needed), plus global sampler count.
        // This is dynamically allocated now.
        u32 ubo_count = s->global_uniform_count ? 1 : 0;
        set_config->binding_count = ubo_count + s->global_uniform_sampler_count;
        set_config->bindings = kallocate(sizeof(VkDescriptorSetLayoutBinding) * set_config->binding_count, MEMORY_TAG_ARRAY);

        // Global UBO binding is first, if present.
        u8 global_binding_index = 0;
        if (s->global_uniform_count > 0) {
            set_config->bindings[global_binding_index].binding = global_binding_index;
            set_config->bindings[global_binding_index].descriptorCount = 1; // NOTE: the whole UBO is one binding.
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
                shader_uniform_config* u = &config->uniforms[s->global_sampler_indices[i]];
                set_config->bindings[global_binding_index].binding = global_binding_index;
                set_config->bindings[global_binding_index].descriptorCount = KMAX(u->array_length, 1); // Either treat as an array or a single texture, depending on what is passed in.
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
        vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_sets[internal_shader->descriptor_set_count];

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
                shader_uniform_config* u = &config->uniforms[s->instance_sampler_indices[i]];
                set_config->bindings[instance_binding_index].binding = instance_binding_index;
                set_config->bindings[instance_binding_index].descriptorCount = KMAX(u->array_length, 1); // Either treat as an array or a single texture, depending on what is passed in.
                set_config->bindings[instance_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                set_config->bindings[instance_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
                instance_binding_index++;
            }
        }

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // Invalidate global state.
    internal_shader->global_ubo_descriptor_state.generations = kallocate(sizeof(u8) * image_count, MEMORY_TAG_ARRAY);
    internal_shader->global_ubo_descriptor_state.ids = kallocate(sizeof(u32) * image_count, MEMORY_TAG_ARRAY);
    internal_shader->global_ubo_descriptor_state.frame_numbers = kallocate(sizeof(u64) * image_count, MEMORY_TAG_ARRAY);
    for (u32 j = 0; j < image_count; ++j) {
        internal_shader->global_ubo_descriptor_state.generations[j] = INVALID_ID_U8;
        internal_shader->global_ubo_descriptor_state.ids[j] = INVALID_ID;
        internal_shader->global_ubo_descriptor_state.frame_numbers[j] = INVALID_ID_U64;
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

void vulkan_renderer_shader_destroy(renderer_backend_interface* backend, shader* s) {
    if (s && s->internal_data) {
        vulkan_shader* internal_shader = s->internal_data;
        if (!internal_shader) {
            KERROR(
                "vulkan_renderer_shader_destroy requires a valid pointer to a "
                "shader.");
            return;
        }

        vulkan_context* context = (vulkan_context*)backend->internal_context;
        VkDevice logical_device = context->device.logical_device;
        VkAllocationCallbacks* vk_allocator = context->allocator;

        u32 image_count = internal_shader->uniform_buffer_count;

        // Descriptor set layouts.
        for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
            if (internal_shader->descriptor_set_layouts[i]) {
                kfree(internal_shader->descriptor_sets[i].bindings, sizeof(VkDescriptorSetLayoutBinding) * internal_shader->descriptor_sets[i].binding_count, MEMORY_TAG_ARRAY);
                vkDestroyDescriptorSetLayout(logical_device, internal_shader->descriptor_set_layouts[i], vk_allocator);
                internal_shader->descriptor_set_layouts[i] = 0;
            }
        }

        // Global descriptor sets.
        kfree(internal_shader->global_descriptor_sets, sizeof(VkDescriptorSet) * image_count, MEMORY_TAG_ARRAY);

        // Descriptor pool
        if (internal_shader->descriptor_pool) {
            vkDestroyDescriptorPool(logical_device, internal_shader->descriptor_pool, vk_allocator);
        }

        // Destroy the instance states.
        for (u32 i = 0; i < internal_shader->max_instances; ++i) {
            if (internal_shader->instance_states[i].descriptor_sets) {
                kfree(internal_shader->instance_states[i].descriptor_sets, sizeof(VkDescriptorSet) * image_count, MEMORY_TAG_ARRAY);
            }
            if (internal_shader->instance_states[i].sampler_uniforms) {
                kfree(internal_shader->instance_states[i].sampler_uniforms, sizeof(vulkan_uniform_sampler_state) * s->instance_uniform_sampler_count, MEMORY_TAG_ARRAY);
            }
        }
        kfree(internal_shader->instance_states, sizeof(vulkan_shader_instance_state) * internal_shader->max_instances, MEMORY_TAG_ARRAY);

        // Uniform buffer.
        for (u32 i = 0; i < image_count; ++i) {
            vulkan_buffer_unmap_memory(backend, &internal_shader->uniform_buffers[i], 0, VK_WHOLE_SIZE);
            internal_shader->mapped_uniform_buffer_blocks[i] = 0;
            renderer_renderbuffer_destroy(&internal_shader->uniform_buffers[i]);
        }
        kfree(internal_shader->mapped_uniform_buffer_blocks, sizeof(void*) * image_count, MEMORY_TAG_ARRAY);
        kfree(internal_shader->uniform_buffers, sizeof(renderbuffer) * image_count, MEMORY_TAG_ARRAY);

        // Pipelines
        for (u32 i = 0; i < VULKAN_TOPOLOGY_CLASS_MAX; ++i) {
            if (internal_shader->pipelines[i]) {
                vulkan_pipeline_destroy(context, internal_shader->pipelines[i]);
            }
            if (internal_shader->wireframe_pipelines && internal_shader->wireframe_pipelines[i]) {
                vulkan_pipeline_destroy(context, internal_shader->wireframe_pipelines[i]);
            }
        }

        // Shader modules
        for (u32 i = 0; i < internal_shader->stage_count; ++i) {
            vkDestroyShaderModule(context->device.logical_device, internal_shader->stages[i].handle, context->allocator);
        }

        // Free the internal data memory.
        kfree(s->internal_data, sizeof(vulkan_shader), MEMORY_TAG_RENDERER);
        s->internal_data = 0;
    }
}

static b8 shader_create_modules_and_pipelines(renderer_backend_interface* backend, shader* s) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = (vulkan_shader*)s->internal_data;

    b8 has_error = false;

    // Only dynamic topology is supported. Create one pipeline per topology class.
    // If this isn't supported, perhaps a different backend should be used.
    u32 pipeline_count = 3;

    // Create a temporary array for the pipelines to sit in. These will sit here until all loading is
    // complete, in the event this is called during a reload. This will ensure the current pipelines continue to
    // function as they should until this load is complete and ready to go successfully.
    vulkan_pipeline* new_pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
    // Same for wireframe_pipelines, if needed.
    vulkan_pipeline* new_wireframe_pipelines = 0;
    if (internal_shader->wireframe_pipelines) {
        new_wireframe_pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
    }

    // Create a module for each stage.
    vulkan_shader_stage* new_stages = kallocate(sizeof(vulkan_shader_stage) * VULKAN_SHADER_MAX_STAGES, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        if (!create_shader_module(context, s, &s->stage_configs[i], &new_stages[i])) {
            KERROR("Unable to create %s shader module for '%s'. Shader will be destroyed.", s->stage_configs[i].filename, s->name);
            has_error = true;
            goto shader_module_pipeline_cleanup;
        }
    }

    u32 framebuffer_width = context->current_window->width;
    u32 framebuffer_height = context->current_window->height;

    // Default viewport/scissor, can be dynamically overidden.
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)framebuffer_height;
    viewport.width = (f32)framebuffer_width;
    viewport.height = -(f32)framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = framebuffer_width;
    scissor.extent.height = framebuffer_height;

    VkPipelineShaderStageCreateInfo stage_create_infos[VULKAN_SHADER_MAX_STAGES];
    kzero_memory(stage_create_infos, sizeof(VkPipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        stage_create_infos[i] = new_stages[i].shader_stage_create_info;
    }

    // Loop through and config/create one pipeline per class. Null entries are skipped.
    for (u32 i = 0; i < pipeline_count; ++i) {
        if (!internal_shader->pipelines[i]) {
            continue;
        }

        // Make sure the supported types are noted in the temp array pipelines.
        new_pipelines[i].supported_topology_types = internal_shader->pipelines[i]->supported_topology_types;
        if (internal_shader->wireframe_pipelines) {
            new_wireframe_pipelines[i].supported_topology_types = internal_shader->wireframe_pipelines[i]->supported_topology_types;
        }

        vulkan_pipeline_config pipeline_config = {0};
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

        // Strip the wireframe flag if it's there.
        shader_flag_bits flags = s->flags;
        flags &= ~(SHADER_FLAG_WIREFRAME);
        pipeline_config.shader_flags = flags;
        // NOTE: Always one block for the push constant.
        pipeline_config.push_constant_range_count = 1;
        range push_constant_range;
        push_constant_range.offset = 0;
        push_constant_range.size = s->local_ubo_stride;
        pipeline_config.push_constant_ranges = &push_constant_range;
        pipeline_config.name = string_duplicate(s->name);
        pipeline_config.topology_types = s->topology_types;

        if ((s->flags & SHADER_FLAG_COLOUR_READ) || (s->flags & SHADER_FLAG_COLOUR_WRITE)) {
            // TODO: Figure out the format(s) of the colour attachments (if they exist) and pass them along here.
            // This just assumes the same format as the default render target/swapchain. This will work
            // until there is a shader with more than 1 colour attachment, in which case either the
            // shader configuration itself will have to be amended to indicate this directly and/or the
            // shader configuration can specify some known "pipeline type" (i.e. "forward"), and that
            // type contains the image format information needed here. Putting a pin in this for now
            // until the eventual shader refactoring.
            pipeline_config.colour_attachment_count = 1;
            pipeline_config.colour_attachment_formats = &context->current_window->renderer_state->backend_state->swapchain.image_format.format;
        } else {
            pipeline_config.colour_attachment_count = 0;
            pipeline_config.colour_attachment_formats = 0;
        }

        if ((s->flags & SHADER_FLAG_DEPTH_TEST) || (s->flags & SHADER_FLAG_DEPTH_WRITE) || (s->flags & SHADER_FLAG_STENCIL_TEST) || (s->flags & SHADER_FLAG_STENCIL_WRITE)) {
            pipeline_config.depth_attachment_format = context->device.depth_format;
            pipeline_config.stencil_attachment_format = context->device.depth_format;
        } else {
            pipeline_config.depth_attachment_format = VK_FORMAT_UNDEFINED;
            pipeline_config.stencil_attachment_format = VK_FORMAT_UNDEFINED;
        }

        b8 pipeline_result = vulkan_graphics_pipeline_create(context, &pipeline_config, &new_pipelines[i]);

        // Create the wireframe version.
        if (pipeline_result && new_wireframe_pipelines) {
            // Use the same config, but make sure the wireframe flag is set.
            pipeline_config.shader_flags |= SHADER_FLAG_WIREFRAME;
            pipeline_result = vulkan_graphics_pipeline_create(context, &pipeline_config, &new_wireframe_pipelines[i]);
        }

        kfree(pipeline_config.name, string_length(pipeline_config.name) + 1, MEMORY_TAG_STRING);

        if (!pipeline_result) {
            KERROR("Failed to load graphics pipeline for shader: '%s'.", s->name);
            has_error = true;
            break;
        }
    }

    // If failed, cleanup.
    if (has_error) {
        for (u32 i = 0; i < pipeline_count; ++i) {
            vulkan_pipeline_destroy(context, &new_pipelines[i]);
            if (new_wireframe_pipelines) {
                vulkan_pipeline_destroy(context, &new_wireframe_pipelines[i]);
            }
        }
        for (u32 i = 0; i < internal_shader->stage_count; ++i) {
            vkDestroyShaderModule(context->device.logical_device, new_stages[i].handle, context->allocator);
        }
        goto shader_module_pipeline_cleanup;
    }

    // In success, destroy the old pipelines and move the new pipelines over.
    vkDeviceWaitIdle(context->device.logical_device);
    for (u32 i = 0; i < pipeline_count; ++i) {
        if (internal_shader->pipelines[i]) {
            vulkan_pipeline_destroy(context, internal_shader->pipelines[i]);
            kcopy_memory(internal_shader->pipelines[i], &new_pipelines[i], sizeof(vulkan_pipeline));
        }
        if (new_wireframe_pipelines) {
            if (internal_shader->wireframe_pipelines[i]) {
                vulkan_pipeline_destroy(context, internal_shader->wireframe_pipelines[i]);
                kcopy_memory(internal_shader->wireframe_pipelines[i], &new_wireframe_pipelines[i], sizeof(vulkan_pipeline));
            }
        }
    }

    // Destroy the old shader modules and copy over the new ones.
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        vkDestroyShaderModule(context->device.logical_device, internal_shader->stages[i].handle, context->allocator);
        kcopy_memory(&internal_shader->stages[i], &new_stages[i], sizeof(vulkan_shader_stage));
    }

shader_module_pipeline_cleanup:
    kfree(new_pipelines, sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
    if (new_wireframe_pipelines) {
        kfree(new_wireframe_pipelines, sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
    }

    kfree(new_stages, sizeof(vulkan_shader_stage) * VULKAN_SHADER_MAX_STAGES, MEMORY_TAG_ARRAY);

    return !has_error;
}

b8 vulkan_renderer_shader_initialize(renderer_backend_interface* backend, shader* s) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    VkDevice logical_device = context->device.logical_device;
    VkAllocationCallbacks* vk_allocator = context->allocator;
    vulkan_shader* internal_shader = (vulkan_shader*)s->internal_data;

    // FIXME: This is really only valid for the window it's attached to, unless this number is synced and
    // used across all windows. This should probably be stored and accessed elsewhere.
    u32 image_count = context->current_window->renderer_state->backend_state->swapchain.image_count;

    b8 needs_wireframe = (s->flags & SHADER_FLAG_WIREFRAME) != 0;
    // Determine if the implementation supports this and set to false if not.
    if (!context->device.features.fillModeNonSolid) {
        KINFO("Renderer backend does not support fillModeNonSolid. Wireframe mode is not possible, but was requested for the shader '%s'.", s->name);
        needs_wireframe = false;
    }

    // Static lookup table for our types->Vulkan ones.
    static VkFormat* types = 0;
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
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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

    // Only dynamic topology is supported. Create one pipeline per topology class.
    // If this isn't supported, perhaps a different backend should be used.
    u32 pipeline_count = 3;

    // Create an array of pointers to pipelines, one per topology class. Null means not supported for this shader.
    internal_shader->pipelines = kallocate(sizeof(vulkan_pipeline*) * pipeline_count, MEMORY_TAG_ARRAY);

    // Do the same as above, but a wireframe version.
    if (needs_wireframe) {
        internal_shader->wireframe_pipelines = kallocate(sizeof(vulkan_pipeline*) * pipeline_count, MEMORY_TAG_ARRAY);
    } else {
        internal_shader->wireframe_pipelines = 0;
    }

    // Create one pipeline per topology class.
    // Point class.
    if (s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST;

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_POINT] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_POINT]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST;
        }
    }

    // Line class.
    if (s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST || s->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP;

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP;
        }
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

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN;
        }
    }

    if (!shader_create_modules_and_pipelines(backend, s)) {
        KERROR("Failed initial load on shader '%s'. See logs for details.", s->name);
        return false;
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

    internal_shader->mapped_uniform_buffer_blocks = kallocate(sizeof(void*) * image_count, MEMORY_TAG_ARRAY);
    internal_shader->uniform_buffers = kallocate(sizeof(renderbuffer) * image_count, MEMORY_TAG_ARRAY);
    internal_shader->uniform_buffer_count = image_count;

    // Uniform  buffers, one per swapchain image.
    u64 total_buffer_size = s->global_ubo_stride + (s->ubo_stride * internal_shader->max_instances);
    for (u32 i = 0; i < image_count; ++i) {
        const char* buffer_name = string_format("renderbuffer_uniform_%s_idx_%d", s->name, i);
        if (!renderer_renderbuffer_create(buffer_name, RENDERBUFFER_TYPE_UNIFORM, total_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST, &internal_shader->uniform_buffers[i])) {
            KERROR("Vulkan buffer creation failed for object shader.");
            string_free(buffer_name);
            return false;
        }
        string_free(buffer_name);
        renderer_renderbuffer_bind(&internal_shader->uniform_buffers[i], 0);
        // Map the entire buffer's memory.
        internal_shader->mapped_uniform_buffer_blocks[i] = vulkan_buffer_map_memory(backend, &internal_shader->uniform_buffers[i], 0, VK_WHOLE_SIZE);
    }

    // NOTE: All of this below is only allocated if actually needed.
    //
    //  Allocate space for the global UBO, whcih should occupy the _stride_ space,
    //  _not_ the actual size used.
    if (s->global_ubo_size > 0 && s->global_ubo_stride > 0) {
        // Per swapchain image
        for (u32 i = 0; i < internal_shader->uniform_buffer_count; ++i) {
            if (!renderer_renderbuffer_allocate(&internal_shader->uniform_buffers[i], s->global_ubo_stride, &s->global_ubo_offset)) {
                KERROR("Failed to allocate space for the uniform buffer!");
                return false;
            }
        }

        // Allocate global descriptor sets, one per frame. Global is always the first set.
        internal_shader->global_descriptor_sets = kallocate(sizeof(VkDescriptorSet) * image_count, MEMORY_TAG_ARRAY);
        VkDescriptorSetLayout* global_layouts = kallocate(sizeof(VkDescriptorSetLayout) * image_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < image_count; ++i) {
            global_layouts[i] = internal_shader->descriptor_set_layouts[0];
        }

        VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc_info.descriptorPool = internal_shader->descriptor_pool;
        alloc_info.descriptorSetCount = image_count;
        alloc_info.pSetLayouts = global_layouts;
        VK_CHECK(vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, internal_shader->global_descriptor_sets));

#ifdef _DEBUG
        for (u32 i = 0; i < image_count; ++i) {
            char* desc_set_object_name = string_format("desc_set_shader_%s_global_frame_%u", s->name, i);
            VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, internal_shader->global_descriptor_sets[i], desc_set_object_name);
            string_free(desc_set_object_name);
        }
#endif
        kfree(global_layouts, sizeof(VkDescriptorSetLayout) * image_count, MEMORY_TAG_ARRAY);
    }

    return true;
}

b8 vulkan_renderer_shader_reload(renderer_backend_interface* backend, shader* s) {
    return shader_create_modules_and_pipelines(backend, s);
}

b8 vulkan_renderer_shader_use(renderer_backend_interface* backend, shader* s) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = s->internal_data;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Pick the correct pipeline.
    vulkan_pipeline** pipeline_array = s->is_wireframe ? internal->wireframe_pipelines : internal->pipelines;
    vulkan_pipeline_bind(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_array[internal->bound_pipeline_index]);

    context->bound_shader = s;
    // Make sure to use the current bound type as well.
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetPrimitiveTopology(command_buffer->handle, internal->current_topology);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetPrimitiveTopologyEXT(command_buffer->handle, internal->current_topology);
    }
    return true;
}

b8 vulkan_renderer_shader_supports_wireframe(const renderer_backend_interface* backend, const shader* s) {
    const vulkan_shader* internal = s->internal_data;

    // If the array exists, this is supported.
    if (internal->wireframe_pipelines) {
        return true;
    }

    return false;
}

static b8 vulkan_descriptorset_update_and_bind(
    renderer_backend_interface* backend,
    u64 renderer_frame_number,
    shader* s,
    VkDescriptorSet descriptor_set,
    u32 descriptor_set_index,
    vulkan_descriptor_state* descriptor_state,
    u64 ubo_offset,
    u64 ubo_stride,
    u32 uniform_count,
    vulkan_uniform_sampler_state* samplers,
    u32 sampler_count) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    u32 image_index = get_current_image_index(context);
    vulkan_shader* internal = s->internal_data;

    const frame_data* p_frame_data = engine_frame_data_get();

    // The descriptor_state holds frame number, which is compared against the current
    // renderer frame number. If no match, it gets an update. Otherwise, it's bind-only.
    b8 needs_update = descriptor_state->frame_numbers[image_index] != renderer_frame_number;
    if (needs_update) {
        // Allocate enough descriptor writes to handle the max allowed bound textures.
        VkWriteDescriptorSet descriptor_writes[1 + VULKAN_SHADER_MAX_TEXTURE_BINDINGS];
        kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * (1 + VULKAN_SHADER_MAX_TEXTURE_BINDINGS));

        u32 descriptor_write_count = 0;
        u32 binding_index = 0;

        VkDescriptorBufferInfo ubo_buffer_info = {0};

        // Descriptor 0 - Uniform buffer
        if (uniform_count > 0) {
            // Only do this if the descriptor has not yet been updated.
            u8* ubo_generation = &(descriptor_state->generations[image_index]);
            if (*ubo_generation == INVALID_ID_U8) {
                ubo_buffer_info.buffer = ((vulkan_buffer*)internal->uniform_buffers[image_index].internal_data)->handle;
                KASSERT_MSG((ubo_offset % context->device.properties.limits.minUniformBufferOffsetAlignment) == 0, "Ubo offset must be a multiple of device.properties.limits.minUniformBufferOffsetAlignment.");
                ubo_buffer_info.offset = ubo_offset;
                ubo_buffer_info.range = ubo_stride;

                VkWriteDescriptorSet ubo_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                ubo_descriptor.dstSet = descriptor_set;
                ubo_descriptor.dstBinding = binding_index;
                ubo_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                ubo_descriptor.descriptorCount = 1;
                ubo_descriptor.pBufferInfo = &ubo_buffer_info;

                descriptor_writes[descriptor_write_count] = ubo_descriptor;
                descriptor_write_count++;

                // Update the frame generation. In this case it is only needed once since this is a buffer.
                *ubo_generation = 1;
            }
            binding_index++;
        }

        // Iterate samplers.
        if (sampler_count > 0) {
            vulkan_descriptor_set_config set_config = internal->descriptor_sets[descriptor_set_index];

            // Allocate enough space to hold all the descriptor image infos needed for this scope (one array per binding).
            // NOTE: Using the frame allocator, so this does not have to be freed as it's handled automatically at the end of the frame on allocator reset.
            VkDescriptorImageInfo** binding_image_infos = p_frame_data->allocator.allocate(sizeof(VkDescriptorImageInfo*) * sampler_count);

            // Iterate each sampler binding.
            for (u32 sb = 0; sb < sampler_count; ++sb) {
                vulkan_uniform_sampler_state* binding_sampler_state = &samplers[sb];

                u32 binding_descriptor_count = set_config.bindings[binding_index].descriptorCount;

                u32 update_sampler_count = 0;

                // Allocate enough space to build all image infos.
                binding_image_infos[sb] = p_frame_data->allocator.allocate(sizeof(VkDescriptorImageInfo) * binding_descriptor_count);

                // Each sampler descriptor within the binding.
                for (u32 d = 0; d < binding_descriptor_count; ++d) {
                    // TODO: only update in the list if actually needing an update.
                    //
                    // Instead of a flat list of texture maps, the sampler state should have a list of
                    // uniform samplers, each with their own list of texture maps associated with them.
                    // This will make for fast lookups/assignments here.
                    texture_map* map = binding_sampler_state->uniform_texture_maps[d];
                    texture* t = map->texture;

                    u8 t_generation;
                    struct texture_internal_data* texture_internal = texture_system_get_internal_or_default(t, &t_generation);

                    // Ensure the texture is valid.
                    if (t_generation == INVALID_ID_U8) {
                        // Using the default texture, so invalidate the map's generation so it's updated next run.
                        map->generation = INVALID_ID_U8;
                    } else {
                        // If valid, ensure the texture map's generation matches the texture's.
                        // If not, the texture map resources should be regenerated.
                        if (t_generation != map->generation) {
                            b8 refresh_required = t->mip_levels != map->mip_levels;
                            KTRACE("A sampler refresh is%s required. Tex/map mips: %u/%u", refresh_required ? "" : " not", t->mip_levels, map->mip_levels);
                            if (refresh_required && !vulkan_renderer_texture_map_resources_refresh(backend, map)) {
                                KWARN("Failed to refresh texture map resources. This means the sampler settings could be out of date.");
                            } else {
                                // Sync the generations.
                                map->generation = t->generation;
                            }
                        }
                    }

                    u32 image_index = texture_internal->image_count > 1 ? get_current_image_index(context) : 0;
                    vulkan_image* image = &texture_internal->images[image_index];
                    binding_image_infos[sb][d].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    binding_image_infos[sb][d].imageView = image->view;
                    binding_image_infos[sb][d].sampler = context->samplers[map->internal_id];

                    // TODO: change up descriptor state to handle this properly.
                    // Sync frame generation if not using a default texture.
                    // if (t->generation != INVALID_ID) {
                    //     *descriptor_generation = t->generation;
                    //     *descriptor_id = t->id;
                    // }

                    update_sampler_count++;
                }

                VkWriteDescriptorSet sampler_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                sampler_descriptor.dstSet = descriptor_set;
                sampler_descriptor.dstBinding = binding_index;
                sampler_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                sampler_descriptor.descriptorCount = update_sampler_count;
                sampler_descriptor.pImageInfo = binding_image_infos[sb];

                descriptor_writes[descriptor_write_count] = sampler_descriptor;
                descriptor_write_count++;

                binding_index++;
            }
        }

        // Immediately update the descriptor set's data.
        if (descriptor_write_count > 0) {
            // TODO: This can (and probably should be) split out to a separate frame_prepare step from the
            // bind below. This is a prime candidate for jobification during that stage that could be
            // multi-threaded.
            vkUpdateDescriptorSets(context->device.logical_device, descriptor_write_count, descriptor_writes, 0, 0);
        }

        // Sync the frame number.
        descriptor_state->frame_numbers[image_index] = renderer_frame_number;
    }

    // Pick the correct pipeline.
    vulkan_pipeline** pipeline_array = s->is_wireframe ? internal->wireframe_pipelines : internal->pipelines;

    VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;
    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_array[internal->bound_pipeline_index]->pipeline_layout, descriptor_set_index, 1,
                            &descriptor_set, 0, 0);

    return true;
}

b8 vulkan_renderer_shader_apply_globals(renderer_backend_interface* backend, shader* s, u64 renderer_frame_number) {
    // Don't do anything if there are no updatable globals.
    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    if (!has_global) {
        return true;
    }
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    u32 image_index = get_current_image_index(context);
    vulkan_shader* internal = s->internal_data;

    // Obtain global data.
    VkDescriptorSet global_descriptor_set = internal->global_descriptor_sets[image_index];

    // Global is always first, if it exists.
    u32 descriptor_set_index = 0;

    // TODO: global descriptor state is in internal instead of base shader like instance is.
    if (!vulkan_descriptorset_update_and_bind(
            backend,
            renderer_frame_number,
            s,
            global_descriptor_set,
            descriptor_set_index,
            &internal->global_ubo_descriptor_state,
            s->global_ubo_offset,
            s->global_ubo_stride,
            s->global_uniform_count,
            internal->global_sampler_uniforms,
            s->global_uniform_sampler_count)) {
        KERROR("Failed to update/bind global descriptor.");
        return false;
    }

    return true;
}

b8 vulkan_renderer_shader_apply_instance(renderer_backend_interface* backend, shader* s, u64 renderer_frame_number) {
    // Bleat if there are no instances for this shader.
    if (s->instance_uniform_count < 1 && s->instance_uniform_sampler_count < 1) {
        KERROR("This shader does not use instances.");
        return false;
    }
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    u32 image_index = get_current_image_index(context);
    vulkan_shader* internal = s->internal_data;

    // Obtain instance data.
    vulkan_shader_instance_state* instance_state = &internal->instance_states[s->bound_instance_id];
    VkDescriptorSet instance_descriptor_set = instance_state->descriptor_sets[image_index];

    // Determine the descriptor set index which will be first. If there are no globals, for example,
    // this will be 0. If there are globals, this will be 1.
    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    u32 descriptor_set_index = has_global ? 1 : 0;

    if (!vulkan_descriptorset_update_and_bind(
            backend,
            renderer_frame_number,
            s,
            instance_descriptor_set,
            descriptor_set_index,
            &instance_state->ubo_descriptor_state,
            instance_state->offset,
            s->ubo_stride,
            s->instance_uniform_count,
            instance_state->sampler_uniforms,
            s->instance_uniform_sampler_count)) {
        KERROR("Failed to update/bind instance descriptor.");
        return false;
    }

    return true;
}

b8 vulkan_renderer_shader_apply_local(renderer_backend_interface* backend, shader* s, u64 renderer_frame_number) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = s->internal_data;
    VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;

    // Pick the correct pipeline.
    vulkan_pipeline** pipeline_array = s->is_wireframe ? internal->wireframe_pipelines : internal->pipelines;

    vkCmdPushConstants(
        command_buffer,
        pipeline_array[internal->bound_pipeline_index]->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, 128, internal->local_push_constant_block);
    return true;
}

static VkSamplerAddressMode convert_repeat_type(const char* axis,
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

static VkFilter convert_filter_type(const char* op, texture_filter filter) {
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

static b8 create_sampler(vulkan_context* context, texture_map* map, VkSampler* sampler) {
    // Create a sampler for the texture
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    b8 is_depth = map->texture && ((map->texture->flags & TEXTURE_FLAG_DEPTH) != 0);

    // Sync the mip levels with that of the assigned texture.
    map->mip_levels = is_depth ? 1 : map->texture->mip_levels;

    sampler_info.minFilter = convert_filter_type("min", map->filter_minify);
    sampler_info.magFilter = convert_filter_type("mag", map->filter_magnify);

    sampler_info.addressModeU = convert_repeat_type("U", map->repeat_u);
    sampler_info.addressModeV = convert_repeat_type("V", map->repeat_v);
    sampler_info.addressModeW = convert_repeat_type("W", map->repeat_w);

    // TODO: Configurable
    if (is_depth) {
        // Disable anisotropy for depth texture sampling because AMD has a fit over it.
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 0;
    } else {
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = 16;
    }
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

b8 vulkan_renderer_texture_map_resources_acquire(renderer_backend_interface* backend, texture_map* map) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
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

#if _DEBUG
    char* formatted_name = string_format("%s_texmap_sampler", map->texture->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_SAMPLER, context->samplers[selected_id], formatted_name);
    string_free(formatted_name);
#endif
    map->internal_id = selected_id;

    return true;
}

void vulkan_renderer_texture_map_resources_release(renderer_backend_interface* backend, texture_map* map) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (map && map->internal_id != INVALID_ID) {
        // Make sure there's no way this is in use.
        vkDeviceWaitIdle(context->device.logical_device);
        vkDestroySampler(context->device.logical_device, context->samplers[map->internal_id], context->allocator);
        context->samplers[map->internal_id] = 0;
        map->internal_id = INVALID_ID;
    }
}

b8 vulkan_renderer_texture_map_resources_refresh(renderer_backend_interface* backend, texture_map* map) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
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

b8 vulkan_renderer_shader_instance_resources_acquire(renderer_backend_interface* backend, struct shader* s, const shader_instance_resource_config* config, u32* out_instance_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = s->internal_data;

    // FIXME: This is really only valid for the window it's attached to, unless this number is synced and
    // used across all windows. This should probably be stored and accessed elsewhere.
    u32 image_count = context->current_window->renderer_state->backend_state->swapchain.image_count;

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

    texture* default_texture = texture_system_get_default_texture();

    // Map texture maps in the config to the correct uniforms
    vulkan_shader_instance_state* instance_state = &internal->instance_states[*out_instance_id];
    // Only setup if the shader actually requires it.
    if (s->instance_texture_count > 0) {
        instance_state->sampler_uniforms = kallocate(sizeof(vulkan_uniform_sampler_state) * s->instance_uniform_sampler_count, MEMORY_TAG_ARRAY);

        // Assign uniforms to each of the sampler states.
        for (u32 ii = 0; ii < s->instance_uniform_sampler_count; ++ii) {
            vulkan_uniform_sampler_state* sampler_state = &instance_state->sampler_uniforms[ii];
            sampler_state->uniform = &s->uniforms[s->instance_sampler_indices[ii]];

            // Grab the uniform texture config as well.
            shader_instance_uniform_texture_config* tc = &config->uniform_configs[ii];

            u32 array_length = KMAX(sampler_state->uniform->array_length, 1);
            // Setup the array for the sampler texture maps.
            sampler_state->uniform_texture_maps = kallocate(sizeof(texture_map*) * array_length, MEMORY_TAG_ARRAY);
            // Setup descriptor states
            sampler_state->descriptor_states = kallocate(sizeof(vulkan_descriptor_state) * array_length, MEMORY_TAG_ARRAY);
            // Per descriptor
            for (u32 d = 0; d < array_length; ++d) {
                sampler_state->uniform_texture_maps[d] = tc->texture_maps[d];
                // Make sure it has a texture map assigned. Use default if not.
                if (!sampler_state->uniform_texture_maps[d]->texture) {
                    sampler_state->uniform_texture_maps[d]->texture = default_texture;
                }

                sampler_state->descriptor_states[d].generations = kallocate(sizeof(u8) * image_count, MEMORY_TAG_ARRAY);
                sampler_state->descriptor_states[d].ids = kallocate(sizeof(u32) * image_count, MEMORY_TAG_ARRAY);
                sampler_state->descriptor_states[d].frame_numbers = kallocate(sizeof(u64) * image_count, MEMORY_TAG_ARRAY);
                // Per swapchain image
                for (u32 j = 0; j < image_count; ++j) {
                    sampler_state->descriptor_states[d].generations[j] = INVALID_ID_U8;
                    sampler_state->descriptor_states[d].ids[j] = INVALID_ID;
                    sampler_state->descriptor_states[d].frame_numbers[j] = INVALID_ID_U64;
                }
            }
        }
    }

    // Allocate some space in the UBO - by the stride, not the size.
    u64 size = s->ubo_stride;
    if (size > 0) {
        for (u32 i = 0; i < internal->uniform_buffer_count; ++i) {
            if (!renderer_renderbuffer_allocate(&internal->uniform_buffers[i], size, &instance_state->offset)) {
                KERROR("vulkan_material_shader_acquire_resources failed to acquire ubo space");
                return false;
            }
        }
    }

    // UBO binding. NOTE: really only matters where there are instance uniforms, but set them anyway.
    instance_state->ubo_descriptor_state.generations = kallocate(sizeof(u8) * image_count, MEMORY_TAG_ARRAY);
    instance_state->ubo_descriptor_state.ids = kallocate(sizeof(u32) * image_count, MEMORY_TAG_ARRAY);
    instance_state->ubo_descriptor_state.frame_numbers = kallocate(sizeof(u64) * image_count, MEMORY_TAG_ARRAY);
    // Per swapchain image
    for (u32 j = 0; j < image_count; ++j) {
        instance_state->ubo_descriptor_state.generations[j] = INVALID_ID_U8;
        instance_state->ubo_descriptor_state.ids[j] = INVALID_ID_U8;
        instance_state->ubo_descriptor_state.frame_numbers[j] = INVALID_ID_U64;
    }

    b8 has_global = s->global_uniform_count > 0 || s->global_uniform_sampler_count > 0;
    u8 instance_desc_set_index = has_global ? 1 : 0;

    // Per swapchain image
    instance_state->descriptor_sets = kallocate(sizeof(VkDescriptorSet), MEMORY_TAG_ARRAY);
    VkDescriptorSetLayout* layouts = kallocate(sizeof(VkDescriptorSetLayout), MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < image_count; ++i) {
        layouts[i] = internal->descriptor_set_layouts[instance_desc_set_index];
    }

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = internal->descriptor_pool;
    alloc_info.descriptorSetCount = image_count;
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, instance_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating instance descriptor sets in shader: '%s'.", vulkan_result_string(result, true));
        return false;
    }

#ifdef _DEBUG
    for (u32 i = 0; i < image_count; ++i) {
        char* desc_set_object_name = string_format("desc_set_shader_%s_instance_%u_frame_%u", s->name, *out_instance_id, i);
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, instance_state->descriptor_sets[i], desc_set_object_name);
        string_free(desc_set_object_name);
    }
#endif

    return true;
}

b8 vulkan_renderer_shader_instance_resources_release(renderer_backend_interface* backend, shader* s, u32 instance_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = s->internal_data;
    vulkan_shader_instance_state* instance_state = &internal->instance_states[instance_id];

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
        instance_state->ubo_descriptor_state.frame_numbers[j] = INVALID_ID_U64;
    }

    // Destroy bindings and their descriptor states/uniforms.
    for (u32 a = 0; a < s->instance_uniform_sampler_count; ++a) {
        vulkan_uniform_sampler_state* sampler_state = &instance_state->sampler_uniforms[a];
        u32 array_length = KMAX(sampler_state->uniform->array_length, 1);
        kfree(sampler_state->descriptor_states, sizeof(vulkan_descriptor_state) * array_length, MEMORY_TAG_ARRAY);
        sampler_state->descriptor_states = 0;
        kfree(sampler_state->uniform_texture_maps, sizeof(texture_map*) * array_length, MEMORY_TAG_ARRAY);
        sampler_state->uniform_texture_maps = 0;
    }

    if (s->ubo_stride != 0) {
        for (u32 i = 0; i < internal->uniform_buffer_count; ++i) {
            if (!renderer_renderbuffer_free(&internal->uniform_buffers[i], s->ubo_stride, instance_state->offset)) {
                KERROR("vulkan_renderer_shader_release_instance_resources failed to free range from renderbuffer.");
            }
        }
    }
    instance_state->offset = INVALID_ID;
    instance_state->id = INVALID_ID;

    return true;
}

static b8 sampler_state_try_set(vulkan_uniform_sampler_state* sampler_uniforms, u32 sampler_count, u16 uniform_location, u32 array_index, texture_map* map) {
    // Find the sampler uniform state to update.
    for (u32 i = 0; i < sampler_count; ++i) {
        vulkan_uniform_sampler_state* su = &sampler_uniforms[i];
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

b8 vulkan_renderer_uniform_set(renderer_backend_interface* backend, shader* s, shader_uniform* uniform, u32 array_index, const void* value) {
    vulkan_shader* internal = s->internal_data;
    if (uniform_type_is_sampler(uniform->type)) {
        // Samplers can only be assigned at the instance or global level.
        texture_map* map = (texture_map*)value;
        if (uniform->scope == SHADER_SCOPE_GLOBAL) {
            return sampler_state_try_set(internal->global_sampler_uniforms, s->global_uniform_sampler_count, uniform->location, array_index, map);
        } else {
            vulkan_shader_instance_state* instance_state = &internal->instance_states[s->bound_instance_id];
            return sampler_state_try_set(instance_state->sampler_uniforms, s->instance_uniform_sampler_count, uniform->location, array_index, map);
        }
    } else {
        u64 addr;
        u64 ubo_offset = 0;
        u32 image_index = ((vulkan_context*)backend->internal_context)->current_window->renderer_state->backend_state->image_index;
        switch (uniform->scope) {
        case SHADER_SCOPE_LOCAL:
            addr = (u64)internal->local_push_constant_block;
            break;
        case SHADER_SCOPE_INSTANCE:
            if (s->bound_instance_id == INVALID_ID) {
                KERROR("An instance must be bound before setting an instance uniform.");
                return false;
            }
            addr = (u64)internal->mapped_uniform_buffer_blocks[image_index];
            vulkan_shader_instance_state* instance = &internal->instance_states[s->bound_instance_id];
            ubo_offset = instance->offset;
            break;
        case SHADER_SCOPE_GLOBAL:
        default:
            addr = (u64)internal->mapped_uniform_buffer_blocks[image_index];
            ubo_offset = s->global_ubo_offset;
            break;
        }
        addr += ubo_offset + uniform->offset + (uniform->size * array_index);
        kcopy_memory((void*)addr, value, uniform->size);
    }
    return true;
}

#ifdef _DEBUG
static const char* shader_stage_to_string(shader_stage stage) {
    switch (stage) {
    case SHADER_STAGE_VERTEX:
        return "vertex";
        break;
    case SHADER_STAGE_FRAGMENT:
        return "fragment";
        break;
    case SHADER_STAGE_COMPUTE:
        return "compute";
        break;
    case SHADER_STAGE_GEOMETRY:
        return "geometry";
        break;
    default:
        return "";
        break;
    }
}
#endif

static b8 create_shader_module(vulkan_context* context, shader* s, shader_stage_config* config, vulkan_shader_stage* out_stage) {
    shaderc_shader_kind shader_kind;
    VkShaderStageFlagBits stage;
    switch (config->stage) {
    case SHADER_STAGE_VERTEX:
        shader_kind = shaderc_glsl_default_vertex_shader;
        stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        shader_kind = shaderc_glsl_default_fragment_shader;
        stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case SHADER_STAGE_COMPUTE:
        shader_kind = shaderc_glsl_default_compute_shader;
        stage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
    case SHADER_STAGE_GEOMETRY:
        shader_kind = shaderc_glsl_default_geometry_shader;
        stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        break;
    default:
        KERROR("Unsupported shader kind. Unable to create module.");
        return false;
    }

    KDEBUG("Compiling stage '%s' for shader '%s'...", shader_stage_to_string(config->stage), s->name);

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
        const char* error_message = shaderc_result_get_error_message(compilation_result);
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
    const char* bytes = shaderc_result_get_bytes(compilation_result);
    size_t result_length = shaderc_result_get_length(compilation_result);
    // Take a copy of the result data and cast it to a u32* as is required by Vulkan.
    u32* code = kallocate(result_length, MEMORY_TAG_RENDERER);
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

b8 vulkan_renderer_is_multithreaded(renderer_backend_interface* backend) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return context->multithreading_enabled;
}

b8 vulkan_renderer_flag_enabled_get(renderer_backend_interface* backend, renderer_config_flags flag) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return (context->current_window->renderer_state->backend_state->swapchain.flags & flag);
}

void vulkan_renderer_flag_enabled_set(renderer_backend_interface* backend, renderer_config_flags flag, b8 enabled) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_swapchain* swapchain = &context->current_window->renderer_state->backend_state->swapchain;
    swapchain->flags = (enabled ? (swapchain->flags | flag) : (swapchain->flags & ~flag));
    context->render_flag_changed = true;
}

// NOTE: Begin vulkan buffer.

// Indicates if the provided buffer has device-local memory.
static b8 vulkan_buffer_is_device_local(renderer_backend_interface* backend, vulkan_buffer* buffer) {
    return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

// Indicates if the provided buffer has host-visible memory.
static b8 vulkan_buffer_is_host_visible(renderer_backend_interface* backend, vulkan_buffer* buffer) {
    return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}

// Indicates if the provided buffer has host-coherent memory.
static b8 vulkan_buffer_is_host_coherent(renderer_backend_interface* backend, vulkan_buffer* buffer) {
    return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

b8 vulkan_buffer_create_internal(renderer_backend_interface* backend, renderbuffer* buffer) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
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
        VK_SHARING_MODE_EXCLUSIVE; // NOTE: Only used in one queue.

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
    *((vulkan_buffer*)buffer->internal_data) = internal_buffer;

    return true;
}

void vulkan_buffer_destroy_internal(renderer_backend_interface* backend, renderbuffer* buffer) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vkDeviceWaitIdle(context->device.logical_device);
    if (buffer) {
        vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
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

b8 vulkan_buffer_resize(renderer_backend_interface* backend, renderbuffer* buffer, u64 new_size) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data) {
        return false;
    }

    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;

    // Create new buffer.
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = new_size;
    buffer_info.usage = internal_buffer->usage;
    buffer_info.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE; // NOTE: Only used in one queue.

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
                                      new_buffer, 0, buffer->total_size, false);

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

b8 vulkan_buffer_bind(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_bind requires valid pointer to a buffer.");
        return false;
    }
    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    VK_CHECK(vkBindBufferMemory(context->device.logical_device,
                                internal_buffer->handle, internal_buffer->memory,
                                offset));
    return true;
}

b8 vulkan_buffer_unbind(renderer_backend_interface* backend, renderbuffer* buffer) {
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_unbind requires valid pointer to a buffer.");
        return false;
    }

    // NOTE: Does nothing, for now.
    return true;
}

void* vulkan_buffer_map_memory(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_map_memory requires a valid pointer to a buffer.");
        return 0;
    }
    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    void* data;
    VK_CHECK(vkMapMemory(context->device.logical_device, internal_buffer->memory, offset, size, 0, &data));
    return data;
}

void vulkan_buffer_unmap_memory(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_unmap_memory requires a valid pointer to a buffer.");
        return;
    }
    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
}

b8 vulkan_buffer_flush(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data) {
        KERROR("vulkan_buffer_flush requires a valid pointer to a buffer.");
        return false;
    }
    // NOTE: If not host-coherent, flush the mapped memory range.
    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    if (!vulkan_buffer_is_host_coherent(backend, internal_buffer)) {
        VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = internal_buffer->memory;
        range.offset = offset;
        range.size = size;
        VK_CHECK(
            vkFlushMappedMemoryRanges(context->device.logical_device, 1, &range));
    }

    return true;
}

b8 vulkan_buffer_read(renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size, void** out_memory) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data || !out_memory) {
        KERROR(
            "vulkan_buffer_read requires a valid pointer to a buffer and "
            "out_memory, and the size must be nonzero.");
        return false;
    }

    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    if (vulkan_buffer_is_device_local(backend, internal_buffer) &&
        !vulkan_buffer_is_host_visible(backend, internal_buffer)) {
        // NOTE: If a read buffer is needed (i.e.) the target buffer's memory is not
        // host visible but is device-local, create the read buffer, copy data to
        // it, then read from that buffer.

        // Create a host-visible staging buffer to copy to. Mark it as the
        // destination of the transfer.
        renderbuffer read;
        if (!renderer_renderbuffer_create("renderbuffer_read", RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, &read)) {
            KERROR("vulkan_buffer_read() - Failed to create read buffer.");
            return false;
        }
        renderer_renderbuffer_bind(&read, 0);
        vulkan_buffer* read_internal = (vulkan_buffer*)read.internal_data;

        // Perform the copy from device local to the read buffer.
        vulkan_buffer_copy_range(backend, buffer, offset, &read, 0, size, true);

        // Map/copy/unmap
        void* mapped_data;
        VK_CHECK(vkMapMemory(context->device.logical_device, read_internal->memory,
                             0, size, 0, &mapped_data));
        kcopy_memory(*out_memory, mapped_data, size);
        vkUnmapMemory(context->device.logical_device, read_internal->memory);

        // Clean up the read buffer.
        renderer_renderbuffer_unbind(&read);
        renderer_renderbuffer_destroy(&read);
    } else {
        // If no staging buffer is needed, map/copy/unmap.
        void* data_ptr;
        VK_CHECK(vkMapMemory(context->device.logical_device,
                             internal_buffer->memory, offset, size, 0, &data_ptr));
        kcopy_memory(*out_memory, data_ptr, size);
        vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
    }

    return true;
}

b8 vulkan_buffer_load_range(
    renderer_backend_interface* backend,
    renderbuffer* buffer,
    u64 offset,
    u64 size,
    const void* data,
    b8 include_in_frame_workload) {
    //
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!buffer || !buffer->internal_data || !size || !data) {
        KERROR(
            "vulkan_buffer_load_range requires a valid pointer to a buffer, a "
            "nonzero size and a valid pointer to data.");
        return false;
    }

    vulkan_buffer* internal_buffer = (vulkan_buffer*)buffer->internal_data;
    if (vulkan_buffer_is_device_local(backend, internal_buffer) &&
        !vulkan_buffer_is_host_visible(backend, internal_buffer)) {
        // NOTE: If a staging buffer is needed (i.e.) the target buffer's memory is
        // not host visible but is device-local, create a staging buffer to load the
        // data into first. Then copy from it to the target buffer.

        // Load the data into the staging buffer.
        u64 staging_offset = 0;
        renderbuffer* staging = &context->current_window->renderer_state->backend_state->staging[get_current_frame_index(context)];
        renderer_renderbuffer_allocate(staging, size, &staging_offset);
        vulkan_buffer_load_range(backend, staging, staging_offset, size, data, include_in_frame_workload);

        // Perform the copy from staging to the device local buffer.
        vulkan_buffer_copy_range(backend, staging, staging_offset, buffer, offset, size, include_in_frame_workload);
    } else {
        // If no staging buffer is needed, map/copy/unmap.
        void* data_ptr;
        VK_CHECK(vkMapMemory(context->device.logical_device, internal_buffer->memory, offset, size, 0, &data_ptr));
        kcopy_memory(data_ptr, data, size);
        vkUnmapMemory(context->device.logical_device, internal_buffer->memory);
    }

    return true;
}

static b8 vulkan_buffer_copy_range_internal(
    vulkan_context* context,
    VkBuffer source, u64 source_offset,
    VkBuffer dest, u64 dest_offset,
    u64 size, b8 include_in_frame_workload) {
    //
    VkQueue queue = context->device.graphics_queue;
    vulkan_command_buffer temp_command_buffer;
    vulkan_command_buffer* command_buffer = 0;

    // If not including in frame workload, then utilize a new temp command buffer as well. Otherwise this should be done
    // as part of the current frame's work.
    if (!include_in_frame_workload) {
        vkQueueWaitIdle(queue);
        // Create a one-time-use command buffer.
        vulkan_command_buffer_allocate_and_begin_single_use(context, context->device.graphics_command_pool, &temp_command_buffer);
        command_buffer = &temp_command_buffer;
    } else {
        command_buffer = get_current_command_buffer(context);
    }

    // Prepare the copy command and add it to the command buffer.
    VkBufferCopy copy_region;
    copy_region.srcOffset = source_offset;
    copy_region.dstOffset = dest_offset;
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer->handle, source, dest, 1, &copy_region);

    if (!include_in_frame_workload) {
        // Submit the buffer for execution and wait for it to complete.
        vulkan_command_buffer_end_single_use(context, context->device.graphics_command_pool, &temp_command_buffer, queue);
    }
    // NOTE: if not waiting, submission will be handled later.

    return true;
}

b8 vulkan_buffer_copy_range(
    renderer_backend_interface* backend,
    renderbuffer* source,
    u64 source_offset,
    renderbuffer* dest,
    u64 dest_offset,
    u64 size,
    b8 include_in_frame_workload) {
    //
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!source || !source->internal_data || !dest || !dest->internal_data ||
        !size) {
        KERROR(
            "vulkan_buffer_copy_range requires a valid pointers to source and "
            "destination buffers as well as a nonzero size.");
        return false;
    }

    return vulkan_buffer_copy_range_internal(
        context, ((vulkan_buffer*)source->internal_data)->handle, source_offset,
        ((vulkan_buffer*)dest->internal_data)->handle, dest_offset, size, include_in_frame_workload);
    return true;
}

b8 vulkan_buffer_draw(
    renderer_backend_interface* backend,
    renderbuffer* buffer,
    u64 offset,
    u32 element_count,
    b8 bind_only) {
    //
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    if (buffer->type == RENDERBUFFER_TYPE_VERTEX) {
        // Bind vertex buffer at offset.
        VkDeviceSize offsets[1] = {offset};
        vkCmdBindVertexBuffers(command_buffer->handle, 0, 1,
                               &((vulkan_buffer*)buffer->internal_data)->handle,
                               offsets);
        if (!bind_only) {
            vkCmdDraw(command_buffer->handle, element_count, 1, 0, 0);
        }
        return true;
    } else if (buffer->type == RENDERBUFFER_TYPE_INDEX) {
        // Bind index buffer at offset.
        vkCmdBindIndexBuffer(command_buffer->handle,
                             ((vulkan_buffer*)buffer->internal_data)->handle,
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

void vulkan_renderer_wait_for_idle(renderer_backend_interface* backend) {
    if (backend) {
        vulkan_context* context = backend->internal_context;
        VK_CHECK(vkDeviceWaitIdle(context->device.logical_device));
    }
}

/**
 * =================== VULKAN ALLOCATOR ===================
 */

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
static void* vulkan_alloc_allocation(void* user_data, size_t size, size_t alignment,
                                     VkSystemAllocationScope allocation_scope) {
    // Null MUST be returned if this fails.
    if (size == 0) {
        return 0;
    }

    void* result = kallocate_aligned(size, (u16)alignment, MEMORY_TAG_VULKAN);
#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Allocated block %p. Size=%llu, Alignment=%llu", result, size,
           alignment);
#    endif
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
static void vulkan_alloc_free(void* user_data, void* memory) {
    if (!memory) {
#    ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Block is null, nothing to free: %p", memory);
#    endif
        return;
    }

#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Attempting to free block %p...", memory);
#    endif
    u64 size;
    u16 alignment;
    b8 result = kmemory_get_size_alignment(memory, &size, &alignment);
    if (result) {
#    ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE(
            "Block %p found with size/alignment: %llu/%u. Freeing aligned block...",
            memory, size, alignment);
#    endif
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
static void* vulkan_alloc_reallocation(void* user_data, void* original, size_t size,
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

#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Attempting to realloc block %p...", original);
#    endif

    void* result = vulkan_alloc_allocation(user_data, size, alloc_alignment,
                                           allocation_scope);
    if (result) {
#    ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Block %p reallocated to %p, copying data...", original, result);
#    endif

        // Copy over the original memory.
        kcopy_memory(result, original, alloc_size);
#    ifdef KVULKAN_ALLOCATOR_TRACE
        KTRACE("Freeing original aligned block %p...", original);
#    endif
        // Free the original memory only if the new allocation was successful.
        kfree_aligned(original, alloc_size, alloc_alignment, MEMORY_TAG_VULKAN);
    } else {
#    ifdef KVULKAN_ALLOCATOR_TRACE
        KERROR("Failed to realloc %p.", original);
#    endif
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
static void vulkan_alloc_internal_alloc(void* pUserData, size_t size,
                                        VkInternalAllocationType allocationType,
                                        VkSystemAllocationScope allocationScope) {
#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("External allocation of size: %llu", size);
#    endif
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
static void vulkan_alloc_internal_free(void* pUserData, size_t size,
                                       VkInternalAllocationType allocationType,
                                       VkSystemAllocationScope allocationScope) {
#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("External free of size: %llu", size);
#    endif
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
static b8 create_vulkan_allocator(vulkan_context* context,
                                  VkAllocationCallbacks* callbacks) {
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

static vulkan_command_buffer* get_current_command_buffer(vulkan_context* context) {
    kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    return &window_backend->graphics_command_buffers[window_backend->image_index];
}

static u32 get_current_image_index(vulkan_context* context) {
    return context->current_window->renderer_state->backend_state->image_index;
}
static u32 get_current_frame_index(vulkan_context* context) {
    return context->current_window->renderer_state->backend_state->current_frame;
}

static b8 vulkan_graphics_pipeline_create(vulkan_context* context, const vulkan_pipeline_config* config, vulkan_pipeline* out_pipeline) {
    // Viewport state
    VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &config->viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &config->scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer_create_info.depthClampEnable = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = (config->shader_flags & SHADER_FLAG_WIREFRAME) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer_create_info.lineWidth = 1.0f;
    switch (config->cull_mode) {
    case FACE_CULL_MODE_NONE:
        rasterizer_create_info.cullMode = VK_CULL_MODE_NONE;
        break;
    case FACE_CULL_MODE_FRONT:
        rasterizer_create_info.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    default:
    case FACE_CULL_MODE_BACK:
        rasterizer_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    case FACE_CULL_MODE_FRONT_AND_BACK:
        rasterizer_create_info.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
        break;
    }

    if (config->winding == RENDERER_WINDING_CLOCKWISE) {
        rasterizer_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    } else if (config->winding == RENDERER_WINDING_COUNTER_CLOCKWISE) {
        rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    } else {
        KWARN("Invalid front-face winding order specified, default to counter-clockwise");
        rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    rasterizer_create_info.depthBiasEnable = VK_FALSE;
    rasterizer_create_info.depthBiasConstantFactor = 0.0f;
    rasterizer_create_info.depthBiasClamp = 0.0f;
    rasterizer_create_info.depthBiasSlopeFactor = 0.0f;

    // Smooth line rasterisation, if supported.
    VkPipelineRasterizationLineStateCreateInfoEXT line_rasterization_ext = {0};
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT) {
        line_rasterization_ext.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
        line_rasterization_ext.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
        rasterizer_create_info.pNext = &line_rasterization_ext;
    }

    // Multisampling.
    VkPipelineMultisampleStateCreateInfo multisampling_create_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling_create_info.sampleShadingEnable = VK_FALSE;
    multisampling_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling_create_info.minSampleShading = 1.0f;
    multisampling_create_info.pSampleMask = 0;
    multisampling_create_info.alphaToCoverageEnable = VK_FALSE;
    multisampling_create_info.alphaToOneEnable = VK_FALSE;

    // Depth and stencil testing.
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (config->shader_flags & SHADER_FLAG_DEPTH_TEST) {
        depth_stencil.depthTestEnable = VK_TRUE;
        if (config->shader_flags & SHADER_FLAG_DEPTH_WRITE) {
            depth_stencil.depthWriteEnable = VK_TRUE;
        }
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
    }
    depth_stencil.stencilTestEnable = (config->shader_flags & SHADER_FLAG_STENCIL_TEST) ? VK_TRUE : VK_FALSE;
    if (config->shader_flags & SHADER_FLAG_STENCIL_TEST) {
        // equivalent to glStencilFunc(func, ref, mask)
        depth_stencil.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depth_stencil.back.reference = 1;
        depth_stencil.back.compareMask = 0xFF;

        // equivalent of glStencilOp(stencilFail, depthFail, depthPass)pipelin
        depth_stencil.back.failOp = VK_STENCIL_OP_ZERO;
        depth_stencil.back.depthFailOp = VK_STENCIL_OP_ZERO;
        depth_stencil.back.passOp = VK_STENCIL_OP_REPLACE;
        // equivalent of glStencilMask(mask)

        // Back face
        depth_stencil.back.writeMask = (config->shader_flags & SHADER_FLAG_STENCIL_WRITE) ? 0xFF : 0x00;

        // Front face. Just use the same settings for front/back.
        depth_stencil.front = depth_stencil.back;
    }

    VkPipelineColorBlendAttachmentState color_blend_attachment_state;
    kzero_memory(&color_blend_attachment_state, sizeof(VkPipelineColorBlendAttachmentState));
    color_blend_attachment_state.blendEnable = VK_TRUE;
    color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;

    color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend_state_create_info.logicOpEnable = VK_FALSE;
    color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount = 1;
    color_blend_state_create_info.pAttachments = &color_blend_attachment_state;

    // Dynamic state
    VkDynamicState* dynamic_states = darray_create(VkDynamicState);
    darray_push(dynamic_states, VK_DYNAMIC_STATE_VIEWPORT);
    darray_push(dynamic_states, VK_DYNAMIC_STATE_SCISSOR);
    // Dynamic state, if supported.
    if ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) || (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT)) {
        darray_push(dynamic_states, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_FRONT_FACE);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_STENCIL_OP);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_STENCIL_REFERENCE);
        /* darray_push(dynamic_states, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT);
        darray_push(dynamic_states, VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT); */
    }

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state_create_info.dynamicStateCount = darray_length(dynamic_states);
    dynamic_state_create_info.pDynamicStates = dynamic_states;

    // Vertex input
    VkVertexInputBindingDescription binding_description;
    binding_description.binding = 0; // Binding index
    binding_description.stride = config->stride;
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to next data entry for each vertex.

    // Attributes
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = config->attribute_count;
    vertex_input_info.pVertexAttributeDescriptions = config->attributes;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    // The pipeline being created already has available types, so just grab the first one.
    for (u32 i = 1; i < PRIMITIVE_TOPOLOGY_TYPE_MAX; i = i << 1) {
        if (out_pipeline->supported_topology_types & i) {
            primitive_topology_type ptt = i;

            switch (ptt) {
            case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                break;
            default:
                KWARN("primitive topology '%u' not supported. Skipping.", ptt);
                break;
            }

            break;
        }
    }
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    // Push constants
    VkPushConstantRange ranges[32];
    if (config->push_constant_range_count > 0) {
        if (config->push_constant_range_count > 32) {
            KERROR("vulkan_graphics_pipeline_create: cannot have more than 32 push constant ranges. Passed count: %i", config->push_constant_range_count);
            return false;
        }

        // NOTE: 32 is the max number of ranges we can ever have, since spec only guarantees 128 bytes with 4-byte alignment.
        kzero_memory(ranges, sizeof(VkPushConstantRange) * 32);
        for (u32 i = 0; i < config->push_constant_range_count; ++i) {
            ranges[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            ranges[i].offset = config->push_constant_ranges[i].offset;
            ranges[i].size = config->push_constant_ranges[i].size;
        }
        pipeline_layout_create_info.pushConstantRangeCount = config->push_constant_range_count;
        pipeline_layout_create_info.pPushConstantRanges = ranges;
    } else {
        pipeline_layout_create_info.pushConstantRangeCount = 0;
        pipeline_layout_create_info.pPushConstantRanges = 0;
    }

    // Descriptor set layouts
    pipeline_layout_create_info.setLayoutCount = config->descriptor_set_layout_count;
    pipeline_layout_create_info.pSetLayouts = config->descriptor_set_layouts;

    // Create the pipeline layout.
    VK_CHECK(vkCreatePipelineLayout(
        context->device.logical_device,
        &pipeline_layout_create_info,
        context->allocator,
        &out_pipeline->pipeline_layout));

#    if _DEBUG
    char* pipeline_layout_name_buf = string_format("pipeline_layout_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT, out_pipeline->pipeline_layout, pipeline_layout_name_buf);
    string_free(pipeline_layout_name_buf);
#    endif

    // Pipeline create
    VkGraphicsPipelineCreateInfo pipeline_create_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_create_info.stageCount = config->stage_count;
    pipeline_create_info.pStages = config->stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;

    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    pipeline_create_info.pMultisampleState = &multisampling_create_info;
    pipeline_create_info.pDepthStencilState = ((config->shader_flags & SHADER_FLAG_DEPTH_TEST) || (config->shader_flags & SHADER_FLAG_STENCIL_TEST)) ? &depth_stencil : 0;
    pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    pipeline_create_info.pTessellationState = 0;

    pipeline_create_info.layout = out_pipeline->pipeline_layout;

    pipeline_create_info.renderPass = VK_NULL_HANDLE;
    pipeline_create_info.subpass = 0;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;

    // dynamic rendering
    VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    pipeline_rendering_create_info.pNext = VK_NULL_HANDLE;
    pipeline_rendering_create_info.colorAttachmentCount = config->colour_attachment_count;
    pipeline_rendering_create_info.pColorAttachmentFormats = config->colour_attachment_formats;
    pipeline_rendering_create_info.depthAttachmentFormat = config->depth_attachment_format;
    pipeline_rendering_create_info.stencilAttachmentFormat = config->stencil_attachment_format;

    pipeline_create_info.pNext = &pipeline_rendering_create_info;

    VkResult result = vkCreateGraphicsPipelines(
        context->device.logical_device,
        VK_NULL_HANDLE,
        1,
        &pipeline_create_info,
        context->allocator,
        &out_pipeline->handle);

    // Cleanup
    darray_destroy(dynamic_states);

#    if _DEBUG
    char* pipeline_name_buf = string_format("pipeline_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE, out_pipeline->handle, pipeline_name_buf);
    string_free(pipeline_name_buf);
#    endif

    if (vulkan_result_is_success(result)) {
        KDEBUG("Graphics pipeline created!");
        return true;
    }

    KERROR("vkCreateGraphicsPipelines failed with %s.", vulkan_result_string(result, true));
    return false;
}

static void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline) {
    if (pipeline) {
        // Destroy pipeline
        if (pipeline->handle) {
            vkDestroyPipeline(context->device.logical_device, pipeline->handle, context->allocator);
            pipeline->handle = 0;
        }

        // Destroy layout
        if (pipeline->pipeline_layout) {
            vkDestroyPipelineLayout(context->device.logical_device, pipeline->pipeline_layout, context->allocator);
            pipeline->pipeline_layout = 0;
        }
    }
}

static void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline) {
    vkCmdBindPipeline(command_buffer->handle, bind_point, pipeline->handle);
}

#endif // KVULKAN_USE_CUSTOM_ALLOCATOR == 1
