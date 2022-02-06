/**
 * @file vulkan_shader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A generic, configurable implementation of a Vulkan shader.
 * @version 1.0
 * @date 2022-02-02
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */
#pragma once

#include "vulkan_types.inl"
#include "containers/hashtable.h"

// Forward dec
struct texture;

// TODO: This should be in the generic renderer frontend, methinks.
typedef enum shader_attribute_type {
    FLOAT32,
    FLOAT32_2,
    FLOAT32_3,
    FLOAT32_4,
    MATRIX_4,
    INT8,
    INT8_2,
    INT8_3,
    INT8_4,
    UINT8,
    UINT8_2,
    UINT8_3,
    UINT8_4,
    INT16,
    INT16_2,
    INT16_3,
    INT16_4,
    UINT16,
    UINT16_2,
    UINT16_3,
    UINT16_4,
    INT32,
    INT32_2,
    INT32_3,
    INT32_4,
    UINT32,
    UINT32_2,
    UINT32_3,
    UINT32_4
} shader_attribute_type;

/**
 * @brief Defines shader scope, which indicates how
 * often it gets updated.
 */
typedef enum vulkan_shader_scope {
    /** @brief Global shader scope, generally updated once per frame. */
    VULKAN_SHADER_SCOPE_GLOBAL = 0,
    /** @brief Instance shader scope, generally updated "per-instance" of the shader. */
    VULKAN_SHADER_SCOPE_INSTANCE = 1,
    /** @brief Local shader scope, generally updated per-object */
    VULKAN_SHADER_SCOPE_LOCAL = 2
} vulkan_shader_scope;

typedef enum vulkan_shader_state {
    VULKAN_SHADER_STATE_NOT_CREATED,
    VULKAN_SHADER_STATE_UNINITIALIZED,
    VULKAN_SHADER_STATE_INITIALIZED,
    // TODO: more states?
} vulkan_shader_state;

typedef struct vulkan_shader_stage_config {
    VkShaderStageFlagBits stage;
    char stage_str[8];
} vulkan_shader_stage_config;

typedef struct vulkan_descriptor_set_config {
    // darray
    VkDescriptorSetLayoutBinding* bindings;
} vulkan_descriptor_set_config;

/** Shader config */
typedef struct vulkan_shader_config {
    // darray
    vulkan_shader_stage_config* stages;
    VkDescriptorPoolSize pool_sizes[2];
    u32 max_descriptor_set_count;
    //darray
    vulkan_descriptor_set_config* descriptor_sets;
    // darray
    VkVertexInputAttributeDescription* attributes;
    // darray
    range* push_constant_ranges;
} vulkan_shader_config;

typedef struct vulkan_uniform_lookup_entry {
    u64 offset;
    u64 location;
    u32 index;
    u32 size;
    u32 set_index;
    vulkan_shader_scope scope;
    VkFormat format;
} vulkan_uniform_lookup_entry;

typedef struct vulkan_shader_descriptor_set_state {
    /** @brief The descriptor sets for this instance, one per frame. */
    VkDescriptorSet descriptor_sets[3];

    /** @brief A descriptor state per descriptor, which in turn handles frames. darray */
    vulkan_descriptor_state* descriptor_states;
} vulkan_shader_descriptor_set_state;

typedef struct vulkan_shader_instance_state {
    u32 id;
    u64 offset;

    // darray A set for each descriptor state.
    vulkan_shader_descriptor_set_state* descriptor_set_states;

    // darray of texture*
    struct texture** instance_textures;
} vulkan_shader_instance_state;

// TODO: move to vulkan_types
typedef struct vulkan_shader {
    u32 id;
    vulkan_context* context;
    char* name;
    b8 use_instances;
    b8 use_push_constants;
    vulkan_shader_config config;
    vulkan_shader_state state;
    // darray
    vulkan_shader_stage* stages;

    VkDescriptorPool descriptor_pool;
    // darray
    VkDescriptorSetLayout* descriptor_set_layouts;
    // One per frame
    VkDescriptorSet global_descriptor_sets[3];
    // darray
    vulkan_buffer uniform_buffer;

    /** @brief The pipeline associated with this shader. */
    vulkan_pipeline pipeline;

    void* hashtable_block;
    hashtable uniform_lookup;
    // darray
    vulkan_uniform_lookup_entry* uniforms;
    u64 required_ubo_alignment;

    u64 global_ubo_size;
    u64 global_ubo_stride;
    u64 global_ubo_offset;
    u64 ubo_size;
    u64 ubo_stride;
    u64 push_constant_size;
    u64 push_constant_stride;
    

    // darray of texture*
    struct texture** global_textures;
    u32 instance_texture_count;

    u32 bound_ubo_offset;
    void* ubo_block;

    u32 bound_instance_id;

    /** @brief The instance states for all instances. @todo TODO: make dynamic */
    vulkan_shader_instance_state instance_states[VULKAN_MAX_MATERIAL_COUNT];
} vulkan_shader;

