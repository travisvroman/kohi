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

typedef enum renderer_debug_view_mode {
    RENDERER_VIEW_MODE_DEFAULT = 0,
    RENDERER_VIEW_MODE_LIGHTING = 1,
    RENDERER_VIEW_MODE_NORMALS = 2
} renderer_debug_view_mode;

/** @brief Represents a render target, which is used for rendering to a texture or set of textures. */
typedef struct render_target {
    /** @brief Indicates if this render target should be updated on window resize. */
    b8 sync_to_window_size;
    /** @brief The number of attachments */
    u8 attachment_count;
    /** @brief An array of attachments (pointers to textures). */
    struct texture** attachments;
    /** @brief The renderer API internal framebuffer object. */
    void* internal_framebuffer;
} render_target;

/**
 * @brief The types of clearing to be done on a renderpass.
 * Can be combined together for multiple clearing functions.
 */
typedef enum renderpass_clear_flag {
    /** @brief No clearing shoudl be done. */
    RENDERPASS_CLEAR_NONE_FLAG = 0x0,
    /** @brief Clear the colour buffer. */
    RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG = 0x1,
    /** @brief Clear the depth buffer. */
    RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG = 0x2,
    /** @brief Clear the stencil buffer. */
    RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG = 0x4
} renderpass_clear_flag;

typedef struct renderpass_config {
    /** @brief The name of this renderpass. */
    const char* name;
    /** @brief The name of the previous renderpass. */
    const char* prev_name;
    /** @brief The name of the next renderpass. */
    const char* next_name;
    /** @brief The current render area of the renderpass. */
    vec4 render_area;
    /** @brief The clear colour used for this renderpass. */
    vec4 clear_colour;

    /** @brief The clear flags for this renderpass. */
    u8 clear_flags;
} renderpass_config;

/**
 * @brief Represents a generic renderpass.
 */
typedef struct renderpass {
    /** @brief The id of the renderpass */
    u16 id;

    /** @brief The current render area of the renderpass. */
    vec4 render_area;
    /** @brief The clear colour used for this renderpass. */
    vec4 clear_colour;

    /** @brief The clear flags for this renderpass. */
    u8 clear_flags;
    /** @brief The number of render targets for this renderpass. */
    u8 render_target_count;
    /** @brief An array of render targets used by this renderpass. */
    render_target* targets;

    /** @brief Internal renderpass data */
    void* internal_data;
} renderpass;

