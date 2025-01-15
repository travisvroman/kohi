// #pragma pack(8)

#include "vulkan_backend.h"

#include <shaderc/env.h>
#include <vulkan/vulkan_core.h>
// For runtime shader compilation.
#include <shaderc/shaderc.h>
#include <shaderc/status.h>

#include <containers/darray.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <kresources/kresource_types.h>
#include <logger.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <platform/vulkan_platform.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/resource_types.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <utils/ksort.h>
#include <utils/render_type_utils.h>

#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_image.h"
#include "vulkan_swapchain.h"
#include "vulkan_types.h"
#include "vulkan_utils.h"

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

static b8 recreate_swapchain(renderer_backend_interface* backend, kwindow* window);
static b8 create_shader_module(vulkan_context* context, vulkan_shader* internal_shader, shader_stage stage, const char* source, const char* filename, vulkan_shader_stage* out_stage);
static b8 vulkan_buffer_copy_range_internal(vulkan_context* context,
                                            VkBuffer source, u64 source_offset,
                                            VkBuffer dest, u64 dest_offset,
                                            u64 size, b8 queue_wait);
static vulkan_command_buffer* get_current_command_buffer(vulkan_context* context);
static u32 get_current_image_index(vulkan_context* context);
static u32 get_current_frame_index(vulkan_context* context);

// Returns the current image count. Typically 2 for double-buffering, 3 for triple.
// Should NOT be used when determining resource size. See VULKAN_RESOURCE_IMAGE_COUNT.
static u32 get_current_image_count(vulkan_context* context);

static b8 vulkan_graphics_pipeline_create(vulkan_context* context, const vulkan_pipeline_config* config, vulkan_pipeline* out_pipeline);
static void vulkan_pipeline_destroy(vulkan_context* context, vulkan_pipeline* pipeline);
static void vulkan_pipeline_bind(vulkan_command_buffer* command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline* pipeline);
static b8 setup_frequency_state(renderer_backend_interface* backend, vulkan_shader* internal_shader, shader_update_frequency frequency, u32* out_frequency_id);
static b8 release_shader_frequency_state(vulkan_context* context, vulkan_shader* internal_shader, shader_update_frequency frequency, u32 frequency_id);
static void destroy_shader_frequency_states(shader_update_frequency frequency, vulkan_shader_frequency_state* states, u32 state_count, vulkan_shader_frequency_info* info);
static b8 shader_create_modules_and_pipelines(renderer_backend_interface* backend, vulkan_shader* internal_shader, u8 stage_count, shader_stage_config* stage_configs);
static void setup_frequency_descriptors(b8 do_ubo, vulkan_shader_frequency_info* frequency_info, vulkan_descriptor_set_config* set_config, const kresource_shader* config);
static b8 vulkan_descriptorset_update_and_bind(
    vulkan_context* context,
    u16 generation,
    vulkan_shader* internal_shader,
    const vulkan_shader_frequency_info* info,
    vulkan_shader_frequency_state* frequency_state,
    u32 descriptor_set_index);
static b8 frequency_has_uniforms(vulkan_shader_frequency_info* frequency_info);

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
    darray_push(required_extensions, &VK_KHR_SURFACE_EXTENSION_NAME);   // Generic surface extension
    vulkan_platform_get_required_extension_names(&required_extensions); // Platform-specific extension(s)
    u32 required_extension_count = 0;

    darray_push(required_extensions, &VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // debug utilities

    KDEBUG("Required extensions:");
    required_extension_count = darray_length(required_extensions);
    for (u32 i = 0; i < required_extension_count; ++i) {
        KDEBUG(required_extensions[i]);
    }

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
    if (context->validation_enabled) {
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

        PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
        KASSERT_MSG(func, "Failed to create debug messenger!");
        VK_CHECK(func(context->instance, &debug_create_info, context->allocator, &context->debug_messenger));
        KDEBUG("Vulkan debugger created.");

        // Load up debug function pointers.
        context->pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(context->instance, "vkSetDebugUtilsObjectNameEXT");
        if (!context->pfnSetDebugUtilsObjectNameEXT) {
            KWARN("Unable to load function pointer for vkSetDebugUtilsObjectNameEXT. Debug functions associated with this will not work.");
        }
        context->pfnSetDebugUtilsObjectTagEXT = (PFN_vkSetDebugUtilsObjectTagEXT)vkGetInstanceProcAddr(context->instance, "vkSetDebugUtilsObjectTagEXT");
        if (!context->pfnSetDebugUtilsObjectTagEXT) {
            KWARN("Unable to load function pointer for vkSetDebugUtilsObjectTagEXT. Debug functions associated with this will not work.");
        }

        context->pfnCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(context->instance, "vkCmdBeginDebugUtilsLabelEXT");
        if (!context->pfnCmdBeginDebugUtilsLabelEXT) {
            KWARN("Unable to load function pointer for vkCmdBeginDebugUtilsLabelEXT. Debug functions associated with this will not work.");
        }

        context->pfnCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(context->instance, "vkCmdEndDebugUtilsLabelEXT");
        if (!context->pfnCmdEndDebugUtilsLabelEXT) {
            KWARN("Unable to load function pointer for vkCmdEndDebugUtilsLabelEXT. Debug functions associated with this will not work.");
        }
    }

    // Device creation
    if (!vulkan_device_create(context)) {
        KERROR("Failed to create device!");
        return false;
    }

    // Samplers array.
    context->samplers = darray_create(vulkan_sampler_handle_data);

    // Shaders array.
    context->shaders = darray_reserve(vulkan_shader, config->max_shader_count);

    // Create a shader compiler to be used.
    context->shader_compiler = shaderc_compiler_initialize();

    KINFO("Renderer config requests %s-buffering to be used.", config->use_triple_buffering ? "triple" : "double");
    context->triple_buffering_enabled = config->use_triple_buffering;

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

    if (context->validation_enabled) {
        KDEBUG("Destroying Vulkan debugger...");
        if (context->debug_messenger) {
            PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT");
            func(context->instance, context->debug_messenger, context->allocator);
        }
    }

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

    // Start with a zero frame index.
    window_backend->current_frame = 0;

    // Create swapchain.
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

    // Setup initial max frames in flight based on config. This may be overridden if the max number of swapchain images < 3.
    window_backend->max_frames_in_flight = context->triple_buffering_enabled ? 2 : 1;

    // Create per-frame-in-flight resources.
    {
        // Sync objects are owned by the window since they go hand-in-hand
        // with the swapchain and window resources.
        window_backend->image_available_semaphores = KALLOC_TYPE_CARRAY(VkSemaphore, window_backend->max_frames_in_flight);
        window_backend->queue_complete_semaphores = KALLOC_TYPE_CARRAY(VkSemaphore, window_backend->max_frames_in_flight);
        window_backend->in_flight_fences = KALLOC_TYPE_CARRAY(VkFence, window_backend->max_frames_in_flight);

        window_backend->frame_texture_updated_list = KALLOC_TYPE_CARRAY(khandle*, window_backend->max_frames_in_flight);
        window_backend->graphics_command_buffers = KALLOC_TYPE_CARRAY(vulkan_command_buffer, window_backend->max_frames_in_flight);

        // The staging buffer also goes here since it is tied to the frame.
        // TODO: Reduce this to a single buffer split by max_frames_in_flight.
        const u64 staging_buffer_size = MEBIBYTES(768); // FIXME: This is huge. Need to queue updates per frame in flight to shrink this down.
        window_backend->staging = kallocate(sizeof(renderbuffer) * window_backend->max_frames_in_flight, MEMORY_TAG_ARRAY);

        for (u8 i = 0; i < window_backend->max_frames_in_flight; ++i) {
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

            // Create the per-frame list of updated texture handles.
            window_backend->frame_texture_updated_list[i] = darray_create(khandle);

            // Command buffer.
            vulkan_command_buffer* primary_buffer = &window_backend->graphics_command_buffers[i];
            kzero_memory(primary_buffer, sizeof(vulkan_command_buffer));

            // Allocate a new buffer.
            char* name = string_format("%s_command_buffer_%d", window->name, i);

            // Primary command buffers have secondary command buffers to facilitate "passes", of sorts.
            // TODO: should this be configurable?
            const u32 secondary_count = 16;

            vulkan_command_buffer_allocate(context, context->device.graphics_command_pool, true, name, primary_buffer, secondary_count);
            string_free(name);

            KDEBUG("Vulkan command buffers created.")
        }
    }

    /* // Create the depthbuffer.
    KDEBUG("Creating Vulkan depthbuffer for window '%s'...", window->name);
    if (khandle_is_invalid(window_internal->depthbuffer->renderer_texture_handle)) {
        // If invalid, then a new one needs to be created. This does not reach out to the
        // texture system to create this, but handles it internally instead. This is because
        // the process for this varies greatly between backends.
        if (!renderer_kresource_texture_resources_acquire(
                backend->frontend_state,
                kname_create(window->name),
                KRESOURCE_TEXTURE_TYPE_2D,
                window->width,
                window->height,
                4,
                1,
                1,
                // NOTE: This should be a wrapped texture, so the frontend does not try to
                // acquire the resources we already have here.
                // Also flag as a depth texture
                TEXTURE_FLAG_IS_WRAPPED | TEXTURE_FLAG_IS_WRITEABLE | TEXTURE_FLAG_RENDERER_BUFFERING | TEXTURE_FLAG_DEPTH,
                &window_internal->depthbuffer->renderer_texture_handle)) {
            KFATAL("Failed to acquire internal texture resources for window.depthbuffer");
            return false;
        }
    } */

    /* // Get the texture_internal_data based on the existing or newly-created handle above.
    // Use that to setup the internal images/views for the colourbuffer texture.
    vulkan_texture_handle_data* texture_data = &context->textures[window_internal->depthbuffer->renderer_texture_handle.handle_index];
    if (!texture_data) {
        KFATAL("Unable to get internal data for depthbuffer image. Window creation failed.");
        return false;
    }

    // Name is meaningless here, but might be useful for debugging.
    if (window_internal->depthbuffer->base.name == INVALID_KNAME) {
        window_internal->depthbuffer->base.name = kname_create("__window_depthbuffer_texture__");
    } */

    /* texture_data->image_count = window_backend->swapchain.image_count;
    // Create the array if it doesn't exist.
    if (!texture_data->images) {
        // Also have to setup the internal data.
        texture_data->images = kallocate(sizeof(vulkan_image) * texture_data->image_count, MEMORY_TAG_TEXTURE);
    } */

    /* // Update the parameters and setup a view for each image.
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        vulkan_image* image = &texture_data->images[i];

        // Construct a unique name for each image.
        char* formatted_name = string_format("__window_%s_depth_stencil_texture_%u", window->name, i);

        // Create the actual backing image.
        vulkan_image_create(
            context,
            KRESOURCE_TEXTURE_TYPE_2D,
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
        // TODO: Does this need to be tracked?
        // window->renderer_state->depthbuffer->format = context->device.depth_channel_count;

        // Setup a debug name for the image.
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_IMAGE, image->handle, image->name);
    } */

    /* KINFO("Vulkan depthbuffer created successfully."); */

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

    // Destroy per-frame-in-flight resources.
    {
        for (u32 i = 0; i < window_backend->max_frames_in_flight; ++i) {
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

            // Command buffers
            if (window_backend->graphics_command_buffers[i].handle) {
                vulkan_command_buffer_free(context, context->device.graphics_command_pool, &window_backend->graphics_command_buffers[i]);
                window_backend->graphics_command_buffers[i].handle = 0;
            }
        }
        KFREE_TYPE_CARRAY(window_backend->image_available_semaphores, VkSemaphore, window_backend->max_frames_in_flight);
        window_backend->image_available_semaphores = 0;

        KFREE_TYPE_CARRAY(window_backend->queue_complete_semaphores, VkSemaphore, window_backend->max_frames_in_flight);
        window_backend->queue_complete_semaphores = 0;

        KFREE_TYPE_CARRAY(window_backend->in_flight_fences, VkFence, window_backend->max_frames_in_flight);
        window_backend->in_flight_fences = 0;

        KFREE_TYPE_CARRAY(window_backend->staging, renderbuffer, window_backend->max_frames_in_flight);
        window_backend->staging = 0;

        KFREE_TYPE_CARRAY(window_backend->graphics_command_buffers, vulkan_command_buffer, window_backend->max_frames_in_flight);
        window_backend->graphics_command_buffers = 0;
    }

    // Destroy per-swapchain-image resources.
    {

        // Destroy depthbuffer images/views.
        vulkan_texture_handle_data* texture_data = &context->textures[window_internal->depthbuffer->renderer_texture_handle.handle_index];
        if (!texture_data) {
            KWARN("Unable to get internal data for depthbuffer image. Underlying resources may not be properly destroyed.");
        } else {
            // Free the name
            window_internal->depthbuffer->base.name = INVALID_KNAME;

            // Destroy each backing image.
            for (u32 i = 0; i < texture_data->image_count; ++i) {
                vulkan_image_destroy(context, &texture_data->images[i]);
            }

            // Releasing the resources for the default depthbuffer should destroy backing resources too.
            renderer_texture_resources_release(backend->frontend_state, &window->renderer_state->depthbuffer->renderer_texture_handle);
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
#if KOHI_DEBUG
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    vec4 rgba = (vec4){colour.r, colour.g, colour.b, 1.0f};
    VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, label_text, rgba);
#endif
}

void vulkan_renderer_end_debug_label(renderer_backend_interface* backend) {
#if KOHI_DEBUG
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

        if (window_backend->skip_frames == window_backend->max_frames_in_flight) {
            // Sync the framebuffer size generation.
            window_backend->framebuffer_previous_size_generation = window_backend->framebuffer_size_generation;

            window_backend->skip_frames = 0;
        }

        KINFO("Resized, booting. (frame=%u, image_index=%u)", window_backend->current_frame, window_backend->image_index);
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

    // Increment texture generations in list of handles updated within frame workload.
    khandle* updated_textures = context->current_window->renderer_state->backend_state->frame_texture_updated_list[window_backend->current_frame];
    u32 updated_texture_count = 0;
    for (u32 i = 0; i < updated_texture_count; ++i) {
        vulkan_texture_handle_data* texture = &context->textures[updated_textures[i].handle_index];
        texture->generation++;
        // Roll over when at max u16.
        if (texture->generation == INVALID_ID_U16) {
            texture->generation = 0;
        }
    }
    // Clear the list.
    darray_clear(updated_textures);

    // Acquire the next image from the swap chain. Pass along the semaphore that
    // should signaled when this completes. This same semaphore will later be
    // waited on by the queue submission to ensure this image is available.
    result = vkAcquireNextImageKHR(
        context->device.logical_device,
        window_backend->swapchain.handle,
        U64_MAX,
        window_backend->image_available_semaphores[window_backend->current_frame],
        0,
        &window_backend->swapchain.image_index);

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
    return true;
}

b8 vulkan_renderer_frame_command_list_end(renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    // Source is the window's colour buffer texture.
    vulkan_texture_handle_data* source_image_handle = &context->textures[context->current_window->renderer_state->colourbuffer->renderer_texture_handle.handle_index];
    vulkan_image* source_image = &source_image_handle->images[window_backend->image_index];
    // Target is the current swapchain image.
    vulkan_texture_handle_data* target_image_handle = &context->textures[window_backend->swapchain.swapchain_colour_texture->renderer_texture_handle.handle_index];
    vulkan_image* target_image = &target_image_handle->images[window_backend->swapchain.image_index];

    // Before ending the command buffer, blit the current colour buffer's contents to
    // the current swapchain image. Start by transitioning to transfer source layout.
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Or previous layout
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = source_image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = source_image->layer_count;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = source_image->mip_levels;
        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, 0, 0, 0, 1, &barrier);
    }

    // Make sure the acquired image is done being read from
    {

        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;            // Image was read by the presentation engine
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Image will be written to during rendering
        barrier.oldLayout = 0;                                        // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;          // Layout before rendering
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Layout for rendering
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target_image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = target_image->mip_levels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = target_image->layer_count;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // Presentation completed (prior to rendering)
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // Rendering will occur here
            0, 0, 0, 0, 0, 1, &barrier);
    }

    // Transition the swapchain image to transfer destination layout.
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Or previous layout
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = target_image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = target_image->layer_count;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = target_image->mip_levels;
        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, 0, 0, 0, 1, &barrier);
    }

    // Now do the blit operation from the source image to the target image
    {
        VkImageBlit blit_region = {};
        blit_region.srcOffsets[0] = (VkOffset3D){0, 0, 0};                                      // Starting coordinates in the source image
        blit_region.srcOffsets[1] = (VkOffset3D){source_image->width, source_image->height, 1}; // Ending coordinates in the source image
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = source_image->layer_count;
        blit_region.srcSubresource.mipLevel = 0;

        blit_region.dstOffsets[0] = (VkOffset3D){0, 0, 0};                                      // Starting coordinates in the swapchain image
        blit_region.dstOffsets[1] = (VkOffset3D){target_image->width, target_image->height, 1}; // Ending coordinates in the swapchain image
        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = target_image->layer_count;
        blit_region.dstSubresource.mipLevel = 0;

        // Perform the blit operation
        vkCmdBlitImage(
            command_buffer->handle,
            source_image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            target_image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit_region,
            VK_FILTER_LINEAR);
    }

    // Transition source back to the correct layout for rendering to
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0; // No access (or VK_ACCESS_SHADER_READ_BIT for example)
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Or any appropriate layout
        barrier.image = source_image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = source_image->layer_count;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = source_image->mip_levels;
        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, 0, 0, 0, 1, &barrier);
    }

    // Transition target for presentation.
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.image = target_image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = target_image->layer_count;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = target_image->mip_levels;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, 0, 0, 0, 1, &barrier);
    }

    // Barrier for vertex buffer
    {
        VkBufferMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
        barrier.buffer = ((vulkan_buffer*)renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX)->internal_data)->handle;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;                             //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,     // _LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // _FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            0,
            0, 0,
            1, &barrier,
            0, 0);
    }

    // Barrier for index buffer
    {
        VkBufferMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
        barrier.buffer = ((vulkan_buffer*)renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX)->internal_data)->handle;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;  // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,     // _LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // _FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            0,
            0, 0,
            1, &barrier,
            0, 0);
    }

    // Just end the command buffer.
    vulkan_command_buffer_end(command_buffer);

    // Increment (and wrap) the colour buffer image index.
    context->current_window->renderer_state->backend_state->image_index =
        ((context->current_window->renderer_state->backend_state->image_index + 1) % (context->triple_buffering_enabled ? 3 : 2));

    return true;
}

