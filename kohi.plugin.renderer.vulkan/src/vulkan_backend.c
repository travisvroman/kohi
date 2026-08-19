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
#include <logger.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <platform/vulkan_platform.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <utils/ksort.h>
#include <utils/render_type_utils.h>

#include "vulkan_command_buffer.h"
#include "vulkan_device.h"
#include "vulkan_image.h"
#include "vulkan_loader.h"
#include "vulkan_swapchain.h"
#include "vulkan_types.h"
#include "vulkan_utils.h"

// NOTE: If wanting to trace allocations, uncomment this.
// #ifndef KVULKAN_ALLOCATOR_TRACE
// #define KVULKAN_ALLOCATOR_TRACE 1
// #endif

// NOTE: To disable the custom allocator, comment this out or set to 0.
#ifndef KVULKAN_USE_CUSTOM_ALLOCATOR
#	define KVULKAN_USE_CUSTOM_ALLOCATOR 1
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback (
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);

static b8 recreate_swapchain (renderer_backend_interface *backend, kwindow *window);
static b8 create_shader_module (vulkan_context *context, vulkan_shader *internal_shader, shader_stage stage, const char *source, const char *filename, vulkan_shader_stage *out_stage);
static b8 vulkan_buffer_copy_range_internal (vulkan_context *context,
											 VkBuffer source, u64 source_offset,
											 VkBuffer dest, u64 dest_offset,
											 u64 size, b8 queue_wait);
static vulkan_command_buffer *get_current_command_buffer (vulkan_context *context);
static u32 get_current_image_index (vulkan_context *context);
static u32 get_current_frame_index (vulkan_context *context);

// Returns the current image count. Typically 2 for double-buffering, 3 for triple.
// Should NOT be used when determining resource size. See VULKAN_RESOURCE_IMAGE_COUNT.
static u32 get_current_image_count (vulkan_context *context);

static b8 vulkan_graphics_pipeline_create (vulkan_context *context, const vulkan_pipeline_config *config, vulkan_pipeline *out_pipeline);
static void vulkan_pipeline_destroy (vulkan_context *context, vulkan_pipeline *pipeline);
static void vulkan_pipeline_bind (vulkan_context *context, vulkan_command_buffer *command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline *pipeline);
static b8 shader_create_modules_and_pipelines (renderer_backend_interface *backend, vulkan_shader *internal_shader, shader_pipeline_config *config, vulkan_vertex_layout_pipeline *pipeline);
static b8 vulkan_descriptorset_update_and_bind (
	vulkan_context *context,
	u16 renderer_frame_number,
	vulkan_shader *internal_shader,
	u8 vertex_pipeline_index,
	u32 descriptor_set_index,
	u32 use_id);

// FIXME: May want to have this as a configurable option instead.
// Forward declarations of custom vulkan allocator functions.
#if KVULKAN_USE_CUSTOM_ALLOCATOR == 1
static void *vulkan_alloc_allocation (void *user_data, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope);
static void vulkan_alloc_free (void *user_data, void *memory);
static void *vulkan_alloc_reallocation (void *user_data, void *original, size_t size, size_t alignment, VkSystemAllocationScope allocation_scope);
static void vulkan_alloc_internal_alloc (void *pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
static void vulkan_alloc_internal_free (void *pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
static b8 create_vulkan_allocator (vulkan_context *context, VkAllocationCallbacks *callbacks);
#endif // KVULKAN_USE_CUSTOM_ALLOCATOR == 1

b8 vulkan_renderer_backend_initialize (renderer_backend_interface *backend, const renderer_backend_config *config) {
	backend->internal_context_size = get_aligned(sizeof(vulkan_context), 16);
	backend->internal_context = kallocate_aligned(backend->internal_context_size, 16, MEMORY_TAG_RENDERER);
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	context->rhi = (krhi_vulkan){0};
	if (!vulkan_loader_initialize(&context->rhi)) {
		KERROR("Failed to initialize Vulkan RHI. See logs for details");
		return false;
	}
	krhi_vulkan *rhi = &context->rhi;

	if (!vulkan_loader_load_core(rhi)) {
		KERROR("Failed to load core Vulkan functions. See logs for details.");
		return false;
	}

	if (config->flags & RENDERER_CONFIG_FLAG_ENABLE_VALIDATION) {
		context->validation_enabled = true;
	}
	context->flags = config->flags;
	context->render_flag_changed = false;

	// NOTE: Custom allocator.
#if KVULKAN_USE_CUSTOM_ALLOCATOR == 1
	context->allocator =
		kallocate_aligned(sizeof(VkAllocationCallbacks), 16, MEMORY_TAG_RENDERER);
	if (!create_vulkan_allocator(context, context->allocator)) {
		// If this fails, gracefully fall back to the default allocator.
		KFATAL(
			"Failed to create custom Vulkan allocator. Continuing using the "
			"driver's default allocator.");
		kfree(context->allocator);
		context->allocator = 0;
	}
#else
	context->allocator = 0;
#endif

	// Get the currently-installed instance version. Not necessarily what the device
	// uses, though. Use this to create the instance though.
	u32 api_version = 0;
	rhi->kvkEnumerateInstanceVersion(&api_version);
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
	const char *ext = VK_KHR_SURFACE_EXTENSION_NAME;
	darray_push(required_extensions, &ext);								// Generic surface extension
	vulkan_platform_get_required_extension_names(&required_extensions); // Platform-specific extension(s)
	u32 required_extension_count = 0;

	ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	darray_push(required_extensions, &ext); // debug utilities

	KDEBUG("Required extensions:");
	required_extension_count = darray_length(required_extensions);
	for (u32 i = 0; i < required_extension_count; ++i) {
		KDEBUG(required_extensions[i]);
	}

	create_info.enabledExtensionCount = darray_length(required_extensions);
	create_info.ppEnabledExtensionNames = required_extensions;

	u32 available_extension_count = 0;
	rhi->kvkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0);
	VkExtensionProperties *available_extensions = darray_reserve(VkExtensionProperties, available_extension_count);
	rhi->kvkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions);

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
	const char **required_validation_layer_names = 0;
	u32 required_validation_layer_count = 0;

	// If validation should be done, get a list of the required validation layert
	// names and make sure they exist. Validation layers should only be enabled on
	// non-release builds.
	if (context->validation_enabled) {
		KINFO("Validation layers enabled. Enumerating...");

		// The list of validation layers required.
		required_validation_layer_names = darray_create(const char *);
		const char *lname = "VK_LAYER_KHRONOS_validation";
		darray_push(required_validation_layer_names, &lname);
		lname = "VK_LAYER_LUNARG_api_dump";
		// NOTE: enable this when needed for debugging.
		// darray_push(required_validation_layer_names, &lname);
		required_validation_layer_count = darray_length(required_validation_layer_names);

		// Obtain a list of available validation layers
		u32 available_layer_count = 0;
		VK_CHECK(rhi->kvkEnumerateInstanceLayerProperties(&available_layer_count, 0));
		VkLayerProperties *available_layers = darray_reserve(VkLayerProperties, available_layer_count);
		VK_CHECK(rhi->kvkEnumerateInstanceLayerProperties(&available_layer_count, available_layers));

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

		darray_destroy(available_layers);

		KINFO("All required validation layers are present.");
	} else {
		KINFO("Vulkan validation layers are not enabled.");
	}

	darray_destroy(available_extensions);

	create_info.enabledLayerCount = required_validation_layer_count;
	create_info.ppEnabledLayerNames = required_validation_layer_names;

#if KPLATFORM_APPLE == 1
	create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

	VkResult instance_result = rhi->kvkCreateInstance(&create_info, context->allocator, &context->instance);
	if (!vulkan_result_is_success(instance_result)) {
		const char *result_string = vulkan_result_string(instance_result, true);
		KFATAL("Vulkan instance creation failed with result: '%s'", result_string);
		return false;
	}

	rhi->instance = context->instance;

	if (!vulkan_loader_load_instance(rhi, context->instance)) {
		KERROR("Failed to load Vulkan instance functions. Renderer init failed.");
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

		PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
		KASSERT_MSG(func, "Failed to create debug messenger!");
		VK_CHECK(func(context->instance, &debug_create_info, context->allocator, &context->debug_messenger));
		KDEBUG("Vulkan debugger created.");
	}

	// TODO: conditionally enable labels via config?
	// Load up debug function pointers.
	context->pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkSetDebugUtilsObjectNameEXT");
	if (!context->pfnSetDebugUtilsObjectNameEXT) {
		KWARN("Unable to load function pointer for vkSetDebugUtilsObjectNameEXT. Debug functions associated with this will not work.");
	}
	context->pfnSetDebugUtilsObjectTagEXT = (PFN_vkSetDebugUtilsObjectTagEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkSetDebugUtilsObjectTagEXT");
	if (!context->pfnSetDebugUtilsObjectTagEXT) {
		KWARN("Unable to load function pointer for vkSetDebugUtilsObjectTagEXT. Debug functions associated with this will not work.");
	}

	context->pfnCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkCmdBeginDebugUtilsLabelEXT");
	if (!context->pfnCmdBeginDebugUtilsLabelEXT) {
		KWARN("Unable to load function pointer for vkCmdBeginDebugUtilsLabelEXT. Debug functions associated with this will not work.");
	}

	context->pfnCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkCmdEndDebugUtilsLabelEXT");
	if (!context->pfnCmdEndDebugUtilsLabelEXT) {
		KWARN("Unable to load function pointer for vkCmdEndDebugUtilsLabelEXT. Debug functions associated with this will not work.");
	}

	// Device creation
	if (!vulkan_device_create(context, config->require_discrete_gpu)) {
		KERROR("Failed to create device!");
		return false;
	}

	// Textures array. Matches array size in texture system.
	context->max_texture_count = config->max_texture_count;
	context->textures = KALLOC_TYPE_CARRAY(vulkan_texture_handle_data, config->max_texture_count);

	// Samplers array.
	context->samplers = darray_create(vulkan_sampler_handle_data);

	// Shaders array.
	context->shaders = darray_reserve(vulkan_shader, config->max_shader_count);

	// Create a shader compiler to be used.
	context->shader_compiler = shaderc_compiler_initialize();

	KINFO("Renderer config requests %s-buffering to be used.", config->use_triple_buffering ? "triple" : "double");
	context->triple_buffering_enabled = config->use_triple_buffering;

	context->renderbuffers = darray_create(vulkan_buffer);

	context->standard_vertex_buffer_name = kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD);
	context->index_buffer_name = kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD);

	context->bound_shader = KSHADER_INVALID;

	u8 max_frames_in_flight = context->triple_buffering_enabled ? 2 : 1;

	// Setup querypool for timestamp collection.
	VkQueryPoolCreateInfo timestamp_pool_info = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = max_frames_in_flight * KGPU_PROFILE_MAX_TIMESTAMPS * 2};
	VK_CHECK(rhi->kvkCreateQueryPool(context->device.logical_device, &timestamp_pool_info, context->allocator, &context->query_pool));

	KINFO("Vulkan renderer initialized successfully.");
	return true;
}

void vulkan_renderer_backend_shutdown (renderer_backend_interface *backend) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	rhi->kvkDeviceWaitIdle(context->device.logical_device);

	// Destroy the query pool.
	rhi->kvkDestroyQueryPool(context->device.logical_device, context->query_pool, context->allocator);

	// Destroy the runtime shader compiler.
	if (context->shader_compiler) {
		shaderc_compiler_release(context->shader_compiler);
		context->shader_compiler = 0;
	}

	KDEBUG("Destroying Vulkan device...");
	vulkan_device_destroy(context);

	darray_destroy(context->shaders);
	context->shaders = KNULL;
	darray_destroy(context->samplers);
	context->samplers = KNULL;
	kfree(context->textures);
	context->textures = KNULL;

	// Cleanup backend of renderbuffer data.
	u32 rbcount = darray_length(context->renderbuffers);
	for (u32 i = 0; i < rbcount; ++i) {
		if (context->renderbuffers[i].infos) {
			kfree(context->renderbuffers[i].infos);
		}
	}
	darray_destroy(context->renderbuffers);

	if (context->validation_enabled) {
		KDEBUG("Destroying Vulkan debugger...");
		if (context->debug_messenger) {
			PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)rhi->kvkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT");
			func(context->instance, context->debug_messenger, context->allocator);
		}
	}

	KDEBUG("Destroying Vulkan instance...");
	rhi->kvkDestroyInstance(context->instance, context->allocator);

	// Destroy the allocator callbacks if set.
	if (context->allocator) {
		kfree_aligned(context->allocator);
		context->allocator = 0;
	}

	// Shutdown the platform-specific items.
	vulkan_loader_shutdown(&context->rhi);

	// Free the context last.
	if (backend->internal_context) {
		kfree_aligned(backend->internal_context);
		backend->internal_context_size = 0;
		backend->internal_context = KNULL;
	}
}

b8 vulkan_renderer_on_window_created (renderer_backend_interface *backend, kwindow *window) {
	KASSERT(backend && window);

	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	kwindow_renderer_state *window_internal = window->renderer_state;

	// Setup backend-specific state for the window.
	window_internal->backend_state = kallocate(sizeof(kwindow_renderer_backend_state), MEMORY_TAG_RENDERER);
	kwindow_renderer_backend_state *window_backend = window_internal->backend_state;

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
	if (!vulkan_device_detect_depth_format(context, &context->device)) {
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
		window_backend->acquire_semaphores = KALLOC_TYPE_CARRAY(VkSemaphore, window_backend->max_frames_in_flight);
		// Create submission semaphores based on the number of swapchain images.
		window_backend->submit_semaphores = KALLOC_TYPE_CARRAY(VkSemaphore, window_backend->swapchain.image_count);
		window_backend->in_flight_fences = KALLOC_TYPE_CARRAY(VkFence, window_backend->max_frames_in_flight);

		window_backend->frame_texture_updated_list = KALLOC_TYPE_CARRAY(ktexture *, window_backend->max_frames_in_flight);
		window_backend->graphics_command_buffers = KALLOC_TYPE_CARRAY(vulkan_command_buffer, window_backend->max_frames_in_flight);

		// The staging buffer also goes here since it is tied to the frame.
		// TODO: Reduce this to a single buffer split by max_frames_in_flight.
		const u64 staging_buffer_size = MEBIBYTES(768); // FIXME: This is huge. Need to queue updates per frame in flight to shrink this down.
		window_backend->staging = kallocate(sizeof(krenderbuffer) * window_backend->max_frames_in_flight, MEMORY_TAG_ARRAY);

		for (u8 i = 0; i < window_backend->swapchain.image_count; ++i) {
			VkSemaphoreCreateInfo semaphore_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			VK_CHECK(rhi->kvkCreateSemaphore(context->device.logical_device, &semaphore_create_info, context->allocator, &window_backend->submit_semaphores[i]));
		}

		for (u8 i = 0; i < window_backend->max_frames_in_flight; ++i) {
			VkSemaphoreCreateInfo semaphore_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			VK_CHECK(rhi->kvkCreateSemaphore(context->device.logical_device, &semaphore_create_info, context->allocator, &window_backend->acquire_semaphores[i]));

			// Create the fence in a signaled state, indicating that the first frame has
			// already been "rendered". This will prevent the application from waiting
			// indefinitely for the first frame to render since it cannot be rendered
			// until a frame is "rendered" before it.
			VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK(rhi->kvkCreateFence(context->device.logical_device, &fence_create_info, context->allocator, &window_backend->in_flight_fences[i]));

			VK_SET_DEBUG_OBJECT_NAME_INDEXED(context, VK_OBJECT_TYPE_FENCE, window_backend->in_flight_fences[i], "in_flight_fences_", i);

			// Staging buffer.
			// TODO: Reduce this to a single buffer split by max_frames_in_flight.
			char *buf_name = string_format("window_staging_%u", i);
			window_backend->staging[i] = renderer_renderbuffer_create(backend->frontend_state, kname_create(buf_name), RENDERBUFFER_TYPE_STAGING, staging_buffer_size, RENDERBUFFER_TRACK_TYPE_LINEAR, RENDERBUFFER_FLAG_NONE);
			string_free(buf_name);
			if (window_backend->staging[i] == KRENDERBUFFER_INVALID) {
				KERROR("Failed to create staging buffer.");
				return false;
			}

			// Create the per-frame list of updated texture handles.
			window_backend->frame_texture_updated_list[i] = darray_create(ktexture);

			// Command buffer.
			vulkan_command_buffer *primary_buffer = &window_backend->graphics_command_buffers[i];
			kzero_memory(primary_buffer, sizeof(vulkan_command_buffer));

			// Allocate a new buffer.
			char *name = string_format("%s_command_buffer_%d", window->name, i);

			// Primary command buffers have secondary command buffers to facilitate "passes", of sorts.
			// TODO: should this be configurable?
			const u32 secondary_count = 16;

			vulkan_command_buffer_allocate(context, context->device.graphics_command_pool, true, name, primary_buffer, secondary_count);
			string_free(name);

			KDEBUG("Vulkan command buffers created.")
		}
	}

	// If there is not yet a current window, assign it now.
	if (!context->current_window) {
		context->current_window = window;
	}

	return true;
}

void vulkan_renderer_on_window_destroyed (renderer_backend_interface *backend, kwindow *window) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	kwindow_renderer_state *window_internal = window->renderer_state;
	kwindow_renderer_backend_state *window_backend = window_internal->backend_state;

	rhi->kvkDeviceWaitIdle(context->device.logical_device);

	// Per-swapchain-image resources.
	for (u32 i = 0; i < window_backend->swapchain.image_count; ++i) {
		if (window_backend->submit_semaphores[i]) {
			rhi->kvkDestroySemaphore(context->device.logical_device, window_backend->submit_semaphores[i], context->allocator);
			window_backend->submit_semaphores[i] = 0;
		}
	}
	kfree(window_backend->submit_semaphores);
	window_backend->submit_semaphores = 0;

	// Destroy per-frame-in-flight resources.
	{
		for (u32 i = 0; i < window_backend->max_frames_in_flight; ++i) {
			// Destroy staging buffers
			renderer_renderbuffer_destroy(backend->frontend_state, window_backend->staging[i]);

			// Sync objects
			if (window_backend->acquire_semaphores[i]) {
				rhi->kvkDestroySemaphore(context->device.logical_device, window_backend->acquire_semaphores[i], context->allocator);
				window_backend->acquire_semaphores[i] = 0;
			}

			rhi->kvkDestroyFence(context->device.logical_device, window_backend->in_flight_fences[i], context->allocator);

			// Command buffers
			if (window_backend->graphics_command_buffers[i].handle) {
				vulkan_command_buffer_free(context, context->device.graphics_command_pool, &window_backend->graphics_command_buffers[i]);
				window_backend->graphics_command_buffers[i].handle = KNULL;
			}

			darray_destroy(window_backend->frame_texture_updated_list[i]);
		}
		kfree(window_backend->acquire_semaphores);
		window_backend->acquire_semaphores = KNULL;

		kfree(window_backend->in_flight_fences);
		window_backend->in_flight_fences = KNULL;

		kfree(window_backend->staging);
		window_backend->staging = KNULL;

		kfree(window_backend->graphics_command_buffers);
		window_backend->graphics_command_buffers = KNULL;

		kfree(window_backend->frame_texture_updated_list);
		window_backend->frame_texture_updated_list = KNULL;
	}

	// Swapchain
	KDEBUG("Destroying Vulkan swapchain for window '%s'...", window->name);
	vulkan_swapchain_destroy(backend, &window_backend->swapchain);

	KDEBUG("Destroying Vulkan surface for window '%s'...", window->name);
	if (window_backend->surface) {
		rhi->kvkDestroySurfaceKHR(context->instance, window_backend->surface, context->allocator);
		window_backend->surface = KNULL;
	}

	// Free the backend state.
	kfree(window_internal->backend_state);
	window_internal->backend_state = KNULL;
}

