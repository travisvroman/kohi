#include "vulkan_backend.h"

#include "vulkan_types.inl"
#include "vulkan_platform.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "vulkan_command_buffer.h"
#include "vulkan_utils.h"
#include "vulkan_buffer.h"
#include "vulkan_image.h"
#include "vulkan_pipeline.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "core/kmemory.h"

#include "containers/darray.h"

#include "math/math_types.h"

#include "platform/platform.h"

#include "systems/shader_system.h"
#include "systems/material_system.h"
#include "systems/texture_system.h"
#include "systems/resource_system.h"

// static Vulkan context
static vulkan_context context;

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data);

i32 find_memory_index(u32 type_filter, u32 property_flags);
b8 create_buffers(vulkan_context* context);

void create_command_buffers(renderer_backend* backend);
b8 recreate_swapchain(renderer_backend* backend);
b8 create_module(vulkan_shader* shader, vulkan_shader_stage_config config, vulkan_shader_stage* shader_stage);

b8 upload_data_range(vulkan_context* context, VkCommandPool pool, VkFence fence, VkQueue queue, vulkan_buffer* buffer, u64* out_offset, u64 size, const void* data) {
    // Allocate space in the buffer.
    if (!vulkan_buffer_allocate(buffer, size, out_offset)) {
        KERROR("upload_data_range failed to allocate from the given buffer!");
        return false;
    }

    // Create a host-visible staging buffer to upload to. Mark it as the source of the transfer.
    VkBufferUsageFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan_buffer staging;
    vulkan_buffer_create(context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, flags, true, false, &staging);

    // Load the data into the staging buffer.
    vulkan_buffer_load_data(context, &staging, 0, size, 0, data);

    // Perform the copy from staging to the device local buffer.
    vulkan_buffer_copy_to(context, pool, fence, queue, staging.handle, 0, buffer->handle, *out_offset, size);

    // Clean up the staging buffer.
    vulkan_buffer_destroy(context, &staging);

    return true;
}

void free_data_range(vulkan_buffer* buffer, u64 offset, u64 size) {
    if (buffer) {
        vulkan_buffer_free(buffer, size, offset);
    }
}

b8 vulkan_renderer_backend_initialize(renderer_backend* backend, const renderer_backend_config* config, u8* out_window_render_target_count) {
    // Function pointers
    context.find_memory_index = find_memory_index;

    // TODO: custom allocator.
    context.allocator = 0;

    context.on_rendertarget_refresh_required = config->on_rendertarget_refresh_required;

    // Just set some default values for the framebuffer for now.
    // It doesn't really matyer what these are because they will be
    // overridden, but are needed for swapchain creation.
    context.framebuffer_width = 800;
    context.framebuffer_height = 600;

    // Setup Vulkan instance.
    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.apiVersion = VK_API_VERSION_1_2;
    app_info.pApplicationName = config->application_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Kohi Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;

    // Obtain a list of required extensions
    const char** required_extensions = darray_create(const char*);
    darray_push(required_extensions, &VK_KHR_SURFACE_EXTENSION_NAME);  // Generic surface extension
    platform_get_required_extension_names(&required_extensions);       // Platform-specific extension(s)
#if defined(_DEBUG)
    darray_push(required_extensions, &VK_EXT_DEBUG_UTILS_EXTENSION_NAME);  // debug utilities

    KDEBUG("Required extensions:");
    u32 length = darray_length(required_extensions);
    for (u32 i = 0; i < length; ++i) {
        KDEBUG(required_extensions[i]);
    }
#endif

    create_info.enabledExtensionCount = darray_length(required_extensions);
    create_info.ppEnabledExtensionNames = required_extensions;

    // Validation layers.
    const char** required_validation_layer_names = 0;
    u32 required_validation_layer_count = 0;

// If validation should be done, get a list of the required validation layert names
// and make sure they exist. Validation layers should only be enabled on non-release builds.
#if defined(_DEBUG)
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
        KINFO("Searching for layer: %s...", required_validation_layer_names[i]);
        b8 found = false;
        for (u32 j = 0; j < available_layer_count; ++j) {
            if (strings_equal(required_validation_layer_names[i], available_layers[j].layerName)) {
                found = true;
                KINFO("Found.");
                break;
            }
        }

        if (!found) {
            KFATAL("Required validation layer is missing: %s", required_validation_layer_names[i]);
            return false;
        }
    }
    KINFO("All required validation layers are present.");
#endif

    create_info.enabledLayerCount = required_validation_layer_count;
    create_info.ppEnabledLayerNames = required_validation_layer_names;

    VK_CHECK(vkCreateInstance(&create_info, context.allocator, &context.instance));
    KINFO("Vulkan Instance created.");

    // Debugger
#if defined(_DEBUG)
    KDEBUG("Creating Vulkan debugger...");
    u32 log_severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;  //|
                                                                      //    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug_create_info.messageSeverity = log_severity;
    debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debug_create_info.pfnUserCallback = vk_debug_callback;

    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT");
    KASSERT_MSG(func, "Failed to create debug messenger!");
    VK_CHECK(func(context.instance, &debug_create_info, context.allocator, &context.debug_messenger));
    KDEBUG("Vulkan debugger created.");
#endif

    // Surface
    KDEBUG("Creating Vulkan surface...");
    if (!platform_create_vulkan_surface(&context)) {
        KERROR("Failed to create platform surface!");
        return false;
    }
    KDEBUG("Vulkan surface created.");

    // Device creation
    if (!vulkan_device_create(&context)) {
        KERROR("Failed to create device!");
        return false;
    }

    // Swapchain
    vulkan_swapchain_create(
        &context,
        context.framebuffer_width,
        context.framebuffer_height,
        &context.swapchain);

    // Save off the number of images we have as the number of render targets needed.
    *out_window_render_target_count = context.swapchain.image_count;

    // Hold registered renderpasses.
    for (u32 i = 0; i < VULKAN_MAX_REGISTERED_RENDERPASSES; ++i) {
        context.registered_passes[i].id = INVALID_ID_U16;
    }

    // The renderpass table will be a lookup of array indices. Start off every index with an invalid id.
    context.renderpass_table_block = kallocate(sizeof(u32) * VULKAN_MAX_REGISTERED_RENDERPASSES, MEMORY_TAG_RENDERER);
    hashtable_create(sizeof(u32), VULKAN_MAX_REGISTERED_RENDERPASSES, context.renderpass_table_block, false, &context.renderpass_table);
    u32 value = INVALID_ID;
    hashtable_fill(&context.renderpass_table, &value);

    // Renderpasses
    for (u32 i = 0; i < config->renderpass_count; ++i) {
        // TODO: move to a function for reusability.
        // Make sure there are no collisions with the name first.
        u32 id = INVALID_ID;
        hashtable_get(&context.renderpass_table, config->pass_configs[i].name, &id);
        if (id != INVALID_ID) {
            KERROR("Collision with renderpass named '%s'. Initialization failed.", config->pass_configs[i].name);
            return false;
        }
        // Snip up a new id.
        for (u32 j = 0; j < VULKAN_MAX_REGISTERED_RENDERPASSES; ++j) {
            if (context.registered_passes[j].id == INVALID_ID_U16) {
                // Found one.
                context.registered_passes[j].id = j;
                id = j;
                break;
            }
        }

        // Verify we got an id
        if (id == INVALID_ID) {
            KERROR("No space was found for a new renderpass. Increase VULKAN_MAX_REGISTERED_RENDERPASSES. Initialization failed.");
            return false;
        }

        // Setup the renderpass.
        context.registered_passes[id].clear_flags = config->pass_configs[i].clear_flags;
        context.registered_passes[id].clear_colour = config->pass_configs[i].clear_colour;
        context.registered_passes[id].render_area = config->pass_configs[i].render_area;

        vulkan_renderpass_create(&context.registered_passes[id], 1.0f, 0, config->pass_configs[i].prev_name != 0, config->pass_configs[i].next_name != 0);

        // Update the table with the new id.
        hashtable_set(&context.renderpass_table, config->pass_configs[i].name, &id);
    }

    // Create command buffers.
    create_command_buffers(backend);

    // Create sync objects.
    context.image_available_semaphores = darray_reserve(VkSemaphore, context.swapchain.max_frames_in_flight);
    context.queue_complete_semaphores = darray_reserve(VkSemaphore, context.swapchain.max_frames_in_flight);

    for (u8 i = 0; i < context.swapchain.max_frames_in_flight; ++i) {
        VkSemaphoreCreateInfo semaphore_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(context.device.logical_device, &semaphore_create_info, context.allocator, &context.image_available_semaphores[i]);
        vkCreateSemaphore(context.device.logical_device, &semaphore_create_info, context.allocator, &context.queue_complete_semaphores[i]);

        // Create the fence in a signaled state, indicating that the first frame has already been "rendered".
        // This will prevent the application from waiting indefinitely for the first frame to render since it
        // cannot be rendered until a frame is "rendered" before it.
        VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(context.device.logical_device, &fence_create_info, context.allocator, &context.in_flight_fences[i]));
    }

    // In flight fences should not yet exist at this point, so clear the list. These are stored in pointers
    // because the initial state should be 0, and will be 0 when not in use. Acutal fences are not owned
    // by this list.
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        context.images_in_flight[i] = 0;
    }

    create_buffers(&context);

    // Mark all geometries as invalid
    for (u32 i = 0; i < VULKAN_MAX_GEOMETRY_COUNT; ++i) {
        context.geometries[i].id = INVALID_ID;
    }

    KINFO("Vulkan renderer initialized successfully.");
    return true;
}