b8 vulkan_renderer_frame_submit(struct renderer_backend_interface* backend, struct frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Only a primary command buffer should be submitted.
    if (!command_buffer->is_primary) {
        KFATAL("vulkan_renderer_frame_submit tried to submit Secondary command buffers. This must not happen.");
        return false;
    }

    // Submit the command buffer for execution.
    b8 result = vulkan_command_buffer_submit(
        command_buffer,
        context->device.graphics_queue,
        1,
        // The semaphore(s) to be signaled when the queue is complete.
        &window_backend->queue_complete_semaphores[window_backend->current_frame],
        1,
        // Wait semaphore ensures that the operation cannot begin until the image is available.
        &window_backend->image_available_semaphores[window_backend->current_frame],
        window_backend->in_flight_fences[window_backend->current_frame]);

    if (!result) {
        KERROR("Failed to submit vulkan command buffer successfully. See logs for details");
        return false;
    }

    // Loop back to the first index.
    command_buffer->secondary_buffer_index = 0;
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
    present_info.pImageIndices = &window_backend->swapchain.image_index;
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
    window_backend->current_frame = (window_backend->current_frame + 1) % window_backend->max_frames_in_flight;

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

void vulkan_renderer_cull_mode_set(struct renderer_backend_interface* backend, renderer_cull_mode cull_mode) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    VkCullModeFlags vulkan_cull_mode = VK_CULL_MODE_NONE;
    switch (cull_mode) {
    default:
    case RENDERER_CULL_MODE_NONE:
        vulkan_cull_mode = VK_CULL_MODE_NONE;
        break;
    case RENDERER_CULL_MODE_FRONT:
        vulkan_cull_mode = VK_CULL_MODE_FRONT_BIT;
        break;
    case RENDERER_CULL_MODE_BACK:
        vulkan_cull_mode = VK_CULL_MODE_BACK_BIT;
        break;
    case RENDERER_CULL_MODE_FRONT_AND_BACK:
        vulkan_cull_mode = VK_CULL_MODE_FRONT_AND_BACK;
        break;
    }
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetCullMode(command_buffer->handle, vulkan_cull_mode);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetCullModeEXT(command_buffer->handle, vulkan_cull_mode);
    } else {
        KFATAL("renderer_cull_mode_set cannot be used on a device without dynamic state support.");
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

void vulkan_renderer_begin_rendering(struct renderer_backend_interface* backend, frame_data* p_frame_data, rect_2d render_area, u32 colour_target_count, khandle* colour_targets, khandle depth_stencil_target, u32 depth_stencil_layer) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* primary = get_current_command_buffer(context);
    u32 image_index = get_current_image_index(context);

    // Anytime we "begin" a render, update the "in-secondary" state and get the appropriate secondary buffer.
    primary->in_secondary = true;
    vulkan_command_buffer* secondary = get_current_command_buffer(context);
    vulkan_command_buffer_begin(secondary, false, false, false);

    VkRenderingInfo render_info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    render_info.renderArea.offset.x = render_area.x;
    render_info.renderArea.offset.y = render_area.y;
    render_info.renderArea.extent.width = render_area.width;
    render_info.renderArea.extent.height = render_area.height;

    // TODO: This may be a problem for layered images/cubemaps
    render_info.layerCount = 1;

    // Depth

    VkRenderingAttachmentInfoKHR depth_attachment_info = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (!khandle_is_invalid(depth_stencil_target)) {
        vulkan_texture_handle_data* depth_stencil_data = &context->textures[depth_stencil_target.handle_index];
        vulkan_image* image = &depth_stencil_data->images[image_index];

        depth_attachment_info.imageView = image->view;
        if (image->layer_count > 1) {
            depth_attachment_info.imageView = image->layer_views[depth_stencil_layer];
        }

        depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;    // Always load.
        depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Always store.
        depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment_info.resolveImageView = 0;
        if (image->flags & TEXTURE_FLAG_DEPTH) {
            render_info.pDepthAttachment = &depth_attachment_info;
        } else {
            render_info.pDepthAttachment = 0;
        }
        if (image->flags & TEXTURE_FLAG_STENCIL) {
            render_info.pStencilAttachment = &depth_attachment_info;
        } else {
            render_info.pStencilAttachment = 0;
        }
    } else {
        render_info.pDepthAttachment = 0;
        render_info.pStencilAttachment = 0;
    }

    render_info.colorAttachmentCount = colour_target_count;
    if (colour_target_count) {
        // NOTE: this memory won't be leaked because it uses the frame allocator, which is reset per frame.
        VkRenderingAttachmentInfo* colour_attachments = p_frame_data->allocator.allocate(sizeof(VkRenderingAttachmentInfo) * colour_target_count);
        // VkImageMemoryBarrier colour_barriers[32] = {0};
        for (u32 i = 0; i < colour_target_count; ++i) {
            vulkan_texture_handle_data* colour_target_data = &context->textures[colour_targets[i].handle_index];
            vulkan_image* image = &colour_target_data->images[image_index];

            VkRenderingAttachmentInfo* attachment_info = &colour_attachments[i];
            attachment_info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment_info->imageView = image->view;
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

    // Kick off the render using the secondary buffer.
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdBeginRendering(secondary->handle, &render_info);
    } else {
        context->vkCmdBeginRenderingKHR(secondary->handle, &render_info);
    }
}

void vulkan_renderer_end_rendering(struct renderer_backend_interface* backend, frame_data* p_frame_data) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    // Since ending a rendering, will be in a secondary buffer.
    vulkan_command_buffer* secondary = get_current_command_buffer(context);

    // End rendering
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdEndRendering(secondary->handle);
    } else {
        context->vkCmdEndRenderingKHR(secondary->handle);
    }

    // End secondary command buffer.
    vulkan_command_buffer_end(secondary);

    // Barrier for vertex buffer
    {
        VkBufferMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
        barrier.buffer = ((vulkan_buffer*)renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX)->internal_data)->handle;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;  // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

        vkCmdPipelineBarrier(
            secondary->parent->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,     // _LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // _FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            0,
            0, 0,
            1, &barrier,
            0, 0);
    }

    // Barrier for index buffer
    {
        VkBufferMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
        barrier.buffer = ((vulkan_buffer*)renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX)->internal_data)->handle;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;  // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

        vkCmdPipelineBarrier(
            secondary->parent->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,     // _LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // _FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            0,
            0, 0,
            1, &barrier,
            0, 0);
    }

    // Execute secondary command buffer.
    vulkan_command_buffer_execute_secondary(secondary);
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

void vulkan_renderer_clear_colour_texture(renderer_backend_interface* backend, khandle renderer_texture_handle) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    vulkan_texture_handle_data* tex_internal = &context->textures[renderer_texture_handle.handle_index];
    u32 image_index = get_current_image_index(context);

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];

    // Transition the layout to transfer, since clearing is a transfer operation.
    {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.image = image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->mip_levels;
        barrier.subresourceRange.layerCount = image->layer_count;
        barrier.subresourceRange.baseArrayLayer = 0;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);
    }

    // Clear the image.
    vkCmdClearColorImage(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        &context->colour_clear_value,
        image->layer_count,
        image->layer_count == 1 ? &image->view_subresource_range : image->layer_view_subresource_ranges);

    // Transition to colour attachment optimal layout for rendering.
    {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.image = image->handle;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->mip_levels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = image->layer_count;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);
    }
}

void vulkan_renderer_clear_depth_stencil(renderer_backend_interface* backend, khandle renderer_texture_handle) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    u32 image_index = get_current_image_index(context);

    vulkan_texture_handle_data* tex_internal = &context->textures[renderer_texture_handle.handle_index];

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];
    b8 is_depth = FLAG_GET(image->flags, TEXTURE_FLAG_DEPTH);
    // b8 is_stencil = FLAG_GET(image->flags, TEXTURE_FLAG_STENCIL);

    VkImageAspectFlags aspect_flags = 0;
    // aspect_flags |= is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    // aspect_flags |= is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    // HACK: Must use both because of the internal depth format containing stencil anyway.
    aspect_flags = is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : 0;

    // Transition the layout to transfer, since clearing is a transfer operation.
    {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.image = image->handle;
        barrier.subresourceRange.aspectMask = aspect_flags;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->mip_levels;
        barrier.subresourceRange.layerCount = image->layer_count;
        barrier.subresourceRange.baseArrayLayer = 0;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);
    }

    // Clear the image.
    vkCmdClearDepthStencilImage(
        command_buffer->handle,
        image->handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &context->depth_stencil_clear_value,
        image->layer_count,
        image->layer_count == 1 ? &image->view_subresource_range : image->layer_view_subresource_ranges);

    // Transition to depth/stencil attachment optimal layout for rendering.
    {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = 0;                                                // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // NOTE: may have to check if stencil
        barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
        barrier.image = image->handle;
        barrier.subresourceRange.aspectMask = aspect_flags;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = image->mip_levels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = image->layer_count;

        vkCmdPipelineBarrier(
            command_buffer->handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0,
            0, 0,
            0, 0,
            1, &barrier);
    }
}

void vulkan_renderer_colour_texture_prepare_for_present(renderer_backend_interface* backend, khandle renderer_texture_handle) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    u32 image_index = get_current_image_index(context);

    vulkan_texture_handle_data* tex_internal = &context->textures[renderer_texture_handle.handle_index];

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = 0; // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
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

    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // 0;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;                                         //| VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, // VK_PIPELINE_STAGE_TRANSFER_BIT,// VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, 0,
        0, 0,
        1, &barrier);
}

