/**
 * @file vulkan_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains a collection fo Vulkan-specific types used
 * for the Vulkan backend.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include <vulkan/vulkan.h>

#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "math/math_types.h"
#include "platform/vulkan_platform.h"
#include "renderer/renderer_types.h"
#include "vulkan/vulkan_core.h"

// Frames in flight can differ for double-buffering (1) or triple-buffering (2), but will never exceed this amount.
#define VULKAN_MAX_FRAMES_IN_FLIGHT 2
// The colour buffer count can differ for double-buffering (2) or triple-buffering (3), but will never exceed this amount.
#define VULKAN_MAX_COLOUR_BUFFER_COUNT 3

// The array size for resources created per-image. Regardless of whether double- or
// triple-buffering is used, this should always be used for resource array sizes so that
// triple buffering can be toggled in settings.
#define VULKAN_RESOURCE_IMAGE_COUNT 3

/**
 * @brief Checks the given expression's return value against VK_SUCCESS.
 * @param expr The expression whose result should be checked.
 */
#define VK_CHECK(expr)                                                                                 \
	{                                                                                                  \
		VkResult vulkan_expr_result = expr;                                                            \
		KASSERT_MSG(vulkan_expr_result == VK_SUCCESS, vulkan_result_string(vulkan_expr_result, true)); \
	}

struct vulkan_context;

typedef struct vkbuffer_info {
	VkBuffer handle;
	VkDeviceMemory memory;
	// 0 unless buffer has been mapped to it.
	void *mapped_memory;
} vkbuffer_info;

/**
 * @brief Represents a Vulkan-specific buffer.
 * Used to load data onto the GPU.
 */
typedef struct vulkan_buffer {
	u8 handle_count;
	/** @brief An array of vulkan buffer infos, 3 if triple-buffering, otherwise 1 */
	vkbuffer_info *infos;
	/** @brief The usage flags. */
	VkBufferUsageFlags usage;
	/** @brief Indicates if the buffer's memory is currently locked. */
	b8 is_locked;
	/** @brief The memory requirements for this buffer. */
	VkMemoryRequirements memory_requirements;
	/** @brief The index of the memory used by the buffer. */
	i32 memory_index;
	/** @brief The property flags for the memory used by the buffer. */
	u32 memory_property_flags;
	u64 size;
	kname name;
	renderbuffer_type type;

	renderbuffer_flags flags;

} vulkan_buffer;

/** @brief Contains swapchain support information and capabilities. */
typedef struct vulkan_swapchain_support_info {
	/** @brief The surface capabilities. */
	VkSurfaceCapabilitiesKHR capabilities;
	/** @brief The number of available surface formats. */
	u32 format_count;
	/** @brief An array of the available surface formats. */
	VkSurfaceFormatKHR *formats;
	/** @brief The number of available presentation modes. */
	u32 present_mode_count;
	/** @brief An array of available presentation modes. */
	VkPresentModeKHR *present_modes;
} vulkan_swapchain_support_info;

typedef enum vulkan_device_support_flag_bits {
	VULKAN_DEVICE_SUPPORT_FLAG_NONE_BIT = 0x00,

	/** @brief Indicates if the device supports native dynamic state (i.e. using Vulkan API >= 1.3). */
	VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_STATE_BIT = 0x01,

	/** @brief Indicates if this device supports dynamic state. If not, the renderer will need to generate a separate pipeline per topology type. */
	VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_STATE_BIT = 0x02,
	VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT = 0x04
} vulkan_device_support_flag_bits;

/** @brief Bitwise flags for device support. @see vulkan_device_support_flag_bits. */
typedef u32 vulkan_device_support_flags;

/**
 * @brief A representation of both the physical and logical
 * Vulkan devices. Also contains handles to queues, command pools,
 * and various properties of the devices.
 */