void vulkan_renderer_backend_shutdown(renderer_backend* backend) {
    vkDeviceWaitIdle(context.device.logical_device);

    // Destroy in the opposite order of creation.
    // Destroy buffers
    vulkan_buffer_destroy(&context, &context.object_vertex_buffer);
    vulkan_buffer_destroy(&context, &context.object_index_buffer);

    // Sync objects
    for (u8 i = 0; i < context.swapchain.max_frames_in_flight; ++i) {
        if (context.image_available_semaphores[i]) {
            vkDestroySemaphore(
                context.device.logical_device,
                context.image_available_semaphores[i],
                context.allocator);
            context.image_available_semaphores[i] = 0;
        }
        if (context.queue_complete_semaphores[i]) {
            vkDestroySemaphore(
                context.device.logical_device,
                context.queue_complete_semaphores[i],
                context.allocator);
            context.queue_complete_semaphores[i] = 0;
        }
        vkDestroyFence(context.device.logical_device, context.in_flight_fences[i], context.allocator);
    }
    darray_destroy(context.image_available_semaphores);
    context.image_available_semaphores = 0;

    darray_destroy(context.queue_complete_semaphores);
    context.queue_complete_semaphores = 0;

    // Command buffers
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(
                &context,
                context.device.graphics_command_pool,
                &context.graphics_command_buffers[i]);
            context.graphics_command_buffers[i].handle = 0;
        }
    }
    darray_destroy(context.graphics_command_buffers);
    context.graphics_command_buffers = 0;

    // Destroy render targets
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        vulkan_renderer_render_target_destroy(&context.world_render_targets[i], true);
        vulkan_renderer_render_target_destroy(&context.swapchain.render_targets[i], true);
    }

    // Renderpasses
    for (u32 i = 0; i < VULKAN_MAX_REGISTERED_RENDERPASSES; ++i) {
        if (context.registered_passes[i].id != INVALID_ID_U16) {
            vulkan_renderpass_destroy(&context.registered_passes[i]);
        }
    }

    // Swapchain
    vulkan_swapchain_destroy(&context, &context.swapchain);

    KDEBUG("Destroying Vulkan device...");
    vulkan_device_destroy(&context);

    KDEBUG("Destroying Vulkan surface...");
    if (context.surface) {
        vkDestroySurfaceKHR(context.instance, context.surface, context.allocator);
        context.surface = 0;
    }

#if defined(_DEBUG)
    KDEBUG("Destroying Vulkan debugger...");
    if (context.debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance, "vkDestroyDebugUtilsMessengerEXT");
        func(context.instance, context.debug_messenger, context.allocator);
    }
#endif

    KDEBUG("Destroying Vulkan instance...");
    vkDestroyInstance(context.instance, context.allocator);
}

void vulkan_renderer_backend_on_resized(renderer_backend* backend, u16 width, u16 height) {
    // Update the "framebuffer size generation", a counter which indicates when the
    // framebuffer size has been updated.
    context.framebuffer_width = width;
    context.framebuffer_height = height;
    context.framebuffer_size_generation++;

    KINFO("Vulkan renderer backend->resized: w/h/gen: %i/%i/%llu", width, height, context.framebuffer_size_generation);
}

b8 vulkan_renderer_backend_begin_frame(renderer_backend* backend, f32 delta_time) {
    context.frame_delta_time = delta_time;
    vulkan_device* device = &context.device;

    // Check if recreating swap chain and boot out.
    if (context.recreating_swapchain) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (1) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }
        KINFO("Recreating swapchain, booting.");
        return false;
    }

    // Check if the framebuffer has been resized. If so, a new swapchain must be created.
    if (context.framebuffer_size_generation != context.framebuffer_size_last_generation) {
        VkResult result = vkDeviceWaitIdle(device->logical_device);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_renderer_backend_begin_frame vkDeviceWaitIdle (2) failed: '%s'", vulkan_result_string(result, true));
            return false;
        }

        // If the swapchain recreation failed (because, for example, the window was minimized),
        // boot out before unsetting the flag.
        if (!recreate_swapchain(backend)) {
            return false;
        }

        KINFO("Resized, booting.");
        return false;
    }

    // Wait for the execution of the current frame to complete. The fence being free will allow this one to move on.
    VkResult result = vkWaitForFences(context.device.logical_device, 1, &context.in_flight_fences[context.current_frame], true, UINT64_MAX);
    if (!vulkan_result_is_success(result)) {
        KFATAL("In-flight fence wait failure! error: %s", vulkan_result_string(result, true));
        return false;
    }

    // Acquire the next image from the swap chain. Pass along the semaphore that should signaled when this completes.
    // This same semaphore will later be waited on by the queue submission to ensure this image is available.
    if (!vulkan_swapchain_acquire_next_image_index(
            &context,
            &context.swapchain,
            UINT64_MAX,
            context.image_available_semaphores[context.current_frame],
            0,
            &context.image_index)) {
        KERROR("Failed to acquire next image index, booting.");
        return false;
    }

    // Begin recording commands.
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];
    vulkan_command_buffer_reset(command_buffer);
    vulkan_command_buffer_begin(command_buffer, false, false, false);

    // Dynamic state
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)context.framebuffer_height;
    viewport.width = (f32)context.framebuffer_width;
    viewport.height = -(f32)context.framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = context.framebuffer_width;
    scissor.extent.height = context.framebuffer_height;

    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

    return true;
}

b8 vulkan_renderer_backend_end_frame(renderer_backend* backend, f32 delta_time) {
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];

    vulkan_command_buffer_end(command_buffer);

    // Make sure the previous frame is not using this image (i.e. its fence is being waited on)
    if (context.images_in_flight[context.image_index] != VK_NULL_HANDLE) {  // was frame
        VkResult result = vkWaitForFences(context.device.logical_device, 1, &context.images_in_flight[context.image_index], true, UINT64_MAX);
        if (!vulkan_result_is_success(result)) {
            KFATAL("vkWaitForFences error: %s", vulkan_result_string(result, true));
        }
    }

    // Mark the image fence as in-use by this frame.
    context.images_in_flight[context.image_index] = context.in_flight_fences[context.current_frame];

    // Reset the fence for use on the next frame
    VK_CHECK(vkResetFences(context.device.logical_device, 1, &context.in_flight_fences[context.current_frame]));

    // Submit the queue and wait for the operation to complete.
    // Begin queue submission
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};

    // Command buffer(s) to be executed.
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer->handle;

    // The semaphore(s) to be signaled when the queue is complete.
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &context.queue_complete_semaphores[context.current_frame];

    // Wait semaphore ensures that the operation cannot begin until the image is available.
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &context.image_available_semaphores[context.current_frame];

    // Each semaphore waits on the corresponding pipeline stage to complete. 1:1 ratio.
    // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT prevents subsequent colour attachment
    // writes from executing until the semaphore signals (i.e. one frame is presented at a time)
    VkPipelineStageFlags flags[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = flags;

    VkResult result = vkQueueSubmit(
        context.device.graphics_queue,
        1,
        &submit_info,
        context.in_flight_fences[context.current_frame]);
    if (result != VK_SUCCESS) {
        KERROR("vkQueueSubmit failed with result: %s", vulkan_result_string(result, true));
        return false;
    }

    vulkan_command_buffer_update_submitted(command_buffer);
    // End queue submission

    // Give the image back to the swapchain.
    vulkan_swapchain_present(
        &context,
        &context.swapchain,
        context.device.graphics_queue,
        context.device.present_queue,
        context.queue_complete_semaphores[context.current_frame],
        context.image_index);

    return true;
}

