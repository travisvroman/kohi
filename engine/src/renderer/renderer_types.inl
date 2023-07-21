#pragma once

#include "containers/freelist.h"
#include "defines.h"
#include "math/math_types.h"
#include "resources/resource_types.h"

struct shader;
struct shader_uniform;
struct frame_data;
struct terrain;

typedef struct geometry_render_data {
    mat4 model;
    geometry* geometry;
    u32 unique_id;
} geometry_render_data;

typedef enum renderer_debug_view_mode {
    RENDERER_VIEW_MODE_DEFAULT = 0,
    RENDERER_VIEW_MODE_LIGHTING = 1,
    RENDERER_VIEW_MODE_NORMALS = 2
} renderer_debug_view_mode;

typedef enum render_target_attachment_type {
    RENDER_TARGET_ATTACHMENT_TYPE_COLOUR = 0x1,
    RENDER_TARGET_ATTACHMENT_TYPE_DEPTH = 0x2,
    RENDER_TARGET_ATTACHMENT_TYPE_STENCIL = 0x4
} render_target_attachment_type;

typedef enum render_target_attachment_source {
    RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT = 0x1,
    RENDER_TARGET_ATTACHMENT_SOURCE_VIEW = 0x2
} render_target_attachment_source;

typedef enum render_target_attachment_load_operation {
    RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE = 0x0,
    RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD = 0x1
} render_target_attachment_load_operation;

typedef enum render_target_attachment_store_operation {
    RENDER_TARGET_ATTACHMENT_STORE_OPERATION_DONT_CARE = 0x0,
    RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE = 0x1
} render_target_attachment_store_operation;

typedef struct render_target_attachment_config {
    render_target_attachment_type type;
    render_target_attachment_source source;
    render_target_attachment_load_operation load_operation;
    render_target_attachment_store_operation store_operation;
    b8 present_after;
} render_target_attachment_config;

typedef struct render_target_config {
    u8 attachment_count;
    render_target_attachment_config* attachments;
} render_target_config;

typedef struct render_target_attachment {
    render_target_attachment_type type;
    render_target_attachment_source source;
    render_target_attachment_load_operation load_operation;
    render_target_attachment_store_operation store_operation;
    b8 present_after;
    struct texture* texture;
} render_target_attachment;

/** @brief Represents a render target, which is used for rendering to a texture or set of textures. */
typedef struct render_target {
    /** @brief The number of attachments */
    u8 attachment_count;
    /** @brief An array of attachments. */
    struct render_target_attachment* attachments;
    /** @brief The renderer API internal framebuffer object. */
    void* internal_framebuffer;
} render_target;

/**
 * @brief The types of clearing to be done on a renderpass.
 * Can be combined together for multiple clearing functions.
 */
typedef enum renderpass_clear_flag {
    /** @brief No clearing should be done. */
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
    f32 depth;
    u32 stencil;

    /** @brief The current render area of the renderpass. */
    vec4 render_area;
    /** @brief The clear colour used for this renderpass. */
    vec4 clear_colour;

    /** @brief The clear flags for this renderpass. */
    u8 clear_flags;

    /** @brief The number of render targets created according to the render target config. */
    u8 render_target_count;
    /** @brief The render target configuration. */
    render_target_config target;
} renderpass_config;

/**
 * @brief Represents a generic renderpass.
 */