typedef struct vulkan_device {
	/** @brief The supported device-level api major version. */
	u32 api_major;

	/** @brief The supported device-level api minor version. */
	u32 api_minor;

	/** @brief The supported device-level api patch version. */
	u32 api_patch;

	/** @brief The physical device. This is a representation of the GPU itself. */
	VkPhysicalDevice physical_device;
	/** @brief The logical device. This is the application's view of the device, used for most Vulkan operations. */
	VkDevice logical_device;
	/** @brief The swapchain support info. */
	vulkan_swapchain_support_info swapchain_support;

	/** @brief The index of the graphics queue. */
	i32 graphics_queue_index;
	/** @brief The index of the present queue. */
	i32 present_queue_index;
	/** @brief The index of the transfer queue. */
	i32 transfer_queue_index;
	/** @brief Indicates if the device supports a memory type that is both host visible and device local. */
	b8 supports_device_local_host_visible;

	/** @brief A handle to a graphics queue. */
	VkQueue graphics_queue;
	/** @brief A handle to a present queue. */
	VkQueue present_queue;
	/** @brief A handle to a transfer queue. */
	VkQueue transfer_queue;

	/** @brief A handle to a command pool for graphics operations. */
	VkCommandPool graphics_command_pool;

	/** @brief The physical device properties. */
	VkPhysicalDeviceProperties properties;
	/** @brief The physical device features. */
	VkPhysicalDeviceFeatures features;
	/** @brief The physical device memory properties. */
	VkPhysicalDeviceMemoryProperties memory;

	/** @brief The chosen supported depth format. */
	VkFormat depth_format;
	/** @brief The chosen depth format's number of channels.*/
	u8 depth_channel_count;

	/** @brief Indicates support for various features. */
	vulkan_device_support_flags support_flags;
} vulkan_device;

/**
 * @brief A representation of a Vulkan image. This can be thought
 * of as a texture. Also contains the view and memory used by
 * the internal image.
 */
typedef struct vulkan_image {
	/** @brief The handle to the internal image object. */
	VkImage handle;
	/** @brief The memory used by the image. */
	VkDeviceMemory memory;
	/** @brief The image creation info. */
	VkImageCreateInfo image_create_info;

	/** @brief The view for the image, which is used to access the image. */
	VkImageView view;
	VkImageSubresourceRange view_subresource_range;
	VkImageViewCreateInfo view_create_info;
	/** @brief If there are multiple layers, one view per layer exists here. */
	VkImageView *layer_views;
	VkImageSubresourceRange *layer_view_subresource_ranges;
	VkImageViewCreateInfo *layer_view_create_infos;
	/** @brief The GPU memory requirements for this image. */
	VkMemoryRequirements memory_requirements;
	/** @brief Memory property flags */
	VkMemoryPropertyFlags memory_flags;
	/** @brief The format of the image. */
	VkFormat format;
	/** @brief The image width. */
	u32 width;
	/** @brief The image height. */
	u32 height;
	/** @brief The number of layers in this image. */
	u16 layer_count;
	/** @brief The name of the image. */
	char *name;
	/** @brief texture flag bits */
	ktexture_flag_bits flags;
	/** The number of mipmaps to be generated for this image. Must always be at least 1. */
	u32 mip_levels;
	b8 has_view;
} vulkan_image;

/**
 * @brief Representation of the Vulkan swapchain.
 */
typedef struct vulkan_swapchain {
	/** @brief The swapchain image format. */
	VkSurfaceFormatKHR image_format;

	/** @brief Indicates various flags used for swapchain instantiation. */
	renderer_config_flags flags;

	/** @brief The swapchain internal handle. */
	VkSwapchainKHR handle;
	/** @brief The number of swapchain images. */
	u32 image_count;

	/** @brief Supports being used as a blit destination. */
	b8 supports_blit_dest;

	/** @brief Supports being used as a blit source. */
	b8 supports_blit_src;

	ktexture swapchain_colour_texture;

	/** @brief The swapchain image index (i.e. the swapchain image index that will be blitted to). */
	u32 image_index;
} vulkan_swapchain;