void vulkan_renderer_texture_prepare_for_sampling(renderer_backend_interface* backend, khandle renderer_texture_handle, texture_flag_bits flags) {
    // Cold-cast the context
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);
    u32 image_index = get_current_image_index(context);

    vulkan_texture_handle_data* tex_internal = &context->textures[renderer_texture_handle.handle_index];

    // If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
    vulkan_image* image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];
    b8 is_depth = FLAG_GET(image->flags, TEXTURE_FLAG_DEPTH);
    // b8 is_stencil = FLAG_GET(image->flags, TEXTURE_FLAG_STENCIL);

    VkImageAspectFlags aspect_flags = 0;
    // aspect_flags |= is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    // aspect_flags |= is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    // HACK: Must use both because of the internal depth format containing stencil anyway.
    aspect_flags = is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : 0;
    if (!aspect_flags) {
        aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    // Transition the layout
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = 0; // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
    barrier.image = image->handle;
    barrier.subresourceRange.aspectMask = aspect_flags;
    // Mips
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image->mip_levels;

    // Transition all layers at once.
    barrier.subresourceRange.layerCount = image->layer_count;

    // Start at the first layer.
    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

    vkCmdPipelineBarrier(
        command_buffer->handle,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
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

    // Indicate to listeners that a render target refresh is required.
    // TODO: Might remove this.
    event_fire(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, 0, (event_context){0});

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

b8 vulkan_renderer_texture_resources_acquire(renderer_backend_interface* backend, const char* name, texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, texture_flag_bits flags, khandle* out_renderer_texture_handle) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    if (!context->textures) {
        // FIXME: Should be max textures in config.
        context->textures = darray_reserve(vulkan_texture_handle_data, 512);
    }

    // Get an entry into the lookup table.
    vulkan_texture_handle_data* texture_data = 0;
    u32 texture_count = darray_length(context->textures);
    for (u32 i = 0; i < texture_count; ++i) {
        texture_data = &context->textures[i];
        if (texture_data->uniqueid == INVALID_ID_U64) {
            // Found a free "slot", use it.
            khandle new_handle = khandle_create(i);
            texture_data->uniqueid = new_handle.unique_id.uniqueid;
            *out_renderer_texture_handle = new_handle;
            break;
        }
    }
    if (khandle_is_invalid(*out_renderer_texture_handle)) {
        // No free "slots", add one.
        vulkan_texture_handle_data new_lookup = {0};
        khandle new_handle = khandle_create(texture_count);
        new_lookup.uniqueid = new_handle.unique_id.uniqueid;
        darray_push(context->textures, new_lookup);
        *out_renderer_texture_handle = new_handle;
        texture_data = &context->textures[texture_count];
    }

    if (flags & TEXTURE_FLAG_IS_WRAPPED) {
        // If the texure is considered "wrapped" (i.e. internal resources are created somwhere else,
        // such as swapchain images), then nothing further is required. Just return the handle.
        return true;
    }

    // Internal data creation.
    if (flags & TEXTURE_FLAG_RENDERER_BUFFERING) {
        // Need to generate enough images to support triple-buffering.
        texture_data->image_count = VULKAN_RESOURCE_IMAGE_COUNT;
    } else {
        // Only one needed.
        texture_data->image_count = 1;
    }

    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlagBits aspect = 0;
    VkFormat image_format;
    b8 is_depth = FLAG_GET(flags, TEXTURE_FLAG_DEPTH);
    b8 is_stencil = FLAG_GET(flags, TEXTURE_FLAG_STENCIL);
    if (is_depth || is_stencil) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (is_depth) {
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if (is_stencil) {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        image_format = context->device.depth_format;
    } else {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        image_format = channel_count_to_format(channel_count, VK_FORMAT_R8G8B8A8_UNORM);
    }

    // Create the required number of images.
    texture_data->images = KALLOC_TYPE_CARRAY(vulkan_image, texture_data->image_count);
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        char* image_name = string_format("%s_vkimage_%d", name, i);
        vulkan_image_create(
            context, type, width, height, array_size, image_format,
            VK_IMAGE_TILING_OPTIMAL, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, aspect,
            image_name, mip_levels, &texture_data->images[i]);

        texture_data->images[i].flags = flags;
        string_free(image_name);
    }

    return true;
}

void vulkan_renderer_texture_resources_release(renderer_backend_interface* backend, khandle* renderer_texture_handle) {

    vulkan_context* context = (vulkan_context*)backend->internal_context;

    vulkan_texture_handle_data* texture_data = &context->textures[renderer_texture_handle->handle_index];
    if (texture_data->uniqueid != renderer_texture_handle->unique_id.uniqueid) {
        KWARN("Stale handle passed while trying to release renderer texture resources.");
        return;
    }

    // Invalidate the handle first.
    texture_data->uniqueid = INVALID_ID_U64;
    *renderer_texture_handle = khandle_invalid();

    // Release/destroy the internal data.
    for (u32 i = 0; i < texture_data->image_count; ++i) {
        vulkan_image_destroy(context, &texture_data->images[i]);
    }
    KFREE_TYPE_CARRAY(texture_data->images, vulkan_image, texture_data->image_count);
    texture_data->images = 0;
    texture_data->image_count = 0;
}

b8 vulkan_renderer_texture_resize(renderer_backend_interface* backend, khandle renderer_texture_handle, u32 new_width, u32 new_height) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Ensure the handle isn't stale.
    vulkan_texture_handle_data* texture_data = &context->textures[renderer_texture_handle.handle_index];
    if (texture_data->uniqueid != renderer_texture_handle.unique_id.uniqueid) {
        KERROR("Stale handle passed while trying to resize a texture.");
        return false;
    }

    for (u32 i = 0; i < texture_data->image_count; ++i) {
        // Resizing is really just destroying the old image and creating a new one.
        // Data is not preserved because there's no reliable way to map the old data
        // to the new since the amount of data differs.
        vulkan_image* image = &texture_data->images[i];
        image->width = new_width;
        image->height = new_height;
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

b8 vulkan_renderer_texture_write_data(renderer_backend_interface* backend, khandle renderer_texture_handle,
                                      u32 offset, u32 size, const u8* pixels, b8 include_in_frame_workload) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Ensure the handle isn't stale.
    vulkan_texture_handle_data* texture = &context->textures[renderer_texture_handle.handle_index];
    if (texture->uniqueid != renderer_texture_handle.unique_id.uniqueid) {
        KERROR("Stale handle passed while trying to write data to a texture.");
        return false;
    }

    // If no window, can't include in a frame workload.
    if (!context->current_window) {
        include_in_frame_workload = false;
    }

    // Temporary staging renderbuffer, if needed.
    renderbuffer temp;
    // Temporary command buffer, if needed.
    vulkan_command_buffer temp_command_buffer;

    // A pointer to the staging buffer to be used.
    renderbuffer* staging = 0;
    // A pointer to the command buffer to be used.
    vulkan_command_buffer* command_buffer = 0;
    if (include_in_frame_workload) {
        // Including in the frame workload means the current window's current-frame staging buffer can be used.
        u32 current_frame = context->current_window->renderer_state->backend_state->current_frame;
        staging = &context->current_window->renderer_state->backend_state->staging[current_frame];
        command_buffer = get_current_command_buffer(context);
    } else {
        // Not including in the frame workload means a temporary staging buffer needs to be created and bound.
        // This buffer is the exact size required for the operation, so no allocation is needed later.
        renderer_renderbuffer_create("temp_staging", RENDERBUFFER_TYPE_STAGING, size * texture->image_count, RENDERBUFFER_TRACK_TYPE_NONE, &temp);
        renderer_renderbuffer_bind(&temp, 0);
        // Set the temp buffer as the staging buffer to be used.
        staging = &temp;
    }
    for (u32 i = 0; i < texture->image_count; ++i) {
        vulkan_image* image = &texture->images[i];

        // Staging buffer.
        u64 staging_offset = 0;
        if (include_in_frame_workload) {
            // If including in frame workload, space needs to be allocated from the buffer.
            renderer_renderbuffer_allocate(staging, size, &staging_offset);
        }

        // Results in a wait if not included in frame workload.
        vulkan_buffer_load_range(backend, staging, staging_offset, size, pixels, include_in_frame_workload);

        // Need a temp command buffer if not included in frame workload.
        // HACK: Not doing this breaks things...
        // if (!include_in_frame_workload) {
        vulkan_command_buffer_allocate_and_begin_single_use(
            context,
            context->device.graphics_command_pool,
            &temp_command_buffer);
        command_buffer = &temp_command_buffer;
        // }

        // Transition the layout from whatever it is currently to optimal for recieving data.
        vulkan_image_transition_layout(context, command_buffer, image, image->format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy the data from the buffer.
        vulkan_image_copy_from_buffer(context, image, ((vulkan_buffer*)staging->internal_data)->handle, staging_offset, command_buffer);

        if (image->mip_levels <= 1 || !vulkan_image_mipmaps_generate(context, image, command_buffer)) {
            // If mip generation isn't needed or fails, fall back to ordinary transition.
            // Transition from optimal for data reciept to shader-read-only optimal layout.
            vulkan_image_transition_layout(context, command_buffer, image, image->format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Need to submit temp command buffer.
        // HACK: Not doing this breaks things...
        // if (!include_in_frame_workload) {
        vulkan_command_buffer_end_single_use(
            context,
            context->device.graphics_command_pool,
            command_buffer,
            context->device.graphics_queue);
        command_buffer = 0;
        // }
    }

    if (!include_in_frame_workload) {
        renderer_renderbuffer_destroy(&temp);

        // Counts as a texture update. The texture generation here can only really be updated if
        // we _don't_ include the upload in the frame workload, since that results in a wait.
        // If we include it in the frame workload, then we must also wait until that frame's queue is complete.
        texture->generation++;
        // Roll over when at max u16.
        if (texture->generation == INVALID_ID_U16) {
            texture->generation = 0;
        }
    } else {
        // Add handle to post-frame-queue-completion list. These will be updated at the end of the frame.
        u32 current_frame = get_current_frame_index(context);
        darray_push(context->current_window->renderer_state->backend_state->frame_texture_updated_list[current_frame], renderer_texture_handle);
    }

    return true;
}

static b8 texture_read_offset_range(
    renderer_backend_interface* backend,
    vulkan_texture_handle_data* texture_data,
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

b8 vulkan_renderer_texture_read_data(renderer_backend_interface* backend, khandle renderer_texture_handle, u32 offset, u32 size, u8** out_pixels) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Ensure the handle isn't stale.
    vulkan_texture_handle_data* texture_data = &context->textures[renderer_texture_handle.handle_index];
    if (texture_data->uniqueid != renderer_texture_handle.unique_id.uniqueid) {
        KERROR("Stale handle passed while trying to reading data from a texture.");
        return false;
    }
    return texture_read_offset_range(backend, texture_data, offset, size, 0, 0, 0, 0, out_pixels);
}

b8 vulkan_renderer_texture_read_pixel(renderer_backend_interface* backend, khandle renderer_texture_handle, u32 x, u32 y, u8** out_rgba) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Ensure the handle isn't stale.
    vulkan_texture_handle_data* texture_data = &context->textures[renderer_texture_handle.handle_index];
    if (texture_data->uniqueid != renderer_texture_handle.unique_id.uniqueid) {
        KERROR("Stale handle passed while trying to reading pixel data from a texture.");
        return false;
    }
    return texture_read_offset_range(backend, texture_data, 0, 0, x, y, 1, 1, out_rgba);
}

static void calculate_sorted_indices(vulkan_shader_frequency_info* frequency_info) {
    // Sort sampler/texture uniform indices and store them in a list.
    u32 sampler_and_image_count = frequency_info->uniform_sampler_count + frequency_info->uniform_texture_count;
    if (!sampler_and_image_count) {
        return;
    }

    frequency_info->sorted_indices = KALLOC_TYPE_CARRAY(u32, sampler_and_image_count);

    // Add all indices, unsorted.
    u32 count = 0;
    for (u32 i = 0; i < frequency_info->uniform_sampler_count; ++i) {
        frequency_info->sorted_indices[count] = frequency_info->sampler_indices[i];
        count++;
    }
    for (u32 i = 0; i < frequency_info->uniform_texture_count; ++i) {
        frequency_info->sorted_indices[count] = frequency_info->texture_indices[i];
        count++;
    }

    KASSERT_DEBUG(count == sampler_and_image_count);

    // Sort them.
    kquick_sort(sizeof(u32), frequency_info->sorted_indices, 0, count - 1, kquicksort_compare_u32);
}

b8 vulkan_renderer_shader_create(renderer_backend_interface* backend, khandle shader, const kresource_shader* shader_resource) {
    // Verify stage support before anything else.
    for (u8 i = 0; i < shader_resource->stage_count; ++i) {
        switch (shader_resource->stage_configs[i].stage) {
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
            KERROR("Unsupported stage type: %d", shader_stage_to_string(shader_resource->stage_configs[i].stage));
            break;
        }
    }

    vulkan_context* context = (vulkan_context*)backend->internal_context;
    VkDevice logical_device = context->device.logical_device;
    VkAllocationCallbacks* vk_allocator = context->allocator;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];

    // Setup the internal shader.
    internal_shader->per_draw_push_constant_block = kallocate(128, MEMORY_TAG_RENDERER);

    internal_shader->stage_count = shader_resource->stage_count;
    internal_shader->flags = shader_resource->flags;
    internal_shader->topology_types = shader_resource->topology_types;
    internal_shader->name = shader_resource->base.name;

    // Count up uniform/sampler/textures and UBO sizes.
    kzero_memory(&internal_shader->per_frame_info, sizeof(vulkan_shader_frequency_info));
    internal_shader->per_frame_info.sampler_indices = darray_create(u32);
    internal_shader->per_frame_info.texture_indices = darray_create(u32);
    kzero_memory(&internal_shader->per_group_info, sizeof(vulkan_shader_frequency_info));
    internal_shader->per_group_info.sampler_indices = darray_create(u32);
    internal_shader->per_group_info.texture_indices = darray_create(u32);
    kzero_memory(&internal_shader->per_draw_info, sizeof(vulkan_shader_frequency_info));
    internal_shader->per_draw_info.sampler_indices = darray_create(u32);
    internal_shader->per_draw_info.texture_indices = darray_create(u32);

    // Ensure all frequencies are "unbound".
    internal_shader->per_frame_info.bound_id = INVALID_ID;
    internal_shader->per_group_info.bound_id = INVALID_ID;
    internal_shader->per_draw_info.bound_id = INVALID_ID;

    // Process uniforms.
    internal_shader->uniform_count = shader_resource->uniform_count;
    internal_shader->uniforms = KALLOC_TYPE_CARRAY(shader_uniform, internal_shader->uniform_count);
    for (u32 i = 0; i < shader_resource->uniform_count; ++i) {
        shader_uniform_config* u_config = &shader_resource->uniforms[i];
        b8 is_sampler = uniform_type_is_sampler(u_config->type);
        b8 is_texture = uniform_type_is_texture(u_config->type);
        vulkan_shader_frequency_info* info = 0;
        u32 uniform_size = 0;
        switch (u_config->frequency) {
        case SHADER_UPDATE_FREQUENCY_PER_FRAME:
            info = &internal_shader->per_frame_info;
            break;
        case SHADER_UPDATE_FREQUENCY_PER_GROUP:
            info = &internal_shader->per_group_info;
            break;
        case SHADER_UPDATE_FREQUENCY_PER_DRAW:
            info = &internal_shader->per_draw_info;
            break;
        }

        u32 tex_samp_index = 0;
        if (is_texture) {
            tex_samp_index = info->uniform_texture_count;
            info->uniform_texture_count++;
            darray_push(info->texture_indices, i);
        } else if (is_sampler) {
            tex_samp_index = info->uniform_sampler_count;
            info->uniform_sampler_count++;
            darray_push(info->sampler_indices, i);
        } else {
            tex_samp_index = info->uniform_count;
            uniform_size = (u_config->size * (u_config->array_length ? u_config->array_length : 1));
            info->uniform_count++;
        }

        // Keep a copy of the uniform properties.
        shader_uniform* uniform = &internal_shader->uniforms[i];
        uniform->name = u_config->name;
        uniform->offset = info->ubo_size;
        uniform->location = u_config->location;
        uniform->tex_samp_index = tex_samp_index;
        uniform->size = u_config->size;
        uniform->frequency = u_config->frequency;
        uniform->type = u_config->type;
        uniform->array_length = u_config->array_length;

        info->ubo_size += uniform_size;
    }

    calculate_sorted_indices(&internal_shader->per_frame_info);
    calculate_sorted_indices(&internal_shader->per_group_info);
    calculate_sorted_indices(&internal_shader->per_draw_info);

    // NOTE: The Vulkan spec only guarantees 128 bytes of data. Therefore we align the "UBO"
    // a.k.a. push constant stride to that, and only ever use one.
    internal_shader->per_draw_info.ubo_stride = get_aligned(internal_shader->per_draw_info.ubo_size, 128);

    // The other frequencies can use the UBO min offset from the device limits.
    internal_shader->per_frame_info.ubo_stride = get_aligned(internal_shader->per_frame_info.ubo_size, context->device.properties.limits.minUniformBufferOffsetAlignment);
    internal_shader->per_group_info.ubo_stride = get_aligned(internal_shader->per_group_info.ubo_size, context->device.properties.limits.minUniformBufferOffsetAlignment);

    internal_shader->max_groups = shader_resource->max_groups;
    internal_shader->max_per_draw_count = shader_resource->max_per_draw_count;

    // Need a max of VULKAN_SHADER_DESCRIPTOR_SET_LAYOUT_COUNT descriptor sets, one per shader update frequency.
    // Note that this can mean that only one (or potentially none) exist as well.
    internal_shader->descriptor_set_count = 0;

    b8 has_per_frame = frequency_has_uniforms(&internal_shader->per_frame_info);
    b8 has_per_group = frequency_has_uniforms(&internal_shader->per_group_info);
    b8 has_per_draw = frequency_has_uniforms(&internal_shader->per_draw_info);
    kzero_memory(internal_shader->descriptor_set_configs, sizeof(vulkan_descriptor_set_config) * VULKAN_SHADER_DESCRIPTOR_SET_LAYOUT_COUNT);

    // Attributes array.
    kzero_memory(internal_shader->attributes, sizeof(VkVertexInputAttributeDescription) * VULKAN_SHADER_MAX_ATTRIBUTES);

    // Calculate the total number of descriptors needed.
    // Get a count of sampler descriptors needed.
    u32 per_frame_sampler_count = internal_shader->per_frame_info.uniform_sampler_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_group_sampler_count = shader_resource->max_groups * internal_shader->per_group_info.uniform_sampler_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_draw_sampler_count = shader_resource->max_per_draw_count * internal_shader->per_draw_info.uniform_sampler_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 max_sampler_count = per_frame_sampler_count + per_group_sampler_count + per_draw_sampler_count;
    // Get a count of image descriptors needed.
    u32 per_frame_image_count = internal_shader->per_frame_info.uniform_texture_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_group_image_count = shader_resource->max_groups * internal_shader->per_group_info.uniform_texture_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_draw_image_count = shader_resource->max_per_draw_count * internal_shader->per_draw_info.uniform_texture_count * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 max_image_count = per_frame_image_count + per_group_image_count + per_draw_image_count;
    // Get a count of uniform buffer descriptors needed.
    u32 per_frame_ubo_count = (internal_shader->per_frame_info.uniform_count ? 1 : 0) * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_group_ubo_count = (internal_shader->per_group_info.uniform_count ? 1 : 0) * shader_resource->max_groups * VULKAN_RESOURCE_IMAGE_COUNT;
    u32 per_draw_ubo_count = 0; // NOTE: this is 0 because per_draw ubo is handled as a push constant.
    u32 max_ubo_count = per_frame_ubo_count + per_group_ubo_count + per_draw_ubo_count;

    // Calculate the max number of descriptor sets needed.
    u32 per_frame_desc_set_count = (has_per_frame ? 1 : 0) * VULKAN_RESOURCE_IMAGE_COUNT;                                     // NOTE: only one set of these is ever needed for per-frame frequency, per swapchain image.
    u32 per_group_desc_set_count = (has_per_group ? 1 : 0) * internal_shader->max_groups * VULKAN_RESOURCE_IMAGE_COUNT;       // 1 per group, per swapchain image.
    u32 per_draw_desc_set_count = (has_per_draw ? 1 : 0) * internal_shader->max_per_draw_count * VULKAN_RESOURCE_IMAGE_COUNT; // 1 per draw, per swapchain image.
    internal_shader->max_descriptor_set_count = per_frame_desc_set_count + per_group_desc_set_count + per_draw_desc_set_count;

    // For now, shaders will only ever have these 2 types of descriptor pools. One is for unifrom buffers,
    // and the other is for images and/or samplers.
    internal_shader->pool_size_count = 0;
    if (max_ubo_count > 0) {
        internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_ubo_count};
        internal_shader->pool_size_count++;
    }
    if (max_sampler_count > 0 || max_image_count > 0) {
        internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, max_sampler_count};
        internal_shader->pool_size_count++;
        internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_image_count};
        internal_shader->pool_size_count++;
    }

    // per-frame descriptor set config.
    if (has_per_frame) {
        vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_set_configs[internal_shader->descriptor_set_count];

        setup_frequency_descriptors(true, &internal_shader->per_frame_info, set_config, shader_resource);

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // If using per_group uniforms, add a UBO descriptor set.
    if (has_per_group) {
        // In that set, add a binding for UBO if used.
        vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_set_configs[internal_shader->descriptor_set_count];

        setup_frequency_descriptors(true, &internal_shader->per_group_info, set_config, shader_resource);

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // If using per_draw uniform samplers, add a sampler descriptor set.
    if (has_per_draw) {
        // In that set, add a binding for UBO if used.
        vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_set_configs[internal_shader->descriptor_set_count];

        setup_frequency_descriptors(false, &internal_shader->per_draw_info, set_config, shader_resource);

        // Increment the set counter.
        internal_shader->descriptor_set_count++;
    }

    // Invalidate per-frame state.
    kzero_memory(&internal_shader->per_frame_state, sizeof(vulkan_shader_frequency_state));
    internal_shader->per_frame_state.id = INVALID_ID;

    // Invalidate all per-group states.
    if (internal_shader->max_groups) {
        internal_shader->group_states = kallocate(sizeof(vulkan_shader_frequency_state) * internal_shader->max_groups, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < internal_shader->max_groups; ++i) {
            internal_shader->group_states[i].id = INVALID_ID;
        }
    }

    // Invalidate per-draw states.
    if (internal_shader->max_per_draw_count) {
        internal_shader->per_draw_states = kallocate(sizeof(vulkan_shader_frequency_state) * internal_shader->max_per_draw_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < internal_shader->max_per_draw_count; ++i) {
            internal_shader->per_draw_states[i].id = INVALID_ID;
        }
    }

    // Keep a copy of the cull mode.
    internal_shader->cull_mode = shader_resource->cull_mode;

    b8 needs_wireframe = (internal_shader->flags & SHADER_FLAG_WIREFRAME_BIT) != 0;
    // Determine if the implementation supports this and set to false if not.
    if (!context->device.features.fillModeNonSolid) {
        KINFO("Renderer backend does not support fillModeNonSolid. Wireframe mode is not possible, but was requested for the shader '%s'.", kname_string_get(shader_resource->base.name));
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
    internal_shader->attribute_count = shader_resource->attribute_count;
    u32 offset = 0;
    for (u32 i = 0; i < internal_shader->attribute_count; ++i) {
        // Setup the new attribute.
        VkVertexInputAttributeDescription attribute;
        attribute.location = i;
        attribute.binding = 0;
        attribute.offset = offset;
        attribute.format = types[shader_resource->attributes[i].type];

        // Push into the config's attribute collection and add to the stride.
        internal_shader->attributes[i] = attribute;

        offset += shader_resource->attributes[i].size;
        internal_shader->attribute_stride += shader_resource->attributes[i].size;
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

    char* desc_pool_name = string_format("desc_pool_shader_%s", kname_string_get(shader_resource->base.name));
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_POOL, internal_shader->descriptor_pool, desc_pool_name);
    string_free(desc_pool_name);

    // Create descriptor set layouts.
    kzero_memory(internal_shader->descriptor_set_layouts, internal_shader->descriptor_set_count);
    for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = internal_shader->descriptor_set_configs[i].binding_count;
        layout_info.pBindings = internal_shader->descriptor_set_configs[i].bindings;

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
    if (shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_POINT]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT;

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_POINT] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_POINT]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT;
        }
    }

    // Line class.
    if (shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT || shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT;

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT;
        }
    }

    // Triangle class.
    if (shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT ||
        shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT ||
        shader_resource->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT) {
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
        // Set the supported types for this class.
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT;
        internal_shader->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT;

        // Wireframe versions.
        if (needs_wireframe) {
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE] = kallocate(sizeof(vulkan_pipeline), MEMORY_TAG_VULKAN);
            // Set the supported types for this class.
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT;
            internal_shader->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE]->supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT;
        }
    }

    if (!shader_create_modules_and_pipelines(backend, internal_shader, shader_resource->stage_count, shader_resource->stage_configs)) {
        KERROR("Failed initial load on shader '%s'. See logs for details.", kname_string_get(shader_resource->base.name));
        return false;
    }

    // TODO: Figure out what the default should be here.
    internal_shader->bound_pipeline_index = 0;
    b8 pipeline_found = false;
    for (u32 i = 0; i < pipeline_count; ++i) {
        if (internal_shader->pipelines[i]) {
            internal_shader->bound_pipeline_index = i;

            // Extract the first type from the pipeline
            for (u32 j = 1; j < PRIMITIVE_TOPOLOGY_TYPE_MAX_BIT; j = j << 1) {
                if (internal_shader->pipelines[i]->supported_topology_types & j) {
                    switch (j) {
                    case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT:
                        internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                        break;
                    case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT:
                        internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                        break;
                    case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT:
                        internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                        break;
                    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT:
                        internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                        break;
                    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT:
                        internal_shader->current_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                        break;
                    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT:
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
    internal_shader->required_ubo_alignment = context->device.properties.limits.minUniformBufferOffsetAlignment;

    // Make sure the UBO is aligned according to device requirements.
    internal_shader->per_frame_info.ubo_stride = get_aligned(internal_shader->per_frame_info.ubo_size, internal_shader->required_ubo_alignment);
    internal_shader->per_group_info.ubo_stride = get_aligned(internal_shader->per_group_info.ubo_size, internal_shader->required_ubo_alignment);
    // NOTE: While the maxPushConstantsSize can be > 128, the Vulkan spec only requires 128 and thus that is
    // what will be supported here. Otherwise this would just use context->device.properties.limits.maxPushConstantsSize.
    internal_shader->per_draw_info.ubo_stride = 128;

    kzero_memory(internal_shader->mapped_uniform_buffer_blocks, sizeof(void*) * VULKAN_RESOURCE_IMAGE_COUNT);
    kzero_memory(internal_shader->uniform_buffers, sizeof(renderbuffer) * VULKAN_RESOURCE_IMAGE_COUNT);

    // Uniform  buffers, one per swapchain image.
    u64 total_buffer_size = internal_shader->per_frame_info.ubo_stride + (internal_shader->per_group_info.ubo_stride * internal_shader->max_groups);
    for (u32 i = 0; i < VULKAN_RESOURCE_IMAGE_COUNT; ++i) {
        const char* buffer_name = string_format("renderbuffer_uniform_%s_idx_%d", kname_string_get(shader_resource->base.name), i);
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

    return setup_frequency_state(backend, internal_shader, SHADER_UPDATE_FREQUENCY_PER_FRAME, 0);
}

void vulkan_renderer_shader_destroy(renderer_backend_interface* backend, khandle shader) {
    if (!khandle_is_invalid(shader)) {
        vulkan_context* context = (vulkan_context*)backend->internal_context;
        VkDevice logical_device = context->device.logical_device;
        VkAllocationCallbacks* vk_allocator = context->allocator;
        vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
        if (!internal_shader) {
            KERROR("vulkan_renderer_shader_destroy requires a valid pointer to a shader.");
            return;
        }

        // Descriptor set layouts.
        for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
            vulkan_descriptor_set_config* set_config = &internal_shader->descriptor_set_configs[i];
            if (set_config->bindings && set_config->binding_count) {
                KFREE_TYPE_CARRAY(set_config->bindings, VkDescriptorSetLayoutBinding, set_config->binding_count);
                set_config->bindings = 0;
            }
            if (internal_shader->descriptor_set_layouts[i]) {
                vkDestroyDescriptorSetLayout(logical_device, internal_shader->descriptor_set_layouts[i], vk_allocator);
                internal_shader->descriptor_set_layouts[i] = 0;
            }
        }

        // Global descriptor sets.
        kzero_memory(internal_shader->per_frame_state.descriptor_sets, sizeof(VkDescriptorSet) * VULKAN_RESOURCE_IMAGE_COUNT);

        // Descriptor pool
        if (internal_shader->descriptor_pool) {
            vkDestroyDescriptorPool(logical_device, internal_shader->descriptor_pool, vk_allocator);
            internal_shader->descriptor_pool = 0;
        }

        // Destroy frame state
        {
            vulkan_shader_frequency_state* frequency_state = &internal_shader->per_frame_state;
            vulkan_shader_frequency_info* info = &internal_shader->per_frame_info;
            destroy_shader_frequency_states(SHADER_UPDATE_FREQUENCY_PER_FRAME, frequency_state, 1, info);
            kzero_memory(frequency_state, sizeof(vulkan_shader_frequency_state));
        }

        // Destroy the group states.
        {
            vulkan_shader_frequency_info* info = &internal_shader->per_group_info;
            destroy_shader_frequency_states(SHADER_UPDATE_FREQUENCY_PER_GROUP, internal_shader->group_states, internal_shader->max_groups, info);
            if (internal_shader->group_states && internal_shader->max_groups) {
                KFREE_TYPE_CARRAY(internal_shader->group_states, vulkan_shader_frequency_state, internal_shader->max_groups);
            }
            internal_shader->group_states = 0;
            internal_shader->max_groups = 0;
        }

        // Destroy the per-draw states.
        {
            vulkan_shader_frequency_info* info = &internal_shader->per_draw_info;
            destroy_shader_frequency_states(SHADER_UPDATE_FREQUENCY_PER_DRAW, internal_shader->per_draw_states, internal_shader->max_per_draw_count, info);
            if (internal_shader->per_draw_states && internal_shader->max_per_draw_count) {
                KFREE_TYPE_CARRAY(internal_shader->per_draw_states, vulkan_shader_frequency_state, internal_shader->max_per_draw_count);
            }
            internal_shader->per_draw_states = 0;
            internal_shader->max_per_draw_count = 0;
        }

        // Uniform buffer.
        for (u32 i = 0; i < VULKAN_RESOURCE_IMAGE_COUNT; ++i) {
            if (internal_shader->uniform_buffers[i].internal_data) {
                vulkan_buffer_unmap_memory(backend, &internal_shader->uniform_buffers[i], 0, VK_WHOLE_SIZE);
                internal_shader->mapped_uniform_buffer_blocks[i] = 0;
                renderer_renderbuffer_destroy(&internal_shader->uniform_buffers[i]);
            }
        }
        kzero_memory(internal_shader->uniform_buffers, sizeof(renderbuffer) * VULKAN_RESOURCE_IMAGE_COUNT);

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
        internal_shader->stage_count = 0;

        // Internal shader arrays, etc.
        // Per frame
        if (internal_shader->per_frame_info.sampler_indices) {
            darray_destroy(internal_shader->per_frame_info.sampler_indices);
            internal_shader->per_frame_info.sampler_indices = 0;
        }
        if (internal_shader->per_frame_info.texture_indices) {
            darray_destroy(internal_shader->per_frame_info.texture_indices);
            internal_shader->per_frame_info.texture_indices = 0;
        }
        // Per group
        if (internal_shader->per_group_info.sampler_indices) {
            darray_destroy(internal_shader->per_group_info.sampler_indices);
            internal_shader->per_group_info.sampler_indices = 0;
        }
        if (internal_shader->per_group_info.texture_indices) {
            darray_destroy(internal_shader->per_group_info.texture_indices);
            internal_shader->per_group_info.texture_indices = 0;
        }
        // Per draw
        if (internal_shader->per_draw_info.sampler_indices) {
            internal_shader->per_draw_info.sampler_indices = 0;
        }
        if (internal_shader->per_draw_info.texture_indices) {
            darray_destroy(internal_shader->per_draw_info.texture_indices);
            internal_shader->per_draw_info.texture_indices = 0;
        }
    }
}

b8 vulkan_renderer_shader_reload(renderer_backend_interface* backend, khandle shader, u32 shader_stage_count, shader_stage_config* shader_stages) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return shader_create_modules_and_pipelines(backend, &context->shaders[shader.handle_index], shader_stage_count, shader_stages);
}

b8 vulkan_renderer_shader_use(renderer_backend_interface* backend, khandle shader) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_command_buffer* command_buffer = get_current_command_buffer(context);

    // Pick the correct pipeline.
    b8 wireframe_enabled = vulkan_renderer_shader_flag_get(backend, shader, SHADER_FLAG_WIREFRAME_BIT);
    vulkan_pipeline** pipeline_array = wireframe_enabled ? internal_shader->wireframe_pipelines : internal_shader->pipelines;
    vulkan_pipeline_bind(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_array[internal_shader->bound_pipeline_index]);

    context->bound_shader = internal_shader;
    // Make sure to use the current bound type as well.
    if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
        vkCmdSetPrimitiveTopology(command_buffer->handle, internal_shader->current_topology);
    } else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
        context->vkCmdSetPrimitiveTopologyEXT(command_buffer->handle, internal_shader->current_topology);
    }
    return true;
}

