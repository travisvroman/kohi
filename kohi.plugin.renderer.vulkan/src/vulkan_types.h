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
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "renderer/renderer_types.h"
#include "vulkan/vulkan_core.h"

/**
 * @brief Checks the given expression's return value against VK_SUCCESS.
 * @param expr The expression whose result should be checked.
 */
#define VK_CHECK(expr)               \
    {                                \
        KASSERT(expr == VK_SUCCESS); \
    }

struct vulkan_context;

/**
 * @brief Represents a Vulkan-specific buffer.
 * Used to load data onto the GPU.
 */
typedef struct vulkan_buffer {
    /** @brief The handle to the internal buffer. */
    VkBuffer handle;
    /** @brief The usage flags. */
    VkBufferUsageFlagBits usage;
    /** @brief Indicates if the buffer's memory is currently locked. */
    b8 is_locked;
    /** @brief The memory used by the buffer. */
    VkDeviceMemory memory;
    /** @brief The memory requirements for this buffer. */
    VkMemoryRequirements memory_requirements;
    /** @brief The index of the memory used by the buffer. */
    i32 memory_index;
    /** @brief The property flags for the memory used by the buffer. */
    u32 memory_property_flags;
} vulkan_buffer;

/** @brief Contains swapchain support information and capabilities. */
typedef struct vulkan_swapchain_support_info {
    /** @brief The surface capabilities. */
    VkSurfaceCapabilitiesKHR capabilities;
    /** @brief The number of available surface formats. */
    u32 format_count;
    /** @brief An array of the available surface formats. */
    VkSurfaceFormatKHR* formats;
    /** @brief The number of available presentation modes. */
    u32 present_mode_count;
    /** @brief An array of available presentation modes. */
    VkPresentModeKHR* present_modes;
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
    VkImageView* layer_views;
    VkImageSubresourceRange* layer_view_subresource_ranges;
    VkImageViewCreateInfo* layer_view_create_infos;
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
    char* name;
    /** The number of mipmaps to be generated for this image. Must always be at least 1. */
    u32 mip_levels;
    b8 has_view;
} vulkan_image;

// Struct definition for renderer-specific framebuffer data.
typedef struct framebuffer_internal_data {
    // The number of VkFramebuffers in the array. Typically 1 unless the attachment
    // requires the frame_count to be taken into account.
    u32 framebuffer_count;
    // Arrat of framebuffers.
    VkFramebuffer* framebuffers;
} framebuffer_internal_data;

/** @brief Represents the possible states of a renderpass. */
typedef enum vulkan_render_pass_state {
    /** @brief The renderpass is ready to begin. */
    READY,
    /** @brief The renderpass is currently being recorded to. */
    RECORDING,
    /** @brief The renderpass is currently active. */
    IN_RENDER_PASS,
    /** @brief The renderpass is has ended recording. */
    RECORDING_ENDED,
    /** @brief The renderpass has been submitted to the queue. */
    SUBMITTED,
    /** @brief The renderpass is not allocated. */
    NOT_ALLOCATED
} vulkan_render_pass_state;

/**
 * @brief A representation of the Vulkan renderpass.
 */
typedef struct vulkan_renderpass {
    /** @brief The internal renderpass handle. */
    VkRenderPass handle;
    /** @brief The current render area of the renderpass. */

    /** @brief Indicates renderpass state. */
    vulkan_render_pass_state state;

    // darray
    VkClearValue* clear_values;
    /* u32 clear_value_count; */

} vulkan_renderpass;

/**
 * @brief Representation of the Vulkan swapchain.
 */