/**
 * @brief Represents all of the available states that
 * a command buffer can be in.
 */
typedef enum vulkan_command_buffer_state {
	/** @brief The command buffer is ready to begin. */
	COMMAND_BUFFER_STATE_READY,
	/** @brief The command buffer is currently being recorded to. */
	COMMAND_BUFFER_STATE_RECORDING,
	/** @brief The command buffer is currently active. */
	COMMAND_BUFFER_STATE_IN_RENDER_PASS,
	/** @brief The command buffer is has ended recording. */
	COMMAND_BUFFER_STATE_RECORDING_ENDED,
	/** @brief The command buffer has been submitted to the queue. */
	COMMAND_BUFFER_STATE_SUBMITTED,
	/** @brief The command buffer is not allocated. */
	COMMAND_BUFFER_STATE_NOT_ALLOCATED
} vulkan_command_buffer_state;

/**
 * @brief Represents a Vulkan-specific command buffer, which
 * holds a list of commands and is submitted to a queue
 * for execution.
 */
typedef struct vulkan_command_buffer {
	/** @brief The internal command buffer handle. */
	VkCommandBuffer handle;

#if KOHI_DEBUG
	// Name, kept for debugging purposes.
	const char *name;
#endif

	/** @brief Command buffer state. */
	vulkan_command_buffer_state state;

	/** @brief Indicates if this is a primary or secondary command buffer. */
	b8 is_primary;

	/** @brief The number of secondary buffers that are children to this one. Primary buffer use only. */
	u16 secondary_count;
	/** @brief An array of secondary buffers that are children to this one. Primary buffer use only. */
	struct vulkan_command_buffer *secondary_buffers;

	/** @brief The currently selected secondary buffer index. */
	u16 secondary_buffer_index;
	/** @brief Indicates if a secondary command buffer is currently being recorded to. */
	b8 in_secondary;

	/** A pointer to the parent (primary) command buffer, if there is one. Only applies to secondary buffers. */
	struct vulkan_command_buffer *parent;

	// Current attachments.
	u8 colour_attachment_count;
	ktexture colour_attachments[16];
	ktexture depth_attachment;
} vulkan_command_buffer;

/**
 * @brief Represents a single shader stage.
 */
typedef struct vulkan_shader_stage {
	shader_stage stage;
	/** @brief The shader module creation info. */
	VkShaderModuleCreateInfo create_info;
	/** @brief The internal shader module handle. */
	VkShaderModule handle;
	/** @brief The pipeline shader stage creation info. */
	VkPipelineShaderStageCreateInfo shader_stage_create_info;
} vulkan_shader_stage;

typedef enum vulkan_topology_class {
	VULKAN_TOPOLOGY_CLASS_POINT = 0,
	VULKAN_TOPOLOGY_CLASS_LINE = 1,
	VULKAN_TOPOLOGY_CLASS_TRIANGLE = 2,
	VULKAN_TOPOLOGY_CLASS_MAX = VULKAN_TOPOLOGY_CLASS_TRIANGLE + 1
} vulkan_topology_class;

/**
 * @brief Holds a Vulkan pipeline and its layout.
 */
typedef struct vulkan_pipeline {
	/** @brief The internal pipeline handle. */
	VkPipeline handle;
	/** @brief The pipeline layout. */
	VkPipelineLayout pipeline_layout;
	/** @brief Indicates the topology types used by this pipeline. See primitive_topology_type.*/
	primitive_topology_type_bits supported_topology_types;
} vulkan_pipeline;

