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

#include <vulkan/vulkan.h>

/**
 * @brief Checks the given expression's return value against VK_SUCCESS.
 * @param expr The expression whose result should be checked.
 */
#define VK_CHECK(expr)               \
    {                                \
        KASSERT(expr == VK_SUCCESS); \
    }

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
    vec4 render_area;
    /** @brief The clear colour used for this renderpass. */
    vec4 clear_colour;

    /** @brief The depth clear value. */
    f32 depth;
    /** @brief The stencil clear value. */
    u32 stencil;

    /** @brief The clear flags for this renderpass. */
    u8 clear_flags;

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
    /** @brief An array of swapchain images. */
    VkImage* images;
    /** @brief An array of swapchain image views for the swapchain images. */
    VkImageView* views;

    /** @brief The depth image attachment. */
    vulkan_image depth_attachment;

    /** @brief Framebuffers used for on-screen rendering, one per frame */
    VkFramebuffer framebuffers[3];
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

/** @brief The number of shader stages in the material shader. */
#define MATERIAL_SHADER_STAGE_COUNT 2

/** 
 * @brief Represents a state for a given descriptor. This is used 
 * to determine when a descriptor needs updating. There is a state
 * per frame (with a max of 3).
 */
typedef struct vulkan_descriptor_state {
    /** @brief The descriptor generation, per frame */
    u32 generations[3];
    /** @brief The identifier, per frame. Typically used for texture ids. */
    u32 ids[3];
} vulkan_descriptor_state;

/** @brief The number of descriptors present in the material shader */
#define VULKAN_MATERIAL_SHADER_DESCRIPTOR_COUNT 2
/** @brief The number of texture samplers present in the material shader */
#define VULKAN_MATERIAL_SHADER_SAMPLER_COUNT 1

/**
 * @brief Represents the state of a material instance. Each
 * material in the world is a material instance, so one per
 * material exists.
 */
typedef struct vulkan_material_shader_instance_state {
    /** @brief The descriptor sets for this material instance, one per frame. */
    VkDescriptorSet descriptor_sets[3];

    /** @brief A descriptor state per descriptor, which in turn handles frames. */
    vulkan_descriptor_state descriptor_states[VULKAN_MATERIAL_SHADER_DESCRIPTOR_COUNT];
} vulkan_material_shader_instance_state;

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
    u32 vertex_buffer_offset;
    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. */
    u32 index_element_size;
    /** @brief The offset in bytes in the index buffer. */
    u32 index_buffer_offset;
} vulkan_geometry_data;

/**
 * @brief Represents the global material shader uniform buffer data
 * that should be loaded into the global ubo buffer. 
 * @note This must be padded out to 256 bytes because of the requirement
 * from some GPUs (I'm looking at you, nVidia) that UBO buffer elements be
 * at least 256 bytes apart.
 */
typedef struct vulkan_material_shader_global_ubo {
    /** @brief The projection matrix. @note 64 bytes. */
    mat4 projection;
    /** @brief The view matrix. @note 64 bytes. */
    mat4 view;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved0;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved1;
} vulkan_material_shader_global_ubo;

/**
 * @brief Represents per-material instance uniform buffer data
 * that should be loaded into the local ubo buffer.
 * @note This must be padded out to 256 bytes because of the requirement
 * from some GPUs (I'm looking at you, nVidia) that UBO buffer elements be
 * at least 256 bytes apart.
 */
typedef struct vulkan_material_shader_instance_ubo {
    /** @brief The diffuse colour. @note 16 bytes. */
    vec4 diffuse_color;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved0;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved1;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved2;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved0;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved1;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved2;
} vulkan_material_shader_instance_ubo;

/**
 * @brief A representation of the vulkan material shader.
 * Holds internal resources such as descriptors/layouts, ubos,
 * buffers, etc.
 */
typedef struct vulkan_material_shader {
    /** @brief The shader stages. @note vertex, fragment */
    vulkan_shader_stage stages[MATERIAL_SHADER_STAGE_COUNT];

    /** @brief The pool which global descriptors are allocated. */
    VkDescriptorPool global_descriptor_pool;
    /** @brief The global descriptor set layout. */
    VkDescriptorSetLayout global_descriptor_set_layout;

    /** @brief Global descriptor sets. @note One descriptor set per frame - max 3 for triple-buffering. */
    VkDescriptorSet global_descriptor_sets[3];

    /** @brief Global uniform object, to be loaded into the global uniform buffer. */
    vulkan_material_shader_global_ubo global_ubo;

    /** @brief Global uniform buffer. */
    vulkan_buffer global_uniform_buffer;

    /** @brief The pool from which material-instance descriptors are allocated. */
    VkDescriptorPool object_descriptor_pool;
    /** @brief The descriptor set layout for material instance descriptors. */
    VkDescriptorSetLayout object_descriptor_set_layout;
    /** @brief Material instance uniform buffer. @todo TODO: This is a linear list of material instances. Switch to use free list. */
    vulkan_buffer object_uniform_buffer;
    /** @brief The current material instance index for linear allocations. @todo TODO: Manage a free list of some kind here instead. */
    u32 object_uniform_buffer_index;

    /** @brief represents the usage of samplers. Ordered in zero-indexed sampler use order. */
    texture_use sampler_uses[VULKAN_MATERIAL_SHADER_SAMPLER_COUNT];

    /** @brief The material instance states for all material instances. @todo TODO: make dynamic */
    vulkan_material_shader_instance_state instance_states[VULKAN_MAX_MATERIAL_COUNT];

    /** @brief The pipeline associated with this shader. */
    vulkan_pipeline pipeline;

} vulkan_material_shader;