b8 vulkan_renderer_shader_supports_wireframe(const renderer_backend_interface* backend, khandle shader) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = &context->shaders[shader.handle_index];

    // If the array exists, this is supported.
    if (internal->wireframe_pipelines) {
        return true;
    }

    return false;
}

b8 vulkan_renderer_shader_flag_get(const renderer_backend_interface* backend, khandle shader, shader_flags flag) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];

    return FLAG_GET(internal_shader->flags, flag);
}

void vulkan_renderer_shader_flag_set(renderer_backend_interface* backend, khandle shader, shader_flags flag, b8 enabled) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];

    internal_shader->flags = FLAG_SET(internal_shader->flags, flag, enabled);
}

b8 vulkan_renderer_shader_bind_per_frame(renderer_backend_interface* backend, khandle shader) {
    // NOTE: For Vulkan, this is a no-op.
    return true;
}

b8 vulkan_renderer_shader_bind_per_group(renderer_backend_interface* backend, khandle shader, u32 group_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = &internal_shader->per_group_info;
    frequency_info->bound_id = group_id;
    return true;
}

b8 vulkan_renderer_shader_bind_per_draw(renderer_backend_interface* backend, khandle shader, u32 draw_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = &internal_shader->per_draw_info;
    frequency_info->bound_id = draw_id;
    return true;
}

