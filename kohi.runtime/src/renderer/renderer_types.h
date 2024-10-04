#pragma once

#include <containers/freelist.h>
#include <core_render_types.h>
#include <defines.h>
#include <kresources/kresource_types.h>
#include <math/math_types.h>
#include <strings/kname.h>

#include "resources/resource_types.h"

struct shader;
struct shader_uniform;
struct frame_data;
struct terrain;
struct viewport;
struct camera;
struct material;
struct kwindow_renderer_backend_state;
struct texture_internal_data;
struct texture_map;

typedef struct renderbuffer_data {
    /** @brief The element count. */
    u32 element_count;
    /** @brief The size of each element. */
    u32 element_size;
    /** @brief The element data. */
    void* elements;
    /** @brief The offset from the beginning of the buffer. */
    u64 buffer_offset;
} renderbuffer_data;

typedef struct geometry_render_data {
    mat4 model;
    // TODO: keep material id/handle instead.
    struct material* material;
    // geometry* geometry;
    u64 unique_id;
    b8 winding_inverted;
    vec4 diffuse_colour;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The size of each vertex. */
    u32 vertex_element_size;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. */
    u32 index_element_size;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;
} geometry_render_data;

typedef enum renderer_debug_view_mode {
    RENDERER_VIEW_MODE_DEFAULT = 0,
    RENDERER_VIEW_MODE_LIGHTING = 1,
    RENDERER_VIEW_MODE_NORMALS = 2,
    RENDERER_VIEW_MODE_CASCADES = 3,
    RENDERER_VIEW_MODE_WIREFRAME = 4
} renderer_debug_view_mode;

typedef enum renderer_projection_matrix_type {
    RENDERER_PROJECTION_MATRIX_TYPE_PERSPECTIVE = 0x0,
    /** @brief An orthographic matrix that is zero-based on the top left. */
    RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC = 0x1,
    /** @brief An orthographic matrix that is centered around width/height instead of zero-based. Uses fov as a "zoom". */
    RENDERER_PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED = 0x2
} renderer_projection_matrix_type;

typedef enum renderer_stencil_op {
    /** @brief Keeps the current value. */
    RENDERER_STENCIL_OP_KEEP = 0,
    /** @brief Sets the stencil buffer value to 0. */
    RENDERER_STENCIL_OP_ZERO = 1,
    /** @brief Sets the stencil buffer value to _ref_, as specified in the function. */
    RENDERER_STENCIL_OP_REPLACE = 2,
    /** @brief Increments the current stencil buffer value. Clamps to the maximum representable unsigned value. */
    RENDERER_STENCIL_OP_INCREMENT_AND_CLAMP = 3,
    /** @brief Decrements the current stencil buffer value. Clamps to 0. */
    RENDERER_STENCIL_OP_DECREMENT_AND_CLAMP = 4,
    /** @brief Bitwise inverts the current stencil buffer value. */
    RENDERER_STENCIL_OP_INVERT = 5,
    /** @brief Increments the current stencil buffer value. Wraps stencil buffer value to zero when incrementing the maximum representable unsigned value. */
    RENDERER_STENCIL_OP_INCREMENT_AND_WRAP = 6,
    /** @brief Decrements the current stencil buffer value. Wraps stencil buffer value to the maximum representable unsigned value when decrementing a stencil buffer value of zero. */
    RENDERER_STENCIL_OP_DECREMENT_AND_WRAP = 7
} renderer_stencil_op;

typedef enum renderer_compare_op {
    /** @brief Specifies that the comparison always evaluates false. */
    RENDERER_COMPARE_OP_NEVER = 0,
    /** @brief Specifies that the comparison evaluates reference < test. */
    RENDERER_COMPARE_OP_LESS = 1,
    /** @brief Specifies that the comparison evaluates reference = test. */
    RENDERER_COMPARE_OP_EQUAL = 2,
    /** @brief Specifies that the comparison evaluates reference <= test. */
    RENDERER_COMPARE_OP_LESS_OR_EQUAL = 3,
    /** @brief Specifies that the comparison evaluates reference > test. */
    RENDERER_COMPARE_OP_GREATER = 4,
    /** @brief Specifies that the comparison evaluates reference != test.*/
    RENDERER_COMPARE_OP_NOT_EQUAL = 5,
    /** @brief Specifies that the comparison evaluates reference >= test. */
    RENDERER_COMPARE_OP_GREATER_OR_EQUAL = 6,
    /** @brief Specifies that the comparison is always true. */
    RENDERER_COMPARE_OP_ALWAYS = 7
} renderer_compare_op;

