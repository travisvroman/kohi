/**
 * @file renderer_frontend.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The renderer frontend, which is the only thing the rest of the engine sees.
 * This is responsible for transferring any data to and from the renderer backend in an
 * agnostic way.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "renderer_types.inl"

/**
 * @brief Initializes the renderer frontend/system. Should be called twice - once
 * to obtain the memory requirement (passing state=0), and a second time passing
 * allocated memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement for this system.
 * @param state A block of memory to hold state data, or 0 if obtaining memory requirement.
 * @param application_name The name of the application.
 * @return True on success; otherwise false.
 */
b8 renderer_system_initialize(u64* memory_requirement, void* state, const char* application_name);

/**
 * @brief Shuts the renderer system/frontend down.
 *
 * @param state A pointer to the state block of memory.
 */
void renderer_system_shutdown(void* state);

/**
 * @brief Handles resize events.
 *
 * @param width The new window width.
 * @param height The new window height.
 */
void renderer_on_resized(u16 width, u16 height);

/**
 * @brief Draws the next frame using the data provided in the render packet.
 *
 * @param packet A pointer to the render packet, which contains data on what should be rendered.
 * @return True on success; otherwise false.
 */
b8 renderer_draw_frame(render_packet* packet);

/**
 * @brief Sets the view matrix in the renderer. NOTE: exposed to public API.
 *
 * @deprecated HACK: this should not be exposed outside the engine.
 * @param view The view matrix to be set.
 */
KAPI void renderer_set_view(mat4 view);

/**
 * @brief Creates a new texture.
 *
 * @param pixels The raw image data to be uploaded to the GPU.
 * @param texture A pointer to the texture to be loaded.
 */
void renderer_create_texture(const u8* pixels, struct texture* texture);

/**
 * @brief Destroys the given texture, releasing internal resources from the GPU.
 *
 * @param texture A pointer to the texture to be destroyed.
 */
void renderer_destroy_texture(struct texture* texture);

/**
 * @brief Creates a new material instance, acquiring GPU resources.
 *
 * @param material A pointer to the material to load.
 * @return True on success; otherwise false.
 */
b8 renderer_create_material(struct material* material);

/**
 * @brief Destroys the given material, releasing GPU resources.
 *
 * @param material A pointer to the material to unload.
 */
void renderer_destroy_material(struct material* material);

/**
 * @brief Acquiores GPU resources and uploads geometry data.
 *
 * @param geometry A pointer to the geometry to acquire resources for.
 * @param vertex_size The size of each vertex.
 * @param vertex_count The number of vertices.
 * @param vertices The vertex array.
 * @param index_size The size of each index.
 * @param index_count The number of indices.
 * @param indices The index array.
 * @return True on success; otherwise false.
 */