b8 vulkan_renderer_shader_apply_per_frame(renderer_backend_interface* backend, khandle shader, u16 renderer_frame_number) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = &internal_shader->per_frame_info;

    // Don't do anything if there are no updatable per-frame uniforms.
    b8 has_per_frame = frequency_has_uniforms(frequency_info);
    if (!has_per_frame) {
        return true;
    }

    vulkan_shader_frequency_state* per_frame_state = &internal_shader->per_frame_state;

    // Per-frame is always first, if it exists.
    u32 descriptor_set_index = 0;

    if (!vulkan_descriptorset_update_and_bind(
            context,
            renderer_frame_number, // Frame number is used as the generation for per-frame data.
            internal_shader,
            frequency_info,
            per_frame_state,
            descriptor_set_index)) {
        KERROR("Failed to update/bind per-frame descriptor set.");
        return false;
    }

    return true;
}

b8 vulkan_renderer_shader_apply_per_group(renderer_backend_interface* backend, khandle shader, u16 renderer_frame_number) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = &internal_shader->per_group_info;

    if (frequency_info->bound_id == INVALID_ID) {
        KERROR("Cannot apply per-group uniforms without having first bound a group.");
        return false;
    }

    // Bleat if there are no groups for this shader.
    if (frequency_info->uniform_count < 1 && frequency_info->uniform_sampler_count < 1) {
        KERROR("This shader does not use groups.");
        return false;
    }

    // Obtain group data.
    vulkan_shader_frequency_state* group_state = &internal_shader->group_states[frequency_info->bound_id];

    // Determine the descriptor set index which will be first. If there are no per-frame uniforms, for example,
    // this will be 0. If there are per-frame uniforms, this will be 1.
    b8 has_per_frame = frequency_has_uniforms(&internal_shader->per_frame_info);
    u32 descriptor_set_index = has_per_frame ? 1 : 0;

    if (!vulkan_descriptorset_update_and_bind(
            context,
            renderer_frame_number, // Frame number is used as the generation for per-frame data.
            internal_shader,
            frequency_info,
            group_state,
            descriptor_set_index)) {
        KERROR("Failed to update/bind per-frame uniforms descriptor set.");
        return false;
    }

    return true;
}

b8 vulkan_renderer_shader_apply_per_draw(renderer_backend_interface* backend, khandle shader, u16 renderer_frame_number) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal_shader = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = &internal_shader->per_draw_info;

    if (frequency_info->bound_id == INVALID_ID) {
        KERROR("Cannot apply per-draw uniforms without having first bound a group.");
        return false;
    }

    VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;

    // Pick the correct pipeline.
    b8 wireframe_enabled = vulkan_renderer_shader_flag_get(backend, shader, SHADER_FLAG_WIREFRAME_BIT);
    vulkan_pipeline** pipeline_array = wireframe_enabled ? internal_shader->wireframe_pipelines : internal_shader->pipelines;

    // Update the non-sampler uniforms via push constants.
    vkCmdPushConstants(
        command_buffer,
        pipeline_array[internal_shader->bound_pipeline_index]->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, 128, internal_shader->per_draw_push_constant_block);

    // Update local descriptor set if there are local samplers to be updated.
    if (internal_shader->per_draw_info.uniform_sampler_count > 0) {

        // Obtain local data.
        vulkan_shader_frequency_state* per_draw_state = &internal_shader->per_draw_states[frequency_info->bound_id];

        // Determine the descriptor set index which will be first. If there are no per-frame uniforms and no per-group uniforms, for example,
        // this will be 0. If there are per-frame uniforms but not per-group, this will be 1, if there are both this will be 2.
        b8 has_per_frame = frequency_has_uniforms(&internal_shader->per_frame_info);
        b8 has_group = frequency_has_uniforms(&internal_shader->per_group_info);
        u32 descriptor_set_index = 0;
        descriptor_set_index += has_per_frame ? 1 : 0;
        descriptor_set_index += has_group ? 1 : 0;

        if (!vulkan_descriptorset_update_and_bind(
                context,
                renderer_frame_number, // Frame number is used as the generation for per-frame data.
                internal_shader,
                frequency_info,
                per_draw_state,
                descriptor_set_index)) {
            KERROR("Failed to update/bind per-draw sampler descriptor set.");
            return false;
        }
    }

    return true;
}

static b8 sampler_create_internal(vulkan_context* context, texture_filter filter, texture_repeat repeat, f32 anisotropy, vulkan_sampler_handle_data* out_sampler_handle_data) {

    // Create a sampler for the texture
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.minFilter = filter == TEXTURE_FILTER_MODE_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_info.magFilter = filter == TEXTURE_FILTER_MODE_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    VkSamplerAddressMode mode;
    switch (repeat) {
    case TEXTURE_REPEAT_CLAMP_TO_EDGE:
        mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
    case TEXTURE_REPEAT_CLAMP_TO_BORDER:
        mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        break;
    case TEXTURE_REPEAT_MIRRORED_REPEAT:
        mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    default:
        mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    }

    sampler_info.addressModeU = mode;
    sampler_info.addressModeV = mode;
    sampler_info.addressModeW = mode;

    b8 use_anisotropy = context->device.features.samplerAnisotropy && anisotropy > 0;
    // Don't exceed device anisotropy limits.
    f32 actual_anisotropy = KMIN(anisotropy, context->device.properties.limits.maxSamplerAnisotropy);
    if (use_anisotropy) {
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = actual_anisotropy;
    } else {
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 0;
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
    sampler_info.maxLod = VK_LOD_CLAMP_NONE; // Don't clamp.

    VkResult result = vkCreateSampler(context->device.logical_device, &sampler_info, context->allocator, &out_sampler_handle_data->sampler);
    if (!vulkan_result_is_success(VK_SUCCESS)) {
        KERROR("Error creating sampler: %s", vulkan_result_string(result, true));
        return false;
    }

    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_SAMPLER, out_sampler_handle_data->sampler, kname_string_get(out_sampler_handle_data->name));

    return true;
}

khandle vulkan_renderer_sampler_acquire(renderer_backend_interface* backend, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

    // Find a free sampler slot.
    u32 length = darray_length(context->samplers);
    u32 selected_id = INVALID_ID;
    for (u32 i = 0; i < length; ++i) {
        if (context->samplers[i].sampler == 0) {
            selected_id = i;
            break;
        }
    }
    if (selected_id == INVALID_ID) {
        // Push an empty entry into the array.
        vulkan_sampler_handle_data empty = (vulkan_sampler_handle_data){INVALID_ID_U64, 0};
        darray_push(context->samplers, empty);
        selected_id = length;
    }

    // Set the name
    context->samplers[selected_id].name = name;

    if (!sampler_create_internal(context, filter, repeat, anisotropy, &context->samplers[selected_id])) {
        return khandle_invalid();
    }

    khandle h = khandle_create(selected_id);
    // Save off the uniqueid for handle validation.
    context->samplers[selected_id].handle_uniqueid = h.unique_id.uniqueid;
    return h;
}