b8 vulkan_renderer_renderpass_begin(renderpass* pass, render_target* target) {
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];

    // Begin the render pass.
    vulkan_renderpass* internal_data = pass->internal_data;

    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = internal_data->handle;
    begin_info.framebuffer = target->internal_framebuffer;
    begin_info.renderArea.offset.x = pass->render_area.x;
    begin_info.renderArea.offset.y = pass->render_area.y;
    begin_info.renderArea.extent.width = pass->render_area.z;
    begin_info.renderArea.extent.height = pass->render_area.w;

    begin_info.clearValueCount = 0;
    begin_info.pClearValues = 0;

    VkClearValue clear_values[2];
    kzero_memory(clear_values, sizeof(VkClearValue) * 2);
    b8 do_clear_colour = (pass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;
    if (do_clear_colour) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, pass->clear_colour.elements, sizeof(f32) * 4);
        begin_info.clearValueCount++;
    }

    b8 do_clear_depth = (pass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
    if (do_clear_depth) {
        kcopy_memory(clear_values[begin_info.clearValueCount].color.float32, pass->clear_colour.elements, sizeof(f32) * 4);
        clear_values[begin_info.clearValueCount].depthStencil.depth = internal_data->depth;

        b8 do_clear_stencil = (pass->clear_flags & RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG) != 0;
        clear_values[begin_info.clearValueCount].depthStencil.stencil = do_clear_stencil ? internal_data->stencil : 0;
        begin_info.clearValueCount++;
    }

    begin_info.pClearValues = begin_info.clearValueCount > 0 ? clear_values : 0;

    vkCmdBeginRenderPass(command_buffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    command_buffer->state = COMMAND_BUFFER_STATE_IN_RENDER_PASS;

    return true;
}

b8 vulkan_renderer_renderpass_end(renderpass* pass) {
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];
    // End the renderpass.
    vkCmdEndRenderPass(command_buffer->handle);
    command_buffer->state = COMMAND_BUFFER_STATE_RECORDING;
    return true;
}

renderpass* vulkan_renderer_renderpass_get(const char* name) {
    if (!name || name[0] == 0) {
        KERROR("vulkan_renderer_renderpass_get requires a name. Nothing will be returned.");
        return 0;
    }

    u32 id = INVALID_ID;
    hashtable_get(&context.renderpass_table, name, &id);
    if (id == INVALID_ID) {
        KWARN("There is no registered renderpass named '%s'.", name);
        return 0;
    }

    return &context.registered_passes[id];
}

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
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

i32 find_memory_index(u32 type_filter, u32 property_flags) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(context.device.physical_device, &memory_properties);

    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        // Check each memory type to see if its bit is set to 1.
        if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags) {
            return i;
        }
    }

    KWARN("Unable to find suitable memory type!");
    return -1;
}

void create_command_buffers(renderer_backend* backend) {
    if (!context.graphics_command_buffers) {
        context.graphics_command_buffers = darray_reserve(vulkan_command_buffer, context.swapchain.image_count);
        for (u32 i = 0; i < context.swapchain.image_count; ++i) {
            kzero_memory(&context.graphics_command_buffers[i], sizeof(vulkan_command_buffer));
        }
    }

    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        if (context.graphics_command_buffers[i].handle) {
            vulkan_command_buffer_free(
                &context,
                context.device.graphics_command_pool,
                &context.graphics_command_buffers[i]);
        }
        kzero_memory(&context.graphics_command_buffers[i], sizeof(vulkan_command_buffer));
        vulkan_command_buffer_allocate(
            &context,
            context.device.graphics_command_pool,
            true,
            &context.graphics_command_buffers[i]);
    }

    KDEBUG("Vulkan command buffers created.");
}

b8 recreate_swapchain(renderer_backend* backend) {
    // If already being recreated, do not try again.
    if (context.recreating_swapchain) {
        KDEBUG("recreate_swapchain called when already recreating. Booting.");
        return false;
    }

    // Detect if the window is too small to be drawn to
    if (context.framebuffer_width == 0 || context.framebuffer_height == 0) {
        KDEBUG("recreate_swapchain called when window is < 1 in a dimension. Booting.");
        return false;
    }

    // Mark as recreating if the dimensions are valid.
    context.recreating_swapchain = true;

    // Wait for any operations to complete.
    vkDeviceWaitIdle(context.device.logical_device);

    // Clear these out just in case.
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        context.images_in_flight[i] = 0;
    }

    // Requery support
    vulkan_device_query_swapchain_support(
        context.device.physical_device,
        context.surface,
        &context.device.swapchain_support);
    vulkan_device_detect_depth_format(&context.device);

    vulkan_swapchain_recreate(
        &context,
        context.framebuffer_width,
        context.framebuffer_height,
        &context.swapchain);

    // Update framebuffer size generation.
    context.framebuffer_size_last_generation = context.framebuffer_size_generation;

    // cleanup swapchain
    for (u32 i = 0; i < context.swapchain.image_count; ++i) {
        vulkan_command_buffer_free(&context, context.device.graphics_command_pool, &context.graphics_command_buffers[i]);
    }

    // Tell the renderer that a refresh is required.
    if (context.on_rendertarget_refresh_required) {
        context.on_rendertarget_refresh_required();
    }

    create_command_buffers(backend);

    // Clear the recreating flag.
    context.recreating_swapchain = false;

    return true;
}

b8 create_buffers(vulkan_context* context) {
    VkMemoryPropertyFlagBits memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Geometry vertex buffer
    const u64 vertex_buffer_size = sizeof(vertex_3d) * 1024 * 1024;
    if (!vulkan_buffer_create(
            context,
            vertex_buffer_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            memory_property_flags,
            true,
            true,
            &context->object_vertex_buffer)) {
        KERROR("Error creating vertex buffer.");
        return false;
    }

    // Geometry index buffer
    const u64 index_buffer_size = sizeof(u32) * 1024 * 1024;
    if (!vulkan_buffer_create(
            context,
            index_buffer_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            memory_property_flags,
            true,
            true,
            &context->object_index_buffer)) {
        KERROR("Error creating vertex buffer.");
        return false;
    }

    return true;
}