typedef enum renderer_attachment_type_flag_bits {
    RENDERER_ATTACHMENT_TYPE_FLAG_COLOUR_BIT = 0x1,
    RENDERER_ATTACHMENT_TYPE_FLAG_DEPTH_BIT = 0x2,
    RENDERER_ATTACHMENT_TYPE_FLAG_STENCIL_BIT = 0x4
} renderer_attachment_type_flag_bits;

typedef u32 renderer_attachment_type_flags;

typedef enum renderer_attachment_load_operation {
    RENDERER_ATTACHMENT_LOAD_OPERATION_DONT_CARE = 0x0,
    RENDERER_ATTACHMENT_LOAD_OPERATION_LOAD = 0x1
} renderer_attachment_load_operation;

typedef enum renderer_attachment_store_operation {
    RENDERER_ATTACHMENT_STORE_OPERATION_DONT_CARE = 0x0,
    RENDERER_ATTACHMENT_STORE_OPERATION_STORE = 0x1
} renderer_attachment_store_operation;

typedef enum renderer_attachment_use {
    RENDERER_ATTACHMENT_USE_DONT_CARE,
    RENDERER_ATTACHMENT_USE_COLOUR_ATTACHMENT,
    RENDERER_ATTACHMENT_USE_COLOUR_PRESENT,
    RENDERER_ATTACHMENT_USE_COLOUR_SHADER_READ,
    RENDERER_ATTACHMENT_USE_COLOUR_SHADER_WRITE,
    RENDERER_ATTACHMENT_USE_DEPTH_STENCIL_ATTACHMENT,
    RENDERER_ATTACHMENT_USE_DEPTH_STENCIL_SHADER_READ,
    RENDERER_ATTACHMENT_USE_DEPTH_STENCIL_SHADER_WRITE
} renderer_attachment_use;

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

typedef enum renderbuffer_track_type {
    RENDERBUFFER_TRACK_TYPE_NONE = 0,
    RENDERBUFFER_TRACK_TYPE_FREELIST = 1,
    RENDERBUFFER_TRACK_TYPE_LINEAR = 2
} renderbuffer_track_type;

typedef struct renderbuffer {
    /** @brief The name of the buffer, used for debugging purposes. */
    char* name;
    /** @brief The type of buffer, which typically determines its use. */
    renderbuffer_type type;
    /** @brief The total size of the buffer in bytes. */
    u64 total_size;
    /** @brief indicates the allocation tracking type. */
    renderbuffer_track_type track_type;
    /** @brief The amount of memory required to store the freelist. 0 if not used. */
    u64 freelist_memory_requirement;
    /** @brief The buffer freelist, if used. */
    freelist buffer_freelist;
    /** @brief The freelist memory block, if needed. */
    void* freelist_block;
    /** @brief Contains internal data for the renderer-API-specific buffer. */
    void* internal_data;
    /** @brief The byte offset used for linear tracking. */
    u64 offset;
} renderbuffer;

typedef enum renderer_config_flag_bits {
    /** @brief Indicates that vsync should be enabled. */
    RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT = 0x1,
    /** @brief Configures the renderer backend in a way that conserves power where possible. */
    RENDERER_CONFIG_FLAG_POWER_SAVING_BIT = 0x2,
    /** @brief Enables advanced validation in the renderer backend, if supported. */
    RENDERER_CONFIG_FLAG_ENABLE_VALIDATION = 0x4,
} renderer_config_flag_bits;

typedef u32 renderer_config_flags;