typedef struct renderpass {
    /** @brief The id of the renderpass */
    u16 id;

    char* name;

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

typedef enum renderbuffer_type {
    /** @brief Buffer is use is unknown. Default, but usually invalid. */
    RENDERBUFFER_TYPE_UNKNOWN,
    /** @brief Buffer is used for vertex data. */
    RENDERBUFFER_TYPE_VERTEX,
    /** @brief Buffer is used for index data. */
    RENDERBUFFER_TYPE_INDEX,
    /** @brief Buffer is used for uniform data. */
    RENDERBUFFER_TYPE_UNIFORM,
    /** @brief Buffer is used for staging purposes (i.e. from host-visible to device-local memory) */
    RENDERBUFFER_TYPE_STAGING,
    /** @brief Buffer is used for reading purposes (i.e copy to from device local, then read) */
    RENDERBUFFER_TYPE_READ,
    /** @brief Buffer is used for data storage. */
    RENDERBUFFER_TYPE_STORAGE
} renderbuffer_type;

typedef struct renderbuffer {
    /** @brief The name of the buffer, used for debugging purposes. */
    char* name;
    /** @brief The type of buffer, which typically determines its use. */
    renderbuffer_type type;
    /** @brief The total size of the buffer in bytes. */
    u64 total_size;
    /** @brief The amount of memory required to store the freelist. 0 if not used. */
    u64 freelist_memory_requirement;
    /** @brief The buffer freelist, if used. */
    freelist buffer_freelist;
    /** @brief The freelist memory block, if needed. */
    void* freelist_block;
    /** @brief Contains internal data for the renderer-API-specific buffer. */
    void* internal_data;
} renderbuffer;

typedef enum renderer_config_flag_bits {
    /** @brief Indicates that vsync should be enabled. */
    RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT = 0x1,
    /** @brief Configures the renderer backend in a way that conserves power where possible. */
    RENDERER_CONFIG_FLAG_POWER_SAVING_BIT = 0x2,
} renderer_config_flag_bits;

typedef u32 renderer_config_flags;

/** @brief The generic configuration for a renderer backend. */
typedef struct renderer_backend_config {
    /** @brief The name of the application */
    const char* application_name;
    /** @brief Various configuration flags for renderer backend setup. */
    renderer_config_flags flags;
} renderer_backend_config;

/**
 * @brief A generic "interface" for the renderer plugin. The renderer backend
 * is what is responsible for making calls to the graphics API such as
 * Vulkan, OpenGL or DirectX. Each of these should implement this interface.
 * The frontend only interacts via this structure and has no knowledge of
 * the way things actually work on the backend.
 */
typedef struct renderer_plugin {
    u64 frame_number;

    /**
     * @brief The size of the plugin-specific renderer context.
     */
    u64 internal_context_size;
    /**
     * @brief The plugin-specific renderer context.
     */
    void* internal_context;

    /**
     * @brief Initializes the backend.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param config A pointer to configuration to be used when initializing the backend.
     * @param out_window_render_target_count A pointer to hold how many render targets are needed for renderpasses targeting the window.
     * @return True if initialized successfully; otherwise false.
     */
    b8 (*initialize)(struct renderer_plugin* plugin, const renderer_backend_config* config, u8* out_window_render_target_count);

    /**
     * @brief Shuts the renderer backend down.
     *
     * @param plugin A pointer to the renderer plugin interface.
     */
    void (*shutdown)(struct renderer_plugin* plugin);

    /**
     * @brief Handles window resizes.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param width The new window width.
     * @param height The new window height.
     */
    void (*resized)(struct renderer_plugin* plugin, u16 width, u16 height);

    /**
     * @brief Performs setup routines required at the start of a frame.
     * @note A false result does not necessarily indicate failure. It can also specify that
     * the backend is simply not in a state capable of drawing a frame at the moment, and
     * that it should be attempted again on the next loop. End frame does not need to (and
     * should not) be called if this is the case.
     * @param plugin A pointer to the renderer plugin interface.
     * @param p_frame_data A constant pointer to the current frame's data.
     * @return True if successful; otherwise false.
     */
    b8 (*frame_begin)(struct renderer_plugin* plugin, const struct frame_data* p_frame_data);

    /**
     * @brief Performs routines required to draw a frame, such as presentation. Should only be called
     * after a successful return of begin_frame.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param p_frame_data A constant pointer to the current frame's data.
     * @return True on success; otherwise false.
     */
    b8 (*frame_end)(struct renderer_plugin* plugin, const struct frame_data* p_frame_data);

    /**
     * @brief Sets the renderer viewport to the given rectangle. Must be done within a renderpass.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param rect The viewport rectangle to be set.
     */
    void (*viewport_set)(struct renderer_plugin* plugin, vec4 rect);

    /**
     * @brief Resets the viewport to the default, which matches the application window.
     * Must be done within a renderpass.
     * @param plugin A pointer to the renderer plugin interface.
     *
     */
    void (*viewport_reset)(struct renderer_plugin* plugin);

    /**
     * @brief Sets the renderer scissor to the given rectangle. Must be done within a renderpass.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param rect The scissor rectangle to be set.
     */
    void (*scissor_set)(struct renderer_plugin* plugin, vec4 rect);

    /**
     * @brief Resets the scissor to the default, which matches the application window.
     * Must be done within a renderpass.
     *
     * @param plugin A pointer to the renderer plugin interface.
     */
    void (*scissor_reset)(struct renderer_plugin* plugin);

    /**
     * @brief Begins a renderpass with the given id.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param pass A pointer to the renderpass to begin.
     * @param target A pointer to the render target to be used.
     * @return True on success; otherwise false.
     */
    b8 (*renderpass_begin)(struct renderer_plugin* plugin, renderpass* pass, render_target* target);

    /**
     * @brief Ends a renderpass with the given id.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param pass A pointer to the renderpass to end.
     * @return True on success; otherwise false.
     */
    b8 (*renderpass_end)(struct renderer_plugin* plugin, renderpass* pass);

    /**
     * @brief Draws the given geometry. Should only be called inside a renderpass, within a frame.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param data A pointer to the render data of the geometry to be drawn.
     */
    void (*geometry_draw)(struct renderer_plugin* plugin, geometry_render_data* data);

    /**
     * @brief Creates a Vulkan-specific texture, acquiring internal resources as needed.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param pixels The raw image data used for the texture.
     * @param texture A pointer to the texture to hold the resources.
     */
    void (*texture_create)(struct renderer_plugin* plugin, const u8* pixels, struct texture* texture);

    /**
     * @brief Destroys the given texture, releasing internal resources.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param texture A pointer to the texture to be destroyed.
     */
    void (*texture_destroy)(struct renderer_plugin* plugin, struct texture* texture);

    /**
     * @brief Creates a new writeable texture with no data written to it.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param t A pointer to the texture to hold the resources.
     */
    void (*texture_create_writeable)(struct renderer_plugin* plugin, texture* t);

    /**
     * @brief Resizes a texture. There is no check at this level to see if the
     * texture is writeable. Internal resources are destroyed and re-created at
     * the new resolution. Data is lost and would need to be reloaded.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param t A pointer to the texture to be resized.
     * @param new_width The new width in pixels.
     * @param new_height The new height in pixels.
     */
    void (*texture_resize)(struct renderer_plugin* plugin, texture* t, u32 new_width, u32 new_height);

    /**
     * @brief Writes the given data to the provided texture.
     * NOTE: At this level, this can either be a writeable or non-writeable texture because
     * this also handles the initial texture load. The texture system itself should be
     * responsible for blocking write requests to non-writeable textures.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param t A pointer to the texture to be written to.
     * @param offset The offset in bytes from the beginning of the data to be written.
     * @param size The number of bytes to be written.
     * @param pixels The raw image data to be written.
     */
    void (*texture_write_data)(struct renderer_plugin* plugin, texture* t, u32 offset, u32 size, const u8* pixels);

    /**
     * @brief Reads the given data from the provided texture.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param t A pointer to the texture to be read from.
     * @param offset The offset in bytes from the beginning of the data to be read.
     * @param size The number of bytes to be read.
     * @param out_memory A pointer to a block of memory to write the read data to.
     */
    void (*texture_read_data)(struct renderer_plugin* plugin, texture* t, u32 offset, u32 size, void** out_memory);

    /**
     * @brief Reads a pixel from the provided texture at the given x/y coordinate.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param t A pointer to the texture to be read from.
     * @param x The pixel x-coordinate.
     * @param y The pixel y-coordinate.
     * @param out_rgba A pointer to an array of u8s to hold the pixel data (should be sizeof(u8) * 4)
     */
    void (*texture_read_pixel)(struct renderer_plugin* plugin, texture* t, u32 x, u32 y, u8** out_rgba);

    /**
     * @brief Creates Vulkan-specific internal resources for the given geometry using
     * the data provided.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param geometry A pointer to the geometry to be created.
     * @param vertex_size The size of a single vertex.
     * @param vertex_count The total number of vertices.
     * @param vertices An array of vertices.
     * @param index_size The size of an individual index.
     * @param index_count The total number of indices.
     * @param indices An array of indices.
     * @return True on success; otherwise false.
     */
    b8 (*geometry_create)(struct renderer_plugin* plugin, geometry* geometry, u32 vertex_size, u32 vertex_count, const void* vertices, u32 index_size, u32 index_count, const void* indices);

    /**
     * @brief Updates vertex data in the given geometry with the provided data in the given range.
     * 
     * @param plugin A pointer to the renderer plugin interface.
     * @param g A pointer to the geometry to be created.
     * @param offset The offset in bytes to update. 0 if updating from the beginning.
     * @param vertex_count The number of vertices which will be updated.
     * @param vertices The vertex data.
     */
    void (*geometry_vertex_update)(struct renderer_plugin *plugin, geometry *g, u32 offset, u32 vertex_count, void *vertices);

    /**
     * @brief Destroys the given geometry, releasing internal resources.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param geometry A pointer to the geometry to be destroyed.
     */
    void (*geometry_destroy)(struct renderer_plugin* plugin, geometry* geometry);

    /**
     * @brief Creates internal shader resources using the provided parameters.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader.
     * @param config A constant pointer to the shader config.
     * @param pass A pointer to the renderpass to be associated with the shader.
     * @param stage_count The total number of stages.
     * @param stage_filenames An array of shader stage filenames to be loaded. Should align with stages array.
     * @param stages A array of shader_stages indicating what render stages (vertex, fragment, etc.) used in this shader.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_create)(struct renderer_plugin* plugin, struct shader* shader, const shader_config* config, renderpass* pass, u8 stage_count, const char** stage_filenames, shader_stage* stages);

    /**
     * @brief Destroys the given shader and releases any resources held by it.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to be destroyed.
     */
    void (*shader_destroy)(struct renderer_plugin* plugin, struct shader* shader);

    /**
     * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
     * Must be done after vulkan_shader_create().
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to be initialized.
     * @return True on success; otherwise false.
     */
    b8 (*shader_initialize)(struct renderer_plugin* plugin, struct shader* shader);

    /**
     * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
     * and for use in draw calls.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to be used.
     * @return True on success; otherwise false.
     */
    b8 (*shader_use)(struct renderer_plugin* plugin, struct shader* shader);

    /**
     * @brief Binds global resources for use and updating.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader whose globals are to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_globals)(struct renderer_plugin* plugin, struct shader* s);

    /**
     * @brief Binds instance resources for use and updating.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader whose instance resources are to be bound.
     * @param instance_id The identifier of the instance to be bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_bind_instance)(struct renderer_plugin* plugin, struct shader* s, u32 instance_id);

    /**
     * @brief Applies global data to the uniform buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to apply the global data for.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_globals)(struct renderer_plugin* plugin, struct shader* s);

    /**
     * @brief Applies data for the currently bound instance.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to apply the instance data for.
     * @param needs_update Indicates if the shader uniforms need to be updated or just bound.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_instance)(struct renderer_plugin* plugin, struct shader* s, b8 needs_update);

    /**
     * @brief Acquires internal instance-level resources and provides an instance id.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to acquire resources from.
     * @param texture_map_count The number of texture maps used.
     * @param maps An array of pointers to texture maps. Must be one map per instance texture.
     * @param out_instance_id A pointer to hold the new instance identifier.
     * @return True on success; otherwise false.
     */
    b8 (*shader_instance_resources_acquire)(struct renderer_plugin* plugin, struct shader* s, u32 texture_map_count, texture_map** maps, u32* out_instance_id);

    /**
     * @brief Releases internal instance-level resources for the given instance id.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A pointer to the shader to release resources from.
     * @param instance_id The instance identifier whose resources are to be released.
     * @return True on success; otherwise false.
     */
    b8 (*shader_instance_resources_release)(struct renderer_plugin* plugin, struct shader* s, u32 instance_id);

    /**
     * @brief Sets the uniform of the given shader to the provided value.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param s A ponter to the shader.
     * @param uniform A constant pointer to the uniform.
     * @param value A pointer to the value to be set.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_uniform_set)(struct renderer_plugin* plugin, struct shader* frontend_shader, struct shader_uniform* uniform, const void* value);

    /**
     * @brief Acquires internal resources for the given texture map.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param map A pointer to the texture map to obtain resources for.
     * @return True on success; otherwise false.
     */
    b8 (*texture_map_resources_acquire)(struct renderer_plugin* plugin, struct texture_map* map);

    /**
     * @brief Releases internal resources for the given texture map.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param map A pointer to the texture map to release resources from.
     */
    void (*texture_map_resources_release)(struct renderer_plugin* plugin, struct texture_map* map);

    /**
     * @brief Creates a new render target using the provided data.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param attachment_count The number of attachments.
     * @param attachments An array of attachments.
     * @param renderpass A pointer to the renderpass the render target is associated with.
     * @param width The width of the render target in pixels.
     * @param height The height of the render target in pixels.
     * @param out_target A pointer to hold the newly created render target.
     */
    b8 (*render_target_create)(struct renderer_plugin* plugin, u8 attachment_count, render_target_attachment* attachments, renderpass* pass, u32 width, u32 height, render_target* out_target);

    /**
     * @brief Destroys the provided render target.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param target A pointer to the render target to be destroyed.
     * @param free_internal_memory Indicates if internal memory should be freed.
     */
    void (*render_target_destroy)(struct renderer_plugin* plugin, render_target* target, b8 free_internal_memory);

    /**
     * @brief Creates a new renderpass.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param config A constant pointer to the configuration to be used when creating the renderpass.
     * @param out_renderpass A pointer to the generic renderpass.
     */
    b8 (*renderpass_create)(struct renderer_plugin* plugin, const renderpass_config* config, renderpass* out_renderpass);

    /**
     * @brief Destroys the given renderpass.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param pass A pointer to the renderpass to be destroyed.
     */
    void (*renderpass_destroy)(struct renderer_plugin* plugin, renderpass* pass);

    /**
     * @brief Attempts to get the window render target at the given index.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param index The index of the attachment to get. Must be within the range of window render target count.
     * @return A pointer to a texture attachment if successful; otherwise 0.
     */
    texture* (*window_attachment_get)(struct renderer_plugin* plugin, u8 index);

    /**
     * @brief Returns a pointer to the main depth texture target.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param index The index of the attachment to get. Must be within the range of window render target count.
     * @return A pointer to a texture attachment if successful; otherwise 0.
     */
    texture* (*depth_attachment_get)(struct renderer_plugin* plugin, u8 index);

    /**
     * @brief Returns the current window attachment index.
     *
     * @param plugin A pointer to the renderer plugin interface.
     */
    u8 (*window_attachment_index_get)(struct renderer_plugin* plugin);

    /**
     * @brief Returns the number of attachments required for window-based render targets.
     *
     * @param plugin A pointer to the renderer plugin interface.
     */
    u8 (*window_attachment_count_get)(struct renderer_plugin* plugin);

    /**
     * @brief Indicates if the renderer is capable of multi-threading.
     *
     * @param plugin A pointer to the renderer plugin interface.
     */
    b8 (*is_multithreaded)(struct renderer_plugin* plugin);

    /**
     * @brief Indicates if the provided renderer flag is enabled. If multiple
     * flags are passed, all must be set for this to return true.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param flag The flag to be checked.
     * @return True if the flag(s) set; otherwise false.
     */
    b8 (*flag_enabled_get)(struct renderer_plugin* plugin, renderer_config_flags flag);
    /**
     * @brief Sets whether the included flag(s) are enabled or not. If multiple flags
     * are passed, multiple are set at once.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param flag The flag to be checked.
     * @param enabled Indicates whether or not to enable the flag(s).
     */
    void (*flag_enabled_set)(struct renderer_plugin* plugin, renderer_config_flags flag, b8 enabled);

    /**
     * @brief Creates and assigns the renderer-backend-specific buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to create the internal buffer for.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_internal_create)(struct renderer_plugin* plugin, renderbuffer* buffer);

    /**
     * @brief Destroys the given buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to be destroyed.
     */
    void (*renderbuffer_internal_destroy)(struct renderer_plugin* plugin, renderbuffer* buffer);

    /**
     * @brief Binds the given buffer at the provided offset.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to bind.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_bind)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset);
    /**
     * @brief Unbinds the given buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to be unbound.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_unbind)(struct renderer_plugin* plugin, renderbuffer* buffer);

    /**
     * @brief Maps memory from the given buffer in the provided range to a block of memory and returns it.
     * This memory should be considered invalid once unmapped.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to map.
     * @param offset The number of bytes from the beginning of the buffer to map.
     * @param size The amount of memory in the buffer to map.
     * @returns A mapped block of memory. Freed and invalid once unmapped.
     */
    void* (*renderbuffer_map_memory)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u64 size);
    /**
     * @brief Unmaps memory from the given buffer in the provided range to a block of memory.
     * This memory should be considered invalid once unmapped.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to unmap.
     * @param offset The number of bytes from the beginning of the buffer to unmap.
     * @param size The amount of memory in the buffer to unmap.
     */
    void (*renderbuffer_unmap_memory)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u64 size);

    /**
     * @brief Flushes buffer memory at the given range. Should be done after a write.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to unmap.
     * @param offset The number of bytes from the beginning of the buffer to flush.
     * @param size The amount of memory in the buffer to flush.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_flush)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u64 size);

    /**
     * @brief Reads memory from the provided buffer at the given range to the output variable.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to read from.
     * @param offset The number of bytes from the beginning of the buffer to read.
     * @param size The amount of memory in the buffer to read.
     * @param out_memory A pointer to a block of memory to read to. Must be of appropriate size.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_read)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u64 size, void** out_memory);

    /**
     * @brief Resizes the given buffer to new_total_size. new_total_size must be
     * greater than the current buffer size. Data from the old internal buffer is copied
     * over.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to be resized.
     * @param new_total_size The new size in bytes. Must be larger than the current size.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_resize)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 new_total_size);

    /**
     * @brief Loads provided data into the specified rage of the given buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to load data into.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @param size The size of the data in bytes to be loaded.
     * @param data The data to be loaded.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_load_range)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u64 size, const void* data);

    /**
     * @brief Copies data in the specified rage fron the source to the destination buffer.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param source A pointer to the source buffer to copy data from.
     * @param source_offset The offset in bytes from the beginning of the source buffer.
     * @param dest A pointer to the destination buffer to copy data to.
     * @param dest_offset The offset in bytes from the beginning of the destination buffer.
     * @param size The size of the data in bytes to be copied.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_copy_range)(struct renderer_plugin* plugin, renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size);

    /**
     * @brief Attempts to draw the contents of the provided buffer at the given offset
     * and element count. Only meant for use with vertex and index buffers.
     *
     * @param plugin A pointer to the renderer plugin interface.
     * @param buffer A pointer to the buffer to be drawn.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @param element_count The number of elements to be drawn.
     * @param bind_only Only binds the buffer, but does not call draw.
     * @return True on success; otherwise false.
     */
    b8 (*renderbuffer_draw)(struct renderer_plugin* plugin, renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only);

} renderer_plugin;