b8 renderer_create_geometry(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

/**
 * @brief Destroys the given geometry, releasing GPU resources.
 *
 * @param geometry A pointer to the geometry to be destroyed.
 */
void renderer_destroy_geometry(geometry* geometry);

/**
 * @brief Obtains the identifier of the renderpass with the given name.
 * 
 * @param name The name of the renderpass whose identifier to obtain.
 * @param out_renderpass_id A pointer to hold the renderpass id.
 * @return True if found; otherwise false.
 */
b8 renderer_renderpass_id(const char* name, u8* out_renderpass_id);

/**
 * @brief Creates a new shader using the provided parameters.
 * @param name The name of the shader.
 * @param renderpass_id The identifier of the renderpass to be associated with the shader.
 * @param stages Bit flags representing the stages the shader will have. Pass a combination of `shader_stage`s.
 * @param use_instances Indicates if instances will be used with the shader.
 * @param use_local Indicates if local uniforms will be used with the shader.
 * @param out_shader A pointer to hold the identifier of the newly-created shader.
 * @returns True on success; otherwise false.
 */
b8 renderer_shader_create(const char* name, u8 renderpass_id, u32 stages, b8 use_instances, b8 use_local, u32* out_shader_id);

/**
 * @brief Destroys the given shader and releases any resources held by it.
 * @param shader_id The identifier of the shader to be destroyed.
 */
void renderer_shader_destroy(u32 shader_id);

/**
 * @brief Adds a new vertex attribute. Must be done after shader initialization.
 *
 * @param shader_id The identifier of the shader to add the attribute to.
 * @param name The name of the attribute.
 * @param type The type of the attribute.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_attribute(u32 shader_id, const char* name, shader_attribute_type type);

/**
 * @brief Adds a texture sampler to the shader. Must be done after shader initialization.
 *
 * @param shader_id The identifier of the shader to add the sampler to.
 * @param sampler_name The name of the sampler.
 * @param scope The scope of the sampler. Can be global or instance.
 * @param out_location A pointer to hold the location of the attribute for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_sampler(u32 shader_id, const char* sampler_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 8-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_i8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 16-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_i16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new signed 32-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_i32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 8-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_u8(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 16-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_u16(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new unsigned 32-bit integer uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_u32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new 32-bit float uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_f32(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector2 (2x 32-bit floats) uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_vec2(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector3 (3x 32-bit floats) uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_vec3(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new vector4 (4x 32-bit floats) uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_vec4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new mat4 (4x4 matrix/16x 32-bit floats) uniform to the shader.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_mat4(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

/**
 * @brief Adds a new custom-sized uniform to the shader. This is useful for structure
 * types. NOTE: Size verification is not done for this type when setting the uniform.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name The name of the uniform.
 * @param size The size of the uniform in bytes.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform_custom(u32 shader_id, const char* uniform_name, u32 size, shader_scope scope, u32* out_location);

/**
 * @brief Adds a uniform of the given type to the shader. This may be used for all
 * uniform types except CUSTOM, which will throw an error. Call renderer_shader_add_uniform_custom
 * for custom uniforms.
 *
 * @param shader_id The identifier of the shader to add the uniform to.
 * @param uniform_name he name of the uniform.
 * @param type The type of the uniform. Do not pass CUSTOM here, use renderer_shader_add_uniform_custom for custom uniforms instead.
 * @param scope The scope of the uniform. Can be global, instance or local.
 * @param out_location A pointer to hold the location of the uniform for future use.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_add_uniform(u32 shader_id, const char* uniform_name, shader_uniform_type type, shader_scope scope, u32* out_location);

// End add attributes/samplers/uniforms

/**
 * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
 * Must be done after vulkan_shader_create().
 *
 * @param shader_id The identifier of the shader to be initialized.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_initialize(u32 shader_id);

/**
 * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
 * and for use in draw calls.
 *
 * @param shader_id The identifier of the shader to be used.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_use(u32 shader_id);

/**
 * @brief Binds global resources for use and updating.
 *
 * @param shader_id The identifier of the shader whose globals are to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_globals(u32 shader_id);

/**
 * @brief Binds instance resources for use and updating.
 *
 * @param shader_id The identifier of the shader whose instance resources are to be bound.
 * @param instance_id The identifier of the instance to be bound.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_bind_instance(u32 shader_id, u32 instance_id);

/**
 * @brief Applies global data to the uniform buffer.
 *
 * @param shader_id The identifier of the shader to apply the global data for.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_globals(u32 shader_id);

/**
 * @brief Applies data for the currently bound instance.
 *
 * @param shader_id The identifier of the shader to apply the instance data for.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_apply_instance(u32 shader_id);

/**
 * @brief Acquires internal instance-level resources and provides an instance id.
 *
 * @param shader_id The identifier of the shader to acquire resources from.
 * @param out_instance_id A pointer to hold the new instance identifier.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_acquire_instance_resources(u32 shader_id, u32* out_instance_id);

/**
 * @brief Releases internal instance-level resources for the given instance id.
 *
 * @param shader_id The identifier of the shader to release resources from.
 * @param instance_id The instance identifier whose resources are to be released.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_release_instance_resources(u32 shader_id, u32 instance_id);

/**
 * @brief Attempts to retrieve uniform location for the given name. Uniforms and
 * samplers both have locations, regardless of scope.
 *
 * @param shader_id The identifier of the shader to retrieve location from.
 * @param uniform_name The name of the uniform.
 * @return The location if successful; otherwise INVALID_ID.
 */
u32 renderer_shader_uniform_location(u32 shader_id, const char* uniform_name);

/**
 * @brief Sets the sampler at the given location to use the provided texture.
 *
 * @param shader_id The identifier of the shader to set the sampler for.
 * @param location The location of the sampler to set.
 * @param t A pointer to the texture to be assigned to the sampler.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_sampler(u32 shader_id, u32 location, texture* t);

/**
 * @brief Sets the value of the signed 8-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_i8(u32 shader_id, u32 location, i8 value);

/**
 * @brief Sets the value of the signed 16-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_i16(u32 shader_id, u32 location, i16 value);

/**
 * @brief Sets the value of the signed 32-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_i32(u32 shader_id, u32 location, i32 value);

/**
 * @brief Sets the value of the unsigned 8-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_u8(u32 shader_id, u32 location, u8 value);

/**
 * @brief Sets the value of the unsigned 16-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_u16(u32 shader_id, u32 location, u16 value);

/**
 * @brief Sets the value of the unsigned 32-bit integer uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_u32(u32 shader_id, u32 location, u32 value);

/**
 * @brief Sets the value of the 32-bit float uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_f32(u32 shader_id, u32 location, f32 value);

/**
 * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec2(u32 shader_id, u32 location, vec2 value);

/**
 * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec2f(u32 shader_id, u32 location, f32 value_0, f32 value_1);

/**
 * @brief Sets the value of the vector3 (3x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec3(u32 shader_id, u32 location, vec3 value);

/**
 * @brief Sets the value of the vector3 (3x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @param value_2 The third value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec3f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2);

/**
 * @brief Sets the value of the vector4 (4x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec4(u32 shader_id, u32 location, vec4 value);

/**
 * @brief Sets the value of the vector4 (4x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value_0 The first value to be set.
 * @param value_1 The second value to be set.
 * @param value_2 The third value to be set.
 * @param value_3 The fourth value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_vec4f(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3);

/**
 * @brief Sets the value of the matrix4 (16x 32-bit float) uniform at the provided location.
 *
 * @param shader_id A pointer to set the uniform value for.
 * @param location The location of the uniform to be set.
 * @param value The value to be set.
 * @return True on success; otherwise false.
 */
b8 renderer_shader_set_uniform_mat4(u32 shader_id, u32 location, mat4 value);

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
b8 renderer_shader_set_uniform_custom(u32 shader_id, u32 location, void* value);