/** @brief The number of shader stages in the UI shader. */
#define UI_SHADER_STAGE_COUNT 2
/** @brief The number of descriptors present in the UI shader */
#define VULKAN_UI_SHADER_DESCRIPTOR_COUNT 2
/** @brief The number of texture samplers present in the UI shader */
#define VULKAN_UI_SHADER_SAMPLER_COUNT 1

/** 
 * @brief Max number of UI control instances 
 * @todo TODO: make configurable
 */
#define VULKAN_MAX_UI_COUNT 1024

/**
 * @brief Represents the state of a UI control instance. Each
 * UI control in the world is a UI control instance, so one per
 * UI control exists.
 */
typedef struct vulkan_ui_shader_instance_state {
    /** @brief The descriptor sets for this UI control instance, one per frame. */
    VkDescriptorSet descriptor_sets[3];

    /** @brief A descriptor state per descriptor, which in turn handles frames. */
    vulkan_descriptor_state descriptor_states[VULKAN_UI_SHADER_DESCRIPTOR_COUNT];
} vulkan_ui_shader_instance_state;

/**
 * @brief Represents the global UI shader uniform buffer data
 * that should be loaded into the global ubo buffer. 
 * @note This must be padded out to 256 bytes because of the requirement
 * from some GPUs (I'm looking at you, nVidia) that UBO buffer elements be
 * at least 256 bytes apart.
 */
typedef struct vulkan_ui_shader_global_ubo {
    /** @brief The projection matrix. @note 64 bytes. */
    mat4 projection;
    /** @brief The view matrix. @note 64 bytes. */
    mat4 view;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved0;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved1;
} vulkan_ui_shader_global_ubo;

/**
 * @brief Represents per-UI instance uniform buffer data
 * that should be loaded into the local ubo buffer.
 * @note This must be padded out to 256 bytes because of the requirement
 * from some GPUs (I'm looking at you, nVidia) that UBO buffer elements be
 * at least 256 bytes apart.
 */
typedef struct vulkan_ui_shader_instance_ubo {
    /** @brief The diffuse colour. @note 16 bytes. */
    vec4 diffuse_color;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved0;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved1;
    /** @brief reserved for future use. @note 16 bytes. */
    vec4 v_reserved2;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved0;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved1;
    /** @brief reserved for future use. @note 64 bytes. */
    mat4 m_reserved2;
} vulkan_ui_shader_instance_ubo;

/**
 * @brief A representation of the vulkan UI shader.
 * Holds internal resources such as descriptors/layouts, ubos,
 * buffers, etc.
 */
typedef struct vulkan_ui_shader {
    /** @brief The shader stages. @note vertex, fragment */
    vulkan_shader_stage stages[UI_SHADER_STAGE_COUNT];

    /** @brief The pool which global descriptors are allocated. */
    VkDescriptorPool global_descriptor_pool;
    /** @brief The global descriptor set layout. */
    VkDescriptorSetLayout global_descriptor_set_layout;

    /** @brief Global descriptor sets. @note One descriptor set per frame - max 3 for triple-buffering. */
    VkDescriptorSet global_descriptor_sets[3];

    /** @brief Global uniform object, to be loaded into the global uniform buffer. */
    vulkan_ui_shader_global_ubo global_ubo;

    /** @brief Global uniform buffer. */
    vulkan_buffer global_uniform_buffer;

    /** @brief The pool from which per-instance descriptors are allocated. */
    VkDescriptorPool object_descriptor_pool;
    /** @brief The descriptor set layout for per-instance descriptors. */
    VkDescriptorSetLayout object_descriptor_set_layout;
    /** @brief Material instance uniform buffer. @todo TODO: This is a linear list of material instances. Switch to use free list. */
    vulkan_buffer object_uniform_buffer;
    /** @brief The current material instance index for linear allocations. @todo TODO: Manage a free list of some kind here instead. */
    u32 object_uniform_buffer_index;

    /** @brief represents the usage of samplers. Ordered in zero-indexed sampler use order. */
    texture_use sampler_uses[VULKAN_UI_SHADER_SAMPLER_COUNT];

    /** @brief The material instance states for all material instances. @todo TODO: make dynamic */
    vulkan_ui_shader_instance_state instance_states[VULKAN_MAX_UI_COUNT];

    /** @brief The pipeline associated with this shader. */
    vulkan_pipeline pipeline;

} vulkan_ui_shader;

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

    /** @brief The main world renderpass. */
    vulkan_renderpass main_renderpass;

    /** @brief The UI renderpass. */
    vulkan_renderpass ui_renderpass;

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

    /** @brief The material shader. */
    vulkan_material_shader material_shader;
    /** @brief The UI shader. */
    vulkan_ui_shader ui_shader;

    /** @brief The geometry vertex buffer offset. @todo TODO: use free lists */
    u64 geometry_vertex_offset;
    /** @brief The geometry index buffer offset. @todo TODO: use free lists */
    u64 geometry_index_offset;

    /** @brief The A collection of loaded geometries. @todo TODO: make dynamic */
    vulkan_geometry_data geometries[VULKAN_MAX_GEOMETRY_COUNT];

    /** @brief Framebuffers used for world rendering. @note One per frame. */
    VkFramebuffer world_framebuffers[3];

    /** 
     * @brief A function pointer to find a memory index of the given type and with the given properties.
     * @param type_filter The types of memory to search for.
     * @param property_flags The required properties which must be present.
     * @returns The index of the found memory type. Returns -1 if not found.
     */
    i32 (*find_memory_index)(u32 type_filter, u32 property_flags);

} vulkan_context;

/**
 * @brief Represents Vulkan-specific texture data. 
 */
typedef struct vulkan_texture_data {
    /** @brief The internal Vulkan image. */
    vulkan_image image;
    /** @brief The internal image sampler. */
    VkSampler sampler;
} vulkan_texture_data;