typedef struct vulkan_swapchain {
    /** @brief The swapchain image format. */
    VkSurfaceFormatKHR image_format;
    /**
     * @brief The maximum number of "images in flight" (images simultaneously being rendered to).
     * Typically one less than the total number of images available.
     */
    u8 max_frames_in_flight;

    /** @brief Indicates various flags used for swapchain instantiation. */
    renderer_config_flags flags;

    /** @brief The swapchain internal handle. */
    VkSwapchainKHR handle;
    /** @brief The number of swapchain images. */
    u32 image_count;

    /** @brief Track the owning window in case something is needed from it. */
    struct kwindow* owning_window;

    /** @brief Supports being used as a blit destination. */
    b8 supports_blit_dest;

    /** @brief Supports being used as a blit source. */
    b8 supports_blit_src;
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

    /** @brief Command buffer state. */
    vulkan_command_buffer_state state;

    /** @brief Indicates if this is a primary or secondary command buffer. */
    b8 is_primary;

    /** @brief The number of secondary buffers that are children to this one. Primary buffer use only. */
    u16 secondary_count;
    /** @brief An array of secondary buffers that are children to this one. Primary buffer use only. */
    struct vulkan_command_buffer* secondary_buffers;

    /** @brief The currently selected secondary buffer index. */
    u16 secondary_buffer_index;
    /** @brief Indicates if the command buffer selected secondary buffer index. */
    b8 in_render;

    /** A pointer to the parent (primary) command buffer, if there is one. Only applies to secondary buffers. */
    struct vulkan_command_buffer* parent;
} vulkan_command_buffer;

/**
 * @brief Represents a single shader stage.
 */