/** @brief Known render view types, which have logic associated with them. */
typedef enum render_view_known_type {
    /** @brief A view which only renders objects with *no* transparency. */
    RENDERER_VIEW_KNOWN_TYPE_WORLD = 0x01,
    /** @brief A view which only renders ui objects. */
    RENDERER_VIEW_KNOWN_TYPE_UI = 0x02,
    /** @brief A view which only renders skybox objects. */
    RENDERER_VIEW_KNOWN_TYPE_SKYBOX = 0x03,
    /** @brief A view which only renders ui and world objects to be picked. */
    RENDERER_VIEW_KNOWN_TYPE_PICK = 0x04,
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
    renderpass_config* passes;
} render_view_config;

struct render_view_packet;
struct linear_allocator;

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
    /** @brief An array of renderpasses used by this view. */
    renderpass* passes;

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
     * @param frame_allocator A frame allocator used this frame when building the packet.
     * @param data Freeform data used to build the packet.
     * @param out_packet A pointer to hold the generated packet.
     * @return True on success; otherwise false.
     */
    b8 (*on_packet_build)(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet);

    /**
     * @brief Destroys a render view packet.
     *
     * @param self A pointer to the view to use.
     * @param packet A pointer to the packet to be destroyed.
     */
    void (*on_packet_destroy)(const struct render_view* self, struct render_view_packet* packet);

    /**
     * @brief Uses the given view and packet to render the contents therein.
     *
     * @param self A pointer to the view to use.
     * @param packet A pointer to the packet whose data is to be rendered.
     * @param frame_number The current renderer frame number, typically used for data synchronization.
     * @param render_target_index The current render target index for renderers that use multiple render targets at once (i.e. Vulkan).
     * @return True on success; otherwise false.
     */
    b8 (*on_render)(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index, const struct frame_data* p_frame_data);

    /**
     * @brief Regenerates the resources for the given attachment at the provided pass index.
     *
     * @param self A pointer to the view to use.
     * @param pass_index The index of the renderpass to generate for.
     * @param attachment A pointer to the attachment whose resources are to be regenerated.
     * @return True on success; otherwise false.
     */
    b8 (*attachment_target_regenerate)(struct render_view* self, u32 pass_index, struct render_target_attachment* attachment);
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

    /** @brief The number of terrain geometries to be drawn. */
    u32 terrain_geometry_count;
    /** @brief The terrain geometries to be drawn. */
    geometry_render_data* terrain_geometries;

    /** @brief The number of debug geometries to be drawn. */
    u32 debug_geometry_count;
    /** @brief The debug geometries to be drawn. */
    geometry_render_data* debug_geometries;

    struct terrain** terrains;
    /** @brief The name of the custom shader to use, if applicable. Otherwise 0. */
    const char* custom_shader_name;
    /** @brief Holds a pointer to freeform data, typically understood both by the object and consuming view. */
    void* extended_data;
} render_view_packet;

typedef struct mesh_packet_data {
    u32 mesh_count;
    mesh** meshes;
} mesh_packet_data;

struct ui_text;
typedef struct ui_packet_data {
    mesh_packet_data mesh_data;
    // TODO: temp
    u32 text_count;
    struct ui_text** texts;
} ui_packet_data;

typedef struct pick_packet_data {
    // Copy of frame data darray ptr
    geometry_render_data* world_mesh_data;
    // Copy of frame data darray ptr
    geometry_render_data* terrain_mesh_data;
    mesh_packet_data ui_mesh_data;
    u32 ui_geometry_count;
    // TODO: temp
    u32 text_count;
    struct ui_text** texts;
} pick_packet_data;

struct skybox;

typedef struct skybox_packet_data {
    struct skybox* sb;
} skybox_packet_data;

/**
 * @brief A structure which is generated by the application and sent once
 * to the renderer to render a given frame. Consists of any data required,
 * such as delta time and a collection of views to be rendered.
 */
typedef struct render_packet {
    /** The number of views to be rendered. */
    u16 view_count;
    /** An array of views to be rendered. */
    render_view_packet* views;
} render_packet;