void vulkan_renderer_backend_on_window_resized (renderer_backend_interface *backend, const kwindow *window) {
	// Cold-cast the context
	/* vulkan_context* context = (vulkan_context*)backend->internal_context; */
	kwindow_renderer_backend_state *backend_window = window->renderer_state->backend_state;
	// Update the "framebuffer size generation", a counter which indicates when
	// the framebuffer size has been updated.
	backend_window->framebuffer_size_generation++;

	KINFO("Vulkan renderer backend->resized: w/h/gen: %i/%i/%llu", window->width, window->height, backend_window->framebuffer_size_generation);
}

void vulkan_renderer_begin_debug_label (renderer_backend_interface *backend, const char *label_text, vec3 colour) {
	VK_BEGIN_DEBUG_LABEL(backend->internal_context, get_current_command_buffer(backend->internal_context)->handle, label_text, ((vec4){colour.r, colour.g, colour.b, 1.0f}));
}

void vulkan_renderer_end_debug_label (renderer_backend_interface *backend) {
	VK_END_DEBUG_LABEL(backend->internal_context, get_current_command_buffer(backend->internal_context)->handle);
}

b8 vulkan_renderer_frame_prepare (renderer_backend_interface *backend, struct frame_data *p_frame_data) {
	// NOTE: this is an intentional no-op in this backend.
	return true;
}

b8 vulkan_renderer_frame_prepare_window_surface (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_device *device = &context->device;

	kwindow_renderer_state *window_internal = window->renderer_state;
	kwindow_renderer_backend_state *window_backend = window_internal->backend_state;

	// Check if recreating swap chain and boot out.
	if (window_backend->recreating_swapchain) {
		VkResult result = rhi->kvkDeviceWaitIdle(device->logical_device);
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
		VkResult result = rhi->kvkDeviceWaitIdle(device->logical_device);
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
			// FIXME: Check this on all platforms, but maybe swapchain recreation wasn't needed here after all?
			// if (!recreate_swapchain(backend, window)) {
			// 	return false;
			// }
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
	VkResult result = rhi->kvkWaitForFences(
		context->device.logical_device, 1,
		&window_backend->in_flight_fences[window_backend->current_frame], true, U64_MAX);
	if (!vulkan_result_is_success(result)) {
		KFATAL("In-flight fence wait failure! error: %s", vulkan_result_string(result, true));
		return false;
	}

	// Increment texture generations in list of handles updated within frame workload.
	ktexture *updated_textures = context->current_window->renderer_state->backend_state->frame_texture_updated_list[window_backend->current_frame];
	u32 updated_texture_count = 0;
	for (u32 i = 0; i < updated_texture_count; ++i) {
		vulkan_texture_handle_data *texture = &context->textures[updated_textures[i]];
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
	result = rhi->kvkAcquireNextImageKHR(
		context->device.logical_device,
		window_backend->swapchain.handle,
		U64_MAX,
		window_backend->acquire_semaphores[window_backend->current_frame],
		0,
		&window_backend->swapchain.image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		// FIXME: Check this on all platforms, but maybe swapchain recreation wasn't needed here after all?
		// // Trigger swapchain recreation, then boot out of the render loop.
		// if (!vulkan_swapchain_recreate(backend, window, &window_backend->swapchain)) {
		// 	KFATAL("Failed to recreate swapchain.");
		// }
		return false;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		KFATAL("Failed to acquire swapchain image!");
		return false;
	}

	// Reset the fence for use on the next frame
	VK_CHECK(rhi->kvkResetFences(context->device.logical_device, 1, &window_backend->in_flight_fences[window_backend->current_frame]));

	// Reset staging buffer.
	if (!renderer_renderbuffer_clear(backend->frontend_state, window_backend->staging[window_backend->current_frame], false)) {
		KERROR("Failed to clear staging buffer.");
		return false;
	}

	return true;
}

b8 vulkan_renderer_frame_command_list_begin (renderer_backend_interface *backend, struct frame_data *p_frame_data) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	// Begin recording commands.
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	vulkan_command_buffer_reset(command_buffer);
	vulkan_command_buffer_begin(context, command_buffer, true, false, false);

	// Setup a pipeline barrier to ensure all vertex updates have happened
	// on the previous frame before trying to read it.
	{
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(backend->frontend_state, context->standard_vertex_buffer_name);
		vulkan_buffer *internal_vertex_buffer = &context->renderbuffers[vertex_buffer];
		u8 index = internal_vertex_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkBufferMemoryBarrier vertex_buffer_barrier = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = NULL,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = internal_vertex_buffer->infos[index].handle,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		};

		context->rhi.kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,	 // srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT, // dstStageMask
			0,																 // dependencyFlags
			0, NULL,														 // pMemoryBarriers
			1, &vertex_buffer_barrier,										 // pBufferMemoryBarriers
			0, NULL															 // pImageMemoryBarriers
		);
	}

	return true;
}

b8 vulkan_renderer_frame_command_list_end (renderer_backend_interface *backend, struct frame_data *p_frame_data) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	kwindow_renderer_backend_state *window_backend = context->current_window->renderer_state->backend_state;
	// Source is the window's colour buffer texture.
	ktexture colourbuffer_handle = context->current_window->renderer_state->colourbuffer;
	vulkan_texture_handle_data *source_image_handle = &context->textures[colourbuffer_handle];
	vulkan_image *source_image = &source_image_handle->images[window_backend->image_index];
	// Target is the current swapchain image.
	vulkan_texture_handle_data *target_image_handle = &context->textures[window_backend->swapchain.swapchain_colour_texture];
	vulkan_image *target_image = &target_image_handle->images[window_backend->swapchain.image_index];

	VkImageMemoryBarrier before_barriers[3];
	kzero_memory(before_barriers, sizeof(VkImageMemoryBarrier) * 3);
	// Need a barrier to ensure all previous writes are complete.
	{
		VkImageMemoryBarrier *barrier = &before_barriers[0];
		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier->newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->image = source_image->handle;
		barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// Mips
		barrier->subresourceRange.baseMipLevel = 0;
		barrier->subresourceRange.levelCount = source_image->mip_levels;
		barrier->subresourceRange.baseArrayLayer = 0;
		barrier->subresourceRange.layerCount = source_image->layer_count;

		barrier->srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier->dstAccessMask = 0;
	}

	// Before ending the command buffer, blit the current colour buffer's contents to
	// the current swapchain image. Start by transitioning to transfer source layout.
	{
		VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, "window colour->transfer_src", vec4_one());
		VkImageMemoryBarrier *barrier = &before_barriers[1];
		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT; // VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
		barrier->dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier->oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier->newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier->image = source_image->handle;
		barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier->subresourceRange.baseArrayLayer = 0;
		barrier->subresourceRange.layerCount = source_image->layer_count;
		barrier->subresourceRange.baseMipLevel = 0;
		barrier->subresourceRange.levelCount = source_image->mip_levels;

		VK_END_DEBUG_LABEL(context, command_buffer->handle);
	}

	// Transition the swapchain image to transfer destination layout.
	{
		VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, "swapchain img->transfer_dst", vec4_one());
		VkImageMemoryBarrier *barrier = &before_barriers[2];
		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier->dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Throw out previous data
		barrier->newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier->image = target_image->handle;
		barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier->subresourceRange.baseArrayLayer = 0;
		barrier->subresourceRange.layerCount = target_image->layer_count;
		barrier->subresourceRange.baseMipLevel = 0;
		barrier->subresourceRange.levelCount = target_image->mip_levels;
		VK_END_DEBUG_LABEL(context, command_buffer->handle);
	}

	rhi->kvkCmdPipelineBarrier(
		command_buffer->handle,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, 0, 0, 0, 3, before_barriers);

	// Now do the blit operation from the source image to the target image
	{
		VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, "blit", vec4_one());
		VkImageBlit blit_region = {};
		blit_region.srcOffsets[0] = (VkOffset3D){0, 0, 0};										// Starting coordinates in the source image
		blit_region.srcOffsets[1] = (VkOffset3D){source_image->width, source_image->height, 1}; // Ending coordinates in the source image
		blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit_region.srcSubresource.baseArrayLayer = 0;
		blit_region.srcSubresource.layerCount = source_image->layer_count;
		blit_region.srcSubresource.mipLevel = 0;

		blit_region.dstOffsets[0] = (VkOffset3D){0, 0, 0};										// Starting coordinates in the swapchain image
		blit_region.dstOffsets[1] = (VkOffset3D){target_image->width, target_image->height, 1}; // Ending coordinates in the swapchain image
		blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit_region.dstSubresource.baseArrayLayer = 0;
		blit_region.dstSubresource.layerCount = target_image->layer_count;
		blit_region.dstSubresource.mipLevel = 0;

		// Perform the blit operation
		rhi->kvkCmdBlitImage(
			command_buffer->handle,
			source_image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target_image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit_region,
			VK_FILTER_LINEAR);
		VK_END_DEBUG_LABEL(context, command_buffer->handle);
	}

	VkImageMemoryBarrier after_image_barriers[2];
	kzero_memory(after_image_barriers, sizeof(VkImageMemoryBarrier) * 2);
	VkBufferMemoryBarrier after_buffer_barriers[2];
	kzero_memory(after_buffer_barriers, sizeof(VkBufferMemoryBarrier) * 2);

	// Transition source back to the correct layout for rendering to
	{
		VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, "window colour->att. opt.", vec4_one());
		VkImageMemoryBarrier *barrier = &after_image_barriers[0];
		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier->dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // VK_ACCESS_MEMORY_WRITE_BIT;
		barrier->oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier->newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier->image = source_image->handle;
		barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier->subresourceRange.baseArrayLayer = 0;
		barrier->subresourceRange.layerCount = source_image->layer_count;
		barrier->subresourceRange.baseMipLevel = 0;
		barrier->subresourceRange.levelCount = source_image->mip_levels;
		VK_END_DEBUG_LABEL(context, command_buffer->handle);
	}

	// Transition target for presentation.
	{
		VK_BEGIN_DEBUG_LABEL(context, command_buffer->handle, "swapchain img->present", vec4_one());
		VkImageMemoryBarrier *barrier = &after_image_barriers[1];
		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier->dstAccessMask = 0;
		barrier->oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier->newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier->image = target_image->handle;
		barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier->subresourceRange.baseArrayLayer = 0;
		barrier->subresourceRange.layerCount = target_image->layer_count;
		barrier->subresourceRange.baseMipLevel = 0;
		barrier->subresourceRange.levelCount = target_image->mip_levels;

		VK_END_DEBUG_LABEL(context, command_buffer->handle);
	}

	// Barrier for standard vertex buffer
	{
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(backend->frontend_state, context->standard_vertex_buffer_name);
		vulkan_buffer *internal_vertex_buffer = &context->renderbuffers[vertex_buffer];
		u8 index = internal_vertex_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkBufferMemoryBarrier *barrier = &after_buffer_barriers[0];
		barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->buffer = internal_vertex_buffer->infos[index].handle;
		barrier->offset = 0;
		barrier->size = VK_WHOLE_SIZE;
		barrier->srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier->dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	}

	// Barrier for index buffer
	{
		krenderbuffer index_buffer = renderer_renderbuffer_get(backend->frontend_state, context->index_buffer_name);
		vulkan_buffer *internal_index_buffer = &context->renderbuffers[index_buffer];
		u8 index = internal_index_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkBufferMemoryBarrier *barrier = &after_buffer_barriers[1];
		barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->buffer = internal_index_buffer->infos[index].handle;
		barrier->offset = 0;
		barrier->size = VK_WHOLE_SIZE;
		barrier->srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier->dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}

	rhi->kvkCmdPipelineBarrier(
		command_buffer->handle,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, 0, 0, 2, after_buffer_barriers, 2, after_image_barriers);

	// Just end the command buffer.
	vulkan_command_buffer_end(context, command_buffer);

	// Increment (and wrap) the colour buffer image index.
	context->current_window->renderer_state->backend_state->image_index =
		((context->current_window->renderer_state->backend_state->image_index + 1) % (context->triple_buffering_enabled ? 3 : 2));

	return true;
}

b8 vulkan_renderer_frame_submit (struct renderer_backend_interface *backend, struct frame_data *p_frame_data) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	kwindow_renderer_backend_state *window_backend = context->current_window->renderer_state->backend_state;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Only a primary command buffer should be submitted.
	if (!command_buffer->is_primary) {
		KFATAL("vulkan_renderer_frame_submit tried to submit Secondary command buffers. This must not happen.");
		return false;
	}

	// Submit the command buffer for execution.
	b8 result = vulkan_command_buffer_submit(
		context,
		command_buffer,
		context->device.graphics_queue,
		1,
		// The semaphore(s) to be signaled when the queue is complete, based on the swapchain image index.
		&window_backend->submit_semaphores[window_backend->swapchain.image_index],
		1,
		// Wait semaphore ensures that the operation cannot begin until the image is available.
		&window_backend->acquire_semaphores[window_backend->current_frame],
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

b8 vulkan_renderer_frame_present (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	/* kwindow_renderer_backend_state* window_backend = context->current_window->renderer_state->backend_state; */
	kwindow_renderer_backend_state *window_backend = window->renderer_state->backend_state;

	// Return the image to the swapchain for presentation.
	VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &window_backend->submit_semaphores[window_backend->swapchain.image_index]; // based on swapchain image index.
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &window_backend->swapchain.handle;
	present_info.pImageIndices = &window_backend->swapchain.image_index;
	present_info.pResults = 0;
	VkResult result = rhi->kvkQueuePresentKHR(context->device.present_queue, &present_info);
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

void vulkan_renderer_viewport_set (renderer_backend_interface *backend, rect_2di rect) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	// Dynamic state
	VkViewport viewport;
	viewport.x = rect.x;
	viewport.y = rect.y;
	viewport.width = rect.width;
	viewport.height = rect.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	rhi->kvkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
}

void vulkan_renderer_viewport_reset (renderer_backend_interface *backend) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	// Just set the current viewport rect.
	vulkan_renderer_viewport_set(backend, context->viewport_rect);
}

void vulkan_renderer_scissor_set (renderer_backend_interface *backend, rect_2di rect) {
	KASSERT_DEBUG(rect.width && rect.height);

	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	VkRect2D scissor;
	scissor.offset.x = rect.x;
	scissor.offset.y = rect.y;
	scissor.extent.width = rect.z;
	scissor.extent.height = rect.w;

	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	rhi->kvkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
}

void vulkan_renderer_scissor_reset (renderer_backend_interface *backend) {
	KASSERT_MSG(false, "don't do that ya dingus");
	// Cold-cast the context
	/* vulkan_context* context = (vulkan_context*)backend->internal_context;
	// Just set the current scissor rect.
	vulkan_renderer_scissor_set(backend, context->scissor_rect); */
}

void vulkan_renderer_winding_set (struct renderer_backend_interface *backend, renderer_winding winding) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	VkFrontFace vk_winding = winding == RENDERER_WINDING_COUNTER_CLOCKWISE ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetFrontFace(command_buffer->handle, vk_winding);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetFrontFaceEXT(command_buffer->handle, vk_winding);
	} else {
		KFATAL("renderer_winding_set cannot be used on a device without dynamic state support.");
	}
}

void vulkan_renderer_cull_mode_set (struct renderer_backend_interface *backend, renderer_cull_mode cull_mode) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

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
		rhi->kvkCmdSetCullMode(command_buffer->handle, vulkan_cull_mode);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetCullModeEXT(command_buffer->handle, vulkan_cull_mode);
	} else {
		KFATAL("renderer_cull_mode_set cannot be used on a device without dynamic state support.");
	}
}

static VkStencilOp vulkan_renderer_get_stencil_op (renderer_stencil_op op) {
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

static VkCompareOp vulkan_renderer_get_compare_op (renderer_compare_op op) {
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

void vulkan_renderer_set_stencil_test_enabled (struct renderer_backend_interface *backend, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetStencilTestEnable(command_buffer->handle, (VkBool32)enabled);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetStencilTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
	} else {
		KFATAL("renderer_set_stencil_test_enabled cannot be used on a device without dynamic state support.");
	}
}

void vulkan_renderer_set_depth_test_enabled (struct renderer_backend_interface *backend, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetDepthTestEnable(command_buffer->handle, (VkBool32)enabled);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetDepthTestEnableEXT(command_buffer->handle, (VkBool32)enabled);
	} else {
		KFATAL("renderer_set_depth_test_enabled cannot be used on a device without dynamic state support.");
	}
}

void vulkan_renderer_set_depth_write_enabled (struct renderer_backend_interface *backend, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetDepthWriteEnable(command_buffer->handle, (VkBool32)enabled);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetDepthWriteEnableEXT(command_buffer->handle, (VkBool32)enabled);
	} else {
		KFATAL("renderer_set_depth_write_enabled cannot be used on a device without dynamic state support.");
	}
}

void vulkan_renderer_set_depth_bias (struct renderer_backend_interface *backend, f32 constant_factor, f32 clamp, f32 slope_factor) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	rhi->kvkCmdSetDepthBias(command_buffer->handle, constant_factor, clamp, slope_factor);
}

void vulkan_renderer_set_depth_bias_enabled (struct renderer_backend_interface *backend, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	rhi->kvkCmdSetDepthBiasEnable(command_buffer->handle, enabled);
}