b8 vulkan_shader_create(vulkan_context* context, const char* name, VkShaderStageFlags stages, u32 max_descriptor_set_count, b8 use_instances, b8 use_local, vulkan_shader* out_shader);
b8 vulkan_shader_destroy(vulkan_shader* shader);

// Add attributes/samplers/uniforms

b8 vulkan_shader_add_attribute(vulkan_shader* shader, const char* name, shader_attribute_type type);

b8 vulkan_shader_add_sampler(vulkan_shader* shader, const char* sampler_name, vulkan_shader_scope scope, u32* out_location);

b8 vulkan_shader_add_uniform_i8(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_i16(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_i32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_u8(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_u16(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_u32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_f32(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_vec2(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_vec3(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_vec4(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);
b8 vulkan_shader_add_uniform_mat4(vulkan_shader* shader, const char* uniform_name, vulkan_shader_scope scope, u32* out_location);

// End add attributes/samplers/uniforms

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * 
 * @param shader A pointer to the shader to be initialized.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_initialize(vulkan_shader* shader);
b8 vulkan_shader_use(vulkan_shader* shader);
b8 vulkan_shader_bind_globals(vulkan_shader* shader);
b8 vulkan_shader_bind_instance(vulkan_shader* shader, u32 instance_id);

b8 vulkan_shader_apply_globals(vulkan_shader* shader);
/**
 * @brief Applies data for the currently bound instance.
 * 
 * @param shader A pointer to the shader to apply the instance data for.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_apply_instance(vulkan_shader* shader);

b8 vulkan_shader_acquire_instance_resources(vulkan_shader* shader, u32* out_instance_id);
b8 vulkan_shader_release_instance_resources(vulkan_shader* shader, u32 instance_id);

b8 vulkan_shader_set_sampler(vulkan_shader* shader, u32 location, texture* t);

/**
 * @brief Attempts to retrieve uniform location for the given name.
 * 
 * @param shader A pointer to the shader to retrieve location from.
 * @param uniform_name The name of the uniform.
 * @return The location if successful; otherwise INVALID_ID.
 */
u32 vulkan_shader_uniform_location(vulkan_shader* shader, const char* uniform_name);

b8 vulkan_shader_set_uniform_i8(vulkan_shader* shader, u32 location, i8 value);
b8 vulkan_shader_set_uniform_i16(vulkan_shader* shader, u32 location, i16 value);
b8 vulkan_shader_set_uniform_i32(vulkan_shader* shader, u32 location, i32 value);
b8 vulkan_shader_set_uniform_u8(vulkan_shader* shader, u32 location, u8 value);
b8 vulkan_shader_set_uniform_u16(vulkan_shader* shader, u32 location, u16 value);
b8 vulkan_shader_set_uniform_u32(vulkan_shader* shader, u32 location, u32 value);
b8 vulkan_shader_set_uniform_f32(vulkan_shader* shader, u32 location, f32 value);
b8 vulkan_shader_set_uniform_vec2(vulkan_shader* shader, u32 location, vec2 value);
b8 vulkan_shader_set_uniform_vec2f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1);
b8 vulkan_shader_set_uniform_vec3(vulkan_shader* shader, u32 location, vec3 value);
b8 vulkan_shader_set_uniform_vec3f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2);
b8 vulkan_shader_set_uniform_vec4(vulkan_shader* shader, u32 location, vec4 value);
b8 vulkan_shader_set_uniform_vec4f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3);
b8 vulkan_shader_set_uniform_mat4(vulkan_shader* shader, u32 location, mat4 value);