typedef struct vulkan_vertex_layout_pipeline {
	/** @brief The number of stages in this vertex layout (vertex, fragment, etc). */
	u8 stage_count;
	vulkan_shader_stage *stages;
	VkPipelineShaderStageCreateInfo *stage_create_infos;
	// Shallow copy of config'd stage sources.
	const char **stage_sources;

	/** @brief The stride of the vertex data to be used (ex: sizeof(vertex_3d)) */
	u32 attribute_stride;
	/** @brief The number of attributes. */
	u32 attribute_count;
	/** @brief An array of attributes. */
	VkVertexInputAttributeDescription *attributes;

	/** @brief An array of pipelines associated with this vertex layout (one per topology class). */
	vulkan_pipeline *pipelines;
	/** @brief An array of wireframe pipelines associated with this vertex layout (one per topology class). */
	vulkan_pipeline *wireframe_pipelines;

	/** @brief The currently bound pipeline index. */
	u8 bound_pipeline_index;
	u8 default_pipeline_index;
} vulkan_vertex_layout_pipeline;

/**
 * @brief A configuration structure for Vulkan pipelines.
 */
typedef struct vulkan_pipeline_config {
	/** @brief The name of the pipeline. Used primarily for debugging purposes. */
	char *name;
	/** @brief The number of stages in this vertex layout (vertex, fragment, etc). */
	u8 stage_count;
	vulkan_shader_stage *stages;
	VkPipelineShaderStageCreateInfo *stage_create_infos;

	/** @brief The stride of the vertex data to be used (ex: sizeof(vertex_3d)) */
	u32 attribute_stride;
	/** @brief The number of attributes. */
	u32 attribute_count;
	/** @brief An array of attributes. */
	VkVertexInputAttributeDescription *attributes;

	/** @brief The number of descriptor set layouts. */
	u32 descriptor_set_layout_count;
	/** @brief An array of descriptor set layouts. */
	VkDescriptorSetLayout *descriptor_set_layouts;
	u32 shader_flags;
	/** @brief The number of push constant data ranges. */
	u32 push_constant_range_count;
	/** @brief An array of push constant data ranges. */
	krange *push_constant_ranges;
	/** @brief Collection of topology types to be supported on this pipeline. */
	u32 topology_types;
	/** @brief The vertex winding order used to determine the front face of triangles. */
	renderer_winding winding;

	u32 colour_attachment_count;
	VkFormat *colour_attachment_formats;
	VkFormat depth_attachment_format;
	VkFormat stencil_attachment_format;
} vulkan_pipeline_config;

/**
 * @brief Put some hard limits in place for the count of supported textures,
 * attributes, uniforms, etc. This is to maintain memory locality and avoid
 * dynamic allocations.
 */

/** @brief The maximum number of stages (such as vertex, fragment, compute, etc.) allowed. */
#define VULKAN_SHADER_MAX_STAGES 8
/** @brief The maximum number of texture bindings allowed at once. */
#define VULKAN_SHADER_MAX_TEXTURE_BINDINGS 16
/** @brief The maximum number of sampler bindings allowed at once. */
#define VULKAN_SHADER_MAX_SAMPLER_BINDINGS 16
/** @brief The maximum number of vertex input attributes allowed. */
#define VULKAN_SHADER_MAX_ATTRIBUTES 16

/** @brief The maximum number of push constant ranges for a shader. */
#define VULKAN_SHADER_MAX_PUSH_CONST_RANGES 32

/**
 * @brief The configuration for a descriptor set.
 */
typedef struct vulkan_descriptor_set_config {
	/** @brief The number of bindings in this set. */
	u8 binding_count;
	/** @brief An array of binding layouts for this set. */
	VkDescriptorSetLayoutBinding *bindings;
} vulkan_descriptor_set_config;

/**
 * @brief Represents a state for a given descriptor. This is used
 * to determine when a descriptor needs updating. There is a state
 * per frame (with a max of 3).
 */
typedef struct vulkan_descriptor_state {
	/** @brief The renderer frame number on which this descriptor was last updated. One per colour image. INVALID_ID_U16 if never loaded. */
	u16 renderer_frame_number[VULKAN_RESOURCE_IMAGE_COUNT];
} vulkan_descriptor_state;