void vulkan_renderer_set_stencil_reference (struct renderer_backend_interface *backend, u32 reference) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	rhi->kvkCmdSetStencilReference(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void vulkan_renderer_set_stencil_op (struct renderer_backend_interface *backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetStencilOp(
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

void vulkan_renderer_begin_rendering (struct renderer_backend_interface *backend, frame_data *p_frame_data, rect_2di render_area, u32 colour_target_count, ktexture *colour_targets, ktexture depth_stencil_target, u32 depth_stencil_layer) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *primary = get_current_command_buffer(context);
	u32 image_index = get_current_image_index(context);

	// Anytime we "begin" a render, update the "in-secondary" state and get the appropriate secondary buffer.
	primary->in_secondary = true;
	vulkan_command_buffer *secondary = get_current_command_buffer(context);
	vulkan_command_buffer_begin(context, secondary, false, false, false);

	VkRenderingInfo render_info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
	render_info.renderArea.offset.x = render_area.x;
	render_info.renderArea.offset.y = render_area.y;
	render_info.renderArea.extent.width = render_area.width;
	render_info.renderArea.extent.height = render_area.height;

	// TODO: This may be a problem for layered images/cubemaps
	render_info.layerCount = 1;

	VkImageMemoryBarrier *attachment_image_barriers = darray_create_with_allocator(VkImageMemoryBarrier, &p_frame_data->allocator);

	// Depth
	VkRenderingAttachmentInfoKHR depth_attachment_info = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
	if (depth_stencil_target != INVALID_KTEXTURE) {
		vulkan_texture_handle_data *depth_stencil_data = &context->textures[depth_stencil_target];
		vulkan_image *image = &depth_stencil_data->images[image_index];

		depth_attachment_info.imageView = image->view;
		if (image->layer_count > 1) {
			depth_attachment_info.imageView = image->layer_views[depth_stencil_layer];
		}

		depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;	  // Always load.
		depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Always store.
		depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
		depth_attachment_info.resolveImageView = 0;
		if (image->flags & KTEXTURE_FLAG_DEPTH) {
			render_info.pDepthAttachment = &depth_attachment_info;
		} else {
			render_info.pDepthAttachment = 0;
		}
		if (image->flags & KTEXTURE_FLAG_STENCIL) {
			render_info.pStencilAttachment = &depth_attachment_info;
		} else {
			render_info.pStencilAttachment = 0;
		}

		// Need a barrier to ensure all previous writes are complete.
		VkImageMemoryBarrier barrier = {0};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image->handle;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		// Mips
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = image->mip_levels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = image->layer_count;

		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

		darray_push(attachment_image_barriers, &barrier);

	} else {
		render_info.pDepthAttachment = 0;
		render_info.pStencilAttachment = 0;
	}

	render_info.colorAttachmentCount = colour_target_count;
	if (colour_target_count) {
		// NOTE: this memory won't be leaked because it uses the frame allocator, which is reset per frame.
		VkRenderingAttachmentInfo *colour_attachments = p_frame_data->allocator.allocate(sizeof(VkRenderingAttachmentInfo) * colour_target_count);
		// VkImageMemoryBarrier colour_barriers[32] = {0};
		for (u32 i = 0; i < colour_target_count; ++i) {
			vulkan_texture_handle_data *colour_target_data = &context->textures[colour_targets[i]];
			vulkan_image *image = &colour_target_data->images[image_index];

			VkRenderingAttachmentInfo *attachment_info = &colour_attachments[i];
			attachment_info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment_info->imageView = image->view;
			attachment_info->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_info->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;	 // Always load.
			attachment_info->storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Always store.
			kzero_memory(attachment_info->clearValue.color.float32, sizeof(f32) * 4);
			attachment_info->resolveMode = VK_RESOLVE_MODE_NONE;
			attachment_info->resolveImageView = 0;
			attachment_info->resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment_info->pNext = 0;

			// Need a barrier to ensure all previous writes are complete.
			VkImageMemoryBarrier barrier = {0};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image->handle;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			// Mips
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = image->mip_levels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = image->layer_count;

			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			darray_push(attachment_image_barriers, &barrier);
		}
		render_info.pColorAttachments = colour_attachments;
	} else {
		render_info.pColorAttachments = 0;
	}

	u32 barrier_count = darray_length(attachment_image_barriers);
	if (barrier_count) {
		rhi->kvkCmdPipelineBarrier(
			secondary->handle,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0,
			0, 0,
			0, 0,
			barrier_count, attachment_image_barriers);
	}

	// Kick off the render using the secondary buffer.
	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdBeginRendering(secondary->handle, &render_info);
	} else {
		context->vkCmdBeginRenderingKHR(secondary->handle, &render_info);
	}
}

void vulkan_renderer_end_rendering (struct renderer_backend_interface *backend, frame_data *p_frame_data) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	// Since ending a rendering, will be in a secondary buffer.
	vulkan_command_buffer *secondary = get_current_command_buffer(context);

	// End rendering
	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdEndRendering(secondary->handle);
	} else {
		context->vkCmdEndRenderingKHR(secondary->handle);
	}

	// End secondary command buffer.
	vulkan_command_buffer_end(context, secondary);

	VkBufferMemoryBarrier buffer_barriers[2];
	kzero_memory(buffer_barriers, sizeof(VkBufferMemoryBarrier) * 2);
	// Barrier for standard vertex buffer
	{
		krenderbuffer vertex_buffer = renderer_renderbuffer_get(backend->frontend_state, context->standard_vertex_buffer_name);
		vulkan_buffer *internal_vertex_buffer = &context->renderbuffers[vertex_buffer];
		u8 index = internal_vertex_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkBufferMemoryBarrier *barrier = &buffer_barriers[0];
		barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
		barrier->buffer = internal_vertex_buffer->infos[index].handle;
		barrier->offset = 0;
		barrier->size = VK_WHOLE_SIZE;
		barrier->srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		barrier->dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;	 // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}

	// Barrier for index buffer
	{
		krenderbuffer index_buffer = renderer_renderbuffer_get(backend->frontend_state, context->index_buffer_name);
		vulkan_buffer *internal_index_buffer = &context->renderbuffers[index_buffer];
		u8 index = internal_index_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkBufferMemoryBarrier *barrier = &buffer_barriers[1];
		barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // context->device.graphics_queue_index;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //  context->device.graphics_queue_index;
		barrier->buffer = internal_index_buffer->infos[index].handle;
		barrier->offset = 0;
		barrier->size = VK_WHOLE_SIZE;
		barrier->srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; //| VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		barrier->dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;	 // | (is_depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}

	rhi->kvkCmdPipelineBarrier(
		secondary->parent->handle,
		VK_PIPELINE_STAGE_TRANSFER_BIT,		// _LATE_FRAGMENT_TESTS_BIT, //  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,    // VK_PIPELINE_STAGE_TRANSFER_BIT
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // _FRAGMENT_SHADER_BIT,     // VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
		0,
		0, 0,
		2, buffer_barriers,
		0, 0);

	// Execute secondary command buffer.
	vulkan_command_buffer_execute_secondary(context, secondary);
}

void vulkan_renderer_set_stencil_compare_mask (struct renderer_backend_interface *backend, u32 compare_mask) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Supported as of vulkan 1.0, so no need to check for dynamic state support.
	rhi->kvkCmdSetStencilCompareMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, compare_mask);
}

void vulkan_renderer_set_stencil_write_mask (struct renderer_backend_interface *backend, u32 write_mask) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Supported as of vulkan 1.0, so no need to check for dynamic state support.
	rhi->kvkCmdSetStencilWriteMask(command_buffer->handle, VK_STENCIL_FACE_FRONT_AND_BACK, write_mask);
}

void vulkan_renderer_clear_colour_set (renderer_backend_interface *backend, vec4 colour) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	// Clamp values.
	for (u8 i = 0; i < 4; ++i) {
		colour.elements[i] = KCLAMP(colour.elements[i], 0.0f, 1.0f);
	}

	// Cache the clear colour for the next colour clear operation.
	kcopy_memory(context->colour_clear_value.float32, colour.elements, sizeof(f32) * 4);
}

void vulkan_renderer_clear_depth_set (renderer_backend_interface *backend, f32 depth) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	// Ensure the value is blamped
	depth = KCLAMP(depth, 0.0f, 1.0f);
	// Cache the depth for the next depth clear operation.
	context->depth_stencil_clear_value.depth = depth;
}

void vulkan_renderer_clear_stencil_set (renderer_backend_interface *backend, u32 stencil) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	// Cache the depth for the next stencil clear operation.
	context->depth_stencil_clear_value.stencil = stencil;
}

void vulkan_renderer_clear_colour_texture (renderer_backend_interface *backend, ktexture t) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);
	vulkan_texture_handle_data *tex_internal = &context->textures[t];
	u32 image_index = get_current_image_index(context);

	// If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
	vulkan_image *image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];

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

		rhi->kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, 0,
			0, 0,
			1, &barrier);
	}

	// Clear the image.
	rhi->kvkCmdClearColorImage(
		command_buffer->handle,
		image->handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

		rhi->kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			0, 0,
			0, 0,
			1, &barrier);
	}
}

void vulkan_renderer_clear_depth_stencil (renderer_backend_interface *backend, ktexture t) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);
	u32 image_index = get_current_image_index(context);

	vulkan_texture_handle_data *tex_internal = &context->textures[t];

	// If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
	vulkan_image *image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];
	b8 is_depth = FLAG_GET(image->flags, KTEXTURE_FLAG_DEPTH);
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

		rhi->kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, 0,
			0, 0,
			1, &barrier);
	}

	// Clear the image.
	rhi->kvkCmdClearDepthStencilImage(
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
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // NOTE: may have to check if stencil
		barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
		barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
		barrier.image = image->handle;
		barrier.subresourceRange.aspectMask = aspect_flags;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = image->mip_levels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = image->layer_count;

		rhi->kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0,
			0, 0,
			0, 0,
			1, &barrier);
	}
}

void vulkan_renderer_colour_texture_prepare_for_present (renderer_backend_interface *backend, ktexture t) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);
	u32 image_index = get_current_image_index(context);

	vulkan_texture_handle_data *tex_internal = &context->textures[t];

	// If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
	vulkan_image *image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];

	// Transition the layout
	VkImageMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // VK_IMAGE_LAYOUT_UNDEFINED; // Throw away data
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.srcQueueFamilyIndex = context->device.graphics_queue_index;
	barrier.dstQueueFamilyIndex = context->device.graphics_queue_index;
	barrier.image = image->handle;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	// Mips
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = image->mip_levels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = image->layer_count;

	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

	rhi->kvkCmdPipelineBarrier(
		command_buffer->handle,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, 0,
		0, 0,
		1, &barrier);
}