void vulkan_renderer_texture_create(const u8* pixels, texture* t) {
    // Internal data creation.
    // TODO: Use an allocator for this.
    t->internal_data = (vulkan_image*)kallocate(sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    vulkan_image* image = (vulkan_image*)t->internal_data;
    u32 size = t->width * t->height * t->channel_count;

    // NOTE: Assumes 8 bits per channel.
    VkFormat image_format = VK_FORMAT_R8G8B8A8_UNORM;

    // NOTE: Lots of assumptions here, different texture types will require
    // different options here.
    vulkan_image_create(
        &context,
        VK_IMAGE_TYPE_2D,
        t->width,
        t->height,
        image_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        true,
        VK_IMAGE_ASPECT_COLOR_BIT,
        image);

    // Load the data.
    vulkan_renderer_texture_write_data(t, 0, size, pixels);

    t->generation++;
}

void vulkan_renderer_texture_destroy(struct texture* texture) {
    vkDeviceWaitIdle(context.device.logical_device);

    vulkan_image* image = (vulkan_image*)texture->internal_data;
    if (image) {
        vulkan_image_destroy(&context, image);
        kzero_memory(image, sizeof(vulkan_image));

        kfree(texture->internal_data, sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    }
    kzero_memory(texture, sizeof(struct texture));
}

VkFormat channel_count_to_format(u8 channel_count, VkFormat default_format) {
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

void vulkan_renderer_texture_create_writeable(texture* t) {
    // Internal data creation.
    t->internal_data = (vulkan_image*)kallocate(sizeof(vulkan_image), MEMORY_TAG_TEXTURE);
    vulkan_image* image = (vulkan_image*)t->internal_data;

    VkFormat image_format = channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);
    // TODO: Lots of assumptions here, different texture types will require
    // different options here.
    vulkan_image_create(
        &context,
        VK_IMAGE_TYPE_2D,
        t->width,
        t->height,
        image_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        true,
        VK_IMAGE_ASPECT_COLOR_BIT,
        image);

    t->generation++;
}

void vulkan_renderer_texture_resize(texture* t, u32 new_width, u32 new_height) {
    if (t && t->internal_data) {
        // Resizing is really just destroying the old image and creating a new one.
        // Data is not preserved because there's no reliable way to map the old data to the new
        // since the amount of data differs.
        vulkan_image* image = (vulkan_image*)t->internal_data;
        vulkan_image_destroy(&context, image);

        VkFormat image_format = channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

        // TODO: Lots of assumptions here, different texture types will require
        // different options here.
        vulkan_image_create(
            &context,
            VK_IMAGE_TYPE_2D,
            new_width,
            new_height,
            image_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            true,
            VK_IMAGE_ASPECT_COLOR_BIT,
            image);

        t->generation++;
    }
}

void vulkan_renderer_texture_write_data(texture* t, u32 offset, u32 size, const u8* pixels) {
    vulkan_image* image = (vulkan_image*)t->internal_data;
    VkDeviceSize image_size = t->width * t->height * t->channel_count;

    VkFormat image_format = channel_count_to_format(t->channel_count, VK_FORMAT_R8G8B8A8_UNORM);

    // Create a staging buffer and load data into it.
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags memory_prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan_buffer staging;
    vulkan_buffer_create(&context, image_size, usage, memory_prop_flags, true, false, &staging);

    vulkan_buffer_load_data(&context, &staging, 0, image_size, 0, pixels);

    vulkan_command_buffer temp_buffer;
    VkCommandPool pool = context.device.graphics_command_pool;
    VkQueue queue = context.device.graphics_queue;
    vulkan_command_buffer_allocate_and_begin_single_use(&context, pool, &temp_buffer);

    // Transition the layout from whatever it is currently to optimal for recieving data.
    vulkan_image_transition_layout(
        &context,
        &temp_buffer,
        image,
        image_format,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy the data from the buffer.
    vulkan_image_copy_from_buffer(&context, image, staging.handle, &temp_buffer);

    // Transition from optimal for data reciept to shader-read-only optimal layout.
    vulkan_image_transition_layout(
        &context,
        &temp_buffer,
        image,
        image_format,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vulkan_command_buffer_end_single_use(&context, pool, &temp_buffer, queue);

    vulkan_buffer_destroy(&context, &staging);

    t->generation++;
}

b8 vulkan_renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices) {
    if (!vertex_count || !vertices) {
        KERROR("vulkan_renderer_create_geometry requires vertex data, and none was supplied. vertex_count=%d, vertices=%p", vertex_count, vertices);
        return false;
    }

    // Check if this is a re-upload. If it is, need to free old data afterward.
    b8 is_reupload = geometry->internal_id != INVALID_ID;
    vulkan_geometry_data old_range;

    vulkan_geometry_data* internal_data = 0;
    if (is_reupload) {
        internal_data = &context.geometries[geometry->internal_id];

        // Take a copy of the old range.
        old_range.index_buffer_offset = internal_data->index_buffer_offset;
        old_range.index_count = internal_data->index_count;
        old_range.index_element_size = internal_data->index_element_size;
        old_range.vertex_buffer_offset = internal_data->vertex_buffer_offset;
        old_range.vertex_count = internal_data->vertex_count;
        old_range.vertex_element_size = internal_data->vertex_element_size;
    } else {
        for (u32 i = 0; i < VULKAN_MAX_GEOMETRY_COUNT; ++i) {
            if (context.geometries[i].id == INVALID_ID) {
                // Found a free index.
                geometry->internal_id = i;
                context.geometries[i].id = i;
                internal_data = &context.geometries[i];
                break;
            }
        }
    }
    if (!internal_data) {
        KFATAL("vulkan_renderer_create_geometry failed to find a free index for a new geometry upload. Adjust config to allow for more.");
        return false;
    }

    VkCommandPool pool = context.device.graphics_command_pool;
    VkQueue queue = context.device.graphics_queue;

    // Vertex data.
    internal_data->vertex_count = vertex_count;
    internal_data->vertex_element_size = sizeof(vertex_3d);
    u32 total_size = vertex_count * vertex_size;
    if (!upload_data_range(
            &context,
            pool,
            0,
            queue,
            &context.object_vertex_buffer,
            &internal_data->vertex_buffer_offset,
            total_size,
            vertices)) {
        KERROR("vulkan_renderer_create_geometry failed to upload to the vertex buffer!");
        return false;
    }

    // Index data, if applicable
    if (index_count && indices) {
        internal_data->index_count = index_count;
        internal_data->index_element_size = sizeof(u32);
        total_size = index_count * index_size;
        if (!upload_data_range(
                &context,
                pool,
                0,
                queue,
                &context.object_index_buffer,
                &internal_data->index_buffer_offset,
                total_size,
                indices)) {
            KERROR("vulkan_renderer_create_geometry failed to upload to the index buffer!");
            return false;
        }
    }

    if (internal_data->generation == INVALID_ID) {
        internal_data->generation = 0;
    } else {
        internal_data->generation++;
    }

    if (is_reupload) {
        // Free vertex data
        free_data_range(&context.object_vertex_buffer, old_range.vertex_buffer_offset, old_range.vertex_element_size * old_range.vertex_count);

        // Free index data, if applicable
        if (old_range.index_element_size > 0) {
            free_data_range(&context.object_index_buffer, old_range.index_buffer_offset, old_range.index_element_size * old_range.index_count);
        }
    }

    return true;
}

void vulkan_renderer_destroy_geometry(geometry* geometry) {
    if (geometry && geometry->internal_id != INVALID_ID) {
        vkDeviceWaitIdle(context.device.logical_device);
        vulkan_geometry_data* internal_data = &context.geometries[geometry->internal_id];

        // Free vertex data
        free_data_range(&context.object_vertex_buffer, internal_data->vertex_buffer_offset, internal_data->vertex_element_size * internal_data->vertex_count);

        // Free index data, if applicable
        if (internal_data->index_element_size > 0) {
            free_data_range(&context.object_index_buffer, internal_data->index_buffer_offset, internal_data->index_element_size * internal_data->index_count);
        }

        // Clean up data.
        kzero_memory(internal_data, sizeof(vulkan_geometry_data));
        internal_data->id = INVALID_ID;
        internal_data->generation = INVALID_ID;
    }
}

void vulkan_renderer_draw_geometry(geometry_render_data* data) {
    // Ignore non-uploaded geometries.
    if (data->geometry && data->geometry->internal_id == INVALID_ID) {
        return;
    }

    vulkan_geometry_data* buffer_data = &context.geometries[data->geometry->internal_id];
    vulkan_command_buffer* command_buffer = &context.graphics_command_buffers[context.image_index];

    // Bind vertex buffer at offset.
    VkDeviceSize offsets[1] = {buffer_data->vertex_buffer_offset};
    vkCmdBindVertexBuffers(command_buffer->handle, 0, 1, &context.object_vertex_buffer.handle, (VkDeviceSize*)offsets);

    // Draw indexed or non-indexed.
    if (buffer_data->index_count > 0) {
        // Bind index buffer at offset.
        vkCmdBindIndexBuffer(command_buffer->handle, context.object_index_buffer.handle, buffer_data->index_buffer_offset, VK_INDEX_TYPE_UINT32);

        // Issue the draw.
        vkCmdDrawIndexed(command_buffer->handle, buffer_data->index_count, 1, 0, 0, 0);
    } else {
        vkCmdDraw(command_buffer->handle, buffer_data->vertex_count, 1, 0, 0);
    }
}

// The index of the global descriptor set.
const u32 DESC_SET_INDEX_GLOBAL = 0;
// The index of the instance descriptor set.
const u32 DESC_SET_INDEX_INSTANCE = 1;

// The index of the UBO binding.
const u32 BINDING_INDEX_UBO = 0;

// The index of the image sampler binding.
const u32 BINDING_INDEX_SAMPLER = 1;

b8 vulkan_renderer_shader_create(shader* shader, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages) {
    shader->internal_data = kallocate(sizeof(vulkan_shader), MEMORY_TAG_RENDERER);

    // Translate stages
    VkShaderStageFlags vk_stages[VULKAN_SHADER_MAX_STAGES];
    for (u8 i = 0; i < stage_count; ++i) {
        switch (stages[i]) {
            case SHADER_STAGE_FRAGMENT:
                vk_stages[i] = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case SHADER_STAGE_VERTEX:
                vk_stages[i] = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case SHADER_STAGE_GEOMETRY:
                KWARN("vulkan_renderer_shader_create: VK_SHADER_STAGE_GEOMETRY_BIT is set but not yet supported.");
                vk_stages[i] = VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case SHADER_STAGE_COMPUTE:
                KWARN("vulkan_renderer_shader_create: SHADER_STAGE_COMPUTE is set but not yet supported.");
                vk_stages[i] = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            default:
                KERROR("Unsupported stage type: %d", stages[i]);
                break;
        }
    }

    // TODO: configurable max descriptor allocate count.

    u32 max_descriptor_allocate_count = 1024;

    // Take a copy of the pointer to the context.
    vulkan_shader* out_shader = (vulkan_shader*)shader->internal_data;

    out_shader->renderpass = pass->internal_data;

    // Build out the configuration.
    out_shader->config.max_descriptor_set_count = max_descriptor_allocate_count;

    // Shader stages. Parse out the flags.
    kzero_memory(out_shader->config.stages, sizeof(vulkan_shader_stage_config) * VULKAN_SHADER_MAX_STAGES);
    out_shader->config.stage_count = 0;
    // Iterate provided stages.
    for (u32 i = 0; i < stage_count; i++) {
        // Make sure there is room enough to add the stage.
        if (out_shader->config.stage_count + 1 > VULKAN_SHADER_MAX_STAGES) {
            KERROR("Shaders may have a maximum of %d stages", VULKAN_SHADER_MAX_STAGES);
            return false;
        }

        // Make sure the stage is a supported one.
        VkShaderStageFlagBits stage_flag;
        switch (stages[i]) {
            case SHADER_STAGE_VERTEX:
                stage_flag = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case SHADER_STAGE_FRAGMENT:
                stage_flag = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            default:
                // Go to the next type.
                KERROR("vulkan_shader_create: Unsupported shader stage flagged: %d. Stage ignored.", stages[i]);
                continue;
        }

        // Set the stage and bump the counter.
        out_shader->config.stages[out_shader->config.stage_count].stage = stage_flag;
        string_ncopy(out_shader->config.stages[out_shader->config.stage_count].file_name, stage_filenames[i], 255);
        out_shader->config.stage_count++;
    }

    // Zero out arrays and counts.
    kzero_memory(out_shader->config.descriptor_sets, sizeof(vulkan_descriptor_set_config) * 2);

    // Attributes array.
    kzero_memory(out_shader->config.attributes, sizeof(VkVertexInputAttributeDescription) * VULKAN_SHADER_MAX_ATTRIBUTES);

    // For now, shaders will only ever have these 2 types of descriptor pools.
    out_shader->config.pool_sizes[0] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024};          // HACK: max number of ubo descriptor sets.
    out_shader->config.pool_sizes[1] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096};  // HACK: max number of image sampler descriptor sets.

    // Global descriptor set config.
    vulkan_descriptor_set_config global_descriptor_set_config = {};

    // UBO is always available and first.
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].binding = BINDING_INDEX_UBO;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorCount = 1;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    global_descriptor_set_config.bindings[BINDING_INDEX_UBO].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    global_descriptor_set_config.binding_count++;

    out_shader->config.descriptor_sets[DESC_SET_INDEX_GLOBAL] = global_descriptor_set_config;
    out_shader->config.descriptor_set_count++;
    if (shader->use_instances) {
        // If using instances, add a second descriptor set.
        vulkan_descriptor_set_config instance_descriptor_set_config = {};

        // Add a UBO to it, as instances should always have one available.
        // NOTE: Might be a good idea to only add this if it is going to be used...
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].binding = BINDING_INDEX_UBO;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorCount = 1;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        instance_descriptor_set_config.bindings[BINDING_INDEX_UBO].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        instance_descriptor_set_config.binding_count++;

        out_shader->config.descriptor_sets[DESC_SET_INDEX_INSTANCE] = instance_descriptor_set_config;
        out_shader->config.descriptor_set_count++;
    }

    // Invalidate all instance states.
    // TODO: dynamic
    for (u32 i = 0; i < 1024; ++i) {
        out_shader->instance_states[i].id = INVALID_ID;
    }

    return true;
}