/** @brief The generic configuration for a renderer backend. */
typedef struct renderer_backend_config {
    /** @brief The name of the application */
    const char* application_name;
    /** @brief The number of pointers to renderpasses. */
    u16 renderpass_count;
    /** @brief An array configurations for renderpasses. Will be initialized on the backend automatically. */
    renderpass_config* pass_configs;
    /** @brief A callback that will be made when the backend requires a refresh/regeneration of the render targets. */
    void (*on_rendertarget_refresh_required)();
} renderer_backend_config;

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
     * @param config A pointer to configuration to be used when initializing the backend.
     * @param out_window_render_target_count A pointer to hold how many render targets are needed for renderpasses targeting the window.
     * @return True if initialized successfully; otherwise false.
     */
    b8 (*initialize)(struct renderer_backend* backend, const renderer_backend_config* config, u8* out_window_render_target_count);

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
     * @param pass A pointer to the renderpass to begin.
     * @param target A pointer to the render target to be used.
     * @return True on success; otherwise false.
     */
    b8 (*renderpass_begin)(renderpass* pass, render_target* target);

    /**
     * @brief Ends a renderpass with the given id.
     *
     * @param pass A pointer to the renderpass to end.
     * @return True on success; otherwise false.
     */
    b8 (*renderpass_end)(renderpass* pass);

    /**
     * @brief Obtains a pointer to a renderpass using the provided name.
     *
     * @param name The renderpass name.
     * @return A pointer to a renderpass, if found; otherwise 0.
     */
    renderpass* (*renderpass_get)(const char* name);

    /**
     * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
     *
     * @param data A pointer to the render data of the geometry to be drawn.
     */
    void (*draw_geometry)(geometry_render_data* data);

    /**
     * @brief Creates a Vulkan-specific texture, acquiring internal resources as needed.
     *
     * @param pixels The raw image data used for the texture.
     * @param texture A pointer to the texture to hold the resources.
     */
    void (*texture_create)(const u8* pixels, struct texture* texture);

    /**
     * @brief Destroys the given texture, releasing internal resources.
     *
     * @param texture A pointer to the texture to be destroyed.
     */
    void (*texture_destroy)(struct texture* texture);

    /**
     * @brief Creates a new writeable texture with no data written to it.
     *
     * @param t A pointer to the texture to hold the resources.
     */
    void (*texture_create_writeable)(texture* t);

    /**
     * @brief Resizes a texture. There is no check at this level to see if the
     * texture is writeable. Internal resources are destroyed and re-created at
     * the new resolution. Data is lost and would need to be reloaded.
     *
     * @param t A pointer to the texture to be resized.
     * @param new_width The new width in pixels.
     * @param new_height The new height in pixels.
     */
    void (*texture_resize)(texture* t, u32 new_width, u32 new_height);

    /**
     * @brief Writes the given data to the provided texture.
     * NOTE: At this level, this can either be a writeable or non-writeable texture because
     * this also handles the initial texture load. The texture system itself should be
     * responsible for blocking write requests to non-writeable textures.
     *
     * @param t A pointer to the texture to be written to.
     * @param offset The offset in bytes from the beginning of the data to be written.
     * @param size The number of bytes to be written.
     * @param pixels The raw image data to be written.
     */
    void (*texture_write_data)(texture* t, u32 offset, u32 size, const u8* pixels);

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
     * @brief Creates internal shader resources using the provided parameters.
     *
     * @param s A pointer to the shader.
     * @param pass A pointer to the renderpass to be associated with the shader.
     * @param stage_count The total number of stages.
     * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
     * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_create)(struct shader* shader, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages);

    /**
     * @brief Destroys the given shader and releases any resources held by it.
     * @param s A pointer to the shader to be destroyed.
     */
    void (*shader_destroy)(struct shader* shader);

    /**
     * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
     * Must be done after vulkan_shader_create().
     *
     * @param s A pointer to the shader to be initialized.
     * @return True on success; otherwise false.
     */
    b8 (*shader_initialize)(struct shader* shader);

    /**
     * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
     * and for use in draw calls.
     *
     * @param s A pointer to the shader to be used.
     * @return True on success; otherwise false.
     */
    b8 (*shader_use)(struct shader* shader);

    /**
     * @brief Binds global resources for use and updating.
     *
     * @param s A pointer to the shader whose globals are to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_globals)(struct shader* s);

    /**
     * @brief Binds instance resources for use and updating.
     *
     * @param s A pointer to the shader whose instance resources are to be bound.
     * @param instance_id The identifier of the instance to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_instance)(struct shader* s, u32 instance_id);

    /**
     * @brief Applies global data to the uniform buffer.
     *
     * @param s A pointer to the shader to apply the global data for.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_globals)(struct shader* s);

    /**
     * @brief Applies data for the currently bound instance.
     *
     * @param s A pointer to the shader to apply the instance data for.
     * @param needs_update Indicates if the shader uniforms need to be updated or just bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_instance)(struct shader* s, b8 needs_update);

    /**
     * @brief Acquires internal instance-level resources and provides an instance id.
     *
     * @param s A pointer to the shader to acquire resources from.
     * @param maps An array of pointers to texture maps. Must be one map per instance texture.
     * @param out_instance_id A pointer to hold the new instance identifier.
     * @return True on success; otherwise false.
     */
    b8 (*shader_acquire_instance_resources)(struct shader* s, texture_map** maps, u32* out_instance_id);

    /**
     * @brief Releases internal instance-level resources for the given instance id.
     *
     * @param s A pointer to the shader to release resources from.
     * @param instance_id The instance identifier whose resources are to be released.
     * @return True on success; otherwise false.
     */
    b8 (*shader_release_instance_resources)(struct shader* s, u32 instance_id);

    /**
     * @brief Sets the uniform of the given shader to the provided value.
     *
     * @param s A ponter to the shader.
     * @param uniform A constant pointer to the uniform.
     * @param value A pointer to the value to be set.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_set_uniform)(struct shader* frontend_shader, struct shader_uniform* uniform, const void* value);

    /**
     * @brief Acquires internal resources for the given texture map.
     *
     * @param map A pointer to the texture map to obtain resources for.
     * @return True on success; otherwise false.
     */
    b8 (*texture_map_acquire_resources)(struct texture_map* map);

    /**
     * @brief Releases internal resources for the given texture map.
     *
     * @param map A pointer to the texture map to release resources from.
     */
    void (*texture_map_release_resources)(struct texture_map* map);

    /**
     * @brief Creates a new render target using the provided data.
     *
     * @param attachment_count The number of attachments (texture pointers).
     * @param attachments An array of attachments (texture pointers).
     * @param renderpass A pointer to the renderpass the render target is associated with.
     * @param width The width of the render target in pixels.
     * @param height The height of the render target in pixels.
     * @param out_target A pointer to hold the newly created render target.
     */
    void (*render_target_create)(u8 attachment_count, texture** attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);

    /**
     * @brief Destroys the provided render target.
     *
     * @param target A pointer to the render target to be destroyed.
     * @param free_internal_memory Indicates if internal memory should be freed.
     */
    void (*render_target_destroy)(render_target* target, b8 free_internal_memory);

    /**
     * @brief Creates a new renderpass.
     *
     * @param out_renderpass A pointer to the generic renderpass.
     * @param depth The depth clear amount.
     * @param stencil The stencil clear value.
     * @param clear_flags The combined clear flags indicating what kind of clear should take place.
     * @param has_prev_pass Indicates if there is a previous renderpass.
     * @param has_next_pass Indicates if there is a next renderpass.
     */
    void (*renderpass_create)(renderpass* out_renderpass, f32 depth, u32 stencil, b8 has_prev_pass, b8 has_next_pass);

    /**
     * @brief Destroys the given renderpass.
     *
     * @param pass A pointer to the renderpass to be destroyed.
     */
    void (*renderpass_destroy)(renderpass* pass);

    /**
     * @brief Attempts to get the window render target at the given index.
     *
     * @param index The index of the attachment to get. Must be within the range of window render target count.
     * @return A pointer to a texture attachment if successful; otherwise 0.
     */
    texture* (*window_attachment_get)(u8 index);

    /**
     * @brief Returns a pointer to the main depth texture target.
     */
    texture* (*depth_attachment_get)();

    /**
     * @brief Returns the current window attachment index.
     */
    u8 (*window_attachment_index_get)();

} renderer_backend;