void vulkan_renderer_texture_prepare_for_sampling (renderer_backend_interface *backend, ktexture t, ktexture_flag_bits flags) {
	// Cold-cast the context
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);
	u32 image_index = get_current_image_index(context);

	vulkan_texture_handle_data *tex_internal = &context->textures[t];

	// If a per-frame texture, get the appropriate image index. Otherwise it's just the first one.
	vulkan_image *image = tex_internal->image_count == 1 ? &tex_internal->images[0] : &tex_internal->images[image_index];
	b8 is_depth = FLAG_GET(image->flags, KTEXTURE_FLAG_DEPTH);
	// b8 is_stencil = FLAG_GET(image->flags, TEXTURE_FLAG_STENCIL);

	VkImageAspectFlags aspect_flags = 0;
	// aspect_flags |= is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
	// aspect_flags |= is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
	// HACK: Must use both because of the internal depth format containing stencil anyway.
	aspect_flags = is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : 0;
	if (!aspect_flags) {
		aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	{
		// Ensure any transfers are done.
		VkImageMemoryBarrier barrier = {0};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image->handle;
		barrier.subresourceRange.aspectMask = aspect_flags;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = image->mip_levels;
		barrier.subresourceRange.layerCount = image->layer_count;
		barrier.subresourceRange.baseArrayLayer = 0;

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

		rhi->kvkCmdPipelineBarrier(
			command_buffer->handle,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, 0,
			0, 0,
			1, &barrier);
	}

	// Transition the layout to be read in a shader
	VkImageMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 0; // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
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

	barrier.srcAccessMask =
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
							VK_ACCESS_TRANSFER_READ_BIT |
							VK_ACCESS_MEMORY_READ_BIT |
							VK_ACCESS_UNIFORM_READ_BIT |
							VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

	u32 src_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
						  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
						  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
						  VK_PIPELINE_STAGE_TRANSFER_BIT |
						  VK_PIPELINE_STAGE_HOST_BIT;

	rhi->kvkCmdPipelineBarrier(
		command_buffer->handle,
		src_stage_flags,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, 0,
		0, 0,
		1, &barrier);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback (VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
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

static b8 recreate_swapchain (renderer_backend_interface *backend, kwindow *window) {
	vulkan_context *context = backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	kwindow_renderer_state *window_internal = window->renderer_state;
	kwindow_renderer_backend_state *window_backend = window_internal->backend_state;

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
	rhi->kvkDeviceWaitIdle(context->device.logical_device);

	// Redetect the depth format.
	vulkan_device_detect_depth_format(context, &context->device);

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

static VkFormat channel_count_to_format (u8 channel_count, VkFormat default_format) {
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

b8 vulkan_renderer_texture_resources_acquire (renderer_backend_interface *backend, ktexture t, const char *name, ktexture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, ktexture_flag_bits flags) {

	if (flags & KTEXTURE_FLAG_IS_WRAPPED) {
		// If the texure is considered "wrapped" (i.e. internal resources are created somwhere else,
		// such as swapchain images), then nothing further is required. Just return the handle.
		return true;
	}

	vulkan_context *context = (vulkan_context *)backend->internal_context;

	// Get an entry into the lookup table.
	vulkan_texture_handle_data *texture_data = &context->textures[t];

	// Internal data creation.
	if (flags & KTEXTURE_FLAG_RENDERER_BUFFERING) {
		// Need to generate enough images to support triple-buffering.
		texture_data->image_count = VULKAN_RESOURCE_IMAGE_COUNT;
	} else {
		// Only one needed.
		texture_data->image_count = 1;
	}

	VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkImageAspectFlagBits aspect = 0;
	VkFormat image_format;
	b8 is_depth = FLAG_GET(flags, KTEXTURE_FLAG_DEPTH);
	b8 is_stencil = FLAG_GET(flags, KTEXTURE_FLAG_STENCIL);
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
		char *image_name = string_format("%s_vkimage_%d", name, i);
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

void vulkan_renderer_texture_resources_release (renderer_backend_interface *backend, ktexture t) {
	KASSERT_DEBUG_MSG(t != INVALID_KTEXTURE, "Invalid texture handle passed.");

	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_texture_handle_data *texture_data = &context->textures[t];

	// Release/destroy the internal data.
	if (texture_data->images && !FLAG_GET(texture_data->images[0].flags, KTEXTURE_FLAG_IS_WRAPPED)) {
		for (u32 i = 0; i < texture_data->image_count; ++i) {
			vulkan_image_destroy(context, &texture_data->images[i]);
		}
		kfree(texture_data->images);
	}
	texture_data->images = 0;
	texture_data->image_count = 0;
}

b8 vulkan_renderer_texture_resize (renderer_backend_interface *backend, ktexture t, u32 new_width, u32 new_height) {
	KASSERT_DEBUG_MSG(t != INVALID_KTEXTURE, "Invalid texture handle passed.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_texture_handle_data *texture_data = &context->textures[t];

	for (u32 i = 0; i < texture_data->image_count; ++i) {
		// Resizing is really just destroying the old image and creating a new one.
		// Data is not preserved because there's no reliable way to map the old data
		// to the new since the amount of data differs.
		vulkan_image *image = &texture_data->images[i];
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

b8 vulkan_renderer_texture_write_data (renderer_backend_interface *backend, ktexture t, u32 bpp, u32 px_x, u32 px_y, i32 layer, u32 width, u32 height, const u8 *pixels, b8 defer_to_next_frame) {

	KASSERT_DEBUG_MSG(t != INVALID_KTEXTURE, "Invalid texture handle passed.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_texture_handle_data *texture = &context->textures[t];

	// If no window, can't include in a frame workload.
	if (!context->current_window) {
		defer_to_next_frame = false;
	}

	// Temporary command buffer, if needed.
	vulkan_command_buffer temp_command_buffer;

	// A pointer to the staging buffer to be used.
	krenderbuffer staging = KRENDERBUFFER_INVALID;
	// A pointer to the command buffer to be used.
	vulkan_command_buffer *command_buffer = 0;
	u32 depth = texture->images[0].layer_count;
	u64 size = width * height * (depth ? depth : 1) * (bpp / 8);
	if (defer_to_next_frame) {
		// Including in the frame workload means the current window's current-frame staging buffer can be used.
		u32 current_frame = context->current_window->renderer_state->backend_state->current_frame;
		staging = context->current_window->renderer_state->backend_state->staging[current_frame];
		command_buffer = get_current_command_buffer(context);
	} else {
		// Not including in the frame workload means a temporary staging buffer needs to be created and bound.
		// This buffer is the exact size required for the operation, so no allocation is needed later.
		staging = renderer_renderbuffer_create(backend->frontend_state, kname_create("temp_staging"), RENDERBUFFER_TYPE_STAGING, size * texture->image_count, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_NONE);
	}
	for (u32 i = 0; i < texture->image_count; ++i) {
		vulkan_image *image = &texture->images[i];

		// Staging buffer.
		u64 staging_offset = 0;
		if (defer_to_next_frame) {
			// If including in frame workload, space needs to be allocated from the buffer.
			renderer_renderbuffer_allocate(backend->frontend_state, staging, size, &staging_offset);
		}

		// Results in a wait if not included in frame workload.
		vulkan_buffer_load_range(backend, staging, staging_offset, size, pixels, defer_to_next_frame);

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
		vulkan_image_copy_from_buffer(context, image, context->renderbuffers[staging].infos[0].handle, size, staging_offset, px_x, px_y, layer, width, height, command_buffer);

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

	if (!defer_to_next_frame) {
		renderer_renderbuffer_destroy(backend->frontend_state, staging);

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
		darray_push(context->current_window->renderer_state->backend_state->frame_texture_updated_list[current_frame], &t);
	}

	return true;
}

static b8 texture_read_offset_range (
	renderer_backend_interface *backend,
	vulkan_texture_handle_data *texture_data,
	u32 offset,
	u32 size,
	u32 x,
	u32 y,
	u32 width,
	u32 height,
	u8 **out_memory) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	if (texture_data) {
		// Always just use the first image for this operaton.
		vulkan_image *image = &texture_data->images[0];

		// NOTE: If offset or size are nonzero, read the entire image and select the offset and size in the range.
		if (offset || size) {
			x = y = 0;
			width = image->width;
			height = image->height;
		} else {
			// FIXME: Assuming RGBA/8bpp
			size = image->width * image->height * 4 * sizeof(u8);
		}

		// Create a staging buffer and load data into it.
		// TODO: global read buffer w/freelist (like staging), but for reading.
		krenderbuffer staging = renderer_renderbuffer_create(backend->frontend_state, kname_create("renderbuffer_texture_read_staging"), RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_NONE);
		if (staging == KRENDERBUFFER_INVALID) {
			KERROR("Failed to create staging buffer for texture read.");
			return false;
		}

		vulkan_command_buffer temp_buffer;
		VkCommandPool pool = context->device.graphics_command_pool;
		VkQueue queue = context->device.graphics_queue;
		vulkan_command_buffer_allocate_and_begin_single_use(context, pool, &temp_buffer);

		// NOTE: transition to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
		// Transition the layout from whatever it is currently to optimal for handing
		// out data.
		vulkan_image_transition_layout(context, &temp_buffer, image, image->format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		// Copy the data to the buffer.
		vulkan_image_copy_region_to_buffer(context, image, context->renderbuffers[staging].infos[0].handle, x, y, width, height, &temp_buffer);

		// Transition from optimal for data reading to shader-read-only optimal layout.
		// TODO: Should probably cache the previous layout and transfer back to that instead.
		vulkan_image_transition_layout(context, &temp_buffer, image, image->format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		vulkan_command_buffer_end_single_use(context, pool, &temp_buffer, queue);

		if (!vulkan_buffer_read(backend, staging, offset, size, (void **)out_memory)) {
			KERROR("vulkan_buffer_read failed.");
		}

		renderer_renderbuffer_unbind(backend->frontend_state, staging);
		renderer_renderbuffer_destroy(backend->frontend_state, staging);
		return true;
	}

	return false;
}

b8 vulkan_renderer_texture_read_data (renderer_backend_interface *backend, ktexture t, u32 offset, u32 size, u8 **out_pixels) {
	KASSERT_DEBUG_MSG(t != INVALID_KTEXTURE, "Invalid texture handle passed.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_texture_handle_data *texture_data = &context->textures[t];
	return texture_read_offset_range(backend, texture_data, offset, size, 0, 0, 0, 0, out_pixels);
}

b8 vulkan_renderer_texture_read_pixel (renderer_backend_interface *backend, ktexture t, u32 x, u32 y, u8 **out_rgba) {
	KASSERT_DEBUG_MSG(t != INVALID_KTEXTURE, "Invalid texture handle passed.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_texture_handle_data *texture_data = &context->textures[t];
	return texture_read_offset_range(backend, texture_data, 0, 0, x, y, 1, 1, out_rgba);
}

b8 vulkan_renderer_shader_create (
	renderer_backend_interface *backend,
	kshader shader,
	kname name,
	shader_flags flags,
	primitive_topology_type_bits topology_types,
	primitive_topology_type default_topology,
	u8 colour_attachment_count,
	kpixel_format *colour_attachment_formats,
	kpixel_format depth_attachment_format,
	kpixel_format stencil_attachment_format,
	u8 pipeline_count,
	shader_pipeline_config *pipelines,
	u8 binding_set_count,
	const shader_binding_set_config *binding_sets) {
	// Verify stage support before anything else.
	for (u8 pi = 0; pi < pipeline_count; ++pi) {
		shader_pipeline_config *pipeline = &pipelines[pi];
		for (u8 i = 0; i < pipeline->stage_count; ++i) {
			switch (pipeline->stages[i]) {
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
				KERROR("Unsupported stage type: %d", shader_stage_to_string(pipeline->stages[i]));
				break;
			}
		}
	}

	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	VkDevice logical_device = context->device.logical_device;
	VkAllocationCallbacks *vk_allocator = context->allocator;
	vulkan_shader *internal_shader = &context->shaders[shader];

	// Setup the internal shader.
	internal_shader->flags = flags;
	internal_shader->topology_types = topology_types;
	internal_shader->name = name;
	internal_shader->vertex_layout_index = INVALID_ID_U8;
	internal_shader->renderer_frame_number = INVALID_ID_U16;

	// Binding and descriptor sets.
	internal_shader->descriptor_set_count = binding_set_count;
	internal_shader->descriptor_set_configs = KALLOC_TYPE_CARRAY(vulkan_descriptor_set_config, binding_set_count);
	internal_shader->binding_set_states = KALLOC_TYPE_CARRAY(vulkan_shader_binding_set_state, binding_set_count);

	// Calculate the total number of descriptors and descriptor sets needed along the way.
	u32 total_descriptor_count[4] = {0}; // indexed by type
	u16 total_descriptor_set_count = 0;
	u64 total_uniform_buffer_size = 0;
	for (u8 i = 0; i < binding_set_count; ++i) {
		const shader_binding_set_config *bset_config = &binding_sets[i];

		vulkan_descriptor_set_config *dset_config = &internal_shader->descriptor_set_configs[i];
		vulkan_shader_binding_set_state *bset_state = &internal_shader->binding_set_states[i];

		// Descriptor set config.
		dset_config->binding_count = bset_config->binding_count;
		dset_config->bindings = KALLOC_TYPE_CARRAY(VkDescriptorSetLayoutBinding, bset_config->binding_count);

		bset_state->binding_count = bset_config->binding_count;
		bset_state->bindings = KALLOC_TYPE_CARRAY(vulkan_shader_binding, bset_state->binding_count);

		bset_state->texture_binding_count = 0;
		bset_state->sampler_binding_count = 0;
		bset_state->ssbo_binding_count = 0;

		// Setup set layout bindings
		for (u8 b = 0; b < bset_config->binding_count; ++b) {
			shader_binding_config *binding_config = &bset_config->bindings[b];

			u32 descriptor_count = 0;
			VkDescriptorType desc_type;
			VkShaderStageFlags stage_flags;
			u8 type_index = 0;
			switch (binding_config->binding_type) {
			case SHADER_BINDING_TYPE_UBO: {
				descriptor_count = 1; // NOTE: the whole UBO is one binding.
				desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				stage_flags = VK_SHADER_STAGE_ALL;
				type_index = 0;
			} break;
			case SHADER_BINDING_TYPE_SSBO: {
				descriptor_count = 1; // NOTE: the whole SSBO is one binding.
				desc_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				stage_flags = VK_SHADER_STAGE_ALL;
				type_index = bset_state->ssbo_binding_count;
				bset_state->ssbo_binding_count++;
			} break;
			case SHADER_BINDING_TYPE_TEXTURE: {
				desc_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				descriptor_count = KMAX(binding_config->array_size, 1); // Either treat as an array or a single texture, depending on what is passed in.
				stage_flags = VK_SHADER_STAGE_ALL;
				type_index = bset_state->texture_binding_count;
				bset_state->texture_binding_count++;
			} break;
			case SHADER_BINDING_TYPE_SAMPLER: {
				desc_type = VK_DESCRIPTOR_TYPE_SAMPLER;
				descriptor_count = KMAX(binding_config->array_size, 1); // Either treat as an array or a single sampler, depending on what is passed in.
				stage_flags = VK_SHADER_STAGE_ALL;
				type_index = bset_state->sampler_binding_count;
				bset_state->sampler_binding_count++;
			} break;
			case SHADER_BINDING_TYPE_COUNT:
				KFATAL("Why are you trying to bind this type, ya dingus?");
				return false;
			}

			dset_config->bindings[b].binding = b;
			dset_config->bindings[b].descriptorCount = descriptor_count;
			dset_config->bindings[b].descriptorType = desc_type;
			dset_config->bindings[b].stageFlags = stage_flags;

			// Setup a binding lookup
			bset_state->bindings[b].binding_type = binding_config->binding_type;
			bset_state->bindings[b].binding_type_index = type_index;

			// Track total descriptors needed.
			total_descriptor_count[binding_config->binding_type] += (descriptor_count * bset_config->max_instance_count * VULKAN_RESOURCE_IMAGE_COUNT);
		}

		// Binding set states.
		bset_state->max_instance_count = bset_config->max_instance_count;
		bset_state->instances = KALLOC_TYPE_CARRAY(vulkan_shader_binding_set_instance_state, bset_config->max_instance_count);

		// Make sure to count the number of sets needed. Account for triple-buffering.
		total_descriptor_set_count += (bset_state->max_instance_count * VULKAN_RESOURCE_IMAGE_COUNT);

		// Setup all instances
		for (u32 u = 0; u < bset_state->max_instance_count; ++u) {
			vulkan_shader_binding_set_instance_state *instance_state = &bset_state->instances[u];

#if KOHI_DEBUG
			instance_state->descriptor_set_index = i;
#endif

			if (bset_config->ssbo_count) {
				instance_state->ssbo_states = KALLOC_TYPE_CARRAY(vulkan_ssbo_state, bset_config->ssbo_count);
			}

			if (bset_config->sampler_count) {
				instance_state->sampler_states = KALLOC_TYPE_CARRAY(vulkan_sampler_state, bset_config->sampler_count);
			}

			if (bset_config->texture_count) {
				instance_state->texture_states = KALLOC_TYPE_CARRAY(vulkan_texture_state, bset_config->texture_count);
			}

			if (bset_config->ubo_index == INVALID_ID_U8) {
				// Not used, invalidate
				instance_state->ubo_offset = INVALID_ID_U64;
			}

			// Setup bindings
			u8 ssbo_idx = 0;
			u8 samp_idx = 0;
			u8 tex_idx = 0;
			for (u8 b = 0; b < bset_config->binding_count; ++b) {
				shader_binding_config *binding_config = &bset_config->bindings[b];

				switch (binding_config->binding_type) {
				case SHADER_BINDING_TYPE_UBO: {
					// Setup UBO binding.
					instance_state->ubo_offset = binding_config->offset;
					instance_state->ubo_size = binding_config->data_size;
					// Ensure the stride is the UBO min alignment from the device limits.
					instance_state->ubo_stride = get_aligned(instance_state->ubo_size, context->device.properties.limits.minUniformBufferOffsetAlignment);

					// Invalidate states
					for (u8 d = 0; d < VULKAN_RESOURCE_IMAGE_COUNT; ++d) {
						instance_state->ubo_descriptor_state.renderer_frame_number[d] = INVALID_ID_U16;
					}

					// Track how large the uniform buffer needs to be.
					total_uniform_buffer_size += instance_state->ubo_stride;
				} break;

				case SHADER_BINDING_TYPE_SSBO: {
					vulkan_ssbo_state *ssbo_state = &instance_state->ssbo_states[ssbo_idx];

					ssbo_state->buffer = renderer_renderbuffer_get(backend->frontend_state, binding_config->name);

					// If the SSBO does not exist, create one with that name.
					if (ssbo_state->buffer == KRENDERBUFFER_INVALID) {
						// NOTE: Can only create one if size is nonzero.
						if (!binding_config->data_size) {
							KERROR("%s - Configured SSBO binding at index %u must have a nonzero size to be created. Cannot create shader.", __FUNCTION__, b);
							return false;
						}

						// FIXME: This probably doesn't need to be checked/happen per use, perhaps hoist this to happen once outside this loop.
						ssbo_state->buffer = renderer_renderbuffer_create(backend->frontend_state, binding_config->name, RENDERBUFFER_TYPE_STORAGE, binding_config->data_size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT);
						if (ssbo_state->buffer == KRENDERBUFFER_INVALID) {
							KERROR("Failed to create storage buffer needed for shader at binding index %u. Shader creation failed.", b);
							return false;
						}
					}

					// Invalidate states
					for (u8 d = 0; d < VULKAN_RESOURCE_IMAGE_COUNT; ++d) {
						ssbo_state->descriptor_state.renderer_frame_number[d] = INVALID_ID_U16;
					}

					ssbo_idx++;
				} break;

				case SHADER_BINDING_TYPE_TEXTURE: {
					vulkan_texture_state *tex_state = &instance_state->texture_states[tex_idx];

					tex_state->array_size = KMAX(binding_config->array_size, 1);
					tex_state->type = binding_config->texture_type;
					tex_state->texture_handles = KALLOC_TYPE_CARRAY(ktexture, tex_state->array_size);
					tex_state->descriptor_states = KALLOC_TYPE_CARRAY(vulkan_descriptor_state, tex_state->array_size);

					// Invalidate states
					for (u8 a = 0; a < tex_state->array_size; ++a) {
						for (u8 d = 0; d < VULKAN_RESOURCE_IMAGE_COUNT; ++d) {
							tex_state->descriptor_states[a].renderer_frame_number[d] = INVALID_ID_U16;
						}
					}

					tex_idx++;
				} break;

				case SHADER_BINDING_TYPE_SAMPLER: {
					vulkan_sampler_state *samp_state = &instance_state->sampler_states[samp_idx];

					samp_state->array_size = KMAX(binding_config->array_size, 1);
					samp_state->type = binding_config->sampler_type;
					samp_state->sampler_handles = KALLOC_TYPE_CARRAY(ksampler_backend, samp_state->array_size);
					samp_state->descriptor_states = KALLOC_TYPE_CARRAY(vulkan_descriptor_state, samp_state->array_size);

					// Invalidate states
					for (u8 a = 0; a < samp_state->array_size; ++a) {
						for (u8 d = 0; d < VULKAN_RESOURCE_IMAGE_COUNT; ++d) {
							samp_state->descriptor_states[a].renderer_frame_number[d] = INVALID_ID_U16;
						}
					}

					samp_idx++;
				} break;

				case SHADER_BINDING_TYPE_COUNT:
					KFATAL("Why would you bind this, ya dingus?");
					return false;
				}
			}
		} // end instances setup
	} // end binding sets setup

	// Setup descriptor pools
	{
		VkDescriptorType dtypes[SHADER_BINDING_TYPE_COUNT] = {0};
		dtypes[SHADER_BINDING_TYPE_UBO] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		dtypes[SHADER_BINDING_TYPE_SSBO] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		dtypes[SHADER_BINDING_TYPE_TEXTURE] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		dtypes[SHADER_BINDING_TYPE_SAMPLER] = VK_DESCRIPTOR_TYPE_SAMPLER;

		// Setup pool sizes
		internal_shader->pool_size_count = 0;
		for (u8 t = 0; t < SHADER_BINDING_TYPE_COUNT; ++t) {
			if (total_descriptor_count[t] > 0) {
				internal_shader->pool_sizes[internal_shader->pool_size_count] = (VkDescriptorPoolSize){dtypes[t], total_descriptor_count[t]};
				internal_shader->pool_size_count++;
			}
		}

		// Descriptor pool.
		VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		pool_info.poolSizeCount = internal_shader->pool_size_count;
		pool_info.pPoolSizes = internal_shader->pool_sizes;
		pool_info.maxSets = total_descriptor_set_count;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
#if defined(VK_USE_PLATFORM_MACOS_MVK)
		// NOTE: increase the per-stage descriptor samplers limit on macOS (maxPerStageDescriptorUpdateAfterBindSamplers > maxPerStageDescriptorSamplers)
		pool_info.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
#endif
		// Create descriptor pool.
		VkResult result = rhi->kvkCreateDescriptorPool(logical_device, &pool_info, vk_allocator, &internal_shader->descriptor_pool);
		if (!vulkan_result_is_success(result)) {
			KERROR("vulkan_shader_initialize failed creating descriptor pool: '%s'", vulkan_result_string(result, true));
			return false;
		}

		char *desc_pool_name = string_format("desc_pool_shader_%s", kname_string_get(name));
		VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_POOL, internal_shader->descriptor_pool, desc_pool_name);
		string_free(desc_pool_name);

		// Create descriptor set layouts.

		internal_shader->descriptor_set_layouts = KALLOC_TYPE_CARRAY(VkDescriptorSetLayout, internal_shader->descriptor_set_count);
		for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
			VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			layout_info.bindingCount = internal_shader->descriptor_set_configs[i].binding_count;
			layout_info.pBindings = internal_shader->descriptor_set_configs[i].bindings;

			result = rhi->kvkCreateDescriptorSetLayout(logical_device, &layout_info, vk_allocator, &internal_shader->descriptor_set_layouts[i]);
			if (!vulkan_result_is_success(result)) {
				KERROR("vulkan_shader_initialize failed descriptor set layout: '%s'", vulkan_result_string(result, true));
				return false;
			}
		}
	}

	// Attachments.
	{
		// Static lookup table for our attribute types->Vulkan ones.
		static VkFormat *types = 0;
		static VkFormat t[16];
		if (!types) {
			t[KPIXEL_FORMAT_R8] = VK_FORMAT_R8_UNORM;
			t[KPIXEL_FORMAT_RG8] = VK_FORMAT_R8G8_UNORM;
			t[KPIXEL_FORMAT_RGB8] = VK_FORMAT_R8G8B8_UNORM;
			t[KPIXEL_FORMAT_RGBA8] = VK_FORMAT_R8G8B8A8_UNORM;

			t[KPIXEL_FORMAT_R16] = VK_FORMAT_R16_UNORM;
			t[KPIXEL_FORMAT_RG16] = VK_FORMAT_R16G16_UNORM;
			t[KPIXEL_FORMAT_RGB16] = VK_FORMAT_R16G16B16_UNORM;
			t[KPIXEL_FORMAT_RGBA16] = VK_FORMAT_R16G16B16A16_UNORM;

			t[KPIXEL_FORMAT_RGBA32] = VK_FORMAT_R32_UINT;
			t[KPIXEL_FORMAT_RGBA32] = VK_FORMAT_R32G32_UINT;
			t[KPIXEL_FORMAT_RGBA32] = VK_FORMAT_R32G32B32_UINT;
			t[KPIXEL_FORMAT_RGBA32] = VK_FORMAT_R32G32B32A32_UINT;

			t[KPIXEL_FORMAT_D32] = VK_FORMAT_D32_SFLOAT;
			t[KPIXEL_FORMAT_D24] = VK_FORMAT_D24_UNORM_S8_UINT;
			t[KPIXEL_FORMAT_S8] = VK_FORMAT_S8_UINT;
			t[KPIXEL_FORMAT_UNKNOWN] = VK_FORMAT_UNDEFINED;
			types = t;
		}

		internal_shader->colour_attachment_count = colour_attachment_count;
		if (colour_attachment_count) {
			internal_shader->colour_attachments = KALLOC_TYPE_CARRAY(VkFormat, colour_attachment_count);
			for (u8 c = 0; c < colour_attachment_count; ++c) {
				internal_shader->colour_attachments[c] = types[colour_attachment_formats[c]];
			}
		}

		// If both depth and stencil are set, use the combined format for the attachment and assign to both.
		if (depth_attachment_format == KPIXEL_FORMAT_D24 && stencil_attachment_format == KPIXEL_FORMAT_S8) {
			internal_shader->depth_attachment = VK_FORMAT_D24_UNORM_S8_UINT;
			internal_shader->stencil_attachment = VK_FORMAT_D24_UNORM_S8_UINT;
		} else if (depth_attachment_format == KPIXEL_FORMAT_D32) {
			if (stencil_attachment_format == KPIXEL_FORMAT_S8) {
				internal_shader->depth_attachment = VK_FORMAT_D32_SFLOAT_S8_UINT;
			} else {
				internal_shader->depth_attachment = VK_FORMAT_D32_SFLOAT;
			}

			internal_shader->stencil_attachment = internal_shader->depth_attachment;
		}

		// HACK: Use the supported depth type regardless of what's defined on the frontend. Eventually should figure out
		// the lowest common denominator of what's supported and just expose that.
		if (depth_attachment_format != KPIXEL_FORMAT_UNKNOWN) {
			internal_shader->depth_attachment = context->device.depth_format;
			internal_shader->stencil_attachment = context->device.depth_format;
		}
	}

	// Attribute setup
	{
		// Static lookup table for our attribute types->Vulkan ones.
		static VkFormat *types = 0;
		static VkFormat t[17];
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
			t[SHADER_ATTRIB_TYPE_INT32_2] = VK_FORMAT_R32G32_SINT;
			t[SHADER_ATTRIB_TYPE_INT32_3] = VK_FORMAT_R32G32B32_SINT;
			t[SHADER_ATTRIB_TYPE_INT32_4] = VK_FORMAT_R32G32B32A32_SINT;
			t[SHADER_ATTRIB_TYPE_UINT32] = VK_FORMAT_R32_UINT;
			t[SHADER_ATTRIB_TYPE_UINT32_2] = VK_FORMAT_R32G32_UINT;
			t[SHADER_ATTRIB_TYPE_UINT32_3] = VK_FORMAT_R32G32B32_UINT;
			t[SHADER_ATTRIB_TYPE_UINT32_4] = VK_FORMAT_R32G32B32A32_UINT;
			types = t;
		}

		// Each "vertex pipeline" - one per vertex layout in shader config.
		// A separate set of Vulkan pipelines (attributes, etc) must be setup for each.
		internal_shader->vertex_layout_pipeline_count = pipeline_count;
		internal_shader->vertex_layout_pipelines = KALLOC_TYPE_CARRAY(vulkan_vertex_layout_pipeline, pipeline_count);
		for (u8 pi = 0; pi < pipeline_count; ++pi) {
			shader_pipeline_config *pc = &pipelines[pi];
			vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[pi];

			p->bound_pipeline_index = INVALID_ID_U8;

			// Stages
			p->stage_count = pc->stage_count;
			p->stage_create_infos = KALLOC_TYPE_CARRAY(VkPipelineShaderStageCreateInfo, p->stage_count);
			// Shallow copy of stage sources.
			KDUPLICATE_TYPE_CARRAY(p->stage_sources, pc->stage_sources, const char *, p->stage_count);
			p->stages = KALLOC_TYPE_CARRAY(vulkan_shader_stage, p->stage_count);
			for (u8 si = 0; si < p->stage_count; ++si) {
				p->stages[si].stage = pc->stages[si];
			}

			// Attributes
			p->attribute_count = pc->attribute_count;
			p->attribute_stride = pc->attribute_stride;
			p->attributes = KALLOC_TYPE_CARRAY(VkVertexInputAttributeDescription, p->attribute_count);
			u32 offset = 0;
			for (u8 ai = 0; ai < p->attribute_count; ++ai) {
				VkVertexInputAttributeDescription *a = &p->attributes[ai];
				a->location = ai;
				a->binding = 0; // Won't have multiple bindings.
				a->offset = offset;
				a->format = types[pc->attributes[ai].type];

				offset += pc->attributes[ai].size;
			}

			// Only dynamic topology is supported. Create one pipeline per topology class.
			// If this isn't supported, perhaps a different backend should be used.
			u32 pipeline_count = 3;

			// Create an array of pipelines, one per topology class. Having no flags set in supported_topology_types means topology isn't supported.
			p->pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);

			b8 needs_wireframe = (internal_shader->flags & SHADER_FLAG_WIREFRAME_BIT) != 0;
			// Determine if the implementation supports this and set to false if not.
			if (!context->device.features.fillModeNonSolid) {
				KINFO("Renderer backend does not support fillModeNonSolid. Wireframe mode is not possible, but was requested for the shader '%s'.", kname_string_get(name));
				needs_wireframe = false;
			}

			// Do the same as above, but a wireframe version.
			if (needs_wireframe) {
				p->wireframe_pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
			} else {
				p->wireframe_pipelines = 0;
			}

			// Create one pipeline per topology class.
			// Point class.
			if (internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT) {
				// Set the supported types for this class.
				p->pipelines[VULKAN_TOPOLOGY_CLASS_POINT].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT;

				// Wireframe versions.
				if (needs_wireframe) {
					// Set the supported types for this class.
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_POINT].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT;
				}
			}

			// Line class.
			if (internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT || internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT) {
				// Set the supported types for this class.
				p->pipelines[VULKAN_TOPOLOGY_CLASS_LINE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT;
				p->pipelines[VULKAN_TOPOLOGY_CLASS_LINE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT;

				// Wireframe versions.
				if (needs_wireframe) {
					// Set the supported types for this class.
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT;
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_LINE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT;
				}
			}

			// Triangle class.
			if (internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT ||
				internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT ||
				internal_shader->topology_types & PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT) {
				// Set the supported types for this class.
				p->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
				p->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT;
				p->pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT;

				// Wireframe versions.
				if (needs_wireframe) {
					// Set the supported types for this class.
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT;
					p->wireframe_pipelines[VULKAN_TOPOLOGY_CLASS_TRIANGLE].supported_topology_types |= PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT;
				}
			}

			if (!shader_create_modules_and_pipelines(backend, internal_shader, pc, p)) {
				KERROR("Failed initial load on shader '%s'. See logs for details.", kname_string_get(internal_shader->name));
				return false;
			}

			// Figure out what the default should be here.
			vulkan_get_vktopology_type_and_pipeline_index(default_topology, &internal_shader->default_topology, &p->default_pipeline_index);
			p->bound_pipeline_index = p->default_pipeline_index;

		} // end pipeline setup
	}

	// Uniform buffers, one per buffered image (i.e. triple-buffered = 3). Only create if needed, though.
	if (total_uniform_buffer_size) {
		const char *buffer_name = string_format("renderbuffer_uniform_%s", kname_string_get(internal_shader->name));
		internal_shader->uniform_buffer = renderer_renderbuffer_create(backend->frontend_state, kname_create(buffer_name), RENDERBUFFER_TYPE_UNIFORM, total_uniform_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST, RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT);
		string_free(buffer_name);
	} else {
		internal_shader->uniform_buffer = KRENDERBUFFER_INVALID;
	}

	return true;
}

void vulkan_renderer_shader_destroy (renderer_backend_interface *backend, kshader shader) {
	if (shader != KSHADER_INVALID) {
		vulkan_context *context = (vulkan_context *)backend->internal_context;
		krhi_vulkan *rhi = &context->rhi;
		VkDevice logical_device = context->device.logical_device;
		VkAllocationCallbacks *vk_allocator = context->allocator;
		vulkan_shader *internal_shader = &context->shaders[shader];
		if (!internal_shader) {
			KERROR("vulkan_renderer_shader_destroy requires a valid pointer to a shader.");
			return;
		}

		if (internal_shader->colour_attachment_count && internal_shader->colour_attachments) {
			kfree(internal_shader->colour_attachments);
		}

		// Descriptor set layouts.
		for (u32 i = 0; i < internal_shader->descriptor_set_count; ++i) {
			// Destroy descriptor set configs.
			vulkan_descriptor_set_config *set_config = &internal_shader->descriptor_set_configs[i];
			if (set_config->bindings && set_config->binding_count) {
				kfree(set_config->bindings);
				set_config->bindings = 0;
			}

			vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[i];
			if (binding_set_state->max_instance_count && binding_set_state->instances) {
				for (u32 u = 0; u < binding_set_state->max_instance_count; ++u) {
					vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[u];

					if (binding_set_state->ssbo_binding_count && instance_state->ssbo_states) {
						kfree(instance_state->ssbo_states);
					}

					if (binding_set_state->sampler_binding_count && instance_state->sampler_states) {
						for (u8 s = 0; s < binding_set_state->sampler_binding_count; ++s) {
							vulkan_sampler_state *samp_state = &instance_state->sampler_states[s];
							kfree(samp_state->sampler_handles);
							kfree(samp_state->descriptor_states);
						}
						kfree(instance_state->sampler_states);
					}

					if (binding_set_state->texture_binding_count && instance_state->texture_states) {
						for (u8 t = 0; t < binding_set_state->texture_binding_count; ++t) {
							vulkan_texture_state *tex_state = &instance_state->texture_states[t];
							kfree(tex_state->texture_handles);
							kfree(tex_state->descriptor_states);
						}
						kfree(instance_state->texture_states);
					}
				}

				kfree(binding_set_state->instances);
			}

			kfree(binding_set_state->bindings);
			binding_set_state->binding_count = 0;

			if (internal_shader->descriptor_set_layouts[i]) {
				rhi->kvkDestroyDescriptorSetLayout(logical_device, internal_shader->descriptor_set_layouts[i], vk_allocator);
				internal_shader->descriptor_set_layouts[i] = 0;
			}
		}
		kfree(internal_shader->binding_set_states);
		kfree(internal_shader->descriptor_set_configs);
		kfree(internal_shader->descriptor_set_layouts);

		// Descriptor pool
		if (internal_shader->descriptor_pool) {
			rhi->kvkDestroyDescriptorPool(logical_device, internal_shader->descriptor_pool, vk_allocator);
			internal_shader->descriptor_pool = 0;
		}

		// Uniform buffer.
		if (internal_shader->uniform_buffer != KRENDERBUFFER_INVALID) {
			renderer_renderbuffer_destroy(backend->frontend_state, internal_shader->uniform_buffer);
			internal_shader->uniform_buffer = KRENDERBUFFER_INVALID;
		}

		// Vertex layout pipelines
		for (u8 pi = 0; pi < internal_shader->vertex_layout_pipeline_count; ++pi) {
			vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[pi];
			// Pipelines
			for (u32 i = 0; i < VULKAN_TOPOLOGY_CLASS_MAX; ++i) {
				if (p->pipelines[i].supported_topology_types != PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
					vulkan_pipeline_destroy(context, &p->pipelines[i]);
				}
				if (p->wireframe_pipelines && p->wireframe_pipelines[i].supported_topology_types != PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
					vulkan_pipeline_destroy(context, &p->wireframe_pipelines[i]);
				}
			}
			// topology type pipelines
			kfree(p->pipelines);
			if (p->wireframe_pipelines) {
				kfree(p->wireframe_pipelines);
			}

			// Attributes
			kfree(p->attributes);

			// Shader modules
			for (u32 i = 0; i < p->stage_count; ++i) {
				rhi->kvkDestroyShaderModule(context->device.logical_device, p->stages[i].handle, context->allocator);
			}
			kfree(p->stage_create_infos);
			kfree(p->stage_sources);
			kfree(p->stages);
			p->stage_count = 0;
		}
		kfree(internal_shader->vertex_layout_pipelines);

		KZERO_TYPE(internal_shader, vulkan_shader);
	}
}

b8 vulkan_renderer_shader_reload (renderer_backend_interface *backend, kshader shader, u8 pipeline_count, shader_pipeline_config *pipeline_configs) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	for (u8 i = 0; i < pipeline_count; ++i) {
		if (!shader_create_modules_and_pipelines(backend, internal_shader, &pipeline_configs[i], &internal_shader->vertex_layout_pipelines[i])) {
			return false;
		}
	}
	return true;
}

static b8 bind_shader_pipeline_index_topology (renderer_backend_interface *backend, kshader shader, VkPrimitiveTopology type, u8 vertex_pipeline_index, u8 pipeline_index) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_shader *internal_shader = &context->shaders[shader];
	vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[vertex_pipeline_index];

	u16 frame_number = renderer_system_frame_number_get(backend->frontend_state);

	// FIXME: re-enable this and figure out why it's crashing.
	/* // Do not re-bind unless the indices vary _or_ the renderer frame number is out of sync.
	if (frame_number != internal_shader->renderer_frame_number || context->bound_shader == shader || (internal_shader->vertex_layout_index == vertex_pipeline_index && p->bound_pipeline_index == pipeline_index)) {
		// Don't need to re-bind. Boot.
		return true;
	} */

	internal_shader->vertex_layout_index = vertex_pipeline_index;
	p->bound_pipeline_index = pipeline_index;

	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Pick the correct pipeline.
	b8 wireframe_enabled = vulkan_renderer_shader_flag_get(backend, shader, SHADER_FLAG_WIREFRAME_BIT);
	vulkan_pipeline *pipeline_array = wireframe_enabled ? p->wireframe_pipelines : p->pipelines;

	// Get the current pipeline index/type for the topology type
	vulkan_pipeline_bind(context, command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, &pipeline_array[p->bound_pipeline_index]);

	context->bound_shader = shader;
	internal_shader->renderer_frame_number = frame_number;

	// Make sure to use the current bound type as well.
	if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) {
		rhi->kvkCmdSetPrimitiveTopology(command_buffer->handle, type);
	} else if (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT) {
		context->vkCmdSetPrimitiveTopologyEXT(command_buffer->handle, type);
	}

	return true;
}

b8 vulkan_renderer_shader_use (renderer_backend_interface *backend, kshader shader, u8 vertex_layout_index) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];
	vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[vertex_layout_index];
	return bind_shader_pipeline_index_topology(backend, shader, internal_shader->default_topology, vertex_layout_index, p->default_pipeline_index);
}

b8 vulkan_renderer_shader_use_with_topology (renderer_backend_interface *backend, kshader shader, primitive_topology_type type, u8 vertex_layout_index) {

	// Get the current pipeline index/type for the topology type
	VkPrimitiveTopology topology;
	u8 pipeline_index;
	vulkan_get_vktopology_type_and_pipeline_index(type, &topology, &pipeline_index);
	return bind_shader_pipeline_index_topology(backend, shader, topology, vertex_layout_index, pipeline_index);
}

b8 vulkan_renderer_shader_supports_wireframe (const renderer_backend_interface *backend, kshader shader) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal = &context->shaders[shader];

	// If the array exists, this is supported. Fine to just use the first pipeline since if one supports it, they all do.
	if (internal->vertex_layout_pipelines[0].wireframe_pipelines) {
		return true;
	}

	return false;
}

b8 vulkan_renderer_shader_flag_get (const renderer_backend_interface *backend, kshader shader, shader_flags flag) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	return FLAG_GET(internal_shader->flags, flag);
}

void vulkan_renderer_shader_flag_set (renderer_backend_interface *backend, kshader shader, shader_flags flag, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	internal_shader->flags = FLAG_SET(internal_shader->flags, flag, enabled);
}

b8 vulkan_renderer_shader_set_immediate_data (renderer_backend_interface *backend, kshader shader, const void *data, u8 size) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_shader *internal_shader = &context->shaders[shader];

	VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;

	// Pick the correct pipeline.
	b8 wireframe_enabled = vulkan_renderer_shader_flag_get(backend, shader, SHADER_FLAG_WIREFRAME_BIT);
	vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[internal_shader->vertex_layout_index];
	vulkan_pipeline *pipeline_array = wireframe_enabled ? p->wireframe_pipelines : p->pipelines;

	u8 block[128] = {0};
	kcopy_memory(block, data, size);

	// Update the data via push constant.
	rhi->kvkCmdPushConstants(
		command_buffer,
		pipeline_array[p->bound_pipeline_index].pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, 128, block);

	return true;
}

b8 vulkan_renderer_shader_set_binding_data (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];

	vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[instance_id];

	vulkan_shader_binding *binding = &binding_set_state->bindings[binding_index];
	if (binding->binding_type == SHADER_BINDING_TYPE_UBO) {
		// Upload data to UBO
		vulkan_buffer_load_range(backend, internal_shader->uniform_buffer, instance_state->ubo_offset, size, data, true);
	} else if (binding->binding_type == SHADER_BINDING_TYPE_SSBO) {
		// Upload data to SSBO
		krenderbuffer buf = instance_state->ssbo_states[binding->binding_type_index].buffer;
		u8 *block = (u8 *)vulkan_renderbuffer_get_mapped_memory(backend, buf);
		kcopy_memory(block + offset, data, size);
	} else {
		KERROR("%s - must be called on a binding type of either UBO or SSBO. Nothing to do.", __FUNCTION__);
		return false;
	}

	return true;
}

b8 vulkan_renderer_shader_set_binding_texture (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture t) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];

	vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[instance_id];

	vulkan_shader_binding *binding = &binding_set_state->bindings[binding_index];
	if (binding->binding_type == SHADER_BINDING_TYPE_TEXTURE) {
		// Set texture descriptor
		vulkan_texture_state *tex_state = &instance_state->texture_states[binding->binding_type_index];
		tex_state->texture_handles[array_index] = t;
	} else {
		KERROR("%s - must be called on a binding type of texture. Nothing to do.", __FUNCTION__);
		return false;
	}

	return true;
}