typedef struct vulkan_sampler_state {
	shader_sampler_type type;

	/**
	 * @brief An array of sampler handles. Count matches uniform array_count.
	 * Element count matches array_size.
	 */
	ksampler_backend *sampler_handles;

	/**
	 * @brief A descriptor state per sampler. Count matches uniform array_count.
	 */
	vulkan_descriptor_state *descriptor_states;

	u8 array_size;
} vulkan_sampler_state;

typedef struct vulkan_texture_state {
	ktexture_type type;

	/**
	 * @brief An array of handles to texture resources.
	 * Element count matches array_size.
	 */
	ktexture *texture_handles;

	/**
	 * @brief A descriptor state per descriptor, which in turn handles frames.
	 * Count is managed in shader config.
	 */
	vulkan_descriptor_state *descriptor_states;

	u8 array_size;
} vulkan_texture_state;

typedef struct vulkan_ssbo_state {
	/**
	 * @brief Handle to the underlying SSBO.
	 */
	krenderbuffer buffer;

	/**
	 * @brief A descriptor state per descriptor, which in turn handles frames.
	 * Count is managed in shader config.
	 */
	vulkan_descriptor_state descriptor_state;

} vulkan_ssbo_state;

typedef struct vulkan_shader_binding {
	shader_binding_type binding_type;
	u8 binding_type_index;
} vulkan_shader_binding;

/**
 * @brief The state for a shader binding set individual usage
 */
typedef struct vulkan_shader_binding_set_instance_state {
	/** @brief The actual size of the uniform buffer object for this set. */
	u64 ubo_size;
	/** @brief The stride of the uniform buffer object for this set. */
	u64 ubo_stride;
	/** @brief The offset in bytes in the uniform buffer. INVALID_ID_U64 if unused */
	u64 ubo_offset;

	/** @brief The descriptor sets for this set use, one per colour image. */
	VkDescriptorSet descriptor_sets[VULKAN_RESOURCE_IMAGE_COUNT];

	// UBO descriptor state for this set. Max of one UBO per set.
	vulkan_descriptor_state ubo_descriptor_state;

	// SSBO descriptor states for this set.
	vulkan_ssbo_state *ssbo_states;

	// A mapping of samplers to descriptors.
	vulkan_sampler_state *sampler_states;

	// A mapping of textures to descriptors.
	vulkan_texture_state *texture_states;

	// Used to determine if this instance state has already been updated for a given frame.
	u16 renderer_frame_number;

#if KOHI_DEBUG
	// Also the binding set index. Just here for debugging purposes (debug builds only)
	u32 descriptor_set_index;
#endif
} vulkan_shader_binding_set_instance_state;

/**
 * @brief The state for a shader binding set.
 */
typedef struct vulkan_shader_binding_set_state {
	// Total number of uses.
	u32 max_instance_count;
	// Binding set state per use. Array size = max_use_count
	vulkan_shader_binding_set_instance_state *instances;

	// The number of bindings in this set.
	u8 binding_count;
	// A lookup table of bindings for this binding set.
	vulkan_shader_binding *bindings;

	/** @brief the number of texture bindings for this binding set. */
	u8 texture_binding_count;
	/** @brief the number of storage buffer bindings for this binding set. */
	u8 ssbo_binding_count;
	/** @brief the number of sampler bindings for this binding set. */
	u8 sampler_binding_count;
} vulkan_shader_binding_set_state;

/**
 * @brief Represents a generic Vulkan shader. This uses a set of inputs
 * and parameters, as well as the shader programs contained in SPIR-V
 * files to construct a shader for use in rendering.
 */