/** @brief Known render view types, which have logic associated with them. */
typedef enum render_view_known_type {
    /** @brief A view which only renders objects with *no* transparency. */
    RENDERER_VIEW_KNOWN_TYPE_WORLD = 0x01,
    /** @brief A view which only renders ui objects. */
    RENDERER_VIEW_KNOWN_TYPE_UI = 0x02
} render_view_known_type;

/** @brief Known view matrix sources. */
typedef enum render_view_view_matrix_source {
    RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA = 0x01,
    RENDER_VIEW_VIEW_MATRIX_SOURCE_UI_CAMERA = 0x02,
    RENDER_VIEW_VIEW_MATRIX_SOURCE_LIGHT_CAMERA = 0x03,
} render_view_view_matrix_source;

/** @brief Known projection matrix sources. */
typedef enum render_view_projection_matrix_source {
    RENDER_VIEW_PROJECTION_MATRIX_SOURCE_DEFAULT_PERSPECTIVE = 0x01,
    RENDER_VIEW_PROJECTION_MATRIX_SOURCE_DEFAULT_ORTHOGRAPHIC = 0x02,
} render_view_projection_matrix_source;

/** @brief configuration for a renderpass to be associated with a view */
typedef struct render_view_pass_config {
    const char* name;
} render_view_pass_config;

/**
 * @brief The configuration of a render view.
 * Used as a serialization target.
 */