b8 vulkan_renderer_shader_set_binding_sampler (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];

	vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[instance_id];

	vulkan_shader_binding *binding = &binding_set_state->bindings[binding_index];
	if (binding->binding_type == SHADER_BINDING_TYPE_SAMPLER) {
		// Set texture descriptor
		vulkan_sampler_state *tex_state = &instance_state->sampler_states[binding->binding_type_index];
		tex_state->sampler_handles[array_index] = sampler;
	} else {
		KERROR("%s - must be called on a binding type of sampler. Nothing to do.", __FUNCTION__);
		return false;
	}

	return true;
}

b8 vulkan_renderer_shader_apply_binding_set (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	u16 frame_number = renderer_system_frame_number_get(backend->frontend_state);
	return vulkan_descriptorset_update_and_bind(context, frame_number, internal_shader, internal_shader->vertex_layout_index, binding_set, instance_id);
}

static void invalidate_shader_binding_set_instance_state (vulkan_shader_binding_set_instance_state *instance_state, const vulkan_shader_binding_set_state *binding_set_state) {
	for (u8 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
		// Invalidate UBO descriptor state.
		instance_state->ubo_descriptor_state.renderer_frame_number[j] = INVALID_ID_U16;

		// Invalidate all sampler states.
		if (instance_state->sampler_states && binding_set_state->sampler_binding_count) {
			for (u8 b = 0; b < binding_set_state->sampler_binding_count; ++b) {
				u8 array_size = instance_state->sampler_states[b].array_size;
				for (u8 s = 0; s < array_size; ++s) {
					instance_state->sampler_states[b].descriptor_states[s].renderer_frame_number[j] = INVALID_ID_U16;
				}
			}
		}

		// Invalidate all texture states.
		if (instance_state->texture_states && binding_set_state->texture_binding_count) {
			for (u8 b = 0; b < binding_set_state->texture_binding_count; ++b) {
				u8 array_size = instance_state->texture_states[b].array_size;
				for (u8 s = 0; s < array_size; ++s) {
					instance_state->texture_states[b].descriptor_states[s].renderer_frame_number[j] = INVALID_ID_U16;
				}
			}
		}

		// Invalidate all SSBO states.
		if (instance_state->ssbo_states && binding_set_state->ssbo_binding_count) {
			for (u8 b = 0; b < binding_set_state->ssbo_binding_count; ++b) {
				instance_state->ssbo_states[b].descriptor_state.renderer_frame_number[j] = INVALID_ID_U16;
			}
		}
	}
}

