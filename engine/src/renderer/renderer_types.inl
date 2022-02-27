#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

typedef enum renderer_backend_type {
    RENDERER_BACKEND_TYPE_VULKAN,
    RENDERER_BACKEND_TYPE_OPENGL,
    RENDERER_BACKEND_TYPE_DIRECTX
} renderer_backend_type;

typedef struct geometry_render_data {
    mat4 model;
    geometry* geometry;
} geometry_render_data;

typedef enum builtin_renderpass {
    BUILTIN_RENDERPASS_WORLD = 0x01,
    BUILTIN_RENDERPASS_UI = 0x02
} builtin_renderpass;

typedef enum shader_stage {
    SHADER_STAGE_VERTEX = 0x00000001,
    SHADER_STAGE_GEOMETRY = 0x00000002,
    SHADER_STAGE_FRAGMENT = 0x00000004,
    SHADER_STAGE_COMPUTE = 0x0000008
} shader_stage;

typedef enum shader_attribute_type {
    SHADER_ATTRIB_TYPE_FLOAT32,
    SHADER_ATTRIB_TYPE_FLOAT32_2,
    SHADER_ATTRIB_TYPE_FLOAT32_3,
    SHADER_ATTRIB_TYPE_FLOAT32_4,
    SHADER_ATTRIB_TYPE_MATRIX_4,
    SHADER_ATTRIB_TYPE_INT8,
    SHADER_ATTRIB_TYPE_INT8_2,
    SHADER_ATTRIB_TYPE_INT8_3,
    SHADER_ATTRIB_TYPE_INT8_4,
    SHADER_ATTRIB_TYPE_UINT8,
    SHADER_ATTRIB_TYPE_UINT8_2,
    SHADER_ATTRIB_TYPE_UINT8_3,
    SHADER_ATTRIB_TYPE_UINT8_4,
    SHADER_ATTRIB_TYPE_INT16,
    SHADER_ATTRIB_TYPE_INT16_2,
    SHADER_ATTRIB_TYPE_INT16_3,
    SHADER_ATTRIB_TYPE_INT16_4,
    SHADER_ATTRIB_TYPE_UINT16,
    SHADER_ATTRIB_TYPE_UINT16_2,
    SHADER_ATTRIB_TYPE_UINT16_3,
    SHADER_ATTRIB_TYPE_UINT16_4,
    SHADER_ATTRIB_TYPE_INT32,
    SHADER_ATTRIB_TYPE_INT32_2,
    SHADER_ATTRIB_TYPE_INT32_3,
    SHADER_ATTRIB_TYPE_INT32_4,
    SHADER_ATTRIB_TYPE_UINT32,
    SHADER_ATTRIB_TYPE_UINT32_2,
    SHADER_ATTRIB_TYPE_UINT32_3,
    SHADER_ATTRIB_TYPE_UINT32_4
} shader_attribute_type;

/**
 * @brief Defines shader scope, which indicates how
 * often it gets updated.
 */
typedef enum shader_scope {
    /** @brief Global shader scope, generally updated once per frame. */
    SHADER_SCOPE_GLOBAL = 0,
    /** @brief Instance shader scope, generally updated "per-instance" of the shader. */
    SHADER_SCOPE_INSTANCE = 1,
    /** @brief Local shader scope, generally updated per-object */
    SHADER_SCOPE_LOCAL = 2
} shader_scope;

/**
 * @brief A generic "interface" for the backend. The renderer backend
 * is what is responsible for making calls to the graphics API such as
 * Vulkan, OpenGL or DirectX. Each of these should implement this interface.
 * The frontend only interacts via this structure and has no knowledge of
 * the way things actually work on the backend.
 */