void vulkan_renderer_shader_destroy(shader* s) {
    if (s && s->internal_data) {
        vulkan_shader* shader = s->internal_data;
        if (!shader) {
            KERROR("vulkan_renderer_shader_destroy requires a valid pointer to a shader.");
            return;
        }

        VkDevice logical_device = context.device.logical_device;
        VkAllocationCallbacks* vk_allocator = context.allocator;

        // Descriptor set layouts.
        for (u32 i = 0; i < shader->config.descriptor_set_count; ++i) {
            if (shader->descriptor_set_layouts[i]) {
                vkDestroyDescriptorSetLayout(logical_device, shader->descriptor_set_layouts[i], vk_allocator);
                shader->descriptor_set_layouts[i] = 0;
            }
        }

        // Descriptor pool
        if (shader->descriptor_pool) {
            vkDestroyDescriptorPool(logical_device, shader->descriptor_pool, vk_allocator);
        }

        // Uniform buffer.
        vulkan_buffer_unlock_memory(&context, &shader->uniform_buffer);
        shader->mapped_uniform_buffer_block = 0;
        vulkan_buffer_destroy(&context, &shader->uniform_buffer);

        // Pipeline
        vulkan_pipeline_destroy(&context, &shader->pipeline);

        // Shader modules
        for (u32 i = 0; i < shader->config.stage_count; ++i) {
            vkDestroyShaderModule(context.device.logical_device, shader->stages[i].handle, context.allocator);
        }

        // Destroy the configuration.
        kzero_memory(&shader->config, sizeof(vulkan_shader_config));

        // Free the internal data memory.
        kfree(s->internal_data, sizeof(vulkan_shader), MEMORY_TAG_RENDERER);
        s->internal_data = 0;
    }
}