u32 vulkan_renderer_shader_acquire_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];

	// Layout will be the same for all
	VkDescriptorSetLayout layouts[VULKAN_RESOURCE_IMAGE_COUNT];
	for (u8 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
		layouts[j] = internal_shader->descriptor_set_layouts[binding_set];
	}

	for (u32 i = 0; i < binding_set_state->max_instance_count; ++i) {
		vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[i];

		// Set an invalid renderer frame number.
		instance_state->renderer_frame_number = INVALID_ID_U16;

		// Check if free. Descriptor sets set to 0 means non-in-use. Only need to check the first one since they are
		// gotten and released as a group.
		if (!instance_state->descriptor_sets[0]) {
			VkDescriptorSetAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = internal_shader->descriptor_pool,
				.descriptorSetCount = VULKAN_RESOURCE_IMAGE_COUNT,
				.pSetLayouts = layouts};
			VK_CHECK(rhi->kvkAllocateDescriptorSets(context->device.logical_device, &alloc_info, instance_state->descriptor_sets));

			for (u8 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {

				// Ensure the state is invalidated.
				invalidate_shader_binding_set_instance_state(instance_state, binding_set_state);

#if KOHI_DEBUG
				// Give it a debug name
				char desc_set_debug_name[512] = {0};
				string_nformat(desc_set_debug_name, 511, "desc_set_%u_idx_%u_shader_%s_", binding_set, j, kname_string_get(internal_shader->name));
				VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DESCRIPTOR_SET, instance_state->descriptor_sets[j], desc_set_debug_name);
#endif
			}

			// The 'instance id' is the index into the use count.
			return i;
		}
	}

	KERROR("%s - Failed to find a free instance in shader '%s' for binding set %u.", __FUNCTION__, kname_string_get(internal_shader->name), binding_set);
	return INVALID_ID_U32;
}

void vulkan_renderer_shader_release_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];
	if (instance_id == INVALID_ID_U32 || instance_id >= binding_set_state->max_instance_count) {
		KERROR("%s - Invalid or out-of-range instance_id %u passed (binding set %u range is 0-%u)", __FUNCTION__, instance_id, binding_set, binding_set_state->max_instance_count);
		return;
	}

	vulkan_shader_binding_set_instance_state *instance_state = &binding_set_state->instances[instance_id];
	if (instance_state->descriptor_sets[0]) {
		VK_CHECK(rhi->kvkFreeDescriptorSets(context->device.logical_device, internal_shader->descriptor_pool, VULKAN_RESOURCE_IMAGE_COUNT, instance_state->descriptor_sets));
	}

	// This marks this instance as free.
	for (u8 j = 0; j < VULKAN_RESOURCE_IMAGE_COUNT; ++j) {
		instance_state->descriptor_sets[j] = 0;
	}

	invalidate_shader_binding_set_instance_state(instance_state, binding_set_state);
}

u32 vulkan_renderer_shader_binding_set_get_max_instance_count (renderer_backend_interface *backend, kshader shader, u8 binding_set) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_shader *internal_shader = &context->shaders[shader];

	vulkan_shader_binding_set_state *binding_set_state = &internal_shader->binding_set_states[binding_set];
	return binding_set_state->max_instance_count;
}

static b8 sampler_create_internal (vulkan_context *context, texture_filter filter, texture_repeat repeat, f32 anisotropy, vulkan_sampler_handle_data *out_sampler_handle_data) {
	krhi_vulkan *rhi = &context->rhi;

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

	VkResult result = rhi->kvkCreateSampler(context->device.logical_device, &sampler_info, context->allocator, &out_sampler_handle_data->sampler);
	if (!vulkan_result_is_success(VK_SUCCESS)) {
		KERROR("Error creating sampler: %s", vulkan_result_string(result, true));
		return false;
	}

	VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_SAMPLER, out_sampler_handle_data->sampler, kname_string_get(out_sampler_handle_data->name));

	return true;
}

ksampler_backend vulkan_renderer_sampler_acquire (renderer_backend_interface *backend, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;

	// Find a free sampler slot.
	u32 length = darray_length(context->samplers);
	u16 selected_id = KSAMPLER_BACKEND_INVALID;
	for (u32 i = 0; i < length; ++i) {
		if (context->samplers[i].sampler == 0) {
			selected_id = i;
			break;
		}
	}
	if (selected_id == KSAMPLER_BACKEND_INVALID) {
		// Push an empty entry into the array.
		vulkan_sampler_handle_data empty = (vulkan_sampler_handle_data){INVALID_KNAME, 0};
		darray_push(context->samplers, &empty);
		selected_id = length;
	}

	// Set the name
	context->samplers[selected_id].name = name;

	if (!sampler_create_internal(context, filter, repeat, anisotropy, &context->samplers[selected_id])) {
		return KSAMPLER_BACKEND_INVALID;
	}

	return selected_id;
}

void vulkan_renderer_sampler_release (renderer_backend_interface *backend, ksampler_backend *sampler) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	if (*sampler != KSAMPLER_BACKEND_INVALID) {
		vulkan_sampler_handle_data *s = &context->samplers[*sampler];
		if (s->sampler) {
			// Make sure there's no way this is in use.
			rhi->kvkDeviceWaitIdle(context->device.logical_device);
			rhi->kvkDestroySampler(context->device.logical_device, s->sampler, context->allocator);
			// Invalidate the entry and the handle.
			s->sampler = 0;
			*sampler = KSAMPLER_BACKEND_INVALID;
		}
	}
}

b8 vulkan_renderer_sampler_refresh (renderer_backend_interface *backend, ksampler_backend *sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels) {
	KASSERT_DEBUG_MSG(*sampler != KSAMPLER_BACKEND_INVALID, "Invalid sampler provided.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;

	vulkan_sampler_handle_data *s = &context->samplers[*sampler];
	if (s->sampler) {

		// Take a copy of the old sampler.
		VkSampler old = s->sampler;

		// Make sure there's no way this is in use.
		rhi->kvkDeviceWaitIdle(context->device.logical_device);

		// Create/assign the new.
		if (!sampler_create_internal(context, filter, repeat, anisotropy, s)) {
			KERROR("Sampler refresh failed to create new internal sampler.");
			return false;
		}

		// Destroy the old.
		rhi->kvkDestroySampler(context->device.logical_device, old, context->allocator);
	}
	return true;
}

kname vulkan_renderer_sampler_name_get (renderer_backend_interface *backend, ksampler_backend sampler) {
	KASSERT_DEBUG_MSG(sampler != KSAMPLER_BACKEND_INVALID, "Invalid sampler provided.");
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_sampler_handle_data *data = &context->samplers[sampler];
	return data->name;
}

static b8 create_shader_module (vulkan_context *context, vulkan_shader *internal_shader, shader_stage stage, const char *source, const char *filename, vulkan_shader_stage *out_stage) {
	krhi_vulkan *rhi = &context->rhi;
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

	// KTRACE("Shader source:\n%s", source);

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
	shaderc_compile_options_release(options);

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

	VK_CHECK(rhi->kvkCreateShaderModule(context->device.logical_device, &out_stage->create_info, context->allocator, &out_stage->handle));

	// Release the copy of the code.
	kfree(code);

	// Shader stage info
	kzero_memory(&out_stage->shader_stage_create_info, sizeof(VkPipelineShaderStageCreateInfo));
	out_stage->shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	out_stage->shader_stage_create_info.stage = vulkan_stage;
	out_stage->shader_stage_create_info.module = out_stage->handle;
	out_stage->shader_stage_create_info.pName = "main";

	return true;
}

b8 vulkan_renderer_is_multithreaded (renderer_backend_interface *backend) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	return context->multithreading_enabled;
}

b8 vulkan_renderer_flag_enabled_get (renderer_backend_interface *backend, renderer_config_flags flag) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	return (context->current_window->renderer_state->backend_state->swapchain.flags & flag);
}

void vulkan_renderer_flag_enabled_set (renderer_backend_interface *backend, renderer_config_flags flag, b8 enabled) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	vulkan_swapchain *swapchain = &context->current_window->renderer_state->backend_state->swapchain;
	swapchain->flags = (enabled ? (swapchain->flags | flag) : (swapchain->flags & ~flag));
	context->render_flag_changed = true;
}

f32 vulkan_renderer_max_anisotropy_get (renderer_backend_interface *backend) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	if (!context->device.features.samplerAnisotropy) {
		// Not available.
		return 0;
	} else {
		return context->device.properties.limits.maxSamplerAnisotropy;
	}
}

// NOTE: Begin vulkan buffer.

// Indicates if the provided buffer has device-local memory.
static b8 vulkan_buffer_is_device_local (renderer_backend_interface *backend, vulkan_buffer *buffer) {
	return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

// Indicates if the provided buffer has host-visible memory.
static b8 vulkan_buffer_is_host_visible (renderer_backend_interface *backend, vulkan_buffer *buffer) {
	return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}

// Indicates if the provided buffer has host-coherent memory.
static b8 vulkan_buffer_is_host_coherent (renderer_backend_interface *backend, vulkan_buffer *buffer) {
	return (buffer->memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

b8 vulkan_renderbuffer_create (renderer_backend_interface *backend, kname name, u64 size, renderbuffer_type type, renderbuffer_flags flags, krenderbuffer handle) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	KASSERT_DEBUG(handle != KRENDERBUFFER_INVALID);

	/* KTRACE("Creating vulkan renderbuffer: '%s'...", kname_string_get(name)); */

	u16 len = darray_length(context->renderbuffers);
	if (handle > (len - 1)) {
		darray_push(context->renderbuffers, &(vulkan_buffer){0});
	}

	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

	switch (type) {
	case RENDERBUFFER_TYPE_VERTEX:
		internal_buffer->usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
								 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
								 VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		internal_buffer->memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	case RENDERBUFFER_TYPE_INDEX:
		internal_buffer->usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
								 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
								 VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		internal_buffer->memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	case RENDERBUFFER_TYPE_UNIFORM: {
		u32 device_local_bits = context->device.supports_device_local_host_visible
									? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
									: 0;
		internal_buffer->usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		internal_buffer->memory_property_flags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | device_local_bits;
	} break;
	case RENDERBUFFER_TYPE_STAGING:
		internal_buffer->usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		internal_buffer->memory_property_flags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;
	case RENDERBUFFER_TYPE_READ:
		internal_buffer->usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		internal_buffer->memory_property_flags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;
	case RENDERBUFFER_TYPE_STORAGE:
		internal_buffer->usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		internal_buffer->memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		break;
	default:
		KERROR("Unsupported buffer type: %i", type);
		return false;
	}

	internal_buffer->name = name;
	internal_buffer->size = size;
	internal_buffer->type = type;
	internal_buffer->flags = flags;

	// Buffers which are triple-buffered need multiples.
	internal_buffer->handle_count = FLAG_GET(flags, RENDERBUFFER_FLAG_TRIPLE_BUFFERED_BIT) ? 3 : 1;
	internal_buffer->infos = KALLOC_TYPE_CARRAY(vkbuffer_info, internal_buffer->handle_count);

	b8 is_device_memory = FLAG_GET(internal_buffer->memory_property_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_info.size = size;
	buffer_info.usage = internal_buffer->usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // NOTE: Only used in one queue.

	for (u8 i = 0; i < internal_buffer->handle_count; ++i) {

		VK_CHECK(rhi->kvkCreateBuffer(context->device.logical_device, &buffer_info, context->allocator, &internal_buffer->infos[i].handle));
		/* KTRACE("VkBuffer created at %p", internal_buffer->infos[i].handle); */

		VK_SET_DEBUG_OBJECT_NAME_INDEXED(context, VK_OBJECT_TYPE_BUFFER, internal_buffer->infos[i].handle, kname_string_get(internal_buffer->name), i);

		// Gather memory requirements.
		rhi->kvkGetBufferMemoryRequirements(context->device.logical_device, internal_buffer->infos[i].handle, &internal_buffer->memory_requirements);
		internal_buffer->memory_index = vulkan_find_memory_index(
			context, internal_buffer->memory_requirements.memoryTypeBits,
			internal_buffer->memory_property_flags);
		if (internal_buffer->memory_index == -1) {
			KERROR("Unable to create vulkan buffer because the required memory type index was not found.");
			return false;
		}

		// Allocate memory info
		VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate_info.allocationSize = internal_buffer->memory_requirements.size;
		allocate_info.memoryTypeIndex = (u32)internal_buffer->memory_index;

		// Allocate the memory.
		VkResult result = rhi->kvkAllocateMemory(context->device.logical_device, &allocate_info, context->allocator, &internal_buffer->infos[i].memory);
		if (!vulkan_result_is_success(result)) {
			KERROR("Failed to allocate memory for buffer with error: %s", vulkan_result_string(result, true));
			return false;
		}
		VK_SET_DEBUG_OBJECT_NAME_INDEXED(context, VK_OBJECT_TYPE_DEVICE_MEMORY, internal_buffer->infos[i].memory, kname_string_get(internal_buffer->name), i);

		// Determine if memory is on a device heap.
		// Report memory as in-use.
		kallocate_report(internal_buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

		if (result != VK_SUCCESS) {
			KERROR("Unable to create vulkan buffer because the required memory allocation failed. Error: %i", result);
			return false;
		}

		// Bind the allocated memory to the buffer.
		VK_CHECK(rhi->kvkBindBufferMemory(context->device.logical_device, internal_buffer->infos[i].handle, internal_buffer->infos[i].memory, 0));

		// Automatically map entire buffer range if flag is set.
		if (FLAG_GET(internal_buffer->flags, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT)) {
			vkbuffer_info *buf = &internal_buffer->infos[i];
			size = size == KWHOLE_SIZE ? VK_WHOLE_SIZE : size;
			VK_CHECK(rhi->kvkMapMemory(context->device.logical_device, buf->memory, 0, size, 0, &buf->mapped_memory));
		}
	}

	return true;
}

void vulkan_renderbuffer_destroy (renderer_backend_interface *backend, krenderbuffer handle) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	rhi->kvkDeviceWaitIdle(context->device.logical_device);
	if (handle != KRENDERBUFFER_INVALID) {
		vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

		for (u8 i = 0; i < internal_buffer->handle_count; ++i) {
			if (internal_buffer->infos[i].memory) {
				rhi->kvkFreeMemory(context->device.logical_device, internal_buffer->infos[i].memory, context->allocator);
				internal_buffer->infos[i].memory = 0;
			}
			/* KTRACE("VkBuffer destroyed at %p", internal_buffer->infos[i].handle); */
			if (internal_buffer->infos[i].handle) {
				rhi->kvkDestroyBuffer(context->device.logical_device, internal_buffer->infos[i].handle, context->allocator);
				internal_buffer->infos[i].handle = 0;

				// Report the free memory.
				b8 is_device_memory = vulkan_buffer_is_device_local(backend, internal_buffer);
				kfree_report(internal_buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
				kzero_memory(&internal_buffer->memory_requirements, sizeof(VkMemoryRequirements));

				internal_buffer->usage = 0;
				internal_buffer->is_locked = false;

				// Free up the internal buffer.
				context->renderbuffers[handle].infos[i].handle = 0;
			}
		}
		if (internal_buffer->infos) {
			kfree(internal_buffer->infos);
			internal_buffer->infos = KNULL;
		}

		internal_buffer->flags = 0;
	}
}

b8 vulkan_buffer_resize (renderer_backend_interface *backend, krenderbuffer handle, u64 new_size) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	if (handle == KRENDERBUFFER_INVALID) {
		return false;
	}

	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

	// Create new buffer.
	VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_info.size = new_size;
	buffer_info.usage = internal_buffer->usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // NOTE: Only used in one queue.

	for (u8 i = 0; i < internal_buffer->handle_count; ++i) {

		VkBuffer new_buffer;
		VK_CHECK(rhi->kvkCreateBuffer(context->device.logical_device, &buffer_info, context->allocator, &new_buffer));

		// Gather memory requirements.
		VkMemoryRequirements requirements;
		rhi->kvkGetBufferMemoryRequirements(context->device.logical_device, new_buffer, &requirements);

		// Allocate memory info
		VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate_info.allocationSize = requirements.size;
		allocate_info.memoryTypeIndex = (u32)internal_buffer->memory_index;

		// Allocate the memory.
		VkDeviceMemory new_memory;
		VkResult result = rhi->kvkAllocateMemory(context->device.logical_device, &allocate_info, context->allocator, &new_memory);
		if (result != VK_SUCCESS) {
			KERROR("Unable to resize vulkan buffer because the required memory allocation failed. Error: %i", result);
			return false;
		}
		VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_DEVICE_MEMORY, new_memory, kname_string_get(internal_buffer->name));

		// Bind the new buffer's memory
		VK_CHECK(rhi->kvkBindBufferMemory(context->device.logical_device, new_buffer, new_memory, 0));

		// Copy over the data.
		vulkan_buffer_copy_range_internal(context, internal_buffer->infos[i].handle, 0, new_buffer, 0, internal_buffer->size, false);

		// Make sure anything potentially using these is finished.
		// NOTE: We could use vkQueueWaitIdle here if we knew what queue this buffer
		// would be used with...
		rhi->kvkDeviceWaitIdle(context->device.logical_device);

		// Destroy the old
		if (internal_buffer->infos[i].memory) {
			rhi->kvkFreeMemory(context->device.logical_device, internal_buffer->infos[i].memory, context->allocator);
			internal_buffer->infos[i].memory = 0;
		}
		if (internal_buffer->infos[i].handle) {
			rhi->kvkDestroyBuffer(context->device.logical_device, internal_buffer->infos[i].handle, context->allocator);
			internal_buffer->infos[i].handle = 0;
		}

		// Report free of the old, allocate of the new.
		b8 is_device_memory = vulkan_buffer_is_device_local(backend, internal_buffer);

		kfree_report(internal_buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);
		internal_buffer->memory_requirements = requirements;
		kallocate_report(internal_buffer->memory_requirements.size, is_device_memory ? MEMORY_TAG_GPU_LOCAL : MEMORY_TAG_VULKAN);

		// Set new properties
		internal_buffer->infos[i].memory = new_memory;
		internal_buffer->infos[i].handle = new_buffer;

		// Automatically re-map entire buffer range if flag is set.
		if (FLAG_GET(internal_buffer->flags, RENDERBUFFER_FLAG_AUTO_MAP_MEMORY_BIT)) {
			vkbuffer_info *buf = &internal_buffer->infos[i];
			VK_CHECK(rhi->kvkMapMemory(context->device.logical_device, buf->memory, 0, new_size, 0, &buf->mapped_memory));
		}
	}

	return true;
}

b8 vulkan_buffer_bind (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 binding_index) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;

	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("%s - requires valid handle to a buffer.", __FUNCTION__);
		return false;
	}
	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	u8 index = internal_buffer->handle_count == 1 ? 0 : get_current_image_index(context);

	if (internal_buffer->type == RENDERBUFFER_TYPE_VERTEX) {
		// Bind vertex buffer at offset.
		VkDeviceSize offsets[1] = {offset};
		rhi->kvkCmdBindVertexBuffers(command_buffer->handle, binding_index, 1, &internal_buffer->infos[index].handle, offsets);
		return true;
	} else if (internal_buffer->type == RENDERBUFFER_TYPE_INDEX) {
		// Bind index buffer at offset.
		rhi->kvkCmdBindIndexBuffer(command_buffer->handle, internal_buffer->infos[index].handle, offset, VK_INDEX_TYPE_UINT32);
		return true;
	} else {
		KWARN("Cannot bind buffer of type: %i", internal_buffer->type);
		return false;
	}

	return true;
}