typedef struct vulkan_shader_stage {
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
 * @brief A configuration structure for Vulkan pipelines.
 */
typedef struct vulkan_pipeline_config {
    /** @brief The name of the pipeline. Used primarily for debugging purposes. */
    char* name;
    /** @brief The stride of the vertex data to be used (ex: sizeof(vertex_3d)) */
    u32 stride;
    /** @brief The number of attributes. */
    u32 attribute_count;
    /** @brief An array of attributes. */
    VkVertexInputAttributeDescription* attributes;
    /** @brief The number of descriptor set layouts. */
    u32 descriptor_set_layout_count;
    /** @brief An array of descriptor set layouts. */
    VkDescriptorSetLayout* descriptor_set_layouts;
    /** @brief The number of stages (vertex, fragment, etc). */
    u32 stage_count;
    /** @brief An VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BITarray of stages. */
    VkPipelineShaderStageCreateInfo* stages;
    /** @brief The initial viewport configuration. */
    VkViewport viewport;
    /** @brief The initial scissor configuration. */
    VkRect2D scissor;
    /** @brief The face cull mode. */
    face_cull_mode cull_mode;
    /** @brief The shader flags used for creating the pipeline. */
    u32 shader_flags;
    /** @brief The number of push constant data ranges. */
    u32 push_constant_range_count;
    /** @brief An array of push constant data ranges. */
    range* push_constant_ranges;
    /** @brief Collection of topology types to be supported on this pipeline. */
    u32 topology_types;
    /** @brief The vertex winding order used to determine the front face of triangles. */
    renderer_winding winding;

    u32 colour_attachment_count;
    VkFormat* colour_attachment_formats;
    VkFormat depth_attachment_format;
    VkFormat stencil_attachment_format;
} vulkan_pipeline_config;

/**
 * @brief Holds a Vulkan pipeline and its layout.
 */
typedef struct vulkan_pipeline {
    /** @brief The internal pipeline handle. */
    VkPipeline handle;
    /** @brief The pipeline layout. */
    VkPipelineLayout pipeline_layout;
    /** @brief Indicates the topology types used by this pipeline. See primitive_topology_type.*/
    u32 supported_topology_types;
} vulkan_pipeline;

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
/**
 * @brief The maximum number of uniforms and samplers allowed at the
 * global, instance and local levels combined. It's probably more than
 * will ever be needed.
 */
#define VULKAN_SHADER_MAX_UNIFORMS 128

/** @brief The maximum number of push constant ranges for a shader. */
#define VULKAN_SHADER_MAX_PUSH_CONST_RANGES 32

// Max number of descriptor sets based on frequency. (0=per-frame, 1=per-group, 2=per-draw)
#define VULKAN_SHADER_DESCRIPTOR_SET_LAYOUT_COUNT 3

/**
 * @brief The configuration for a descriptor set.
 */
typedef struct vulkan_descriptor_set_config {
    /** @brief The number of bindings in this set. */
    u8 binding_count;
    /** @brief An array of binding layouts for this set. */
    VkDescriptorSetLayoutBinding* bindings;
} vulkan_descriptor_set_config;

/**
 * @brief Represents a state for a given descriptor. This is used
 * to determine when a descriptor needs updating. There is a state
 * per frame (with a max of 3).
 */
typedef struct vulkan_descriptor_state {
    /** @brief The descriptor generation, per swapchain image. INVALID_ID_U16 if never loaded. */
    u16* generations;
} vulkan_descriptor_state;

typedef struct vulkan_uniform_sampler_state {
    shader_uniform uniform;

    /**
     * @brief An array of sampler handles. Count matches uniform array_count.
     */
    khandle* sampler_handles;

    /**
     * @brief A descriptor state per sampler. Count matches uniform array_count.
     */
    vulkan_descriptor_state* descriptor_states;
} vulkan_uniform_sampler_state;

typedef struct vulkan_uniform_texture_state {
    shader_uniform uniform;

    /**
     * @brief An array of handles to texture resources.
     */
    khandle* texture_handles;

    /**
     * @brief A descriptor state per descriptor, which in turn handles frames.
     * Count is managed in shader config.
     */
    vulkan_descriptor_state* descriptor_states;
} vulkan_uniform_texture_state;
/**
 * @brief The frequency-level state for a shader (i.e. per-frame, per-group, per-draw).
 */
typedef struct vulkan_shader_frequency_state {
    /** @brief The frequency id. INVALID_ID if not used. */
    u32 id;
    /** @brief The offset in bytes in the frequency uniform buffer. */
    u64 offset;

    /** @brief The descriptor sets for this frequency, one per swapchain image. */
    VkDescriptorSet* descriptor_sets;

    // UBO descriptor state.
    vulkan_descriptor_state ubo_descriptor_state;

    // A mapping of sampler uniforms to descriptors.
    vulkan_uniform_sampler_state* sampler_states;
    // A mapping of texture uniforms to descriptors.
    vulkan_uniform_texture_state* texture_states;
#ifdef KOHI_DEBUG
    u32 descriptor_set_index;
    shader_update_frequency frequency;
#endif
} vulkan_shader_frequency_state;

/**
 * @brief Contains vulkan shader frequency specific info for UBOs.
 */
typedef struct vulkan_shader_frequency_info {
    /** @brief The actual size of the uniform buffer object for this frequency. */
    u64 ubo_size;
    /** @brief The stride of the uniform buffer object for this frequency. */
    u64 ubo_stride;
    /**
     * @brief The offset in bytes for the UBO from the beginning
     * of the uniform buffer for this frequency.
     */
    u64 ubo_offset;

    /** @brief The number of non-sampler and non-texture uniforms for this frequency. */
    u8 uniform_count;
    /** @brief The number of sampler uniforms for this frequency. */
    u8 uniform_sampler_count;
    // Darray. Keeps the uniform indices of samplers for fast lookups.
    u32* sampler_indices;
    /** @brief The number of texture uniforms for this frequency. */
    u8 uniform_texture_count;
    // Darray. Keeps the uniform indices of textures for fast lookups.
    u32* texture_indices;

    // The currently-bound id for this frequency.
    u32 bound_id;
} vulkan_shader_frequency_info;

/**
 * @brief Represents a generic Vulkan shader. This uses a set of inputs
 * and parameters, as well as the shader programs contained in SPIR-V
 * files to construct a shader for use in rendering.
 */
typedef struct vulkan_shader {
    // The name of the shader (mostly kept for debugging purposes).
    kname name;
    /** @brief The block of memory mapped to the each per-swapchain-image uniform buffer. */
    void** mapped_uniform_buffer_blocks;
    /** @brief The block of memory used for push constants, 128B. */
    void* per_draw_push_constant_block;

    /** @brief The shader identifier. */
    u32 id;

    /**
     * @brief The max number of descriptor sets that can be allocated from this shader.
     * Should typically be a decently high number.
     */
    u16 max_descriptor_set_count;

    /**
     * @brief The total number of descriptor sets configured for this shader.
     */
    u8 descriptor_set_count;
    /** @brief Descriptor sets, max of 3. Index 0=per_frame, 1=per_group, 2=per_draw */
    vulkan_descriptor_set_config descriptor_sets[VULKAN_SHADER_DESCRIPTOR_SET_LAYOUT_COUNT];

    /** @brief The number of vertex attributes in the shader. */
    u8 attribute_count;
    /** @brief An array of attribute descriptions for this shader. */
    VkVertexInputAttributeDescription attributes[VULKAN_SHADER_MAX_ATTRIBUTES];

    /** @brief The number of uniforms in the shader. */
    u32 uniform_count;

    /** @brief An array of uniforms in the shader. */
    shader_uniform* uniforms;

    /** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
    u32 attribute_stride;

    /** @brief Face culling mode, provided by the front end. */
    face_cull_mode cull_mode;

    /** @brief The topology types for the shader pipeline. See primitive_topology_type. Defaults to "triangle list" if unspecified. */
    u32 topology_types;

    u32 max_groups;

    u32 max_per_draw_count;

    /** @brief The number of shader stages in this shader. */
    u8 stage_count;

    /** @brief An array of stages (such as vertex and fragment) for this shader. Count is located in config.*/
    vulkan_shader_stage stages[VULKAN_SHADER_MAX_STAGES];

    u32 pool_size_count;

    /** @brief An array of descriptor pool sizes. */
    VkDescriptorPoolSize pool_sizes[3];

    /** @brief The descriptor pool used for this shader. */
    VkDescriptorPool descriptor_pool;

    /** @brief Descriptor set layouts, max of 3. Index 0=per-frame, 1=per-group, 2=per-draw (samplers only). */
    VkDescriptorSetLayout descriptor_set_layouts[VULKAN_SHADER_DESCRIPTOR_SET_LAYOUT_COUNT];

    /** @brief The uniform buffers used by this shader, one per swapchain image. */
    renderbuffer* uniform_buffers;
    u32 uniform_buffer_count;

    /** @brief An array of pointers to pipelines associated with this shader. */
    vulkan_pipeline** pipelines;
    /** @brief An array of pointers to wireframe pipelines associated with this shader. */
    vulkan_pipeline** wireframe_pipelines;

    /** @brief The currently bound pipeline index. */
    u8 bound_pipeline_index;
    /** @brief The currently-selected topology. */
    VkPrimitiveTopology current_topology;

    /** @brief The per-frame frequency state. */
    vulkan_shader_frequency_state per_frame_state;

    /** @brief The per-group frequency states for all groups. */
    vulkan_shader_frequency_state* group_states;

    /** @brief The per-draw states for all local things/entities/actors/whatever. */
    vulkan_shader_frequency_state* per_draw_states;

    /**
     * @brief The amount of bytes that are required for UBO alignment.
     *
     * This is used along with the UBO size to determine the ultimate
     * stride, which is how much the UBOs are spaced out in the buffer.
     * For example, a required alignment of 256 means that the stride
     * must be a multiple of 256 (true for some nVidia cards).
     */
    u64 required_ubo_alignment;

    vulkan_shader_frequency_info per_frame_info;
    vulkan_shader_frequency_info per_group_info;
    vulkan_shader_frequency_info per_draw_info;

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

    /** @brief Indicates if the swapchain is currently being recreated. */
    b8 recreating_swapchain;

    /** @brief The graphics command buffers, one per swapchain image. */
    vulkan_command_buffer* graphics_command_buffers;

    /** @brief The semaphores used to indicate image availability, one per frame in flight. */
    VkSemaphore* image_available_semaphores;

    /** @brief The semaphores used to indicate queue availability, one per frame in flight. */
    VkSemaphore* queue_complete_semaphores;

    /**
     * @brief The in-flight fences, used to indicate to the application when a frame is
     * busy/ready. One per frame in flight.
     */
    VkFence* in_flight_fences;

    /** @brief Resusable staging buffers (one per frame in flight) to transfer data from a resource to a GPU-only buffer. */
    renderbuffer* staging;

    /**
     * @brief Array of darrays of handles to textures that were updated as part of a frame's workload.
     * One list per frame in flight.
     */
    khandle** frame_texture_updated_list;

    u64 framebuffer_size_generation;
    u64 framebuffer_previous_size_generation;

    u8 skip_frames;
} kwindow_renderer_backend_state;

typedef struct vulkan_sampler_handle_data {
    // Used for handle validation.
    u64 handle_uniqueid;
    // The generation of the internal sampler. Incremented every time the sampler is changed.
    u16 generation;
    // Sampler name for named lookups and serialization.
    kname name;
    // The underlying sampler handle.
    VkSampler sampler;
} vulkan_sampler_handle_data;

/**
 * @brief Represents Vulkan-specific texture data.
 */
typedef struct vulkan_texture_handle_data {
    // Unique identifier for this texture.
    u64 uniqueid;

    // The generation of the internal texture. Incremented every time the texture is changed.
    u16 generation;

    // Number of vulkan_images in the array. This is typically 1 unless the texture
    // requires the frame_count to be taken into account.
    u32 image_count;
    // Array of images. See image_count.
    vulkan_image* images;
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
    vec4 viewport_rect;

    /** @brief The scissor rectangle. */
    vec4 scissor_rect;

    /** @brief The handle to the internal Vulkan instance. */
    VkInstance instance;
    /** @brief The internal Vulkan allocator. */
    VkAllocationCallbacks* allocator;

#if defined(_DEBUG)
    /** @brief The debug messenger, if active.. */
    VkDebugUtilsMessengerEXT debug_messenger;

    /** @brief The function pointer to set debug object names. */
    PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;

    /** @brief The function pointer to set free-form debug object tag data. */
    PFN_vkSetDebugUtilsObjectTagEXT pfnSetDebugUtilsObjectTagEXT;

    PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
    PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;
#endif

    /** @brief The Vulkan device. */
    vulkan_device device;

    /** @brief A pointer to the current window whose resources should be used as default to render to. */
    struct kwindow* current_window;

    b8 render_flag_changed;

    b8 validation_enabled;

    /** @brief Indicates if multi-threading is supported by this device. */
    b8 multithreading_enabled;

    /** @brief Collection of samplers. darray */
    vulkan_sampler_handle_data* samplers;

    /** @brief Collection of textures. darray. */
    vulkan_texture_handle_data* textures;

    /** @brief Collection of vulkan shaders (internal shader data). Matches size of shader array in shader system. */
    vulkan_shader* shaders;

    /**
     * @brief A function pointer to find a memory index of the given type and with the given properties.
     * @param context A pointer to the renderer context.
     * @param type_filter The types of memory to search for.
     * @param property_flags The required properties which must be present.
     * @returns The index of the found memory type. Returns -1 if not found.
     */
    i32 (*find_memory_index)(struct vulkan_context* context, u32 type_filter, u32 property_flags);

    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;
    PFN_vkCmdSetFrontFaceEXT vkCmdSetFrontFaceEXT;
    PFN_vkCmdSetStencilTestEnableEXT vkCmdSetStencilTestEnableEXT;
    PFN_vkCmdSetDepthTestEnableEXT vkCmdSetDepthTestEnableEXT;
    PFN_vkCmdSetDepthWriteEnableEXT vkCmdSetDepthWriteEnableEXT;
    PFN_vkCmdSetStencilOpEXT vkCmdSetStencilOpEXT;

    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR;

    /** @brief A pointer to the currently bound vulkan shader. */
    vulkan_shader* bound_shader;

    /**
     * Used for dynamic compilation of vulkan shaders (using the shaderc lib.)
     */
    struct shaderc_compiler* shader_compiler;
} vulkan_context;