/** @brief The generic configuration for a renderer backend. */
typedef struct renderer_backend_config {
    /** @brief The name of the application */
    const char* application_name;
    /** @brief Various configuration flags for renderer backend setup. */
    renderer_config_flags flags;
} renderer_backend_config;

/** @brief The winding order of vertices, used to determine what is the front-face of a triangle. */
typedef enum renderer_winding {
    /** @brief Counter-clockwise vertex winding. */
    RENDERER_WINDING_COUNTER_CLOCKWISE = 0,
    /** @brief Counter-clockwise vertex winding. */
    RENDERER_WINDING_CLOCKWISE = 1
} renderer_winding;

/**
 * @brief Maps a uniform to a texture map/maps when acquiring instance resources.
 */
typedef struct shader_instance_uniform_texture_config {
    /** @brief The locaton of the uniform to map to. */
    /* u16 uniform_location; */
    /** @brief The number of texture maps bound to the uniform. */
    u32 kresource_texture_map_count;
    /** @brief An array of pointers to texture maps to be mapped to the uniform. */
    struct kresource_texture_map** kresource_texture_maps;
} shader_instance_uniform_texture_config;

/**
 * @brief Represents the configuration of texture map resources and mappings to uniforms
 * required for instance-level shader data.
 */
typedef struct shader_instance_resource_config {
    /** @brief The number of uniform configurations */
    u32 uniform_config_count;
    /** @brief An array of uniform configurations. */
    shader_instance_uniform_texture_config* uniform_configs;
} shader_instance_resource_config;

/**
 * @brief The internal state of a window for the renderer frontend.
 */
typedef struct kwindow_renderer_state {
    // Pointer back to main window.
    struct kwindow* window;
    // The viewport information for the given window.
    struct viewport* active_viewport;

    // This is technically the swapchain images, which should be wrapped into a single texture.
    kresource_texture* colourbuffer;
    // This is technically the per-frame depth image, which should be wrapped into a single texture.
    kresource_texture* depthbuffer;

    /** @brief The internal state of the window containing renderer backend data. */
    struct kwindow_renderer_backend_state* backend_state;
} kwindow_renderer_state;

/**
 * @brief A generic "interface" for the renderer backend. The renderer backend
 * is what is responsible for making calls to the graphics API such as
 * Vulkan, OpenGL or DirectX. Each of these should implement this interface.
 * The frontend only interacts via this structure and has no knowledge of
 * the way things actually work on the backend.
 */