b8 vulkan_buffer_unbind (renderer_backend_interface *backend, krenderbuffer handle) {
	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("%s - requires valid pointer to a buffer.", __FUNCTION__);
		return false;
	}

	// NOTE: Does nothing, for now.
	return true;
}

void vulkan_buffer_map_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) {
	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("vulkan_buffer_map_memory requires a valid pointer to a buffer.");
	}
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

	for (u8 i = 0; i < internal_buffer->handle_count; ++i) {
		vkbuffer_info *buf = &internal_buffer->infos[i];
		size = size == KWHOLE_SIZE ? VK_WHOLE_SIZE : size;
		VK_CHECK(rhi->kvkMapMemory(context->device.logical_device, buf->memory, offset, size, 0, &buf->mapped_memory));
	}
}

void vulkan_buffer_unmap_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) {
	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("%s - requires a valid pointer to a buffer.", __FUNCTION__);
		return;
	}
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

	for (u8 i = 0; i < internal_buffer->handle_count; ++i) {
		rhi->kvkUnmapMemory(context->device.logical_device, internal_buffer->infos[i].memory);
	}
}

void *vulkan_renderbuffer_get_mapped_memory (renderer_backend_interface *backend, krenderbuffer handle) {
	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("%s - requires a valid pointer to a buffer.", __FUNCTION__);
		return 0;
	}

	vulkan_context *context = (vulkan_context *)backend->internal_context;

	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];
	u8 index = internal_buffer->handle_count == 1 ? 0 : get_current_image_index(context);

	return context->renderbuffers[handle].infos[index].mapped_memory;
}

b8 vulkan_buffer_flush (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	if (handle == KRENDERBUFFER_INVALID) {
		KERROR("%s - requires a valid pointer to a buffer.", __FUNCTION__);
		return false;
	}
	// NOTE: If not host-coherent, flush the mapped memory range.
	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];
	if (!vulkan_buffer_is_host_coherent(backend, internal_buffer)) {
		u8 index = internal_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
		VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
		range.memory = internal_buffer->infos[index].memory;
		range.offset = offset;
		range.size = size;
		VK_CHECK(rhi->kvkFlushMappedMemoryRanges(context->device.logical_device, 1, &range));
	}

	return true;
}

b8 vulkan_buffer_read (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size, void **out_memory) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	if (handle == KRENDERBUFFER_INVALID || !out_memory) {
		KERROR("%s - requires a valid pointer to a buffer and out_memory, and the size must be nonzero.", __FUNCTION__);
		return false;
	}

	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];
	u8 index = internal_buffer->handle_count == 1 ? 0 : get_current_image_index(context);

	if (vulkan_buffer_is_device_local(backend, internal_buffer) &&
		!vulkan_buffer_is_host_visible(backend, internal_buffer)) {
		// NOTE: If a read buffer is needed (i.e.) the target buffer's memory is not
		// host visible but is device-local, create the read buffer, copy data to
		// it, then read from that buffer.

		// Create a host-visible staging buffer to copy to. Mark it as the
		// destination of the transfer.
		krenderbuffer read = renderer_renderbuffer_create(backend->frontend_state, kname_create("renderbuffer_read"), RENDERBUFFER_TYPE_READ, size, RENDERBUFFER_TRACK_TYPE_NONE, RENDERBUFFER_FLAG_NONE);
		if (read == KRENDERBUFFER_INVALID) {
			KERROR("vulkan_buffer_read() - Failed to create read buffer.");
			return false;
		}
		vulkan_buffer *read_internal = &context->renderbuffers[read];

		// Perform the copy from device local to the read buffer.
		vulkan_buffer_copy_range(backend, handle, offset, read, 0, size, true);

		// Map/copy/unmap
		void *mapped_data;
		VK_CHECK(rhi->kvkMapMemory(context->device.logical_device, read_internal->infos[index].memory,
								   0, size, 0, &mapped_data));
		kcopy_memory(*out_memory, mapped_data, size);
		rhi->kvkUnmapMemory(context->device.logical_device, read_internal->infos[index].memory);

		// Clean up the read buffer.
		renderer_renderbuffer_unbind(backend->frontend_state, read);
		renderer_renderbuffer_destroy(backend->frontend_state, read);
	} else {
		// If no staging buffer is needed, map/copy/unmap.
		void *data_ptr;
		VK_CHECK(rhi->kvkMapMemory(context->device.logical_device,
								   internal_buffer->infos[index].memory, offset, size, 0, &data_ptr));
		kcopy_memory(*out_memory, data_ptr, size);
		rhi->kvkUnmapMemory(context->device.logical_device, internal_buffer->infos[index].memory);
	}

	return true;
}

b8 vulkan_buffer_load_range (
	renderer_backend_interface *backend,
	krenderbuffer handle,
	u64 offset,
	u64 size,
	const void *data,
	b8 include_in_frame_workload) {
	//
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	if (handle == KRENDERBUFFER_INVALID || !size || !data) {
		KERROR("%s - requires a valid pointer to a buffer, a nonzero size and a valid pointer to data.", __FUNCTION__);
		return false;
	}

	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];
	u8 index = internal_buffer->handle_count == 1 ? 0 : get_current_image_index(context);
	if (vulkan_buffer_is_device_local(backend, internal_buffer) &&
		!vulkan_buffer_is_host_visible(backend, internal_buffer)) {
		// NOTE: If a staging buffer is needed (i.e.) the target buffer's memory is
		// not host visible but is device-local, create a staging buffer to load the
		// data into first. Then copy from it to the target buffer.

		// Load the data into the staging buffer.
		u64 staging_offset = 0;
		krenderbuffer staging = context->current_window->renderer_state->backend_state->staging[get_current_frame_index(context)];
		renderer_renderbuffer_allocate(backend->frontend_state, staging, size, &staging_offset);
		vulkan_buffer_load_range(backend, staging, staging_offset, size, data, include_in_frame_workload);

		// Perform the copy from staging to the device local buffer.
		vulkan_buffer_copy_range(backend, staging, staging_offset, handle, offset, size, include_in_frame_workload);
	} else {
		// If no staging buffer is needed, map/copy/unmap.
		void *data_ptr;
		VK_CHECK(rhi->kvkMapMemory(context->device.logical_device, internal_buffer->infos[index].memory, offset, size, 0, &data_ptr));
		kcopy_memory(data_ptr, data, size);
		rhi->kvkUnmapMemory(context->device.logical_device, internal_buffer->infos[index].memory);
	}

	return true;
}

static b8 vulkan_buffer_copy_range_internal (
	vulkan_context *context,
	VkBuffer source, u64 source_offset,
	VkBuffer dest, u64 dest_offset,
	u64 size, b8 include_in_frame_workload) {
	//
	krhi_vulkan *rhi = &context->rhi;
	VkQueue queue = context->device.graphics_queue;
	vulkan_command_buffer temp_command_buffer;
	vulkan_command_buffer *command_buffer = 0;

	// If not including in frame workload, then utilize a new temp command buffer as well. Otherwise this should be done
	// as part of the current frame's work.
	if (!include_in_frame_workload) {
		rhi->kvkQueueWaitIdle(queue);
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
	rhi->kvkCmdCopyBuffer(command_buffer->handle, source, dest, 1, &copy_region);

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

		rhi->kvkCmdPipelineBarrier(command_buffer->handle,
								   srcStage, dstStage,
								   0, 1, &memoryBarrier, 0, 0, 0, 0);
	}
	// NOTE: if not waiting, submission will be handled later.

	return true;
}

b8 vulkan_buffer_copy_range (
	renderer_backend_interface *backend,
	krenderbuffer source,
	u64 source_offset,
	krenderbuffer dest,
	u64 dest_offset,
	u64 size,
	b8 include_in_frame_workload) {
	//
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	if (source == KRENDERBUFFER_INVALID || dest == KRENDERBUFFER_INVALID || !size) {
		KERROR("%s - requires a valid pointers to source and destination buffers as well as a nonzero size.", __FUNCTION__);
		return false;
	}

	vulkan_buffer *source_internal = &context->renderbuffers[source];
	vulkan_buffer *dest_internal = &context->renderbuffers[dest];

	u8 source_index = source_internal->handle_count == 1 ? 0 : get_current_image_index(context);
	u8 dest_index = dest_internal->handle_count == 1 ? 0 : get_current_image_index(context);

	return vulkan_buffer_copy_range_internal(
		context, source_internal->infos[source_index].handle, source_offset,
		dest_internal->infos[dest_index].handle, dest_offset, size, include_in_frame_workload);
	return true;
}

b8 vulkan_buffer_draw (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 element_count, u32 binding_index, b8 bind_only) {
	if (!vulkan_buffer_bind(backend, handle, offset, binding_index)) {
		KERROR("Failed to bind renderbuffer. See logs for details.");
		return false;
	}

	if (bind_only) {
		return true;
	}

	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);
	vulkan_buffer *internal_buffer = &context->renderbuffers[handle];

	if (internal_buffer->type == RENDERBUFFER_TYPE_VERTEX) {
		rhi->kvkCmdDraw(command_buffer->handle, element_count, 1, 0, 0);
	} else if (internal_buffer->type == RENDERBUFFER_TYPE_INDEX) {
		rhi->kvkCmdDrawIndexed(command_buffer->handle, element_count, 1, 0, 0, 0);
	} else {
		KERROR("Cannot draw buffer of type: %i", internal_buffer->type);
		return false;
	}

	return true;
}

void vulkan_renderer_wait_for_idle (renderer_backend_interface *backend) {
	if (backend) {
		vulkan_context *context = backend->internal_context;
		krhi_vulkan *rhi = &context->rhi;
		VK_CHECK(rhi->kvkDeviceWaitIdle(context->device.logical_device));
	}
}

void vulkan_renderer_gpu_profiler_initialize (renderer_backend_interface *backend, kgpu_profiler *profiler) {
	if (backend && profiler) {
		vulkan_context *context = backend->internal_context;
		profiler->timestamp_period = context->device.properties.limits.timestampPeriod;
	}
}

void vulkan_renderer_gpu_profiler_destroy (renderer_backend_interface *backend, kgpu_profiler *profiler) {
	// NOTE: probably will always be a no-op for this renderer.
}
void vulkan_renderer_gpu_profiler_begin_frame (renderer_backend_interface *backend, kgpu_profiler *profiler) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// profiling - reset queries for current frame.
	profiler->query_base = get_current_frame_index(context) * KGPU_PROFILE_MAX_TIMESTAMPS * 2;
	rhi->kvkCmdResetQueryPool(command_buffer->handle, context->query_pool, profiler->query_base, KGPU_PROFILE_MAX_TIMESTAMPS * 2);
}
void vulkan_renderer_gpu_profiler_end_frame (renderer_backend_interface *backend, kgpu_profiler *profiler) {
	// no-op for now
}
void vulkan_renderer_gpu_profiler_begin_event (renderer_backend_interface *backend, kgpu_profiler_eventid id) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Write a timestamp for the beginning of the event, signified by the provided id.
	rhi->kvkCmdWriteTimestamp2(command_buffer->handle, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, context->query_pool, id);
}
void vulkan_renderer_gpu_profiler_end_event (renderer_backend_interface *backend, kgpu_profiler_eventid id) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	// Write a timestamp for the end of the event, signified by the provided id.
	rhi->kvkCmdWriteTimestamp2(command_buffer->handle, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, context->query_pool, id);
}

struct TimestampResult {
	u64 timestamp;
	u64 available;
};
void vulkan_renderer_gpu_profiler_query_timestamps (renderer_backend_interface *backend, u32 begin_id, u64 *out_start, u64 *out_end) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;

	struct TimestampResult stamps[2];

	VkResult result = rhi->kvkGetQueryPoolResults(
		context->device.logical_device,
		context->query_pool,
		begin_id,
		2, // Get the begin and the next, which is the end
		sizeof(stamps),
		stamps,
		sizeof(struct TimestampResult),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

	if (vulkan_result_is_success(result)) {
		if (stamps[0].available && stamps[1].available) {
			*out_start = stamps[0].timestamp;
			*out_end = stamps[1].timestamp;
		}
	}
}

#if KOHI_DEBUG
void vulkan_renderer_debug_pump_brakes (renderer_backend_interface *backend) {
	vulkan_context *context = backend->internal_context;
	vulkan_command_buffer *command_buffer = get_current_command_buffer(context);

	context->rhi.kvkCmdPipelineBarrier(
		command_buffer->handle,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		1, &(VkMemoryBarrier){
			   .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			   .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			   .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		   },
		0, NULL, 0, NULL);
}
#endif

