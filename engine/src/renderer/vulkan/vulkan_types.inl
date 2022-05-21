/**
 * @file vulkan_types.inl
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

#include "defines.h"
#include "core/asserts.h"
#include "renderer/renderer_types.inl"
#include "containers/freelist.h"
#include "containers/hashtable.h"

#include <vulkan/vulkan.h>

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
    /** @brief The total size of the buffer. */
    u64 total_size;
    /** @brief The handle to the internal buffer. */
    VkBuffer handle;
    /** @brief The usage flags. */
    VkBufferUsageFlagBits usage;
    /** @brief Indicates if the buffer's memory is currently locked. */
    b8 is_locked;
    /** @brief The memory used by the buffer. */
    VkDeviceMemory memory;
    /** @brief The index of the memory used by the buffer. */
    i32 memory_index;
    /** @brief The property flags for the memory used by the buffer. */
    u32 memory_property_flags;
    /** @brief The amount of memory required for the freelist. */
    u64 freelist_memory_requirement;
    /** @brief The memory block used by the internal freelist. */
    void* freelist_block;
    /** @brief A freelist to track allocations. */
    freelist buffer_freelist;
    b8 has_freelist;
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

/**
 * @brief A representation of both the physical and logical
 * Vulkan devices. Also contains handles to queues, command pools,
 * and various properties of the devices.
 */
typedef struct vulkan_device {
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
    /** @brief The view for the image, which is used to access the image. */
    VkImageView view;
    /** @brief The image width. */
    u32 width;
    /** @brief The image height. */
    u32 height;
} vulkan_image;

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

    /** @brief The depth clear value. */
    f32 depth;
    /** @brief The stencil clear value. */
    u32 stencil;

    /** @brief Indicates if there is a previous renderpass. */
    b8 has_prev_pass;
    /** @brief Indicates if there is a next renderpass. */
    b8 has_next_pass;

    /** @brief Indicates renderpass state. */
    vulkan_render_pass_state state;
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

    /** @brief The swapchain internal handle. */
    VkSwapchainKHR handle;
    /** @brief The number of swapchain images. */
    u32 image_count;
    /** @brief An array of pointers to render targets, which contain swapchain images. */
    texture** render_textures;

    /** @brief The depth texture. */
    texture* depth_texture;

    /** 
     * @brief Render targets used for on-screen rendering, one per frame. 
     * The images contained in these are created and owned by the swapchain.
     * */
    render_target render_targets[3];
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

/**
 * @brief Holds a Vulkan pipeline and its layout.
 */
typedef struct vulkan_pipeline {
    /** @brief The internal pipeline handle. */
    VkPipeline handle;
    /** @brief The pipeline layout. */
    VkPipelineLayout pipeline_layout;
} vulkan_pipeline;

/**
 * @brief Max number of material instances
 * @todo TODO: make configurable
 */
#define VULKAN_MAX_MATERIAL_COUNT 1024

/**
 * @brief Max number of simultaneously uploaded geometries
 * @todo TODO: make configurable
 */
#define VULKAN_MAX_GEOMETRY_COUNT 4096

/**
 * @brief Internal buffer data for geometry. This data gets loaded
 * directly into a buffer.
 */
typedef struct vulkan_geometry_data {
    /** @brief The unique geometry identifier. */
    u32 id;
    /** @brief The geometry generation. Incremented every time the geometry data changes. */
    u32 generation;
    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The size of each vertex. */
    u32 vertex_element_size;
    /** @brief The offset in bytes in the vertex buffer. */
    u64 vertex_buffer_offset;
    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. */
    u32 index_element_size;
    /** @brief The offset in bytes in the index buffer. */
    u64 index_buffer_offset;
} vulkan_geometry_data;

/**
 * @brief Max number of UI control instances
 * @todo TODO: make configurable
 */
#define VULKAN_MAX_UI_COUNT 1024

/**
 * @brief Put some hard limits in place for the count of supported textures,
 * attributes, uniforms, etc. This is to maintain memory locality and avoid
 * dynamic allocations.
 */

/** @brief The maximum number of stages (such as vertex, fragment, compute, etc.) allowed. */
#define VULKAN_SHADER_MAX_STAGES 8
/** @brief The maximum number of textures allowed at the global level. */
#define VULKAN_SHADER_MAX_GLOBAL_TEXTURES 31
/** @brief The maximum number of textures allowed at the instance level. */
#define VULKAN_SHADER_MAX_INSTANCE_TEXTURES 31
/** @brief The maximum number of vertex input attributes allowed. */
#define VULKAN_SHADER_MAX_ATTRIBUTES 16
/**
 * @brief The maximum number of uniforms and samplers allowed at the
 * global, instance and local levels combined. It's probably more than
 * will ever be needed.
 */
