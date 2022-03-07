#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

#define BUILTIN_SHADER_NAME_MATERIAL "Shader.Builtin.Material"
#define BUILTIN_SHADER_NAME_UI "Shader.Builtin.UI"

struct shader;
struct shader_uniform;

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
     * @param stage_count The total number of stages.
     * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
     * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
     * @param use_instances Indicates if instances will be used with the shader.
     * @param use_local Indicates if local uniforms will be used with the shader.
     * @param out_shader A pointer to hold the identifier of the newly-created shader.
     * @returns True on success; otherwise false.
     */
    b8 (*shader_create)(struct shader* shader, u8 renderpass_id, u8 stage_count, const char** stage_filenames, shader_stage* stages);

    /**
     * @brief Destroys the given shader and releases any resources held by it.
     * @param shader_id The identifier of the shader to be destroyed.
     */
    void (*shader_destroy)(struct shader* shader);

    b8 (*shader_set_uniform)(struct shader* frontend_shader, struct shader_uniform* uniform, const void* value);
    b8 (*shader_initialize)(struct shader* shader);
    b8 (*shader_use)(struct shader* shader);
    b8 (*shader_bind_globals)(struct shader* s);
    b8 (*shader_bind_instance)(struct shader* s, u32 instance_id);
    b8 (*shader_apply_globals)(struct shader* s);
    b8 (*shader_apply_instance)(struct shader* s);
    b8 (*shader_acquire_instance_resources)(struct shader* s, u32* out_instance_id);
    b8 (*shader_release_instance_resources)(struct shader* s, u32 instance_id);

} renderer_backend;

typedef struct render_packet {
    f32 delta_time;

    u32 geometry_count;
    geometry_render_data* geometries;

    u32 ui_geometry_count;
    geometry_render_data* ui_geometries;
} render_packet;
