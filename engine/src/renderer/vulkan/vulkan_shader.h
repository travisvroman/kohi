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

/**
 * @brief Creates a new shader using the provided parameters. A newly-created
 * shader must be initialized (via a call to vulkan_shader_init()) before it is usable.
 * 
 * @param context A pointer to the Vulkan context. A copy of this is stored with the shader.
 * @param name The name of the shader. Used to open compiled shader SPIR-V files.
 * @param renderpass A pointer to the renderpass this shader will use.
 * @param stages A combination of bitflags indicating what render stages (vertex, fragment, etc.) used in this shader.
 * @param max_descriptor_set_count The maximum number of descriptor sets that can be allocated (generally instance count * 2)
 * @param use_instances Indicates if instance uniforms are used.
 * @param use_local Indicates if local uniforms are used. For Vulkan, these are loaded into push constants.
 * @param out_shader A pointer to hold the newly-created shader.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_create(vulkan_context* context, const char* name, vulkan_renderpass *renderpass, VkShaderStageFlags stages, u16 max_descriptor_set_count, b8 use_instances, b8 use_local, vulkan_shader* out_shader);

/**
 * @brief Destroys the provided shader.
 * 
 * @param shader A pointer to the shader to be destroyed.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_destroy(vulkan_shader* shader);

// Add attributes/samplers/uniforms

/**
 * @brief Adds a new vertex attribute. Must be done after shader initialization.
 * 
 * @param shader A pointer to the shader to add the attribute to.
 * @param name The name of the attribute.
 * @param type The type of the attribute.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_attribute(vulkan_shader* shader, const char* name, shader_attribute_type type);

/**
 * @brief Adds a texture sampler to the shader. Must be done after shader initialization.
 * 
 * @param shader A pointer to the shader to add the sampler to.
 * @param sampler_name The name of the sampler.
 * @param scope The scope of the sampler. Can be global or instance.
 * @param out_location A pointer to hold the location of the attribute for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_sampler(vulkan_shader* shader, const char* sampler_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 8-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_i8(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 16-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_i16(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 32-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_i32(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 8-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_u8(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 16-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_u16(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 32-bit integer uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_u32(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new 32-bit float uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_f32(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector2 (2x 32-bit floats) uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_vec2(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector3 (3x 32-bit floats) uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_vec3(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector4 (4x 32-bit floats) uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_vec4(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new mat4 (4x4 matrix/16x 32-bit floats) uniform to the shader.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_mat4(vulkan_shader* shader, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new custom-sized uniform to the shader. This is useful for structure
 * types. NOTE: Size verification is not done for this type when setting the uniform.
 * 
 * @param shader A pointer to the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param size The size of the uniform in bytes.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_add_uniform_custom(vulkan_shader* shader, const char* uniform_name, u32 size, shader_scope scope, u32* out_location);


// End add attributes/samplers/uniforms

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * Must be done after vulkan_shader_create().
 * 
 * @param shader A pointer to the shader to be initialized.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_initialize(vulkan_shader* shader);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 * 
 * @param shader A pointer to the shader to be used.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_use(vulkan_shader* shader);

/**
 * @brief Binds global resources for use and updating.
 * 
 * @param shader A pointer to the shader whose globals are to be bound.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_bind_globals(vulkan_shader* shader);

/**
 * @brief Binds instance resources for use and updating.
 * 
 * @param shader A pointer to the shader whose instance resources are to be bound.
 * @param instance_id The identifier of the instance to be bound.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_bind_instance(vulkan_shader* shader, u32 instance_id);

/**
 * @brief Applies global data to the uniform buffer.
 * 
 * @param shader A pointer to the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_apply_globals(vulkan_shader* shader);

/**
 * @brief Applies data for the currently bound instance.
 * 
 * @param shader A pointer to the shader to apply the instance data for.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_apply_instance(vulkan_shader* shader);

/**
 * @brief Acquires internal instance-level resources and provides an instance id.
 * 
 * @param shader A pointer to the shader to acquire resources from.
 * @param out_instance_id A pointer to hold the new instance identifier.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_acquire_instance_resources(vulkan_shader* shader, u32* out_instance_id);

/**
 * @brief Releases internal instance-level resources for the given instance id.
 * 
 * @param shader A pointer to the shader to release resources from.
 * @param instance_id The instance identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_release_instance_resources(vulkan_shader* shader, u32 instance_id);

/**
 * @brief Attempts to retrieve uniform location for the given name. Uniforms and
 * samplers both have locations, regardless of scope.
 * 
 * @param shader A pointer to the shader to retrieve location from.
 * @param uniform_name The name of the uniform.
 * @return The location if successful; otherwise INVALID_ID.
 */
u32 vulkan_shader_uniform_location(vulkan_shader* shader, const char* uniform_name);

/**
 * @brief Sets the sampler at the given location to use the provided texture.
 * 
 * @param shader A pointer to the shader to set the sampler for.
 * @param location The location of the sampler to set.
 * @param t A pointer to the texture to be assigned to the sampler.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_sampler(vulkan_shader* shader, u32 location, texture* t);

/**
 * @brief Sets the value of the signed 8-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_i8(vulkan_shader* shader, u32 location, i8 value);

/**
 * @brief Sets the value of the signed 16-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_i16(vulkan_shader* shader, u32 location, i16 value);

/**
 * @brief Sets the value of the signed 32-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_i32(vulkan_shader* shader, u32 location, i32 value);

/**
 * @brief Sets the value of the unsigned 8-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_u8(vulkan_shader* shader, u32 location, u8 value);

/**
 * @brief Sets the value of the unsigned 16-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_u16(vulkan_shader* shader, u32 location, u16 value);

/**
 * @brief Sets the value of the unsigned 32-bit integer uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_u32(vulkan_shader* shader, u32 location, u32 value);

/**
 * @brief Sets the value of the 32-bit float uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_f32(vulkan_shader* shader, u32 location, f32 value);

/**
 * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec2(vulkan_shader* shader, u32 location, vec2 value);

/**
 * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec2f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1);

/**
 * @brief Sets the value of the vector3 (3x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec3(vulkan_shader* shader, u32 location, vec3 value);

/**
 * @brief Sets the value of the vector3 (3x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @param value_2 The third value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec3f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2);

/**
 * @brief Sets the value of the vector4 (4x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec4(vulkan_shader* shader, u32 location, vec4 value);

/**
 * @brief Sets the value of the vector4 (4x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @param value_2 The third value to be set.
 * @param value_3 The fourth value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_vec4f(vulkan_shader* shader, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3);

/**
 * @brief Sets the value of the matrix4 (16x 32-bit float) uniform at the provided location.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_mat4(vulkan_shader* shader, u32 location, mat4 value);

/**
 * @brief Sets the value of the custom-size uniform at the provided location.
 * Size of data should match the size originally added. NOTE: Size verification
 * is bypassed for this type.
 * 
 * @param shader A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 vulkan_shader_set_uniform_custom(vulkan_shader* shader, u32 location, void* value);