#define VULKAN_SHADER_MAX_UNIFORMS 128

/** @brief The maximum number of bindings per descriptor set. */
#define VULKAN_SHADER_MAX_BINDINGS 2
/** @brief The maximum number of push constant ranges for a shader. */
#define VULKAN_SHADER_MAX_PUSH_CONST_RANGES 32

/**
 * @brief Configuration for a shader stage, such as vertex or fragment.
 */
typedef struct vulkan_shader_stage_config {
    /** @brief The shader stage bit flag. */
    VkShaderStageFlagBits stage;
    /** @brief The shader file name. */
    char file_name[255];

} vulkan_shader_stage_config;

/**
 * @brief The configuration for a descriptor set.
 */
typedef struct vulkan_descriptor_set_config {
    /** @brief The number of bindings in this set. */
    u8 binding_count;
    /** @brief An array of binding layouts for this set. */
    VkDescriptorSetLayoutBinding bindings[VULKAN_SHADER_MAX_BINDINGS];
} vulkan_descriptor_set_config;

/** @brief Internal shader configuration generated by vulkan_shader_create(). */
typedef struct vulkan_shader_config {
    /** @brief The number of shader stages in this shader. */
    u8 stage_count;
    /** @brief  The configuration for every stage of this shader. */
    vulkan_shader_stage_config stages[VULKAN_SHADER_MAX_STAGES];
    /** @brief An array of descriptor pool sizes. */
    VkDescriptorPoolSize pool_sizes[2];
    /**
     * @brief The max number of descriptor sets that can be allocated from this shader.
     * Should typically be a decently high number.
     */
    u16 max_descriptor_set_count;

    /**
     * @brief The total number of descriptor sets configured for this shader.
     * Is 1 if only using global uniforms/samplers; otherwise 2.
     */
    u8 descriptor_set_count;
    /** @brief Descriptor sets, max of 2. Index 0=global, 1=instance */
    vulkan_descriptor_set_config descriptor_sets[2];

    /** @brief An array of attribute descriptions for this shader. */
    VkVertexInputAttributeDescription attributes[VULKAN_SHADER_MAX_ATTRIBUTES];

} vulkan_shader_config;

/**
 * @brief Represents a state for a given descriptor. This is used
 * to determine when a descriptor needs updating. There is a state
 * per frame (with a max of 3).
 */
typedef struct vulkan_descriptor_state {
    /** @brief The descriptor generation, per frame. */
    u8 generations[3];
    /** @brief The identifier, per frame. Typically used for texture ids. */
    u32 ids[3];
} vulkan_descriptor_state;

/**
 * @brief Represents the state for a descriptor set. This is used to track
 * generations and updates, potentially for optimization via skipping
 * sets which do not need updating.
 */
typedef struct vulkan_shader_descriptor_set_state {
    /** @brief The descriptor sets for this instance, one per frame. */
    VkDescriptorSet descriptor_sets[3];

    /** @brief A descriptor state per descriptor, which in turn handles frames. Count is managed in shader config. */
    vulkan_descriptor_state descriptor_states[VULKAN_SHADER_MAX_BINDINGS];
} vulkan_shader_descriptor_set_state;

/**
 * @brief The instance-level state for a shader.
 */
typedef struct vulkan_shader_instance_state {
    /** @brief The instance id. INVALID_ID if not used. */
    u32 id;
    /** @brief The offset in bytes in the instance uniform buffer. */
    u64 offset;

    /** @brief  A state for the descriptor set. */
    vulkan_shader_descriptor_set_state descriptor_set_state;

    /**
     * @brief Instance texture map pointers, which are used during rendering. These
     * are set by calls to set_sampler.
     */
    struct texture_map** instance_texture_maps;
} vulkan_shader_instance_state;

/**
 * @brief Represents a generic Vulkan shader. This uses a set of inputs
 * and parameters, as well as the shader programs contained in SPIR-V
 * files to construct a shader for use in rendering.
 */