b8 vulkan_renderer_shader_initialize(shader* shader) {
    VkDevice logical_device = context.device.logical_device;
    VkAllocationCallbacks* vk_allocator = context.allocator;
    vulkan_shader* s = (vulkan_shader*)shader->internal_data;

    // Create a module for each stage.
    kzero_memory(s->stages, sizeof(vulkan_shader_stage) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < s->config.stage_count; ++i) {
        if (!create_module(s, s->config.stages[i], &s->stages[i])) {
            KERROR("Unable to create %s shader module for '%s'. Shader will be destroyed.", s->config.stages[i].file_name, shader->name);
            return false;
        }
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
    u32 attribute_count = darray_length(shader->attributes);
    u32 offset = 0;
    for (u32 i = 0; i < attribute_count; ++i) {
        // Setup the new attribute.
        VkVertexInputAttributeDescription attribute;
        attribute.location = i;
        attribute.binding = 0;
        attribute.offset = offset;
        attribute.format = types[shader->attributes[i].type];

        // Push into the config's attribute collection and add to the stride.
        s->config.attributes[i] = attribute;

        offset += shader->attributes[i].size;
    }

    // Process uniforms.
    u32 uniform_count = darray_length(shader->uniforms);
    for (u32 i = 0; i < uniform_count; ++i) {
        // For samplers, the descriptor bindings need to be updated. Other types of uniforms don't need anything to be done here.
        if (shader->uniforms[i].type == SHADER_UNIFORM_TYPE_SAMPLER) {
            const u32 set_index = (shader->uniforms[i].scope == SHADER_SCOPE_GLOBAL ? DESC_SET_INDEX_GLOBAL : DESC_SET_INDEX_INSTANCE);
            vulkan_descriptor_set_config* set_config = &s->config.descriptor_sets[set_index];
            if (set_config->binding_count < 2) {
                // There isn't a binding yet, meaning this is the first sampler to be added.
                // Create the binding with a single descriptor for this sampler.
                set_config->bindings[BINDING_INDEX_SAMPLER].binding = BINDING_INDEX_SAMPLER;  // Always going to be the second one.
                set_config->bindings[BINDING_INDEX_SAMPLER].descriptorCount = 1;              // Default to 1, will increase with each sampler added to the appropriate level.
                set_config->bindings[BINDING_INDEX_SAMPLER].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                set_config->bindings[BINDING_INDEX_SAMPLER].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                set_config->binding_count++;
            } else {
                // There is already a binding for samplers, so just add a descriptor to it.
                // Take the current descriptor count as the location and increment the number of descriptors.
                set_config->bindings[BINDING_INDEX_SAMPLER].descriptorCount++;
            }
        }
    }

    // Descriptor pool.
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = s->config.pool_sizes;
    pool_info.maxSets = s->config.max_descriptor_set_count;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    // Create descriptor pool.
    VkResult result = vkCreateDescriptorPool(logical_device, &pool_info, vk_allocator, &s->descriptor_pool);
    if (!vulkan_result_is_success(result)) {
        KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
        return false;
    }

    // Create descriptor set layouts.
    kzero_memory(s->descriptor_set_layouts, s->config.descriptor_set_count);
    for (u32 i = 0; i < s->config.descriptor_set_count; ++i) {
        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = s->config.descriptor_sets[i].binding_count;
        layout_info.pBindings = s->config.descriptor_sets[i].bindings;
        result = vkCreateDescriptorSetLayout(logical_device, &layout_info, vk_allocator, &s->descriptor_set_layouts[i]);
        if (!vulkan_result_is_success(result)) {
            KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
            return false;
        }
    }

    // TODO: This feels wrong to have these here, at least in this fashion. Should probably
    // Be configured to pull from someplace instead.
    // Viewport.
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = (f32)context.framebuffer_height;
    viewport.width = (f32)context.framebuffer_width;
    viewport.height = -(f32)context.framebuffer_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // Scissor
    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = context.framebuffer_width;
    scissor.extent.height = context.framebuffer_height;

    VkPipelineShaderStageCreateInfo stage_create_infos[VULKAN_SHADER_MAX_STAGES];
    kzero_memory(stage_create_infos, sizeof(VkPipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
    for (u32 i = 0; i < s->config.stage_count; ++i) {
        stage_create_infos[i] = s->stages[i].shader_stage_create_info;
    }

    b8 pipeline_result = vulkan_graphics_pipeline_create(
        &context,
        s->renderpass,
        shader->attribute_stride,
        darray_length(shader->attributes),
        s->config.attributes,  // shader->attributes,
        s->config.descriptor_set_count,
        s->descriptor_set_layouts,
        s->config.stage_count,
        stage_create_infos,
        viewport,
        scissor,
        false,
        true,
        shader->push_constant_range_count,
        shader->push_constant_ranges,
        &s->pipeline);

    if (!pipeline_result) {
        KERROR("Failed to load graphics pipeline for object shader.");
        return false;
    }

    // Grab the UBO alignment requirement from the device.
    shader->required_ubo_alignment = context.device.properties.limits.minUniformBufferOffsetAlignment;

    // Make sure the UBO is aligned according to device requirements.
    shader->global_ubo_stride = get_aligned(shader->global_ubo_size, shader->required_ubo_alignment);
    shader->ubo_stride = get_aligned(shader->ubo_size, shader->required_ubo_alignment);

    // Uniform  buffer.
    u32 device_local_bits = context.device.supports_device_local_host_visible ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0;
    // TODO: max count should be configurable, or perhaps long term support of buffer resizing.
    u64 total_buffer_size = shader->global_ubo_stride + (shader->ubo_stride * VULKAN_MAX_MATERIAL_COUNT);  // global + (locals)
    if (!vulkan_buffer_create(
            &context,
            total_buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | device_local_bits,
            true,
            true,
            &s->uniform_buffer)) {
        KERROR("Vulkan buffer creation failed for object shader.");
        return false;
    }

    // Allocate space for the global UBO, whcih should occupy the _stride_ space, _not_ the actual size used.
    if (!vulkan_buffer_allocate(&s->uniform_buffer, shader->global_ubo_stride, &shader->global_ubo_offset)) {
        KERROR("Failed to allocate space for the uniform buffer!");
        return false;
    }

    // Map the entire buffer's memory.
    s->mapped_uniform_buffer_block = vulkan_buffer_lock_memory(&context, &s->uniform_buffer, 0, VK_WHOLE_SIZE /*total_buffer_size*/, 0);

    // Allocate global descriptor sets, one per frame. Global is always the first set.
    VkDescriptorSetLayout global_layouts[3] = {
        s->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL],
        s->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL],
        s->descriptor_set_layouts[DESC_SET_INDEX_GLOBAL]};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = s->descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = global_layouts;
    VK_CHECK(vkAllocateDescriptorSets(context.device.logical_device, &alloc_info, s->global_descriptor_sets));

    return true;
}

#ifdef _DEBUG
#define SHADER_VERIFY_SHADER_ID(shader_id)                                        \
    if (shader_id == INVALID_ID || context.shaders[shader_id].id == INVALID_ID) { \
        return false;                                                             \
    }
#else
#define SHADER_VERIFY_SHADER_ID(shader_id)  // do nothing
#endif

b8 vulkan_renderer_shader_use(shader* shader) {
    vulkan_shader* s = shader->internal_data;
    vulkan_pipeline_bind(&context.graphics_command_buffers[context.image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, &s->pipeline);
    return true;
}

b8 vulkan_renderer_shader_bind_globals(shader* s) {
    if (!s) {
        return false;
    }

    // Global UBO is always at the beginning, but use this anyway.
    s->bound_ubo_offset = s->global_ubo_offset;
    return true;
}

b8 vulkan_renderer_shader_bind_instance(shader* s, u32 instance_id) {
    if (!s) {
        KERROR("vulkan_shader_bind_instance requires a valid pointer to a shader.");
        return false;
    }
    vulkan_shader* internal = s->internal_data;

    s->bound_instance_id = instance_id;
    vulkan_shader_instance_state* object_state = &internal->instance_states[instance_id];
    s->bound_ubo_offset = object_state->offset;
    return true;
}

b8 vulkan_renderer_shader_apply_globals(shader* s) {
    u32 image_index = context.image_index;
    vulkan_shader* internal = s->internal_data;
    VkCommandBuffer command_buffer = context.graphics_command_buffers[image_index].handle;
    VkDescriptorSet global_descriptor = internal->global_descriptor_sets[image_index];

    // Apply UBO first
    VkDescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = internal->uniform_buffer.handle;
    bufferInfo.offset = s->global_ubo_offset;
    bufferInfo.range = s->global_ubo_stride;

    // Update descriptor sets.
    VkWriteDescriptorSet ubo_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ubo_write.dstSet = internal->global_descriptor_sets[image_index];
    ubo_write.dstBinding = 0;
    ubo_write.dstArrayElement = 0;
    ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_write.descriptorCount = 1;
    ubo_write.pBufferInfo = &bufferInfo;

    VkWriteDescriptorSet descriptor_writes[2];
    descriptor_writes[0] = ubo_write;

    u32 global_set_binding_count = internal->config.descriptor_sets[DESC_SET_INDEX_GLOBAL].binding_count;
    if (global_set_binding_count > 1) {
        // TODO: There are samplers to be written. Support this.
        global_set_binding_count = 1;
        KERROR("Global image samplers are not yet supported.");

        // VkWriteDescriptorSet sampler_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        // descriptor_writes[1] = ...
    }

    vkUpdateDescriptorSets(context.device.logical_device, global_set_binding_count, descriptor_writes, 0, 0);

    // Bind the global descriptor set to be updated.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, internal->pipeline.pipeline_layout, 0, 1, &global_descriptor, 0, 0);
    return true;
}

b8 vulkan_renderer_shader_apply_instance(shader* s, b8 needs_update) {
    if (!s->use_instances) {
        KERROR("This shader does not use instances.");
        return false;
    }
    vulkan_shader* internal = s->internal_data;
    u32 image_index = context.image_index;
    VkCommandBuffer command_buffer = context.graphics_command_buffers[image_index].handle;

    // Obtain instance data.
    vulkan_shader_instance_state* object_state = &internal->instance_states[s->bound_instance_id];
    VkDescriptorSet object_descriptor_set = object_state->descriptor_set_state.descriptor_sets[image_index];

    if (needs_update) {
        VkWriteDescriptorSet descriptor_writes[2];  // Always a max of 2 descriptor sets.
        kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * 2);
        u32 descriptor_count = 0;
        u32 descriptor_index = 0;

        // Descriptor 0 - Uniform buffer
        // Only do this if the descriptor has not yet been updated.
        u8* instance_ubo_generation = &(object_state->descriptor_set_state.descriptor_states[descriptor_index].generations[image_index]);
        // TODO: determine if update is required.
        if (*instance_ubo_generation == INVALID_ID_U8 /*|| *global_ubo_generation != material->generation*/) {
            VkDescriptorBufferInfo buffer_info;
            buffer_info.buffer = internal->uniform_buffer.handle;
            buffer_info.offset = object_state->offset;
            buffer_info.range = s->ubo_stride;

            VkWriteDescriptorSet ubo_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            ubo_descriptor.dstSet = object_descriptor_set;
            ubo_descriptor.dstBinding = descriptor_index;
            ubo_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubo_descriptor.descriptorCount = 1;
            ubo_descriptor.pBufferInfo = &buffer_info;

            descriptor_writes[descriptor_count] = ubo_descriptor;
            descriptor_count++;

            // Update the frame generation. In this case it is only needed once since this is a buffer.
            *instance_ubo_generation = 1;  // material->generation; TODO: some generation from... somewhere
        }
        descriptor_index++;

        // Samplers will always be in the binding. If the binding count is less than 2, there are no samplers.
        if (internal->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].binding_count > 1) {
            // Iterate samplers.
            u32 total_sampler_count = internal->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].bindings[BINDING_INDEX_SAMPLER].descriptorCount;
            u32 update_sampler_count = 0;
            VkDescriptorImageInfo image_infos[VULKAN_SHADER_MAX_GLOBAL_TEXTURES];
            for (u32 i = 0; i < total_sampler_count; ++i) {
                // TODO: only update in the list if actually needing an update.
                texture_map* map = internal->instance_states[s->bound_instance_id].instance_texture_maps[i];
                texture* t = map->texture;
                vulkan_image* image = (vulkan_image*)t->internal_data;
                image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                image_infos[i].imageView = image->view;
                image_infos[i].sampler = (VkSampler)map->internal_data;

                // TODO: change up descriptor state to handle this properly.
                // Sync frame generation if not using a default texture.
                // if (t->generation != INVALID_ID) {
                //     *descriptor_generation = t->generation;
                //     *descriptor_id = t->id;
                // }

                update_sampler_count++;
            }

            VkWriteDescriptorSet sampler_descriptor = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            sampler_descriptor.dstSet = object_descriptor_set;
            sampler_descriptor.dstBinding = descriptor_index;
            sampler_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_descriptor.descriptorCount = update_sampler_count;
            sampler_descriptor.pImageInfo = image_infos;

            descriptor_writes[descriptor_count] = sampler_descriptor;
            descriptor_count++;
        }

        if (descriptor_count > 0) {
            vkUpdateDescriptorSets(context.device.logical_device, descriptor_count, descriptor_writes, 0, 0);
        }
    }

    // Bind the descriptor set to be updated, or in case the shader changed.
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, internal->pipeline.pipeline_layout, 1, 1, &object_descriptor_set, 0, 0);
    return true;
}

