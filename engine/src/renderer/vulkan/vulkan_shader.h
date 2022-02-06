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