typedef struct renderer_backend {
    u64 frame_number;

    /**
     * @brief Initializes the backend.
     *
     * @param backend A pointer to the generic backend interface.
     * @param application_name The name of the application.
     * @return True if initialized successfully; otherwise false.
     */
    b8 (*initialize)(struct renderer_backend* backend, const char* application_name);

    /**
     * @brief Shuts the renderer backend down.
     *
     * @param backend A pointer to the generic backend interface.
     */
    void (*shutdown)(struct renderer_backend* backend);

    /**
     * @brief Handles window resizes.
     *
     * @param backend A pointer to the generic backend interface.
     * @param width The new window width.
     * @param height The new window height.
     */
    void (*resized)(struct renderer_backend* backend, u16 width, u16 height);

    /**
     * @brief Performs setup routines required at the start of a frame.
     * @note A false result does not necessarily indicate failure. It can also specify that
     * the backend is simply not in a state capable of drawing a frame at the moment, and
     * that it should be attempted again on the next loop. End frame does not need to (and
     * should not) be called if this is the case.
     * @param backend A pointer to the generic backend interface.
     * @param delta_time The time in seconds since the last frame.
     * @return True if successful; otherwise false.
     */
    b8 (*begin_frame)(struct renderer_backend* backend, f32 delta_time);

    /**
     * @brief Performs routines required to draw a frame, such as presentation. Should only be called
     * after a successful return of begin_frame.
     *
     * @param backend A pointer to the generic backend interface.
     * @param delta_time The time in seconds since the last frame.
     * @return True on success; otherwise false.
     */
    b8 (*end_frame)(struct renderer_backend* backend, f32 delta_time);

    /**
     * @brief Begins a renderpass with the given id.
     *
     * @param backend A pointer to the generic backend interface.
     * @param renderpass_id The identifier of the renderpass to begin.
     * @return True on success; otherwise false.
     */
    b8 (*begin_renderpass)(struct renderer_backend* backend, u8 renderpass_id);

    /**
     * @brief Ends a renderpass with the given id.
     *
     * @param backend A pointer to the generic backend interface.
     * @param renderpass_id The identifier of the renderpass to end.
     * @return True on success; otherwise false.
     */
    b8 (*end_renderpass)(struct renderer_backend* backend, u8 renderpass_id);

    /**
     * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
     *
     * @param data The render data of the geometry to be drawn.
     */
    void (*draw_geometry)(geometry_render_data data);

    /**
     * @brief Creates a Vulkan-specific texture, acquiring internal resources as needed.
     *
     * @param pixels The raw image data used for the texture.
     * @param texture A pointer to the texture to hold the resources.
     */
    void (*create_texture)(const u8* pixels, struct texture* texture);

    /**
     * @brief Destroys the given texture, releasing internal resources.
     *
     * @param texture A pointer to the texture to be destroyed.
     */
    void (*destroy_texture)(struct texture* texture);

    /**
     * @brief Creates a material, acquiring required internal resources.
     *
     * @param material A pointer to the material to hold the resources.
     * @return True on success; otherwise false.
     */
    b8 (*create_material)(struct material* material);

    /**
     * @brief Destroys a texture, releasing required internal resouces.
     *
     * @param material A pointer to the material whose resources should be released.
     */
    void (*destroy_material)(struct material* material);

    /**
     * @brief Creates Vulkan-specific internal resources for the given geometry using
     * the data provided.
     *
     * @param geometry A pointer to the geometry to be created.
     * @param vertex_size The size of a single vertex.
     * @param vertex_count The total number of vertices.
     * @param vertices An array of vertices.
     * @param index_size The size of an individual index.
     * @param index_count The total number of indices.
     * @param indices An array of indices.
     * @return True on success; otherwise false.
     */
    b8 (*create_geometry)(geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

    /**
     * @brief Destroys the given geometry, releasing internal resources.
     *
     * @param geometry A pointer to the geometry to be destroyed.
     */
    void (*destroy_geometry)(geometry* geometry);

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
    b8 (*shader_create)(const char* name, u8 renderpass_id, u32 stages, b8 use_instances, b8 use_local, u32* out_shader_id);

    /**
     * @brief Destroys the given shader and releases any resources held by it.
     * @param shader_id The identifier of the shader to be destroyed.
     */
    void (*shader_destroy)(u32 shader_id);

    /**
     * @brief Adds a new vertex attribute. Must be done after shader initialization.
     *
     * @param shader_id The identifier of the shader to add the attribute to.
     * @param name The name of the attribute.
     * @param type The type of the attribute.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_attribute)(u32 shader_id, const char* name, shader_attribute_type type);

    /**
     * @brief Adds a texture sampler to the shader. Must be done after shader initialization.
     *
     * @param shader_id The identifier of the shader to add the sampler to.
     * @param sampler_name The name of the sampler.
     * @param scope The scope of the sampler. Can be global or instance.
     * @param out_location A pointer to hold the location of the attribute for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_sampler)(u32 shader_id, const char* sampler_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new signed 8-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_i8)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new signed 16-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_i16)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new signed 32-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_i32)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new unsigned 8-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_u8)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new unsigned 16-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_u16)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new unsigned 32-bit integer uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_u32)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new 32-bit float uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_f32)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new vector2 (2x 32-bit floats) uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_vec2)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new vector3 (3x 32-bit floats) uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_vec3)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new vector4 (4x 32-bit floats) uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_vec4)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

    /**
     * @brief Adds a new mat4 (4x4 matrix/16x 32-bit floats) uniform to the shader.
     *
     * @param shader_id The identifier of the shader to add the uniform to.
     * @param uniform_name The name of the uniform.
     * @param scope The scope of the uniform. Can be global, instance or local.
     * @param out_location A pointer to hold the location of the uniform for future use.
     * @return True on success; otherwise false.
     */
    b8 (*shader_add_uniform_mat4)(u32 shader_id, const char* uniform_name, shader_scope scope, u32* out_location);

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
    b8 (*shader_add_uniform_custom)(u32 shader_id, const char* uniform_name, u32 size, shader_scope scope, u32* out_location);
    // End add attributes/samplers/uniforms

    /**
     * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
     * Must be done after vulkan_shader_create().
     *
     * @param shader_id The identifier of the shader to be initialized.
     * @return True on success; otherwise false.
     */
    b8 (*shader_initialize)(u32 shader_id);

    /**
     * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
     * and for use in draw calls.
     *
     * @param shader_id The identifier of the shader to be used.
     * @return True on success; otherwise false.
     */
    b8 (*shader_use)(u32 shader_id);

    /**
     * @brief Binds global resources for use and updating.
     *
     * @param shader_id The identifier of the shader whose globals are to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_globals)(u32 shader_id);

    /**
     * @brief Binds instance resources for use and updating.
     *
     * @param shader_id The identifier of the shader whose instance resources are to be bound.
     * @param instance_id The identifier of the instance to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_instance)(u32 shader_id, u32 instance_id);

    /**
     * @brief Applies global data to the uniform buffer.
     *
     * @param shader_id The identifier of the shader to apply the global data for.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_globals)(u32 shader_id);

    /**
     * @brief Applies data for the currently bound instance.
     *
     * @param shader_id The identifier of the shader to apply the instance data for.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_instance)(u32 shader_id);

    /**
     * @brief Acquires internal instance-level resources and provides an instance id.
     *
     * @param shader_id The identifier of the shader to acquire resources from.
     * @param out_instance_id A pointer to hold the new instance identifier.
     * @return True on success; otherwise false.
     */
    b8 (*shader_acquire_instance_resources)(u32 shader_id, u32* out_instance_id);

    /**
     * @brief Releases internal instance-level resources for the given instance id.
     *
     * @param shader_id The identifier of the shader to release resources from.
     * @param instance_id The instance identifier whose resources are to be released.
     * @return True on success; otherwise false.
     */
    b8 (*shader_release_instance_resources)(u32 shader_id, u32 instance_id);

    /**
     * @brief Attempts to retrieve uniform location for the given name. Uniforms and
     * samplers both have locations, regardless of scope.
     *
     * @param shader_id The identifier of the shader to retrieve location from.
     * @param uniform_name The name of the uniform.
     * @return The location if successful; otherwise INVALID_ID.
     */
    u32 (*shader_uniform_location)(u32 shader_id, const char* uniform_name);

    /**
     * @brief Sets the sampler at the given location to use the provided texture.
     *
     * @param shader_id The identifier of the shader to set the sampler for.
     * @param location The location of the sampler to set.
     * @param t A pointer to the texture to be assigned to the sampler.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_sampler)(u32 shader_id, u32 location, texture* t);

    /**
     * @brief Sets the value of the signed 8-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_i8)(u32 shader_id, u32 location, i8 value);

    /**
     * @brief Sets the value of the signed 16-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_i16)(u32 shader_id, u32 location, i16 value);

    /**
     * @brief Sets the value of the signed 32-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_i32)(u32 shader_id, u32 location, i32 value);

    /**
     * @brief Sets the value of the unsigned 8-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_u8)(u32 shader_id, u32 location, u8 value);

    /**
     * @brief Sets the value of the unsigned 16-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_u16)(u32 shader_id, u32 location, u16 value);

    /**
     * @brief Sets the value of the unsigned 32-bit integer uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_u32)(u32 shader_id, u32 location, u32 value);

    /**
     * @brief Sets the value of the 32-bit float uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_f32)(u32 shader_id, u32 location, f32 value);

    /**
     * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_vec2)(u32 shader_id, u32 location, vec2 value);

    /**
     * @brief Sets the value of the vector2 (2x 32-bit float) uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value_0 The first value to be set.
     * @param value_1 The second value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_vec2f)(u32 shader_id, u32 location, f32 value_0, f32 value_1);

    /**
     * @brief Sets the value of the vector3 (3x 32-bit float) uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_vec3)(u32 shader_id, u32 location, vec3 value);

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
    b8 (*shader_set_uniform_vec3f)(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2);

    /**
     * @brief Sets the value of the vector4 (4x 32-bit float) uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_vec4)(u32 shader_id, u32 location, vec4 value);

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
    b8 (*shader_set_uniform_vec4f)(u32 shader_id, u32 location, f32 value_0, f32 value_1, f32 value_2, f32 value_3);

    /**
     * @brief Sets the value of the matrix4 (16x 32-bit float) uniform at the provided location.
     *
     * @param shader_id A pointer to set the uniform value for.
     * @param location The location of the uniform to be set.
     * @param value The value to be set.
     * @return True on success; otherwise false.
     */
    b8 (*shader_set_uniform_mat4)(u32 shader_id, u32 location, mat4 value);

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
    b8 (*shader_set_uniform_custom)(u32 shader_id, u32 location, void* value);
} renderer_backend;

typedef struct render_packet {
    f32 delta_time;

    u32 geometry_count;
    geometry_render_data* geometries;

    u32 ui_geometry_count;
    geometry_render_data* ui_geometries;
} render_packet;