VkSamplerAddressMode convert_repeat_type(const char* axis, texture_repeat repeat) {
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
            KWARN("convert_repeat_type(axis='%s') Type '%x' not supported, defaulting to repeat.", axis, repeat);
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkFilter convert_filter_type(const char* op, texture_filter filter) {
    switch (filter) {
        case TEXTURE_FILTER_MODE_NEAREST:
            return VK_FILTER_NEAREST;
        case TEXTURE_FILTER_MODE_LINEAR:
            return VK_FILTER_LINEAR;
        default:
            KWARN("convert_filter_type(op='%s'): Unsupported filter type '%x', defaulting to linear.", op, filter);
            return VK_FILTER_LINEAR;
    }
}

b8 vulkan_renderer_texture_map_acquire_resources(texture_map* map) {
    // Create a sampler for the texture
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sampler_info.minFilter = convert_filter_type("min", map->filter_minify);
    sampler_info.magFilter = convert_filter_type("mag", map->filter_magnify);

    sampler_info.addressModeU = convert_repeat_type("U", map->repeat_u);
    sampler_info.addressModeV = convert_repeat_type("V", map->repeat_v);
    sampler_info.addressModeW = convert_repeat_type("W", map->repeat_w);

    // TODO: Configurable
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = 16;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    VkResult result = vkCreateSampler(context.device.logical_device, &sampler_info, context.allocator, (VkSampler*)&map->internal_data);
    if (!vulkan_result_is_success(VK_SUCCESS)) {
        KERROR("Error creating texture sampler: %s", vulkan_result_string(result, true));
        return false;
    }

    return true;
}

void vulkan_renderer_texture_map_release_resources(texture_map* map) {
    if (map) {
        vkDestroySampler(context.device.logical_device, map->internal_data, context.allocator);
        map->internal_data = 0;
    }
}

b8 vulkan_renderer_shader_acquire_instance_resources(shader* s, texture_map** maps, u32* out_instance_id) {
    vulkan_shader* internal = s->internal_data;
    // TODO: dynamic
    *out_instance_id = INVALID_ID;
    for (u32 i = 0; i < 1024; ++i) {
        if (internal->instance_states[i].id == INVALID_ID) {
            internal->instance_states[i].id = i;
            *out_instance_id = i;
            break;
        }
    }
    if (*out_instance_id == INVALID_ID) {
        KERROR("vulkan_shader_acquire_instance_resources failed to acquire new id");
        return false;
    }

    vulkan_shader_instance_state* instance_state = &internal->instance_states[*out_instance_id];
    u32 instance_texture_count = internal->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].bindings[BINDING_INDEX_SAMPLER].descriptorCount;
    // Wipe out the memory for the entire array, even if it isn't all used.
    instance_state->instance_texture_maps = kallocate(sizeof(texture_map*) * s->instance_texture_count, MEMORY_TAG_ARRAY);
    texture* default_texture = texture_system_get_default_texture();
    kcopy_memory(instance_state->instance_texture_maps, maps, sizeof(texture_map*) * s->instance_texture_count);
    // Set unassigned texture pointers to default until assigned.
    for (u32 i = 0; i < instance_texture_count; ++i) {
        if (!maps[i]->texture) {
            instance_state->instance_texture_maps[i]->texture = default_texture;
        }
    }

    // Allocate some space in the UBO - by the stride, not the size.
    u64 size = s->ubo_stride;
    if (!vulkan_buffer_allocate(&internal->uniform_buffer, size, &instance_state->offset)) {
        KERROR("vulkan_material_shader_acquire_resources failed to acquire ubo space");
        return false;
    }

    vulkan_shader_descriptor_set_state* set_state = &instance_state->descriptor_set_state;

    // Each descriptor binding in the set
    u32 binding_count = internal->config.descriptor_sets[DESC_SET_INDEX_INSTANCE].binding_count;
    kzero_memory(set_state->descriptor_states, sizeof(vulkan_descriptor_state) * VULKAN_SHADER_MAX_BINDINGS);
    for (u32 i = 0; i < binding_count; ++i) {
        for (u32 j = 0; j < 3; ++j) {
            set_state->descriptor_states[i].generations[j] = INVALID_ID_U8;
            set_state->descriptor_states[i].ids[j] = INVALID_ID;
        }
    }

    // Allocate 3 descriptor sets (one per frame).
    VkDescriptorSetLayout layouts[3] = {
        internal->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE],
        internal->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE],
        internal->descriptor_set_layouts[DESC_SET_INDEX_INSTANCE]};

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = internal->descriptor_pool;
    alloc_info.descriptorSetCount = 3;
    alloc_info.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(
        context.device.logical_device,
        &alloc_info,
        instance_state->descriptor_set_state.descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error allocating instance descriptor sets in shader: '%s'.", vulkan_result_string(result, true));
        return false;
    }

    return true;
}

b8 vulkan_renderer_shader_release_instance_resources(shader* s, u32 instance_id) {
    vulkan_shader* internal = s->internal_data;
    vulkan_shader_instance_state* instance_state = &internal->instance_states[instance_id];

    // Wait for any pending operations using the descriptor set to finish.
    vkDeviceWaitIdle(context.device.logical_device);

    // Free 3 descriptor sets (one per frame)
    VkResult result = vkFreeDescriptorSets(
        context.device.logical_device,
        internal->descriptor_pool,
        3,
        instance_state->descriptor_set_state.descriptor_sets);
    if (result != VK_SUCCESS) {
        KERROR("Error freeing object shader descriptor sets!");
    }

    // Destroy descriptor states.
    kzero_memory(instance_state->descriptor_set_state.descriptor_states, sizeof(vulkan_descriptor_state) * VULKAN_SHADER_MAX_BINDINGS);

    if (instance_state->instance_texture_maps) {
        kfree(instance_state->instance_texture_maps, sizeof(texture_map*) * s->instance_texture_count, MEMORY_TAG_ARRAY);
        instance_state->instance_texture_maps = 0;
    }

    vulkan_buffer_free(&internal->uniform_buffer, s->ubo_stride, instance_state->offset);
    instance_state->offset = INVALID_ID;
    instance_state->id = INVALID_ID;

    return true;
}