typedef struct vulkan_shader {
    /** @brief The block of memory mapped to the uniform buffer. */
    void* mapped_uniform_buffer_block;

    /** @brief The shader identifier. */
    u32 id;

    /** @brief The configuration of the shader generated by vulkan_create_shader(). */
    vulkan_shader_config config;

    /** @brief A pointer to the renderpass to be used with this shader. */
    vulkan_renderpass* renderpass;

    /** @brief An array of stages (such as vertex and fragment) for this shader. Count is located in config.*/
    vulkan_shader_stage stages[VULKAN_SHADER_MAX_STAGES];

    /** @brief The descriptor pool used for this shader. */
    VkDescriptorPool descriptor_pool;

    /** @brief Descriptor set layouts, max of 2. Index 0=global, 1=instance. */
    VkDescriptorSetLayout descriptor_set_layouts[2];
    /** @brief Global descriptor sets, one per frame. */
    VkDescriptorSet global_descriptor_sets[3];
    /** @brief The uniform buffer used by this shader. */
    vulkan_buffer uniform_buffer;

    /** @brief The pipeline associated with this shader. */
    vulkan_pipeline pipeline;

    /** @brief The instance states for all instances. @todo TODO: make dynamic */
    u32 instance_count;
    vulkan_shader_instance_state instance_states[VULKAN_MAX_MATERIAL_COUNT];

} vulkan_shader;

#define VULKAN_MAX_REGISTERED_RENDERPASSES 31

/**
 * @brief The overall Vulkan context for the backend. Holds and maintains
 * global renderer backend state, Vulkan instance, etc.
 */
typedef struct vulkan_context {
    /** @brief The time in seconds since the last frame. */
    f32 frame_delta_time;

    /** @brief The framebuffer's current width. */
    u32 framebuffer_width;

    /** @brief The framebuffer's current height. */
    u32 framebuffer_height;

    /** @brief Current generation of framebuffer size. If it does not match framebuffer_size_last_generation, a new one should be generated. */
    u64 framebuffer_size_generation;

    /** @brief The generation of the framebuffer when it was last created. Set to framebuffer_size_generation when updated. */
    u64 framebuffer_size_last_generation;

    /** @brief The handle to the internal Vulkan instance. */
    VkInstance instance;
    /** @brief The internal Vulkan allocator. */
    VkAllocationCallbacks* allocator;
    /** @brief The internal Vulkan surface for the window to be drawn to. */
    VkSurfaceKHR surface;

#if defined(_DEBUG)
    /** @brief The debug messenger, if active.. */
    VkDebugUtilsMessengerEXT debug_messenger;
#endif

    /** @brief The Vulkan device. */
    vulkan_device device;

    /** @brief The swapchain. */
    vulkan_swapchain swapchain;

    void* renderpass_table_block;
    hashtable renderpass_table;

    /** @brief Registered renderpasses. */
    renderpass registered_passes[VULKAN_MAX_REGISTERED_RENDERPASSES];

    /** @brief The object vertex buffer, used to hold geometry vertices. */
    vulkan_buffer object_vertex_buffer;
    /** @brief The object index buffer, used to hold geometry indices. */
    vulkan_buffer object_index_buffer;

    /** @brief The graphics command buffers, one per frame. @note: darray */
    vulkan_command_buffer* graphics_command_buffers;

    /** @brief The semaphores used to indicate image availability, one per frame. @note: darray */
    VkSemaphore* image_available_semaphores;

    /** @brief The semaphores used to indicate queue availability, one per frame. @note: darray */
    VkSemaphore* queue_complete_semaphores;

    /** @brief The current number of in-flight fences. */
    u32 in_flight_fence_count;
    /** @brief The in-flight fences, used to indicate to the application when a frame is busy/ready. */
    VkFence in_flight_fences[2];

    /** @brief Holds pointers to fences which exist and are owned elsewhere, one per frame. */
    VkFence images_in_flight[3];

    /** @brief The current image index. */
    u32 image_index;

    /** @brief The current frame. */
    u32 current_frame;

    /** @brief Indicates if the swapchain is currently being recreated. */
    b8 recreating_swapchain;

    /** @brief The A collection of loaded geometries. @todo TODO: make dynamic */
    vulkan_geometry_data geometries[VULKAN_MAX_GEOMETRY_COUNT];

    /** @brief Render targets used for world rendering. @note One per frame. */
    render_target world_render_targets[3];

    /**
     * @brief A function pointer to find a memory index of the given type and with the given properties.
     * @param type_filter The types of memory to search for.
     * @param property_flags The required properties which must be present.
     * @returns The index of the found memory type. Returns -1 if not found.
     */
    i32 (*find_memory_index)(u32 type_filter, u32 property_flags);

    /**
     * @brief A pointer to a function to be called when the backend requires
     * rendertargets to be refreshed/regenerated.
     */
    void (*on_rendertarget_refresh_required)();

} vulkan_context;