static vulkan_command_buffer *get_current_command_buffer (vulkan_context *context) {
	kwindow_renderer_backend_state *window_backend = context->current_window->renderer_state->backend_state;
	vulkan_command_buffer *primary = &window_backend->graphics_command_buffers[window_backend->current_frame];

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

static u32 get_current_image_index (vulkan_context *context) {
	return context->current_window->renderer_state->backend_state->image_index;
}
static u32 get_current_frame_index (vulkan_context *context) {
	return context->current_window->renderer_state->backend_state->current_frame;
}

static u32 get_current_image_count (vulkan_context *context) {
	// 3 for triple-buffered, otherwise 2.
	return context->triple_buffering_enabled ? 3 : 2;
}

static b8 vulkan_graphics_pipeline_create (vulkan_context *context, const vulkan_pipeline_config *config, vulkan_pipeline *out_pipeline) {
	krhi_vulkan *rhi = &context->rhi;
	// Viewport state is dynamic, but the count isn't (can use VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT to have this be dynamic and eliminate this struct).
	VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	// Rasterizer
	VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	rasterizer_create_info.depthClampEnable = VK_FALSE;
	rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
	rasterizer_create_info.polygonMode = (config->shader_flags & SHADER_FLAG_WIREFRAME_BIT) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	rasterizer_create_info.lineWidth = 1.0f;
	/* switch (config->cull_mode) {
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
	} */
	// FIXME: remove this since this should be dynamic state and thus should not be required for this pipeline creation.
	rasterizer_create_info.cullMode = VK_CULL_MODE_NONE;

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
		depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS; // VK_COMPARE_OP_LESS_OR_EQUAL; // VK_COMPARE_OP_LESS;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
	}

	rasterizer_create_info.depthBiasEnable = VK_TRUE;

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
	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {0};
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
	u8 d_state_count = 0;
	VkDynamicState dynamic_states[32];
	dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_VIEWPORT;
	dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_SCISSOR;
	// Dynamic state, if supported.
	if ((context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT) || (context->device.support_flags & VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT)) {
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_FRONT_FACE;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_STENCIL_OP;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_CULL_MODE;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE;
		/* dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;
		dynamic_states[d_state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT; */
	}

	VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamic_state_create_info.dynamicStateCount = d_state_count;
	dynamic_state_create_info.pDynamicStates = dynamic_states;

	// Vertex input
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertex_input_info.vertexBindingDescriptionCount = 1;
	VkVertexInputBindingDescription binding = {
		.binding = 0, // First and only binding
		.stride = config->attribute_stride,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX // Move to next data entry for each vertex.
	};

	vertex_input_info.vertexAttributeDescriptionCount = config->attribute_count;
	VkVertexInputAttributeDescription *out_attribs = KALLOC_TYPE_CARRAY(VkVertexInputAttributeDescription, config->attribute_count);
	for (u32 a = 0; a < config->attribute_count; ++a) {
		out_attribs[a] = config->attributes[a];
		out_attribs[a].binding = 0;
		out_attribs[a].location = a;
	}

	vertex_input_info.pVertexBindingDescriptions = &binding;
	vertex_input_info.pVertexAttributeDescriptions = out_attribs;

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
	VK_CHECK(rhi->kvkCreatePipelineLayout(
		context->device.logical_device,
		&pipeline_layout_create_info,
		context->allocator,
		&out_pipeline->pipeline_layout));

#if KOHI_DEBUG
	char *pipeline_layout_name_buf = string_format("pipeline_layout_shader_%s", config->name);
	VK_SET_DEBUG_OBJECT_NAME(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT, out_pipeline->pipeline_layout, pipeline_layout_name_buf);
	string_free(pipeline_layout_name_buf);
#endif

	// Pipeline create
	VkGraphicsPipelineCreateInfo pipeline_create_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	pipeline_create_info.stageCount = config->stage_count;
	pipeline_create_info.pStages = config->stage_create_infos;
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

	VkResult result = rhi->kvkCreateGraphicsPipelines(
		context->device.logical_device,
		VK_NULL_HANDLE,
		1,
		&pipeline_create_info,
		context->allocator,
		&out_pipeline->handle);

	// Cleanup
	kfree(out_attribs);

#if KOHI_DEBUG
	char *pipeline_name_buf = string_format("pipeline_shader_%s", config->name);
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

static void vulkan_pipeline_destroy (vulkan_context *context, vulkan_pipeline *pipeline) {
	krhi_vulkan *rhi = &context->rhi;
	if (pipeline) {
		// Destroy pipeline
		if (pipeline->handle) {
			rhi->kvkDestroyPipeline(context->device.logical_device, pipeline->handle, context->allocator);
			pipeline->handle = 0;
		}

		// Destroy layout
		if (pipeline->pipeline_layout) {
			rhi->kvkDestroyPipelineLayout(context->device.logical_device, pipeline->pipeline_layout, context->allocator);
			pipeline->pipeline_layout = 0;
		}
	}
}

static void vulkan_pipeline_bind (vulkan_context *context, vulkan_command_buffer *command_buffer, VkPipelineBindPoint bind_point, vulkan_pipeline *pipeline) {
	krhi_vulkan *rhi = &context->rhi;
	rhi->kvkCmdBindPipeline(command_buffer->handle, bind_point, pipeline->handle);
}

static b8 shader_create_modules_and_pipelines (renderer_backend_interface *backend, vulkan_shader *internal_shader, shader_pipeline_config *config, vulkan_vertex_layout_pipeline *p) {
	vulkan_context *context = (vulkan_context *)backend->internal_context;
	krhi_vulkan *rhi = &context->rhi;

	b8 has_error = false;

	// Only dynamic topology is supported. Create one pipeline per topology class.
	// If this isn't supported, perhaps a different backend should be used.
	u32 pipeline_count = 3;

	// Create a temporary array for the pipelines to sit in. These will sit here until all loading is
	// complete, in the event this is called during a reload. This will ensure the current pipelines continue to
	// function as they should until this load is complete and ready to go successfully.
	vulkan_pipeline *new_pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
	// Same for wireframe_pipelines, if needed.
	vulkan_pipeline *new_wireframe_pipelines = 0;
	if (p->wireframe_pipelines) {
		new_wireframe_pipelines = kallocate(sizeof(vulkan_pipeline) * pipeline_count, MEMORY_TAG_ARRAY);
	}

	// Create a module for each stage.
	vulkan_shader_stage new_stages[VULKAN_SHADER_MAX_STAGES] = {0};
	for (u32 s = 0; s < config->stage_count; ++s) {
		const char *stage_name = shader_stage_to_string(config->stages[s]);
		if (!create_shader_module(context, internal_shader, config->stages[s], config->stage_sources[s], stage_name, &new_stages[s])) {
			KERROR("Unable to create %s shader module for '%k'. Shader will be destroyed.", stage_name, internal_shader->name);
			has_error = true;
			goto shader_module_pipeline_cleanup;
		}
	}

	VkPipelineShaderStageCreateInfo stage_create_infos[VULKAN_SHADER_MAX_STAGES];
	kzero_memory(stage_create_infos, sizeof(VkPipelineShaderStageCreateInfo) * VULKAN_SHADER_MAX_STAGES);
	for (u32 i = 0; i < p->stage_count; ++i) {
		stage_create_infos[i] = new_stages[i].shader_stage_create_info;
	}

	// Loop through and config/create one pipeline per class. Entries with no supported topology types are skipped.
	for (u32 i = 0; i < pipeline_count; ++i) {
		if (p->pipelines[i].supported_topology_types == PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
			continue;
		}

		// Make sure the supported types are noted in the temp array pipelines.
		new_pipelines[i].supported_topology_types = p->pipelines[i].supported_topology_types;
		if (p->wireframe_pipelines) {
			new_wireframe_pipelines[i].supported_topology_types = p->wireframe_pipelines[i].supported_topology_types;
		}

		vulkan_pipeline_config pipeline_config = {0};
		pipeline_config.descriptor_set_layout_count = internal_shader->descriptor_set_count;
		pipeline_config.descriptor_set_layouts = internal_shader->descriptor_set_layouts;
		pipeline_config.stage_count = p->stage_count;
		pipeline_config.stage_create_infos = stage_create_infos;
		pipeline_config.stages = p->stages;
		pipeline_config.attribute_count = p->attribute_count;
		pipeline_config.attributes = p->attributes;
		pipeline_config.attribute_stride = p->attribute_stride;

		// Strip the wireframe flag if it's there.
		shader_flag_bits flags = internal_shader->flags;
		flags &= ~(SHADER_FLAG_WIREFRAME_BIT);
		pipeline_config.shader_flags = flags;

		// NOTE: Always one block for the push constant, unless there is no per-draw UBO uniforms.
		krange push_constant_range = {0};
		internal_shader->immediate_size = 128; // FIXME: Should probably switch this on some kind of "uses_immediate" flag on the shader...
		if (internal_shader->immediate_size) {
			pipeline_config.push_constant_range_count = 1;
			push_constant_range.offset = 0;
			push_constant_range.size = internal_shader->immediate_size;
			pipeline_config.push_constant_ranges = &push_constant_range;
		} else {
			pipeline_config.push_constant_range_count = 0;
			pipeline_config.push_constant_ranges = 0;
		}
		pipeline_config.name = string_duplicate(kname_string_get(internal_shader->name));
		pipeline_config.topology_types = internal_shader->topology_types;

		if ((internal_shader->flags & SHADER_FLAG_COLOUR_READ_BIT) || (internal_shader->flags & SHADER_FLAG_COLOUR_WRITE_BIT)) {
			pipeline_config.colour_attachment_count = internal_shader->colour_attachment_count;
			pipeline_config.colour_attachment_formats = internal_shader->colour_attachments;
		} else {
			pipeline_config.colour_attachment_count = 0;
			pipeline_config.colour_attachment_formats = 0;
		}

		if ((internal_shader->flags & SHADER_FLAG_DEPTH_TEST_BIT) || (internal_shader->flags & SHADER_FLAG_DEPTH_WRITE_BIT) || (internal_shader->flags & SHADER_FLAG_STENCIL_TEST_BIT) || (internal_shader->flags & SHADER_FLAG_STENCIL_WRITE_BIT)) {
			pipeline_config.depth_attachment_format = internal_shader->depth_attachment;	 // context->device.depth_format;
			pipeline_config.stencil_attachment_format = internal_shader->stencil_attachment; // context->device.depth_format;
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

		string_free(pipeline_config.name);

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
		for (u32 i = 0; i < p->stage_count; ++i) {
			rhi->kvkDestroyShaderModule(context->device.logical_device, new_stages[i].handle, context->allocator);
		}
		goto shader_module_pipeline_cleanup;
	}

	// In success, destroy the old pipelines and move the new pipelines over.
	rhi->kvkDeviceWaitIdle(context->device.logical_device);
	for (u32 i = 0; i < pipeline_count; ++i) {
		if (p->pipelines[i].supported_topology_types != PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
			vulkan_pipeline_destroy(context, &p->pipelines[i]);
			kcopy_memory(&p->pipelines[i], &new_pipelines[i], sizeof(vulkan_pipeline));
		}
		if (new_wireframe_pipelines) {
			if (p->wireframe_pipelines[i].supported_topology_types != PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
				vulkan_pipeline_destroy(context, &p->wireframe_pipelines[i]);
				kcopy_memory(&p->wireframe_pipelines[i], &new_wireframe_pipelines[i], sizeof(vulkan_pipeline));
			}
		}
	}

	// Destroy the old shader modules and copy over the new ones.
	for (u32 i = 0; i < p->stage_count; ++i) {
		rhi->kvkDestroyShaderModule(context->device.logical_device, p->stages[i].handle, context->allocator);
		kcopy_memory(&p->stages[i], &new_stages[i], sizeof(vulkan_shader_stage));
	}

shader_module_pipeline_cleanup:
	kfree(new_pipelines);
	if (new_wireframe_pipelines) {
		kfree(new_wireframe_pipelines);
	}

	return !has_error;
}

static b8 vulkan_descriptorset_update_and_bind (
	vulkan_context *context,
	u16 renderer_frame_number,
	vulkan_shader *internal_shader,
	u8 vertex_pipeline_index,
	u32 descriptor_set_index,
	u32 instance_id) {

	krhi_vulkan *rhi = &context->rhi;

	u32 image_index = get_current_image_index(context);

	const frame_data *p_frame_data = engine_frame_data_get();
	/* vulkan_descriptor_set_config set_config = internal_shader->descriptor_set_configs[descriptor_set_index]; */
	vulkan_shader_binding_set_state *set_state = &internal_shader->binding_set_states[descriptor_set_index];

	// Allocate enough descriptor writes to handle one UBO, all samplers and all textures.
	u32 max_desc_write_count = 1 + set_state->texture_binding_count + set_state->sampler_binding_count + set_state->ssbo_binding_count;
	// NOTE: Using the frame allocator, so this does not have to be freed as it's handled automatically at the end of the frame on allocator reset.
	VkWriteDescriptorSet *descriptor_writes = p_frame_data->allocator.allocate(sizeof(VkWriteDescriptorSet) * max_desc_write_count);
	kzero_memory(descriptor_writes, sizeof(VkWriteDescriptorSet) * max_desc_write_count);

	vulkan_shader_binding_set_instance_state *instance_state = &set_state->instances[instance_id];

	u32 binding_index = 0;
	VkDescriptorBufferInfo ubo_buffer_info = {0};

	u8 ssbo_index = 0;
	VkDescriptorBufferInfo *ssbo_buffers = set_state->ssbo_binding_count ? p_frame_data->allocator.allocate(sizeof(VkDescriptorBufferInfo) * set_state->ssbo_binding_count) : 0;

	// Don't update this instance if already done this frame.
	if (instance_state->renderer_frame_number != renderer_frame_number) {

		// Each binding within the set
		for (u8 b = 0; b < set_state->binding_count; ++b) {
			vulkan_shader_binding *binding = &set_state->bindings[b];

			VkWriteDescriptorSet *write = &descriptor_writes[binding_index];
			write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

			u32 desc_count = 0;
			switch (binding->binding_type) {
			case SHADER_BINDING_TYPE_UBO: {
				write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

				// Only do this if the descriptor has not yet been updated.
				vulkan_buffer *buf = &context->renderbuffers[internal_shader->uniform_buffer];

				u8 index = buf->handle_count == 1 ? 0 : get_current_image_index(context);

				ubo_buffer_info.buffer = buf->infos[index].handle;
				KASSERT_MSG((instance_state->ubo_offset % context->device.properties.limits.minUniformBufferOffsetAlignment) == 0, "Ubo offset must be a multiple of device.properties.limits.minUniformBufferOffsetAlignment.");
				ubo_buffer_info.offset = instance_state->ubo_offset;
				ubo_buffer_info.range = instance_state->ubo_stride;
				write->pBufferInfo = &ubo_buffer_info;

				desc_count = 1;
			} break;

			case SHADER_BINDING_TYPE_SSBO: {
				write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				krenderbuffer buffer = instance_state->ssbo_states[binding->binding_type_index].buffer;
				vulkan_buffer *buf = &context->renderbuffers[buffer];
				u8 index = buf->handle_count == 1 ? 0 : get_current_image_index(context);

				VkDescriptorBufferInfo *ssbo_info = &ssbo_buffers[ssbo_index];
				ssbo_info->buffer = buf->infos[index].handle;
				ssbo_info->offset = 0;
				ssbo_info->range = VK_WHOLE_SIZE;
				write->pBufferInfo = ssbo_info;

				ssbo_index++;

				desc_count = 1;

			} break;
			case SHADER_BINDING_TYPE_TEXTURE: {
				write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

				vulkan_texture_state *tx_state = &instance_state->texture_states[binding->binding_type_index];
				u8 array_size = KMAX(tx_state->array_size, 1);

				u64 sz = sizeof(VkDescriptorImageInfo) * array_size;
				VkDescriptorImageInfo *binding_texture_infos = p_frame_data->allocator.allocate(sz);
				kzero_memory(binding_texture_infos, sz);

				for (u8 t = 0; t < array_size; ++t) {
					VkDescriptorImageInfo *binding_image_info = &binding_texture_infos[t];

					ktexture tex = instance_state->texture_states[binding->binding_type_index].texture_handles[t];
					vulkan_texture_handle_data *texture = &context->textures[tex];

					u32 image_index = texture->image_count > 1 ? get_current_image_index(context) : 0;
					vulkan_image *image = &texture->images[image_index];

					binding_image_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					binding_image_info->imageView = image->view;
					if (!image->view) {
						ktexture d = renderer_default_texture_get(engine_systems_get()->renderer_system, RENDERER_DEFAULT_TEXTURE_BASE_COLOUR);
						texture = &context->textures[d];
						binding_image_info->imageView = texture->images[image_index].view;
					}
					// NOTE: Not using sampler in this descriptor.
					binding_image_info->sampler = 0;
				}

				desc_count = array_size;

				write->pImageInfo = binding_texture_infos;

			} break;
			case SHADER_BINDING_TYPE_SAMPLER: {
				write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

				vulkan_sampler_state *smp_state = &instance_state->sampler_states[binding->binding_type_index];
				u8 array_size = KMAX(smp_state->array_size, 1);

				u64 sz = sizeof(VkDescriptorImageInfo) * set_state->sampler_binding_count;
				VkDescriptorImageInfo *binding_sampler_infos = p_frame_data->allocator.allocate(sz);
				kzero_memory(binding_sampler_infos, sz);

				for (u8 s = 0; s < array_size; ++s) {
					VkDescriptorImageInfo *binding_image_info = &binding_sampler_infos[s];

					ksampler_backend smp = instance_state->sampler_states[binding->binding_type_index].sampler_handles[s];
					vulkan_sampler_handle_data *sampler = &context->samplers[smp];

					// NOTE: Not using image in this descriptor.
					binding_image_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					binding_image_info->imageView = 0;

					binding_image_info->sampler = sampler->sampler ? sampler->sampler : context->samplers[0].sampler;
				}

				desc_count = smp_state->array_size;

				write->pImageInfo = binding_sampler_infos;
			} break;
			case SHADER_BINDING_TYPE_COUNT:
				KFATAL("Why are you trying to bind the count, ya dingus?");
				return false;
			}

			write->dstSet = instance_state->descriptor_sets[image_index];
			write->dstBinding = b;
			write->descriptorCount = desc_count;

			binding_index++;
		}

		// Immediately update the descriptor set's data.
		if (binding_index > 0) {
			rhi->kvkUpdateDescriptorSets(context->device.logical_device, binding_index, descriptor_writes, 0, 0);
		}
	}

	// Pick the correct pipeline.
	b8 wireframe_enabled = FLAG_GET(internal_shader->flags, SHADER_FLAG_WIREFRAME_BIT);
	vulkan_vertex_layout_pipeline *p = &internal_shader->vertex_layout_pipelines[vertex_pipeline_index];
	vulkan_pipeline *pipeline_array = wireframe_enabled ? p->wireframe_pipelines : p->pipelines;

	VkCommandBuffer command_buffer = get_current_command_buffer(context)->handle;
	// Bind the descriptor set to be updated, or in case the shader changed.
	rhi->kvkCmdBindDescriptorSets(
		command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline_array[p->bound_pipeline_index].pipeline_layout,
		descriptor_set_index,
		1,
		&instance_state->descriptor_sets[image_index],
		0,
		0);

	// Sync the renderer frame number.
	instance_state->renderer_frame_number = renderer_frame_number;

	return true;
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
static void *vulkan_alloc_allocation (
	void *user_data,
	size_t size,
	size_t alignment,
	VkSystemAllocationScope allocation_scope) {

	// Null MUST be returned if this fails.
	if (size == 0) {
		return 0;
	}

	// Size must be aligned also.
	size = get_aligned(size, alignment);

	void *result = kallocate_aligned(size, (u16)alignment, MEMORY_TAG_VULKAN);
#	ifdef KVULKAN_ALLOCATOR_TRACE
	KTRACE("Allocated block %p. Size=%llu, Alignment=%llu", result, size, alignment);
#	endif
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
static void vulkan_alloc_free (void *user_data, void *memory) {
	if (!memory) {
#	ifdef KVULKAN_ALLOCATOR_TRACE
		KTRACE("Block is null, nothing to free: %p", memory);
#	endif
		return;
	}

#	ifdef KVULKAN_ALLOCATOR_TRACE
	KTRACE("Attempting to free block %p...", memory);
#	endif
	u64 size;
	u16 alignment;
	memory_tag tag;
	b8 result = kmemory_get_size_alignment(memory, &size, &alignment, &tag);
	if (result) {
#	ifdef KVULKAN_ALLOCATOR_TRACE
		KTRACE(
			"Block %p found with size/alignment: %llu/%u. Freeing aligned block...",
			memory, size, alignment);
#	endif

		// Size must be aligned also.
		size = get_aligned(size, alignment);

		kfree_aligned(memory);
	} else {
		KERROR("vulkan_alloc_free failed to get alignment lookup for block %p.", memory);
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
static void *vulkan_alloc_reallocation (
	void *user_data,
	void *original,
	size_t size,
	size_t alignment,
	VkSystemAllocationScope allocation_scope) {
	if (!original) {
		return kallocate_aligned(size, (u16)alignment, MEMORY_TAG_VULKAN);
	}

	if (size == 0) {
		vulkan_alloc_free(user_data, original);
		return 0;
	}

	// Size must be aligned also.
	size = get_aligned(size, alignment);

	// NOTE: if pOriginal is not null, the same alignment must be used for the new
	// allocation as original.
	u64 original_alloc_size;
	u16 original_alloc_alignment;
	memory_tag original_tag;
	b8 is_aligned = kmemory_get_size_alignment(original, &original_alloc_size, &original_alloc_alignment, &original_tag);
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

#	ifdef KVULKAN_ALLOCATOR_TRACE
	KTRACE("Attempting to realloc block %p...", original);
#	endif

	void *result = kallocate_aligned(size, (u16)alignment, MEMORY_TAG_VULKAN);
	/* void* result = vulkan_alloc_allocation(user_data, size, original_alloc_alignment, allocation_scope); */
	if (result && original) {
#	ifdef KVULKAN_ALLOCATOR_TRACE
		KTRACE("Block %p reallocated to %p, copying data...", original, result);
#	endif

		// Copy over the original memory.
		kcopy_memory(result, original, KMIN(size, original_alloc_size));
#	ifdef KVULKAN_ALLOCATOR_TRACE
		KTRACE("Freeing original aligned block %p...", original);
#	endif
		// Free the original memory only if the new allocation was successful.
		kfree_aligned(original);
	} else {
#	ifdef KVULKAN_ALLOCATOR_TRACE
		KERROR("Failed to realloc %p.", original);
#	endif
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
static void vulkan_alloc_internal_alloc (void *pUserData, size_t size,
										 VkInternalAllocationType allocationType,
										 VkSystemAllocationScope allocationScope) {
#	ifdef KVULKAN_ALLOCATOR_TRACE
	KTRACE("External allocation of size: %llu", size);
#	endif
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
static void vulkan_alloc_internal_free (void *pUserData, size_t size,
										VkInternalAllocationType allocationType,
										VkSystemAllocationScope allocationScope) {
#	ifdef KVULKAN_ALLOCATOR_TRACE
	KTRACE("External free of size: %llu", size);
#	endif
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
static b8 create_vulkan_allocator (vulkan_context *context,
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

#endif // KVULKAN_USE_CUSTOM_ALLOCATOR == 1