void vulkan_renderer_sampler_release(renderer_backend_interface* backend, khandle* sampler) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!khandle_is_invalid(*sampler)) {
        vulkan_sampler_handle_data* s = &context->samplers[sampler->handle_index];
        if (s->sampler && s->handle_uniqueid == sampler->unique_id.uniqueid) {
            // Make sure there's no way this is in use.
            vkDeviceWaitIdle(context->device.logical_device);
            vkDestroySampler(context->device.logical_device, s->sampler, context->allocator);
            // Invalidate the entry and the handle.
            s->sampler = 0;
            s->handle_uniqueid = INVALID_ID_U64;
            khandle_invalidate(sampler);
        }
    }
}

b8 vulkan_renderer_sampler_refresh(renderer_backend_interface* backend, khandle* sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (khandle_is_invalid(*sampler)) {
        KERROR("Attempted to refresh a sampler via an invalid handler.");
        return false;
    }

    vulkan_sampler_handle_data* s = &context->samplers[sampler->handle_index];
    if (s->sampler && s->handle_uniqueid == sampler->unique_id.uniqueid) {

        // Take a copy of the old sampler.
        VkSampler old = s->sampler;

        // Make sure there's no way this is in use.
        vkDeviceWaitIdle(context->device.logical_device);

        // Create/assign the new.
        if (!sampler_create_internal(context, filter, repeat, anisotropy, s)) {
            KERROR("Sampler refresh failed to create new internal sampler.");
            return false;
        }

        // Destroy the old.
        vkDestroySampler(context->device.logical_device, old, context->allocator);

        // Update the handle and handle data.
        sampler->unique_id = identifier_create();
        s->handle_uniqueid = sampler->unique_id.uniqueid;
    }
    return true;
}

kname vulkan_renderer_sampler_name_get(renderer_backend_interface* backend, khandle sampler) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (khandle_is_invalid(sampler)) {
        KERROR("Attempted to obtain a sampler name via an invalid handle.");
        return INVALID_KNAME;
    }

    vulkan_sampler_handle_data* data = &context->samplers[sampler.handle_index];
    if (khandle_is_stale(sampler, data->handle_uniqueid)) {
        KERROR("Attempted to obtain a sampler name via an stale handle.");
    }

    return data->name;
}

b8 vulkan_renderer_shader_per_group_resources_acquire(renderer_backend_interface* backend, khandle shader, u32* out_group_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return setup_frequency_state(backend, &context->shaders[shader.handle_index], SHADER_UPDATE_FREQUENCY_PER_GROUP, out_group_id);
}

b8 vulkan_renderer_shader_per_draw_resources_acquire(renderer_backend_interface* backend, khandle shader, u32* out_per_draw_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return setup_frequency_state(backend, &context->shaders[shader.handle_index], SHADER_UPDATE_FREQUENCY_PER_DRAW, out_per_draw_id);
}

b8 vulkan_renderer_shader_per_group_resources_release(renderer_backend_interface* backend, khandle shader, u32 per_group_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return release_shader_frequency_state(context, &context->shaders[shader.handle_index], SHADER_UPDATE_FREQUENCY_PER_GROUP, per_group_id);
}

b8 vulkan_renderer_shader_per_draw_resources_release(renderer_backend_interface* backend, khandle shader, u32 per_draw_id) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    return release_shader_frequency_state(context, &context->shaders[shader.handle_index], SHADER_UPDATE_FREQUENCY_PER_DRAW, per_draw_id);
}

b8 vulkan_renderer_shader_uniform_set(renderer_backend_interface* backend, khandle shader, shader_uniform* uniform, u32 array_index, const void* value) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = &context->shaders[shader.handle_index];
    vulkan_shader_frequency_info* frequency_info = 0;
    vulkan_shader_frequency_state* frequency_state = 0;
    u32 image_index = get_current_image_index(context);
    u64 ubo_offset = 0;
    u64 addr = 0;

    switch (uniform->frequency) {
    case SHADER_UPDATE_FREQUENCY_PER_FRAME: {
        frequency_info = &internal->per_frame_info;
        frequency_state = &internal->per_frame_state;
        ubo_offset = frequency_info->ubo_offset;
        addr = (u64)internal->mapped_uniform_buffer_blocks[image_index];
    } break;
    case SHADER_UPDATE_FREQUENCY_PER_GROUP: {
        frequency_info = &internal->per_group_info;
        if (frequency_info->bound_id == INVALID_ID) {
            KERROR("Trying to set an per-group-level uniform without having bound a group first.");
            return false;
        }
        frequency_state = &internal->group_states[frequency_info->bound_id];
        ubo_offset = frequency_state->offset;
        addr = (u64)internal->mapped_uniform_buffer_blocks[image_index];
    } break;
    case SHADER_UPDATE_FREQUENCY_PER_DRAW: {
        frequency_info = &internal->per_draw_info;
        if (frequency_info->bound_id == INVALID_ID) {
            KERROR("Trying to set a per_draw-level uniform without having bound a draw id first.");
            return false;
        }
        frequency_state = &internal->per_draw_states[frequency_info->bound_id];
        ubo_offset = 0;
        addr = (u64)(internal->per_draw_push_constant_block);
    } break;
    }

    if (uniform_type_is_texture(uniform->type)) {
        kresource_texture* tex_value = (kresource_texture*)value;

        for (u32 i = 0; i < frequency_info->uniform_texture_count; ++i) {
            vulkan_uniform_texture_state* texture_state = &frequency_state->texture_states[i];
            if (texture_state->uniform.tex_samp_index == uniform->tex_samp_index) {
                u32 index = (texture_state->uniform.array_length > 1) ? array_index : 0;
                if (index && index >= texture_state->uniform.array_length) {
                    KERROR("vulkan_renderer_shader_uniform_set error: index (%u) is out of range (0-%u)", index, texture_state->uniform.array_length);
                    return false;
                }

                if (!texture_state->texture_handles) {
                    KFATAL("Textures array not setup. Check implementation.");
                }
                texture_state->texture_handles[array_index] = tex_value->renderer_texture_handle;
                return true;
            }
        }
        KERROR("texture_state_try_set: Unable to find uniform tex/samp_index %u. Sampler uniform not set.", uniform->tex_samp_index);
        return false;
    } else if (uniform_type_is_sampler(uniform->type)) {
        // TODO: Should be able to set a custom sampler by khandle.
        KERROR("vulkan_renderer_uniform_set - cannot set sampler uniform directly.");
        return false;
    } else {
        addr += ubo_offset + uniform->offset + (uniform->size * array_index);
        kcopy_memory((void*)addr, value, uniform->size);
    }
    return true;
}

static b8 create_shader_module(vulkan_context* context, vulkan_shader* internal_shader, shader_stage stage, const char* source, const char* filename, vulkan_shader_stage* out_stage) {
    shaderc_shader_kind shader_kind;
    VkShaderStageFlagBits vulkan_stage;
    switch (stage) {
    case SHADER_STAGE_VERTEX:
        shader_kind = shaderc_glsl_default_vertex_shader;
        vulkan_stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        shader_kind = shaderc_glsl_default_fragment_shader;
        vulkan_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case SHADER_STAGE_COMPUTE:
        shader_kind = shaderc_glsl_default_compute_shader;
        vulkan_stage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
    case SHADER_STAGE_GEOMETRY:
        shader_kind = shaderc_glsl_default_geometry_shader;
        vulkan_stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        break;
    default:
        KERROR("Unsupported shader kind. Unable to create module.");
        return false;
    }

    KDEBUG("Compiling stage '%s' for shader '%s'...", shader_stage_to_string(stage), kname_string_get(internal_shader->name));

    // Attempt to compile the shader.
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    // shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    u32 source_length = string_length(source);
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        context->shader_compiler,
        source,
        source_length,
        shader_kind,
        filename,
        "main",
        options);

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
    out_stage->shader_stage_create_info.stage = vulkan_stage;
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

f32 vulkan_renderer_max_anisotropy_get(renderer_backend_interface* backend) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;
    if (!context->device.features.samplerAnisotropy) {
        // Not available.
        return 0;
    } else {
        return context->device.properties.limits.maxSamplerAnisotropy;
    }
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
    } else {
        // Insert a pipeline barrier to ensure the write completes.
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkMemoryBarrier memoryBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer->handle,
                             srcStage, dstStage,
                             0, 1, &memoryBarrier, 0, 0, 0, 0);
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

static vulkan_command_buffer* get_current_command_buffer(vulkan_context* context) {
    kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state;
    vulkan_command_buffer* primary = &window_backend->graphics_command_buffers[window_backend->current_frame];

    // If inside a "render", return the secondary buffer at the current index.
    if (primary->in_secondary) {
        if (!primary->secondary_buffers) {
            KWARN("get_current_command_buffer requested draw index, but no secondary buffers exist.");
            return primary;
        } else {
            if (primary->secondary_buffer_index >= primary->secondary_count) {
                KWARN("get_current_command_buffer specified a draw index (%d) outside the bounds of 0-%d. Returning the first one, which may result in errors.", primary->secondary_buffer_index, primary->secondary_count - 1);
                return &primary->secondary_buffers[0];
            } else {
                return &primary->secondary_buffers[primary->secondary_buffer_index];
            }
        }
    } else {
        return primary;
    }
}

static u32 get_current_image_index(vulkan_context* context) {
    return context->current_window->renderer_state->backend_state->image_index;
}
static u32 get_current_frame_index(vulkan_context* context) {
    return context->current_window->renderer_state->backend_state->current_frame;
}