typedef struct render_view_config {
    /** @brief The name of the view. */
    const char* name;

    /**
     * @brief The name of a custom shader to be used
     * instead of the view's default. Must be 0 if
     * not used.
     */
    const char* custom_shader_name;
    /** @brief The width of the view. Set to 0 for 100% width. */
    u16 width;
    /** @brief The height of the view. Set to 0 for 100% height. */
    u16 height;
    /** @brief The known type of the view. Used to associate with view logic. */
    render_view_known_type type;
    /** @brief The source of the view matrix. */
    render_view_view_matrix_source view_matrix_source;
    /** @brief The source of the projection matrix. */
    render_view_projection_matrix_source projection_matrix_source;
    /** @brief The number of renderpasses used in this view. */
    u8 pass_count;
    /** @brief The configuration of renderpasses used in this view. */
    render_view_pass_config* passes;
} render_view_config;

struct render_view_packet;

/**
 * @brief A render view instance, responsible for the generation
 * of view packets based on internal logic and given config.
 */
typedef struct render_view {
    /** @brief The unique identifier of this view. */
    u16 id;
    /** @brief The name of the view. */
    const char* name;
    /** @brief The current width of this view. */
    u16 width;
    /** @brief The current height of this view. */
    u16 height;
    /** @brief The known type of this view. */
    render_view_known_type type;

    /** @brief The number of renderpasses used by this view. */
    u8 renderpass_count;
    /** @brief An array of pointers to renderpasses used by this view. */
    renderpass** passes;

    /** @brief The name of the custom shader used by this view, if there is one. */
    const char* custom_shader_name;
    /** @brief The internal, view-specific data for this view. */
    void* internal_data;

    /**
     * @brief A pointer to a function to be called when this view is created.
     *
     * @param self A pointer to the view being created.
     * @return True on success; otherwise false.
     */
    b8 (*on_create)(struct render_view* self);
    /**
     * @brief A pointer to a function to be called when this view is destroyed.
     *
     * @param self A pointer to the view being destroyed.
     */
    void (*on_destroy)(struct render_view* self);

    /**
     * @brief A pointer to a function to be called when the owner of this view (such
     * as the window) is resized.
     *
     * @param self A pointer to the view being resized.
     * @param width The new width in pixels.
     * @param width The new height in pixels.
     */
    void (*on_resize)(struct render_view* self, u32 width, u32 height);

    /**
     * @brief Builds a render view packet using the provided view and meshes.
     *
     * @param self A pointer to the view to use.
     * @param data Freeform data used to build the packet.
     * @param out_packet A pointer to hold the generated packet.
     * @return True on success; otherwise false.
     */
    b8 (*on_build_packet)(const struct render_view* self, void* data, struct render_view_packet* out_packet);

    /**
     * @brief Uses the given view and packet to render the contents therein.
     *
     * @param self A pointer to the view to use.
     * @param packet A pointer to the packet whose data is to be rendered.
     * @param frame_number The current renderer frame number, typically used for data synchronization.
     * @param render_target_index The current render target index for renderers that use multiple render targets at once (i.e. Vulkan).
     * @return True on success; otherwise false.
     */
    b8 (*on_render)(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index);
} render_view;

/**
 * @brief A packet for and generated by a render view, which contains
 * data about what is to be rendered.
 */
typedef struct render_view_packet {
    /** @brief A constant pointer to the view this packet is associated with. */
    const render_view* view;
    /** @brief The current view matrix. */
    mat4 view_matrix;
    /** @brief The current projection matrix. */
    mat4 projection_matrix;
    /** @brief The current view position, if applicable. */
    vec3 view_position;
    /** @brief The current scene ambient colour, if applicable. */
    vec4 ambient_colour;
    /** @brief The number of geometries to be drawn. */
    u32 geometry_count;
    /** @brief The geometries to be drawn. */
    geometry_render_data* geometries;
    /** @brief The name of the custom shader to use, if applicable. Otherwise 0. */
    const char* custom_shader_name;
    /** @brief Holds a pointer to freeform data, typically understood both by the object and consuming view. */
    void* extended_data;
} render_view_packet;

typedef struct mesh_packet_data {
    u32 mesh_count;
    mesh* meshes;
} mesh_packet_data;

/**
 * @brief A structure which is generated by the application and sent once
 * to the renderer to render a given frame. Consists of any data required,
 * such as delta time and a collection of views to be rendered.
 */
typedef struct render_packet {
    f32 delta_time;

    /** The number of views to be rendered. */
    u16 view_count;
    /** An array of views to be rendered. */
    render_view_packet* views;
} render_packet;