b8 vulkan_renderer_set_uniform(shader* s, shader_uniform* uniform, const void* value) {
    vulkan_shader* internal = s->internal_data;
    if (uniform->type == SHADER_UNIFORM_TYPE_SAMPLER) {
        if (uniform->scope == SHADER_SCOPE_GLOBAL) {
            s->global_texture_maps[uniform->location] = (texture_map*)value;
        } else {
            internal->instance_states[s->bound_instance_id].instance_texture_maps[uniform->location] = (texture_map*)value;
        }
    } else {
        if (uniform->scope == SHADER_SCOPE_LOCAL) {
            // Is local, using push constants. Do this immediately.
            VkCommandBuffer command_buffer = context.graphics_command_buffers[context.image_index].handle;
            vkCmdPushConstants(command_buffer, internal->pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, uniform->offset, uniform->size, value);
        } else {
            // Map the appropriate memory location and copy the data over.
            u64 addr = (u64)internal->mapped_uniform_buffer_block;
            addr += s->bound_ubo_offset + uniform->offset;
            kcopy_memory((void*)addr, value, uniform->size);
            if (addr) {
            }
        }
    }
    return true;
}

b8 create_module(vulkan_shader* shader, vulkan_shader_stage_config config, vulkan_shader_stage* shader_stage) {
    // Read the resource.
    resource binary_resource;
    if (!resource_system_load(config.file_name, RESOURCE_TYPE_BINARY, &binary_resource)) {
        KERROR("Unable to read shader module: %s.", config.file_name);
        return false;
    }

    kzero_memory(&shader_stage->create_info, sizeof(VkShaderModuleCreateInfo));
    shader_stage->create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // Use the resource's size and data directly.
    shader_stage->create_info.codeSize = binary_resource.data_size;
    shader_stage->create_info.pCode = (u32*)binary_resource.data;

    VK_CHECK(vkCreateShaderModule(
        context.device.logical_device,
        &shader_stage->create_info,
        context.allocator,
        &shader_stage->handle));

    // Release the resource.
    resource_system_unload(&binary_resource);

    // Shader stage info
    kzero_memory(&shader_stage->shader_stage_create_info, sizeof(VkPipelineShaderStageCreateInfo));
    shader_stage->shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage->shader_stage_create_info.stage = config.stage;
    shader_stage->shader_stage_create_info.module = shader_stage->handle;
    shader_stage->shader_stage_create_info.pName = "main";

    return true;
}

void vulkan_renderpass_create(renderpass* out_renderpass, f32 depth, u32 stencil, b8 has_prev_pass, b8 has_next_pass) {
    out_renderpass->internal_data = kallocate(sizeof(vulkan_renderpass), MEMORY_TAG_RENDERER);
    vulkan_renderpass* internal_data = (vulkan_renderpass*)out_renderpass->internal_data;
    internal_data->has_prev_pass = has_prev_pass;
    internal_data->has_next_pass = has_next_pass;

    internal_data->depth = depth;
    internal_data->stencil = stencil;

    // Main subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Attachments TODO: make this configurable.
    u32 attachment_description_count = 0;
    VkAttachmentDescription attachment_descriptions[2];

    // Color attachment
    b8 do_clear_colour = (out_renderpass->clear_flags & RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG) != 0;
    VkAttachmentDescription color_attachment;
    color_attachment.format = context.swapchain.image_format.format;  // TODO: configurable
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = do_clear_colour ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // If coming from a previous pass, should already be VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. Otherwise undefined.
    color_attachment.initialLayout = has_prev_pass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

    // If going to another pass, use VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. Otherwise VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
    color_attachment.finalLayout = has_next_pass ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Transitioned to after the render pass
    color_attachment.flags = 0;

    attachment_descriptions[attachment_description_count] = color_attachment;
    attachment_description_count++;

    VkAttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;  // Attachment description array index
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;

    // Depth attachment, if there is one
    b8 do_clear_depth = (out_renderpass->clear_flags & RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG) != 0;
    if (do_clear_depth) {
        VkAttachmentDescription depth_attachment = {};
        depth_attachment.format = context.device.depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = do_clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachment_descriptions[attachment_description_count] = depth_attachment;
        attachment_description_count++;

        // Depth attachment reference
        VkAttachmentReference depth_attachment_reference;
        depth_attachment_reference.attachment = 1;
        depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // TODO: other attachment types (input, resolve, preserve)

        // Depth stencil data.
        subpass.pDepthStencilAttachment = &depth_attachment_reference;
    } else {
        kzero_memory(&attachment_descriptions[attachment_description_count], sizeof(VkAttachmentDescription));
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
    render_pass_create_info.attachmentCount = attachment_description_count;
    render_pass_create_info.pAttachments = attachment_descriptions;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = 1;
    render_pass_create_info.pDependencies = &dependency;
    render_pass_create_info.pNext = 0;
    render_pass_create_info.flags = 0;

    VK_CHECK(vkCreateRenderPass(context.device.logical_device, &render_pass_create_info, context.allocator, &internal_data->handle));
}

void vulkan_renderpass_destroy(renderpass* pass) {
    if (pass && pass->internal_data) {
        vulkan_renderpass* internal_data = pass->internal_data;
        vkDestroyRenderPass(context.device.logical_device, internal_data->handle, context.allocator);
        internal_data->handle = 0;
        kfree(internal_data, sizeof(vulkan_renderpass), MEMORY_TAG_RENDERER);
        pass->internal_data = 0;
    }
}

void vulkan_renderer_render_target_create(u8 attachment_count, texture** attachments, renderpass* pass, u32 width, u32 height, render_target* out_target) {
    // Max number of attachments
    VkImageView attachment_views[32];
    for (u32 i = 0; i < attachment_count; ++i) {
        attachment_views[i] = ((vulkan_image*)attachments[i]->internal_data)->view;
    }

    // Take a copy of the attachments and count.
    out_target->attachment_count = attachment_count;
    if (!out_target->attachments) {
        out_target->attachments = kallocate(sizeof(texture*) * attachment_count, MEMORY_TAG_ARRAY);
    }
    kcopy_memory(out_target->attachments, attachments, sizeof(texture*) * attachment_count);

    VkFramebufferCreateInfo framebuffer_create_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_create_info.renderPass = ((vulkan_renderpass*)pass->internal_data)->handle;
    framebuffer_create_info.attachmentCount = attachment_count;
    framebuffer_create_info.pAttachments = attachment_views;
    framebuffer_create_info.width = width;
    framebuffer_create_info.height = height;
    framebuffer_create_info.layers = 1;

    VK_CHECK(vkCreateFramebuffer(context.device.logical_device, &framebuffer_create_info, context.allocator, (VkFramebuffer*)&out_target->internal_framebuffer));
}
void vulkan_renderer_render_target_destroy(render_target* target, b8 free_internal_memory) {
    if (target && target->internal_framebuffer) {
        vkDestroyFramebuffer(context.device.logical_device, (VkFramebuffer)target->internal_framebuffer, context.allocator);
        target->internal_framebuffer = 0;
        if (free_internal_memory) {
            kfree(target->attachments, sizeof(texture*) * target->attachment_count, MEMORY_TAG_ARRAY);
            target->attachments = 0;
            target->attachment_count = 0;
        }
    }
}

texture* vulkan_renderer_window_attachment_get(u8 index) {
    if (index >= context.swapchain.image_count) {
        KFATAL("Attempting to get attachment index out of range: %d. Attachment count: %d", index, context.swapchain.image_count);
        return 0;
    }

    return context.swapchain.render_textures[index];
}
texture* vulkan_renderer_depth_attachment_get() {
    return context.swapchain.depth_texture;
}
u8 vulkan_renderer_window_attachment_index_get() {
    return (u8)context.image_index;
}