static u32 get_current_image_count(vulkan_context* context) {
    // 3 for triple-buffered, otherwise 2.
    return context->triple_buffering_enabled ? 3 : 2;
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
    rasterizer_create_info.polygonMode = (config->shader_flags & SHADER_FLAG_WIREFRAME_BIT) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
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
    if (config->shader_flags & SHADER_FLAG_DEPTH_TEST_BIT) {
        depth_stencil.depthTestEnable = VK_TRUE;
        if (config->shader_flags & SHADER_FLAG_DEPTH_WRITE_BIT) {
            depth_stencil.depthWriteEnable = VK_TRUE;
        }
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
    }
    depth_stencil.stencilTestEnable = (config->shader_flags & SHADER_FLAG_STENCIL_TEST_BIT) ? VK_TRUE : VK_FALSE;
    if (config->shader_flags & SHADER_FLAG_STENCIL_TEST_BIT) {
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
        depth_stencil.back.writeMask = (config->shader_flags & SHADER_FLAG_STENCIL_WRITE_BIT) ? 0xFF : 0x00;

        // Front face. Just use the same settings for front/back.
        depth_stencil.front = depth_stencil.back;
    }

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState color_blend_attachment_state;
    if (config->colour_attachment_count) {
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

        color_blend_state_create_info.logicOpEnable = VK_FALSE;
        color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
        color_blend_state_create_info.attachmentCount = config->colour_attachment_count;
        color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
    }

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
        darray_push(dynamic_states, VK_DYNAMIC_STATE_CULL_MODE);
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
    for (u32 i = 1; i < PRIMITIVE_TOPOLOGY_TYPE_MAX_BIT; i = i << 1) {
        if (out_pipeline->supported_topology_types & i) {
            primitive_topology_type_bits ptt = i;

            switch (ptt) {
            case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                break;
            case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT:
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                break;
            default:
                KWARN("primitive topology '%u' not supported. Skipping.", ptt);
                break;
            }

            break;
        }
    }
#if defined(VK_USE_PLATFORM_MACOS_MVK)
    // Must be enabled for MoltenVK
    if (input_assembly.topology == PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT || input_assembly.topology == PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT) {
        KTRACE("NOT Force-enabling primitiveRestartEnable for macOS");
        input_assembly.primitiveRestartEnable = VK_TRUE;
    } else {
        KTRACE("Force-enabling primitiveRestartEnable for macOS");
        input_assembly.primitiveRestartEnable = VK_FALSE;
    }
#else
    input_assembly.primitiveRestartEnable = VK_FALSE;
#endif

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    // Push constants
    VkPushConstantRange ranges[32] = {0};
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

#if KOHI_DEBUG
    char* pipeline_layout_name_buf = string_format("pipeline_layout_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT, out_pipeline->pipeline_layout, pipeline_layout_name_buf);
    string_free(pipeline_layout_name_buf);
#endif

    // Pipeline create
    VkGraphicsPipelineCreateInfo pipeline_create_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_create_info.stageCount = config->stage_count;
    pipeline_create_info.pStages = config->stages;
    pipeline_create_info.pVertexInputState = &vertex_input_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly;

    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    pipeline_create_info.pMultisampleState = &multisampling_create_info;
    pipeline_create_info.pDepthStencilState = ((config->shader_flags & SHADER_FLAG_DEPTH_TEST_BIT) || (config->shader_flags & SHADER_FLAG_STENCIL_TEST_BIT)) ? &depth_stencil : 0;
    pipeline_create_info.pColorBlendState = config->colour_attachment_count ? &color_blend_state_create_info : 0;
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

#if KOHI_DEBUG
    char* pipeline_name_buf = string_format("pipeline_shader_%s", config->name);
    VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE, out_pipeline->handle, pipeline_name_buf);
    string_free(pipeline_name_buf);
#endif

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

static u32 get_new_frequency_id(vulkan_shader_frequency_state* frequency_states, u32 max_frequency_count) {
    for (u32 i = 0; i < max_frequency_count; ++i) {
        if (frequency_states[i].id == INVALID_ID) {
            frequency_states[i].id = i;
            return i;
        }
    }
    return INVALID_ID;
}

static b8 setup_frequency_state(renderer_backend_interface* backend, vulkan_shader* internal_shader, shader_update_frequency frequency, u32* out_frequency_id) {

    vulkan_context* context = (vulkan_context*)backend->internal_context;
    vulkan_shader* internal = internal_shader;

    // Array of frequency states.
    vulkan_shader_frequency_state* frequency_states = 0;
    // Pointer to current frequency state based on id.
    vulkan_shader_frequency_state* frequency_state = 0;
    vulkan_shader_frequency_info* frequency_info = 0;
    u32 max_frequency_count = 0;
    b8 do_ubo_setup = false;
    u8 descriptor_set_index = 0;
    const char* frequency_text = shader_update_frequency_to_string(frequency);
    const char* shader_name = kname_string_get(internal_shader->name);

    b8 has_per_frame = frequency_has_uniforms(&internal_shader->per_frame_info);
    b8 has_group = frequency_has_uniforms(&internal_shader->per_group_info);

    switch (frequency) {
    case SHADER_UPDATE_FREQUENCY_PER_FRAME:
        // NOTE: treat single entry as an "array" so the same logic below can be used for it as well.
        frequency_states = &internal->per_frame_state;
        frequency_info = &internal->per_frame_info;
        max_frequency_count = 1;
        do_ubo_setup = true;
        descriptor_set_index = 0;
        break;
    case SHADER_UPDATE_FREQUENCY_PER_GROUP:
        frequency_states = internal->group_states;
        frequency_info = &internal->per_group_info;
        max_frequency_count = internal->max_groups;
        do_ubo_setup = true;
        descriptor_set_index = (has_per_frame ? 1 : 0);
        break;
    case SHADER_UPDATE_FREQUENCY_PER_DRAW:
        frequency_states = internal->per_draw_states;
        frequency_info = &internal->per_draw_info;
        max_frequency_count = internal->max_per_draw_count;
        do_ubo_setup = false;
        descriptor_set_index += (has_per_frame ? 1 : 0);
        descriptor_set_index += (has_group ? 1 : 0);
        break;
    }

    if (frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
        frequency_state = &internal->per_frame_state;
    } else {
        // Obtain an id for the given frequency. An id is not required for the per-frame scope.

        *out_frequency_id = get_new_frequency_id(frequency_states, max_frequency_count);
        if (*out_frequency_id == INVALID_ID) {

            KERROR("setup_frequency_state failed to acquire new %s id for shader '%s', max %s count=%u", frequency_text, shader_name, frequency_text, max_frequency_count);
            return false;
        }
        frequency_state = &frequency_states[*out_frequency_id];
    }

    // Extra debug info
#ifdef KOHI_DEBUG
    frequency_state->descriptor_set_index = descriptor_set_index;
    frequency_state->frequency = frequency;
    // per-frame frequency should always be the first descriptor set.
    if (frequency_state->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME && frequency_state->descriptor_set_index > 0) {
        KERROR("per-frame frequency is somehow descriptor set index > 0");
    }
#endif

    // Setup sampler uniform states. Only setup if the shader actually requires it.
    if (frequency_info->uniform_sampler_count > 0) {
        frequency_state->sampler_states = KALLOC_TYPE_CARRAY(vulkan_uniform_sampler_state, frequency_info->uniform_sampler_count);

        // Assign uniforms to each of the sampler states.
        for (u32 ii = 0; ii < frequency_info->uniform_sampler_count; ++ii) {
            vulkan_uniform_sampler_state* sampler_state = &frequency_state->sampler_states[ii];
            sampler_state->uniform = internal_shader->uniforms[frequency_info->sampler_indices[ii]];

            u32 array_length = KMAX(sampler_state->uniform.array_length, 1);
            // Setup the array for the samplers.
            sampler_state->sampler_handles = KALLOC_TYPE_CARRAY(khandle, array_length);
            // Setup descriptor states
            sampler_state->descriptor_states = KALLOC_TYPE_CARRAY(vulkan_descriptor_state, array_length);
            // Per descriptor
            for (u32 d = 0; d < array_length; ++d) {
                // Use a default sampler.
                // TODO: Allow this to be configured?
                khandle default_sampler = renderer_generic_sampler_get(backend->frontend_state, SHADER_GENERIC_SAMPLER_LINEAR_REPEAT);
                sampler_state->sampler_handles[d] = default_sampler;

                // Per swapchain image
                for (u32 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
                    sampler_state->descriptor_states[d].renderer_frame_number[j] = INVALID_ID_U16;
                }
            }
        }
    }

    // Setup texture uniform states. Only setup if the shader actually requires it.
    if (frequency_info->uniform_texture_count > 0) {
        frequency_state->texture_states = KALLOC_TYPE_CARRAY(vulkan_uniform_texture_state, frequency_info->uniform_texture_count);

        // Assign uniforms to each of the texture states.
        for (u32 ii = 0; ii < frequency_info->uniform_texture_count; ++ii) {
            vulkan_uniform_texture_state* texture_state = &frequency_state->texture_states[ii];
            texture_state->uniform = internal_shader->uniforms[frequency_info->texture_indices[ii]];

            u32 array_length = KMAX(texture_state->uniform.array_length, 1);
            // Setup the array for the textures.
            texture_state->texture_handles = KALLOC_TYPE_CARRAY(khandle, array_length);
            // Setup descriptor states
            texture_state->descriptor_states = KALLOC_TYPE_CARRAY(vulkan_descriptor_state, array_length);
            // Per descriptor
            for (u32 d = 0; d < array_length; ++d) {
                // TODO: Make this configurable.
                texture_state->texture_handles[d] = renderer_default_texture_get(backend->frontend_state, RENDERER_DEFAULT_TEXTURE_BASE_COLOUR);

                // Per swapchain image
                for (u32 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
                    texture_state->descriptor_states[d].renderer_frame_number[j] = INVALID_ID_U16;
                }
            }
        }
    }

    b8 final_result = true;
    // frequency-level UBO binding, if needed.
    if (do_ubo_setup) {

        // Allocate some space in the UBO - by the stride, not the size.
        u64 size = frequency_info->ubo_stride;
        if (size > 0) {
            for (u32 i = 0; i < VULKAN_RESOURCE_IMAGE_COUNT; ++i) {
                if (!renderer_renderbuffer_allocate(&internal->uniform_buffers[i], size, &frequency_state->offset)) {
                    KERROR("setup_frequency_state failed to acquire %s ubo space", frequency_text);
                    return false;
                }
            }
        }
    }

    // Temp array for descriptor set layouts.
    VkDescriptorSetLayout layouts[VULKAN_RESOURCE_IMAGE_COUNT] = {0, 0, 0};

    // Per colour image
    for (u32 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
        // Invalidate descriptor state.
        frequency_state->ubo_descriptor_state.renderer_frame_number[j] = INVALID_ID_U16;

        // Set descriptor set layout for this index.
        layouts[j] = internal->descriptor_set_layouts[descriptor_set_index];
    }

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = internal->descriptor_pool;
    alloc_info.descriptorSetCount = VULKAN_RESOURCE_IMAGE_COUNT;
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(context->device.logical_device, &alloc_info, frequency_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating %s descriptor sets in shader: '%s'.", frequency_text, vulkan_result_string(result, true));
        final_result = false;
    }

#ifdef KOHI_DEBUG
    // Assign a debug name to the descriptor set.
    for (u32 i = 0; i < VULKAN_RESOURCE_IMAGE_COUNT; ++i) {
        u32 fid = (frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME ? INVALID_ID : *out_frequency_id);
        char* desc_set_object_name = string_format("desc_set_shader_%s_per_%s_id_%u_set_idx_%u_img_idx_%u", shader_name, frequency_text, fid, descriptor_set_index, i);
        VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, frequency_state->descriptor_sets[i], desc_set_object_name);
        string_free(desc_set_object_name);
    }
#endif

    // Report failures.
    if (!final_result) {
        KERROR("Failed to setup %s frequency level state.", frequency_text);
    }

    return final_result;
}

static b8 release_shader_frequency_state(vulkan_context* context, vulkan_shader* internal_shader, shader_update_frequency frequency, u32 frequency_id) {

    vulkan_shader_frequency_state* frequency_state = 0;
    vulkan_shader_frequency_info* frequency_info = 0;
    b8 destroy_ubo = false;

    switch (frequency) {
    case SHADER_UPDATE_FREQUENCY_PER_FRAME:
        frequency_state = &internal_shader->per_frame_state;
        destroy_ubo = true;
        frequency_info = &internal_shader->per_frame_info;
        return false;
    case SHADER_UPDATE_FREQUENCY_PER_GROUP:
        frequency_state = &internal_shader->group_states[frequency_id];
        destroy_ubo = true;
        frequency_info = &internal_shader->per_group_info;
        break;
    case SHADER_UPDATE_FREQUENCY_PER_DRAW:
        frequency_state = &internal_shader->per_draw_states[frequency_id];
        destroy_ubo = false;
        frequency_info = &internal_shader->per_draw_info;
        break;
    }

    // Wait for any pending operations using the descriptor set to finish.
    vkDeviceWaitIdle(context->device.logical_device);

    // Destroy bindings and their descriptor states/uniforms.
    // UBO, if one exists.
    if (destroy_ubo) {

        // Release renderbuffer ranges.
        if (frequency_info->ubo_stride != 0) {
            for (u32 i = 0; i < VULKAN_RESOURCE_IMAGE_COUNT; ++i) {
                if (!renderer_renderbuffer_free(&internal_shader->uniform_buffers[i], frequency_info->ubo_stride, frequency_state->offset)) {
                    KERROR("release_shader_frequency_state failed to free range from renderbuffer.");
                }
            }
        }
    }

    // Descriptor sets
    VkResult result = vkFreeDescriptorSets(context->device.logical_device, internal_shader->descriptor_pool, VULKAN_RESOURCE_IMAGE_COUNT, frequency_state->descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error freeing %s shader descriptor sets!", shader_update_frequency_to_string(frequency));
    }

    // Samplers
    if (frequency_state->sampler_states) {
        for (u32 a = 0; a < frequency_info->uniform_sampler_count; ++a) {
            vulkan_uniform_sampler_state* sampler_state = &frequency_state->sampler_states[a];
            u32 array_length = KMAX(sampler_state->uniform.array_length, 1);
            KFREE_TYPE_CARRAY(sampler_state->descriptor_states, vulkan_descriptor_state, array_length);
            sampler_state->descriptor_states = 0;
            if (sampler_state->sampler_handles) {
                KFREE_TYPE_CARRAY(sampler_state->sampler_handles, khandle, array_length);
                sampler_state->sampler_handles = 0;
            }
        }

        KFREE_TYPE_CARRAY(frequency_state->sampler_states, vulkan_uniform_sampler_state, frequency_info->uniform_sampler_count);
        frequency_state->sampler_states = 0;
    }

    // Textures
    if (frequency_state->texture_states) {
        for (u32 a = 0; a < frequency_info->uniform_texture_count; ++a) {
            vulkan_uniform_texture_state* texture_state = &frequency_state->texture_states[a];
            u32 array_length = KMAX(texture_state->uniform.array_length, 1);
            KFREE_TYPE_CARRAY(texture_state->descriptor_states, vulkan_descriptor_state, array_length);
            texture_state->descriptor_states = 0;
            if (texture_state->texture_handles) {
                KFREE_TYPE_CARRAY(texture_state->texture_handles, khandle, array_length);
                texture_state->texture_handles = 0;
            }
        }

        KFREE_TYPE_CARRAY(frequency_state->texture_states, vulkan_uniform_texture_state, frequency_info->uniform_texture_count);
        frequency_state->texture_states = 0;
    }

    frequency_state->offset = INVALID_ID;
    frequency_state->id = INVALID_ID;

    return true;
}

static void destroy_shader_frequency_states(shader_update_frequency frequency, vulkan_shader_frequency_state* states, u32 state_count, vulkan_shader_frequency_info* info) {
    // Free arrays and, if needed, the frequency states array itself.
    for (u32 i = 0; i < state_count; ++i) {
        vulkan_shader_frequency_state* frequency_state = &states[i];
        kzero_memory(frequency_state->descriptor_sets, sizeof(VkDescriptorSet) * VULKAN_RESOURCE_IMAGE_COUNT);
        if (frequency_state->sampler_states) {
            kfree(frequency_state->sampler_states, sizeof(vulkan_uniform_sampler_state) * info->uniform_sampler_count, MEMORY_TAG_ARRAY);
            frequency_state->sampler_states = 0;
        }
        if (frequency_state->texture_states) {
            kfree(frequency_state->texture_states, sizeof(vulkan_uniform_texture_state) * info->uniform_texture_count, MEMORY_TAG_ARRAY);
            frequency_state->texture_states = 0;
        }
    }
}