typedef struct vulkan_shader {
	// The name of the shader (mostly kept for debugging purposes).
	kname name;

	/** @brief The shader identifier. */
	u32 id;

	/**
	 * @brief The total number of descriptor sets configured for this shader.
	 * Count matches binding set count.
	 */
	u8 descriptor_set_count;
	/** @brief Array of descriptor sets, matches binding set count. */
	vulkan_descriptor_set_config *descriptor_set_configs;
	/** @brief Descriptor set layouts, matches binding set count. */
	VkDescriptorSetLayout *descriptor_set_layouts;
	/** @brief Binding set states, matches binding set count. */
	vulkan_shader_binding_set_state *binding_set_states;

	/** @brief The topology types for the shader pipeline. See primitive_topology_type. Defaults to "triangle list" if unspecified. */
	primitive_topology_type topology_types;

	// The size of the immediates block of memory
	u8 immediate_size;

	u32 pool_size_count;

	/** @brief An array of descriptor pool sizes. */
	VkDescriptorPoolSize pool_sizes[SHADER_BINDING_TYPE_COUNT];

	/** @brief The descriptor pool used for this shader. */
	VkDescriptorPool descriptor_pool;

	/** @brief The uniform buffer used by this shader. Triple-buffered by default */
	krenderbuffer uniform_buffer;

	u8 colour_attachment_count;
	VkFormat *colour_attachments;

	VkFormat depth_attachment;
	VkFormat stencil_attachment;

	u8 vertex_layout_pipeline_count;
	// One per vertex layout
	vulkan_vertex_layout_pipeline *vertex_layout_pipelines;
	// Index into the vertex_layout_pipelines array.
	u8 vertex_layout_index;

	u16 renderer_frame_number;

	/** @brief The currently-selected topology. */
	VkPrimitiveTopology default_topology;

	// Shader flags
	shader_flags flags;
} vulkan_shader;

// Forward declare shaderc compiler.
struct shaderc_compiler;

/**
 * @brief The Vulkan-specific backend window state.
 *
 * This owns all resources associated with the window (i.e swapchain)
 * and anything tied to it or max_frames_in_flight (sync objects, staging
 * buffer, command buffers, etc.).
 */
typedef struct kwindow_renderer_backend_state {
	/** @brief The internal Vulkan surface for the window to be drawn to. */
	VkSurfaceKHR surface;
	/** @brief The swapchain. */
	vulkan_swapchain swapchain;

	/** @brief The current image index. */
	u32 image_index;
	/** @brief The current frame index ( % by max_frames_in_flight). */
	u32 current_frame;

	/** @brief Indicates the max number of frames in flight. 1 for double-buffering, 2 for triple-buffering. */
	u8 max_frames_in_flight;

	/** @brief Indicates if the swapchain is currently being recreated. */
	b8 recreating_swapchain;

	/** @brief The graphics command buffers, one per frame-in-flight. */
	vulkan_command_buffer *graphics_command_buffers;

	/** @brief The semaphores used to indicate image availability, one per frame in flight. */
	VkSemaphore *acquire_semaphores;

	/** @brief The semaphores used to indicate queue availability, one per swapchain image. */
	VkSemaphore *submit_semaphores;

	/**
	 * @brief The in-flight fences, used to indicate to the application when a frame is
	 * busy/ready. One per frame in flight.
	 */
	VkFence *in_flight_fences;

	/** @brief Resusable staging buffers (one per frame in flight) to transfer data from a resource to a GPU-only buffer. */
	krenderbuffer *staging;

	/**
	 * @brief Array of darrays of handles to textures that were updated as part of a frame's workload.
	 * One list per frame in flight.
	 */
	ktexture **frame_texture_updated_list;

	u64 framebuffer_size_generation;
	u64 framebuffer_previous_size_generation;

	u8 skip_frames;
} kwindow_renderer_backend_state;

typedef struct vulkan_sampler_handle_data {
	// Sampler name for named lookups and serialization.
	kname name;
	// The underlying sampler handle.
	VkSampler sampler;
} vulkan_sampler_handle_data;