typedef struct renderer_backend_interface {
    // A pointer to the frontend state in case the backend needs to communicate with it.
    struct renderer_system_state* frontend_state;

    // The size needed by the renderer backend to hold texture data.
    u64 texture_internal_data_size;

    /**
     * @brief The size of the backend-specific renderer context.
     */
    u64 internal_context_size;
    /**
     * @brief The backend-specific renderer context.
     */
    void* internal_context;

    /**
     * @brief Initializes the backend.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param config A pointer to configuration to be used when initializing the backend.
     * @return True if initialized successfully; otherwise false.
     */
    b8 (*initialize)(struct renderer_backend_interface* backend, const renderer_backend_config* config);

    /**
     * @brief Shuts the renderer backend down.
     *
     * @param backend A pointer to the renderer backend interface.
     */
    void (*shutdown)(struct renderer_backend_interface* backend);

    void (*begin_debug_label)(struct renderer_backend_interface* backend, const char* label_text, vec3 colour);
    void (*end_debug_label)(struct renderer_backend_interface* backend);

    /**
     * @brief Handles window creation.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param window A pointer to the window being created.
     * @returns True on success; otherwise false.
     */
    b8 (*window_create)(struct renderer_backend_interface* backend, struct kwindow* window);

    /**
     * @brief Handles window destruction.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param window A pointer to the window being resized.
     */
    void (*window_destroy)(struct renderer_backend_interface* backend, struct kwindow* window);

    /**
     * @brief Handles window resizes.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param window A pointer to the window being resized.
     */
    void (*window_resized)(struct renderer_backend_interface* backend, const struct kwindow* window);

    b8 (*frame_prepare)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);

    b8 (*frame_prepare_window_surface)(struct renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data);

    b8 (*frame_commands_begin)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);

    b8 (*frame_commands_end)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);

    b8 (*frame_submit)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);
    b8 (*frame_present)(struct renderer_backend_interface* backend, struct kwindow* window, struct frame_data* p_frame_data);

    /**
     * @brief Sets the renderer viewport to the given rectangle.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param rect The viewport rectangle to be set.
     */
    void (*viewport_set)(struct renderer_backend_interface* backend, vec4 rect);

    /**
     * @brief Resets the viewport to the default, which matches the application window.
     * @param backend A pointer to the renderer backend interface.
     *
     */
    void (*viewport_reset)(struct renderer_backend_interface* backend);

    /**
     * @brief Sets the renderer scissor to the given rectangle.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param rect The scissor rectangle to be set.
     */
    void (*scissor_set)(struct renderer_backend_interface* backend, vec4 rect);

    /**
     * @brief Resets the scissor to the default, which matches the application window.
     *
     * @param backend A pointer to the renderer backend interface.
     */
    void (*scissor_reset)(struct renderer_backend_interface* backend);

    /**
     * @brief Set the renderer to use the given winding direction.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param winding The winding direction.
     */
    void (*winding_set)(struct renderer_backend_interface* backend, renderer_winding winding);

    /**
     * @brief Set stencil testing enabled/disabled.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param enabled Indicates if stencil testing should be enabled/disabled for subsequent draws.
     */
    void (*set_stencil_test_enabled)(struct renderer_backend_interface* backend, b8 enabled);

    /**
     * @brief Set depth testing enabled/disabled.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param enabled Indicates if depth testing should be enabled/disabled for subsequent draws.
     */
    void (*set_depth_test_enabled)(struct renderer_backend_interface* backend, b8 enabled);

    /**
     * @brief Set depth write enabled/disabled.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param enabled Indicates if depth write should be enabled/disabled for subsequent draws.
     */
    void (*set_depth_write_enabled)(struct renderer_backend_interface* backend, b8 enabled);

    /**
     * @brief Set the stencil reference for testing.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param reference The reference to use when stencil testing/writing.
     */
    void (*set_stencil_reference)(struct renderer_backend_interface* backend, u32 reference);

    /**
     * @brief Set stencil operation.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param fail_op Specifys the action performed on samples that fail the stencil test.
     * @param pass_op Specifys the action performed on samples that pass both the depth and stencil tests.
     * @param depth_fail_op Specifys the action performed on samples that pass the stencil test and fail the depth test.
     * @param compare_op Specifys the comparison operator used in the stencil test.
     */
    void (*set_stencil_op)(struct renderer_backend_interface* backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op);

    void (*begin_rendering)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data, rect_2d render_area, u32 colour_target_count, struct texture_internal_data** colour_targets, struct texture_internal_data* depth_stencil_target, u32 depth_stencil_layer);
    void (*end_rendering)(struct renderer_backend_interface* backend, struct frame_data* p_frame_data);

    /**
     * @brief Set stencil compare mask.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param compare_mask The new value to use as the stencil compare mask.
     */
    void (*set_stencil_compare_mask)(struct renderer_backend_interface* backend, u32 compare_mask);

    /**
     * @brief Set stencil write mask.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param write_mask The new value to use as the stencil write mask.
     */
    void (*set_stencil_write_mask)(struct renderer_backend_interface* backend, u32 write_mask);

    void (*clear_colour_set)(struct renderer_backend_interface* backend, vec4 clear_colour);
    void (*clear_depth_set)(struct renderer_backend_interface* backend, f32 depth);
    void (*clear_stencil_set)(struct renderer_backend_interface* backend, u32 stencil);
    void (*clear_colour)(struct renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
    void (*clear_depth_stencil)(struct renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
    void (*colour_texture_prepare_for_present)(struct renderer_backend_interface* backend, struct texture_internal_data* tex_internal);
    void (*texture_prepare_for_sampling)(struct renderer_backend_interface* backend, struct texture_internal_data* tex_internal, texture_flag_bits flags);

    b8 (*texture_resources_acquire)(struct renderer_backend_interface* backend, struct texture_internal_data* data, const char* name, kresource_texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, kresource_texture_flag_bits flags);
    void (*texture_resources_release)(struct renderer_backend_interface* backend, struct texture_internal_data* data);

    /**
     * @brief Resizes a texture. There is no check at this level to see if the
     * texture is writeable. Internal resources are destroyed and re-created at
     * the new resolution. Data is lost and would need to be reloaded.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param t A pointer to the texture internal data to be resized.
     * @param new_width The new width in pixels.
     * @param new_height The new height in pixels.
     * @returns True on success; otherwise false.
     */
    b8 (*texture_resize)(struct renderer_backend_interface* backend, struct texture_internal_data* data, u32 new_width, u32 new_height);

    /**
     * @brief Writes the given data to the provided texture.
     * NOTE: At this level, this can either be a writeable or non-writeable texture because
     * this also handles the initial texture load. The texture system itself should be
     * responsible for blocking write requests to non-writeable textures.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param t A pointer to the texture internal data to be written to.
     * @param offset The offset in bytes from the beginning of the data to be written.
     * @param size The number of bytes to be written.
     * @param pixels The raw image data to be written.
     * @returns True on success; otherwise false.
     */
    b8 (*texture_write_data)(struct renderer_backend_interface* backend, struct texture_internal_data* data, u32 offset, u32 size, const u8* pixels, b8 include_in_frame_workload);

    /**
     * @brief Reads the given data from the provided texture.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param t A pointer to the texture internal data to be read from.
     * @param offset The offset in bytes from the beginning of the data to be read.
     * @param size The number of bytes to be read.
     * @param out_pixels A pointer to a block of memory to write the read data to.
     * @returns True on success; otherwise false.
     */
    b8 (*texture_read_data)(struct renderer_backend_interface* backend, struct texture_internal_data* data, u32 offset, u32 size, u8** out_pixels);

    /**
     * @brief Reads a pixel from the provided texture at the given x/y coordinate.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param t A pointer to the texture internal data to be read from.
     * @param x The pixel x-coordinate.
     * @param y The pixel y-coordinate.
     * @param out_rgba A pointer to an array of u8s to hold the pixel data (should be sizeof(u8) * 4)
     * @returns True on success; otherwise false.
     */
    b8 (*texture_read_pixel)(struct renderer_backend_interface* backend, struct texture_internal_data* data, u32 x, u32 y, u8** out_rgba);

    /**
     * @brief Creates internal shader resources using the provided parameters.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader.
     * @param config A constant pointer to the shader config.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_create)(struct renderer_backend_interface* backend, struct shader* shader, const shader_config* config);

    /**
     * @brief Destroys the given shader and releases any resources held by it.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to be destroyed.
     */
    void (*shader_destroy)(struct renderer_backend_interface* backend, struct shader* shader);

    /**
     * @brief Initializes a configured shader. Will be automatically destroyed if this step fails.
     * Must be done after vulkan_shader_create().
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to be initialized.
     * @return True on success; otherwise false.
     */
    b8 (*shader_initialize)(struct renderer_backend_interface* backend, struct shader* shader);

    /**
     * @brief Reloads the internals of the given shader.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to be reloaded.
     * @return True on success; otherwise false.
     */
    b8 (*shader_reload)(struct renderer_backend_interface* backend, struct shader* s);

    /**
     * @brief Uses the given shader, activating it for updates to attributes, uniforms and such,
     * and for use in draw calls.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to be used.
     * @return True on success; otherwise false.
     */
    b8 (*shader_use)(struct renderer_backend_interface* backend, struct shader* shader);

    /**
     * @brief Indicates if the supplied shader supports wireframe mode.
     *
     * @param backend A constant pointer to the renderer backend interface.
     * @param s A constant pointer to the shader to be used.
     * @return True if supported; otherwise false.
     */
    b8 (*shader_supports_wireframe)(const struct renderer_backend_interface* backend, const struct shader* s);

    /**
     * @brief Applies global data to the uniform buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to apply the global data for.
     * @param renderer_frame_number The current renderer frame number provided by the frontend.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_globals)(struct renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);

    /**
     * @brief Applies data for the currently bound instance.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to apply the instance data for.
     * @param renderer_frame_number The current renderer frame number provided by the frontend.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_instance)(struct renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);

    /**
     * @brief Applies local data to the uniform buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to apply the instance data for.
     * @param renderer_frame_number The current renderer frame number provided by the frontend.
     * @return True on success; otherwise false.
     */
    b8 (*shader_apply_local)(struct renderer_backend_interface* backend, struct shader* s, u64 renderer_frame_number);

    /**
     * @brief Acquires internal instance-level resources and provides an instance id.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to acquire resources from.
     * @param texture_map_count The number of texture maps used.
     * @param maps An array of pointers to texture maps. Must be one map per instance texture.
     * @param out_instance_id A pointer to hold the new instance identifier.
     * @return True on success; otherwise false.
     */
    b8 (*shader_instance_resources_acquire)(struct renderer_backend_interface* backend, struct shader* s, const shader_instance_resource_config* config, u32* out_instance_id);

    /**
     * @brief Releases internal instance-level resources for the given instance id.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to release resources from.
     * @param instance_id The instance identifier whose resources are to be released.
     * @return True on success; otherwise false.
     */
    b8 (*shader_instance_resources_release)(struct renderer_backend_interface* backend, struct shader* s, u32 instance_id);

    /**
     * @brief Acquires internal local-level resources and provides an instance id.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to acquire resources from.
     * @param texture_map_count The number of texture maps used.
     * @param maps An array of pointers to texture maps. Must be one map per instance texture.
     * @param out_local_id A pointer to hold the new local identifier.
     * @return True on success; otherwise false.
     */
    b8 (*shader_local_resources_acquire)(struct renderer_backend_interface* backend, struct shader* s, const shader_instance_resource_config* config, u32* out_local_id);

    /**
     * @brief Releases internal local-level resources for the given instance id.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A pointer to the shader to release resources from.
     * @param instance_id The local identifier whose resources are to be released.
     * @return True on success; otherwise false.
     */
    b8 (*shader_local_resources_release)(struct renderer_backend_interface* backend, struct shader* s, u32 local_id);

    /**
     * @brief Sets the uniform of the given shader to the provided value.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param s A ponter to the shader.
     * @param uniform A constant pointer to the uniform.
     * @param array_index The array index to set, if the uniform is an array. Ignored otherwise.
     * @param value A pointer to the value to be set.
     * @return b8 True on success; otherwise false.
     */
    b8 (*shader_uniform_set)(struct renderer_backend_interface* backend, struct shader* frontend_shader, struct shader_uniform* uniform, u32 array_index, const void* value);

    /**
     * @brief Acquires internal resources for the given texture map.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param map A pointer to the texture map to obtain resources for.
     * @return True on success; otherwise false.
     */
    b8 (*kresource_texture_map_resources_acquire)(struct renderer_backend_interface* backend, struct kresource_texture_map* map);

    /**
     * @brief Releases internal resources for the given texture map.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param map A pointer to the texture map to release resources from.
     */
    void (*kresource_texture_map_resources_release)(struct renderer_backend_interface* backend, struct kresource_texture_map* map);

    /**
     * @brief Indicates if the renderer is capable of multi-threading.
     *
     * @param backend A pointer to the renderer backend interface.
     */
    b8 (*is_multithreaded)(struct renderer_backend_interface* backend);

    /**
     * @brief Indicates if the provided renderer flag is enabled. If multiple
     * flags are passed, all must be set for this to return true.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param flag The flag to be checked.
     * @return True if the flag(s) set; otherwise false.
     */
    b8 (*flag_enabled_get)(struct renderer_backend_interface* backend, renderer_config_flags flag);
    /**
     * @brief Sets whether the included flag(s) are enabled or not. If multiple flags
     * are passed, multiple are set at once.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param flag The flag to be checked.
     * @param enabled Indicates whether or not to enable the flag(s).
     */
    void (*flag_enabled_set)(struct renderer_backend_interface* backend, renderer_config_flags flag, b8 enabled);

    /**
     * @brief Creates and assigns the renderer-backend-specific buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to create the internal buffer for.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_internal_create)(struct renderer_backend_interface* backend, renderbuffer* buffer);

    /**
     * @brief Destroys the given buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to be destroyed.
     */
    void (*renderbuffer_internal_destroy)(struct renderer_backend_interface* backend, renderbuffer* buffer);

    /**
     * @brief Binds the given buffer at the provided offset.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to bind.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_bind)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset);
    /**
     * @brief Unbinds the given buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to be unbound.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_unbind)(struct renderer_backend_interface* backend, renderbuffer* buffer);

    /**
     * @brief Maps memory from the given buffer in the provided range to a block of memory and returns it.
     * This memory should be considered invalid once unmapped.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to map.
     * @param offset The number of bytes from the beginning of the buffer to map.
     * @param size The amount of memory in the buffer to map.
     * @returns A mapped block of memory. Freed and invalid once unmapped.
     */
    void* (*renderbuffer_map_memory)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);
    /**
     * @brief Unmaps memory from the given buffer in the provided range to a block of memory.
     * This memory should be considered invalid once unmapped.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to unmap.
     * @param offset The number of bytes from the beginning of the buffer to unmap.
     * @param size The amount of memory in the buffer to unmap.
     */
    void (*renderbuffer_unmap_memory)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);

    /**
     * @brief Flushes buffer memory at the given range. Should be done after a write.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to unmap.
     * @param offset The number of bytes from the beginning of the buffer to flush.
     * @param size The amount of memory in the buffer to flush.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_flush)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size);

    /**
     * @brief Reads memory from the provided buffer at the given range to the output variable.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to read from.
     * @param offset The number of bytes from the beginning of the buffer to read.
     * @param size The amount of memory in the buffer to read.
     * @param out_memory A pointer to a block of memory to read to. Must be of appropriate size.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_read)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size, void** out_memory);

    /**
     * @brief Resizes the given buffer to new_total_size. new_total_size must be
     * greater than the current buffer size. Data from the old internal buffer is copied
     * over.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to be resized.
     * @param new_total_size The new size in bytes. Must be larger than the current size.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_resize)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 new_total_size);

    /**
     * @brief Loads provided data into the specified rage of the given buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to load data into.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @param size The size of the data in bytes to be loaded.
     * @param data The data to be loaded.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_load_range)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u64 size, const void* data, b8 include_in_frame_workload);

    /**
     * @brief Copies data in the specified rage fron the source to the destination buffer.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param source A pointer to the source buffer to copy data from.
     * @param source_offset The offset in bytes from the beginning of the source buffer.
     * @param dest A pointer to the destination buffer to copy data to.
     * @param dest_offset The offset in bytes from the beginning of the destination buffer.
     * @param size The size of the data in bytes to be copied.
     * @returns True on success; otherwise false.
     */
    b8 (*renderbuffer_copy_range)(struct renderer_backend_interface* backend, renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size, b8 include_in_frame_workload);

    /**
     * @brief Attempts to draw the contents of the provided buffer at the given offset
     * and element count. Only meant for use with vertex and index buffers.
     *
     * @param backend A pointer to the renderer backend interface.
     * @param buffer A pointer to the buffer to be drawn.
     * @param offset The offset in bytes from the beginning of the buffer.
     * @param element_count The number of elements to be drawn.
     * @param bind_only Only binds the buffer, but does not call draw.
     * @return True on success; otherwise false.
     */
    b8 (*renderbuffer_draw)(struct renderer_backend_interface* backend, renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only);

    /**
     * Waits for the renderer backend to be completely idle of work before returning.
     * NOTE: This incurs a lot of overhead/waits, and should be used sparingly.
     */
    void (*wait_for_idle)(struct renderer_backend_interface* backend);
} renderer_backend_interface;