static b8 shader_create_modules_and_pipelines(renderer_backend_interface* backend, vulkan_shader* internal_shader, u8 stage_count, shader_stage_config* stage_configs) {
    vulkan_context* context = (vulkan_context*)backend->internal_context;

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
    vulkan_shader_stage new_stages[VULKAN_SHADER_MAX_STAGES] = {0};
    for (u32 i = 0; i < internal_shader->stage_count; ++i) {
        shader_stage_config* sc = &stage_configs[i];
        if (!create_shader_module(context, internal_shader, sc->stage, sc->resource->text, kname_string_get(sc->resource_name), &new_stages[i])) {
            KERROR("Unable to create %s shader module for '%s'. Shader will be destroyed.", kname_string_get(stage_configs[i].resource_name), kname_string_get(internal_shader->name));
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
        pipeline_config.stride = internal_shader->attribute_stride;
        pipeline_config.attribute_count = internal_shader->attribute_count;
        pipeline_config.attributes = internal_shader->attributes;
        pipeline_config.descriptor_set_layout_count = internal_shader->descriptor_set_count;
        pipeline_config.descriptor_set_layouts = internal_shader->descriptor_set_layouts;
        pipeline_config.stage_count = internal_shader->stage_count;
        pipeline_config.stages = stage_create_infos;
        pipeline_config.viewport = viewport;
        pipeline_config.scissor = scissor;
        pipeline_config.cull_mode = internal_shader->cull_mode;

        // Strip the wireframe flag if it's there.
        shader_flag_bits flags = internal_shader->flags;
        flags &= ~(SHADER_FLAG_WIREFRAME_BIT);
        pipeline_config.shader_flags = flags;

        // skybox
        if (internal_shader->name == 8288729406296736979) {
            KTRACE("skybox shader"); // nocheckin
        }

        // NOTE: Always one block for the push constant, unless there is no per-draw UBO uniforms.
        krange push_constant_range;
        if (internal_shader->per_draw_info.ubo_size) {
            pipeline_config.push_constant_range_count = 1;
            push_constant_range.offset = 0;
            push_constant_range.size = internal_shader->per_draw_info.ubo_stride;
            pipeline_config.push_constant_ranges = &push_constant_range;
        } else {
            pipeline_config.push_constant_range_count = 0;
            pipeline_config.push_constant_ranges = 0;
        }
        pipeline_config.name = string_duplicate(kname_string_get(internal_shader->name));
        pipeline_config.topology_types = internal_shader->topology_types;

        // Always use this format since the render targets will be in this format.
        // TODO: May want to extract this from the attachment resources themselves?
        VkFormat colour_attachment_format = VK_FORMAT_R8G8B8A8_UNORM;

        if ((internal_shader->flags & SHADER_FLAG_COLOUR_READ_BIT) || (internal_shader->flags & SHADER_FLAG_COLOUR_WRITE_BIT)) {
            // TODO: Figure out the format(s) of the colour attachments (if they exist) and pass them along here.
            // This just assumes the same format as the default render target/swapchain. This will work
            // until there is a shader with more than 1 colour attachment, in which case either the
            // shader configuration itself will have to be amended to indicate this directly and/or the
            // shader configuration can specify some known "pipeline type" (i.e. "forward"), and that
            // type contains the image format information needed here. Putting a pin in this for now
            // until the eventual shader refactoring.
            pipeline_config.colour_attachment_count = 1;
            pipeline_config.colour_attachment_formats = &colour_attachment_format; // &context->current_window->renderer_state->backend_state->swapchain.image_format.format;
        } else {
            pipeline_config.colour_attachment_count = 0;
            pipeline_config.colour_attachment_formats = 0;
        }

        if ((internal_shader->flags & SHADER_FLAG_DEPTH_TEST_BIT) || (internal_shader->flags & SHADER_FLAG_DEPTH_WRITE_BIT) || (internal_shader->flags & SHADER_FLAG_STENCIL_TEST_BIT) || (internal_shader->flags & SHADER_FLAG_STENCIL_WRITE_BIT)) {
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
            pipeline_config.shader_flags |= SHADER_FLAG_WIREFRAME_BIT;
            pipeline_result = vulkan_graphics_pipeline_create(context, &pipeline_config, &new_wireframe_pipelines[i]);
        }

        kfree(pipeline_config.name, string_length(pipeline_config.name) + 1, MEMORY_TAG_STRING);

        if (!pipeline_result) {
            KERROR("Failed to load graphics pipeline for shader: '%s'.", kname_string_get(internal_shader->name));
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

    return !has_error;
}

static void setup_frequency_descriptors(b8 do_ubo, vulkan_shader_frequency_info* frequency_info, vulkan_descriptor_set_config* set_config, const kresource_shader* config) {

    // Total bindings are 1 UBO for per_frame (if needed), plus per_frame sampler count.
    // This is dynamically allocated now.
    u32 ubo_count = do_ubo ? (frequency_info->uniform_count ? 1 : 0) : 0;
    u32 sampler_and_image_count = frequency_info->uniform_sampler_count + frequency_info->uniform_texture_count;
    set_config->binding_count = ubo_count + sampler_and_image_count;
    if (!set_config->binding_count) {
        return;
    }
    set_config->bindings = KALLOC_TYPE_CARRAY(VkDescriptorSetLayoutBinding, set_config->binding_count);

    // per_frame UBO binding is first, if present.
    u8 frequency_binding_index = 0;
    if (do_ubo && frequency_info->uniform_count > 0) {
        set_config->bindings[frequency_binding_index].binding = frequency_binding_index;
        set_config->bindings[frequency_binding_index].descriptorCount = 1; // NOTE: the whole UBO is one binding.
        set_config->bindings[frequency_binding_index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        set_config->bindings[frequency_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
        frequency_binding_index++;
    }

    // Need to iterate these in uniform order, which can mix the order between samplers and images if configured that way.
    // This means a combined list of both types is required, which should then be iterated (checking the type along the way).
    if (sampler_and_image_count) {

        // Traverse the sorted list.
        for (u32 i = 0; i < sampler_and_image_count; ++i) {
            shader_uniform_config* u = &config->uniforms[frequency_info->sorted_indices[i]];
            VkDescriptorType type = uniform_type_is_texture(u->type) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
            set_config->bindings[frequency_binding_index].binding = frequency_binding_index;
            set_config->bindings[frequency_binding_index].descriptorCount = KMAX(u->array_length, 1); // Either treat as an array or a single texture, depending on what is passed in.
            set_config->bindings[frequency_binding_index].descriptorType = type;
            set_config->bindings[frequency_binding_index].stageFlags = VK_SHADER_STAGE_ALL;
            frequency_binding_index++;
        }
    }
}

static b8 vulkan_descriptorset_update_and_bind(
    vulkan_context* context,
    u16 renderer_frame_number,
    vulkan_shader* internal_shader,
    const vulkan_shader_frequency_info* info,
    vulkan_shader_frequency_state* frequency_state,
    u32 descriptor_set_index) {

    u32 image_index = get_current_image_index(context);

    const frame_data* p_frame_data = engine_frame_data_get();
    vulkan_descriptor_set_config set_config = internal_shader->descriptor_set_configs[descriptor_set_index];

    // Allocate enough descriptor writes to handle one UBO, all samplers and all textures.
    u32 max_desc_write_count = 1 + info->uniform_sampler_count + info->uniform_texture_count;
    // NOTE: Using the frame allocator, so this does not have to be freed as it's handled automatically at the end of the frame on allocator reset.
    VkWriteDescriptorSet* descriptor_writes = p_frame_data->allocator.allocate(sizeof(VkWriteDescriptorSet) * max_desc_write_count);
    kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * max_desc_write_count);

    u32 descriptor_write_count = 0;
    u32 binding_index = 0;

    // Update UBO, if needed. UBO is always first.
    VkDescriptorBufferInfo ubo_buffer_info = {0};
    if (info->uniform_count > 0) {
        u16* freq_gen = &frequency_state->ubo_descriptor_state.renderer_frame_number[image_index];
        if ((*freq_gen) == INVALID_ID_U16 || (*freq_gen) != renderer_frame_number) {

            // Only do this if the descriptor has not yet been updated.
            ubo_buffer_info.buffer = ((vulkan_buffer*)internal_shader->uniform_buffers[image_index].internal_data)->handle;
            KASSERT_MSG((frequency_state->offset % context->device.properties.limits.minUniformBufferOffsetAlignment) == 0, "Ubo offset must be a multiple of device.properties.limits.minUniformBufferOffsetAlignment.");
            ubo_buffer_info.offset = frequency_state->offset;
            ubo_buffer_info.range = info->ubo_stride;

            VkWriteDescriptorSet ubo_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            ubo_descriptor.dstSet = frequency_state->descriptor_sets[image_index];
            ubo_descriptor.dstBinding = binding_index;
            ubo_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubo_descriptor.descriptorCount = 1;
            ubo_descriptor.pBufferInfo = &ubo_buffer_info;

            descriptor_writes[descriptor_write_count] = ubo_descriptor;
            descriptor_write_count++;

            // Sync the generation.
            *freq_gen = renderer_frame_number == INVALID_ID_U16 ? 0 : renderer_frame_number;
        }

        binding_index++;
    }

    // Need to iterate these in uniform order, which can mix the order between samplers and images if configured that way.
    // This means a combined list of both types is required, which should then be iterated (checking the type along the way).
    u32 sampler_and_image_count = info->uniform_sampler_count + info->uniform_texture_count;
    if (sampler_and_image_count) {

        // Allocate enough space to hold all the descriptor image infos needed for this scope (one array per sampler/image binding).
        // NOTE: Using the frame allocator, so this does not have to be freed as it's handled automatically at the end of the frame on allocator reset.
        VkDescriptorImageInfo** binding_image_infos = p_frame_data->allocator.allocate(sizeof(VkDescriptorImageInfo*) * sampler_and_image_count);

        // Traverse the sorted list of sampler/texture uniforms. Each of these is one binding.
        u32 sampler_binding_index = 0;
        u32 texture_binding_index = 0;
        for (u32 i = 0; i < sampler_and_image_count; ++i) {
            u32 binding_descriptor_count = set_config.bindings[binding_index].descriptorCount;
            u32 update_count = 0;
            shader_uniform* u = &internal_shader->uniforms[info->sorted_indices[i]];
            b8 is_texture = uniform_type_is_texture(u->type);
            VkDescriptorType type = is_texture ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;

            // Build image infos for the binding, enough for all of them to have descriptor updates.
            binding_image_infos[i] = p_frame_data->allocator.allocate(sizeof(VkDescriptorImageInfo) * binding_descriptor_count);

            // Get the appropriate binding state.
            vulkan_uniform_texture_state* binding_texture_state = 0;
            vulkan_uniform_sampler_state* binding_sampler_state = 0;
            if (is_texture) {
                binding_texture_state = &frequency_state->texture_states[texture_binding_index];
            } else {
                binding_sampler_state = &frequency_state->sampler_states[sampler_binding_index];
            }

            // Each descriptor within the binding.
            for (u32 d = 0; d < binding_descriptor_count; ++d) {
                khandle resource_handle = khandle_invalid();
                vulkan_descriptor_state* descriptor_state = 0;
                if (is_texture) {
                    resource_handle = binding_texture_state->texture_handles[d];
                    descriptor_state = &binding_texture_state->descriptor_states[d];

                    if (khandle_is_invalid(resource_handle)) {
                        KERROR("Invalid texture handle found while trying to update/bind descriptor set.");
                        return false;
                    }

                    vulkan_texture_handle_data* texture = &context->textures[resource_handle.handle_index];

                    u32 image_index = texture->image_count > 1 ? get_current_image_index(context) : 0;
                    vulkan_image* image = &texture->images[image_index];

                    // Only update if the descriptor has not been updated this frame.
                    u16* desc_gen = &descriptor_state->renderer_frame_number[image_index];
                    if ((*desc_gen) == INVALID_ID_U16 || (*desc_gen) != renderer_frame_number) {
                        binding_image_infos[i][update_count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        binding_image_infos[i][update_count].imageView = image->view;
                        // NOTE: Not using sampler in this descriptor.
                        binding_image_infos[i][update_count].sampler = 0;

                        update_count++;

                        // Sync the generation.
                        (*desc_gen) = renderer_frame_number == INVALID_ID_U16 ? 0 : renderer_frame_number;
                    }

                } else {

                    resource_handle = binding_sampler_state->sampler_handles[d];
                    descriptor_state = &binding_sampler_state->descriptor_states[d];

                    if (khandle_is_invalid(resource_handle)) {
                        KERROR("Invalid sampler handle found while trying to update/bind descriptor set.");
                        return false;
                    }

                    vulkan_sampler_handle_data* sampler = &context->samplers[resource_handle.handle_index];

                    // Only update if the descriptor has not been updated this frame.
                    u16* desc_gen = &descriptor_state->renderer_frame_number[image_index];
                    if ((*desc_gen) == INVALID_ID_U16 || (*desc_gen) != renderer_frame_number) {
                        // Not using image for sampler updates.
                        binding_image_infos[i][update_count].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        binding_image_infos[i][update_count].imageView = 0;
                        // NOTE: Only the sampler is set here.
                        binding_image_infos[i][update_count].sampler = sampler->sampler;

                        update_count++;

                        // Sync the generation.
                        (*desc_gen) = renderer_frame_number == INVALID_ID_U16 ? 0 : renderer_frame_number;
                    }
                }
            }

            // Move to the next binding.
            if (is_texture) {
                texture_binding_index++;
            } else {
                sampler_binding_index++;
            }

            // Only include if there is actually an update.
            if (update_count > 0) {
                VkWriteDescriptorSet desc_set_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                desc_set_write.dstSet = frequency_state->descriptor_sets[image_index];
                desc_set_write.dstBinding = binding_index;
                desc_set_write.descriptorType = type;
                desc_set_write.descriptorCount = update_count;
                desc_set_write.pImageInfo = binding_image_infos[i];

                descriptor_writes[descriptor_write_count] = desc_set_write;
                descriptor_write_count++;
            }

            binding_index++;
        }
    }

    // Immediately update the descriptor set's data.
    if (descriptor_write_count > 0) {
        vkUpdateDescriptorSets(context->device.logical_device, descriptor_write_count, descriptor_writes, 0, 0);
    }

    // Pick the correct pipeline.
    b8 wireframe_enabled = FLAG_GET(internal_shader->flags, SHADER_FLAG_WIREFRAME_BIT);
    vulkan_pipeline** pipeline_array = wireframe_enabled ? internal_shader->wireframe_pipelines : internal_shader->pipelines;

    VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;
    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_array[internal_shader->bound_pipeline_index]->pipeline_layout, descriptor_set_index, 1,
                            &frequency_state->descriptor_sets[image_index], 0, 0);

    return true;
}

static b8 frequency_has_uniforms(vulkan_shader_frequency_info* frequency_info) {
    return frequency_info->uniform_count > 0 ||
           frequency_info->uniform_sampler_count > 0 ||
           frequency_info->uniform_texture_count > 0;
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

    // if (alloc_alignment != alignment) {
    //     KERROR(
    //         "Attempted realloc using a different alignment of %llu than the "
    //         "original of %hu.",
    //         alignment, alloc_alignment);
    //     return 0;
    // }

#    ifdef KVULKAN_ALLOCATOR_TRACE
    KTRACE("Attempting to realloc block %p...", original);
#    endif

    void* result = vulkan_alloc_allocation(user_data, size, alignment,
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

#endif // KVULKAN_USE_CUSTOM_ALLOCATOR == 1