/**
 * @brief Represents Vulkan-specific texture data.
 */
typedef struct vulkan_texture_handle_data {

	// The generation of the internal texture. Incremented every time the texture is changed.
	u16 generation;

	// Number of vulkan_images in the array. This is typically 1 unless the texture
	// requires the frame_count to be taken into account.
	u32 image_count;
	// Array of images. See image_count.
	vulkan_image *images;
} vulkan_texture_handle_data;

/**
 * @brief The overall Vulkan context for the backend. Holds and maintains
 * global renderer backend state, Vulkan instance, etc.
 */
typedef struct vulkan_context {
	/** @brief The instance-level api major version. */
	u32 api_major;

	/** @brief The instance-level api minor version. */
	u32 api_minor;

	/** @brief The instance-level api patch version. */
	u32 api_patch;

	renderer_config_flags flags;

	/** @brief The currently cached colour buffer clear value. */
	VkClearColorValue colour_clear_value;
	/** @brief The currently cached depth/stencil buffer clear value. */
	VkClearDepthStencilValue depth_stencil_clear_value;

	/** @brief The viewport rectangle. */
	rect_2di viewport_rect;

	/** @brief The scissor rectangle. */
	/* vec4 scissor_rect; */

	/** @brief The handle to the internal Vulkan instance. */
	VkInstance instance;
	/** @brief The internal Vulkan allocator. */
	VkAllocationCallbacks *allocator;

	/** @brief The debug messenger, if active.. */
	VkDebugUtilsMessengerEXT debug_messenger;

	/** @brief The function pointer to set debug object names. */
	PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;

	/** @brief The function pointer to set free-form debug object tag data. */
	PFN_vkSetDebugUtilsObjectTagEXT pfnSetDebugUtilsObjectTagEXT;

	PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
	PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;

	/** @brief The Vulkan device. */
	vulkan_device device;

	/** @brief A pointer to the current window whose resources should be used as default to render to. */
	struct kwindow *current_window;

	b8 render_flag_changed;

	b8 validation_enabled;

	/** @brief Indicates if multi-threading is supported by this device. */
	b8 multithreading_enabled;

	/** @brief Indicates if triple-buffering is enabled (requested) */
	b8 triple_buffering_enabled;

	/** @brief Collection of samplers. darray */
	vulkan_sampler_handle_data *samplers;

	u32 max_texture_count;
	/** @brief Collection of textures. Matches max texture size on frontend. */
	vulkan_texture_handle_data *textures;

	/** @brief Collection of vulkan shaders (internal shader data). Matches size of shader array in shader system. */
	vulkan_shader *shaders;

	PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;
	PFN_vkCmdSetFrontFaceEXT vkCmdSetFrontFaceEXT;
	PFN_vkCmdSetCullModeEXT vkCmdSetCullModeEXT;
	PFN_vkCmdSetStencilTestEnableEXT vkCmdSetStencilTestEnableEXT;
	PFN_vkCmdSetDepthTestEnableEXT vkCmdSetDepthTestEnableEXT;
	PFN_vkCmdSetDepthWriteEnableEXT vkCmdSetDepthWriteEnableEXT;
	PFN_vkCmdSetStencilOpEXT vkCmdSetStencilOpEXT;

	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
	PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR;

	// The render hardware interface.
	krhi_vulkan rhi;

	/** @brief Handle to the currently bound shader. */
	kshader bound_shader;

	// Darray of vulkan buffers, which matches up to the frontend's krenderbuffers.
	vulkan_buffer *renderbuffers;

	// Cached handles to renderbuffers.
	kname standard_vertex_buffer_name;
	kname index_buffer_name;

	/**
	 * Used for dynamic compilation of vulkan shaders (using the shaderc lib.)
	 */
	struct shaderc_compiler *shader_compiler;
} vulkan_context;
