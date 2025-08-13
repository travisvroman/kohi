#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "strings/kname.h"
#include "utils/kcolour.h"

typedef enum projection_matrix_type {
    PROJECTION_MATRIX_TYPE_PERSPECTIVE = 0x0,
    /** @brief An orthographic matrix that is zero-based on the top left. */
    PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC = 0x1,
    /** @brief An orthographic matrix that is centered around width/height instead of zero-based. Uses fov as a "zoom". */
    PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED = 0x2
} projection_matrix_type;

/** @brief Determines face culling mode during rendering. */
typedef enum face_cull_mode {
    /** @brief No faces are culled. */
    FACE_CULL_MODE_NONE = 0x0,
    /** @brief Only front faces are culled. */
    FACE_CULL_MODE_FRONT = 0x1,
    /** @brief Only back faces are culled. */
    FACE_CULL_MODE_BACK = 0x2,
    /** @brief Both front and back faces are culled. */
    FACE_CULL_MODE_FRONT_AND_BACK = 0x3
} face_cull_mode;

/**
 * Various topology type flag bit fields.
 */
typedef enum primitive_topology_type_bits {
    /** Topology type not defined. Not valid for shader creation. */
    PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT = 0x00,
    /** A list of triangles. The default if nothing is defined. */
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT = 0x01,
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT = 0x02,
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT = 0x04,
    PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT = 0x08,
    PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT = 0x10,
    PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT = 0x20,
    PRIMITIVE_TOPOLOGY_TYPE_MAX_BIT = PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT << 1
} primitive_topology_type_bits;

/** @brief A combination of topology bit flags. */
typedef u32 primitive_topology_types;

/** @brief Represents the format of image (or texture) pixel data. */
typedef enum kpixel_format {
    KPIXEL_FORMAT_UNKNOWN,
    KPIXEL_FORMAT_RGBA8,
    KPIXEL_FORMAT_RGB8,
    KPIXEL_FORMAT_RG8,
    KPIXEL_FORMAT_R8,
    KPIXEL_FORMAT_RGBA16,
    KPIXEL_FORMAT_RGB16,
    KPIXEL_FORMAT_RG16,
    KPIXEL_FORMAT_R16,
    KPIXEL_FORMAT_RGBA32,
    KPIXEL_FORMAT_RGB32,
    KPIXEL_FORMAT_RG32,
    KPIXEL_FORMAT_R32,
} kpixel_format;

/** @brief Represents supported texture filtering modes. */
typedef enum texture_filter {
    /** @brief Nearest-neighbor filtering. */
    TEXTURE_FILTER_MODE_NEAREST = 0x0,
    /** @brief Linear (i.e. bilinear) filtering.*/
    TEXTURE_FILTER_MODE_LINEAR = 0x1
} texture_filter;

typedef enum texture_repeat {
    TEXTURE_REPEAT_REPEAT = 0x0,
    TEXTURE_REPEAT_MIRRORED_REPEAT = 0x1,
    TEXTURE_REPEAT_CLAMP_TO_EDGE = 0x2,
    TEXTURE_REPEAT_CLAMP_TO_BORDER = 0x3,
    TEXTURE_REPEAT_COUNT
} texture_repeat;

typedef enum texture_channel {
    TEXTURE_CHANNEL_R,
    TEXTURE_CHANNEL_G,
    TEXTURE_CHANNEL_B,
    TEXTURE_CHANNEL_A,
} texture_channel;

/** @brief Shader stages available in the system. */
typedef enum shader_stage {
    SHADER_STAGE_VERTEX = 0x00000001,
    SHADER_STAGE_GEOMETRY = 0x00000002,
    SHADER_STAGE_FRAGMENT = 0x00000004,
    SHADER_STAGE_COMPUTE = 0x0000008
} shader_stage;

/**
 * @brief Defines shader update frequencies, typically used for uniforms.
 */
typedef enum shader_update_frequency {
    /** @brief The uniform is updated once per frame. */
    SHADER_UPDATE_FREQUENCY_PER_FRAME = 0,
    /** @brief The uniform is updated once per "group", it is up to the shader using this to determine what this means. */
    SHADER_UPDATE_FREQUENCY_PER_GROUP = 1,
    /** @brief The uniform is updated once per draw call (i.e. "instance" of an object in the world). */
    SHADER_UPDATE_FREQUENCY_PER_DRAW = 2,
} shader_update_frequency;

/** @brief Available attribute types. */
typedef enum shader_attribute_type {
    SHADER_ATTRIB_TYPE_FLOAT32 = 0U,
    SHADER_ATTRIB_TYPE_FLOAT32_2 = 1U,
    SHADER_ATTRIB_TYPE_FLOAT32_3 = 2U,
    SHADER_ATTRIB_TYPE_FLOAT32_4 = 3U,
    SHADER_ATTRIB_TYPE_MATRIX_4 = 4U,
    SHADER_ATTRIB_TYPE_INT8 = 5U,
    SHADER_ATTRIB_TYPE_UINT8 = 6U,
    SHADER_ATTRIB_TYPE_INT16 = 7U,
    SHADER_ATTRIB_TYPE_UINT16 = 8U,
    SHADER_ATTRIB_TYPE_INT32 = 9U,
    SHADER_ATTRIB_TYPE_UINT32 = 10U,
} shader_attribute_type;

/** @brief Available uniform types. */
typedef enum shader_uniform_type {
    SHADER_UNIFORM_TYPE_FLOAT32 = 0U,
    SHADER_UNIFORM_TYPE_FLOAT32_2 = 1U,
    SHADER_UNIFORM_TYPE_FLOAT32_3 = 2U,
    SHADER_UNIFORM_TYPE_FLOAT32_4 = 3U,
    SHADER_UNIFORM_TYPE_INT8 = 4U,
    SHADER_UNIFORM_TYPE_UINT8 = 5U,
    SHADER_UNIFORM_TYPE_INT16 = 6U,
    SHADER_UNIFORM_TYPE_UINT16 = 7U,
    SHADER_UNIFORM_TYPE_INT32 = 8U,
    SHADER_UNIFORM_TYPE_UINT32 = 9U,
    SHADER_UNIFORM_TYPE_MATRIX_4 = 10U,
    // Struct uniform type. Requires size to be used.
    SHADER_UNIFORM_TYPE_STRUCT = 11U,
    SHADER_UNIFORM_TYPE_TEXTURE_1D = 12U,
    SHADER_UNIFORM_TYPE_TEXTURE_2D = 13U,
    SHADER_UNIFORM_TYPE_TEXTURE_3D = 14U,
    SHADER_UNIFORM_TYPE_TEXTURE_CUBE = 15U,
    SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY = 16U,
    SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY = 17U,
    SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY = 18U,
    SHADER_UNIFORM_TYPE_SAMPLER = 19U,
    SHADER_UNIFORM_TYPE_CUSTOM = 255U
} shader_uniform_type;

typedef enum shader_generic_sampler {
    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT,
    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER,

    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_NO_ANISOTROPY,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER_NO_ANISOTROPY,

    SHADER_GENERIC_SAMPLER_COUNT,
} shader_generic_sampler;

typedef u16 kshader;
#define KSHADER_INVALID INVALID_ID_U16

typedef enum renderer_default_texture {
    // Used as a default for material base colours
    RENDERER_DEFAULT_TEXTURE_BASE_COLOUR = 0,
    RENDERER_DEFAULT_TEXTURE_ALBEDO = RENDERER_DEFAULT_TEXTURE_BASE_COLOUR,
    RENDERER_DEFAULT_TEXTURE_DIFFUSE = RENDERER_DEFAULT_TEXTURE_BASE_COLOUR,
    RENDERER_DEFAULT_TEXTURE_NORMAL = 1,
    RENDERER_DEFAULT_TEXTURE_METALLIC = 2,
    RENDERER_DEFAULT_TEXTURE_ROUGHNESS = 3,
    RENDERER_DEFAULT_TEXTURE_AMBIENT_OCCLUSION = 4,
    RENDERER_DEFAULT_TEXTURE_EMISSIVE = 5,
    RENDERER_DEFAULT_TEXTURE_DUDV = 6,

    RENDERER_DEFAULT_TEXTURE_COUNT
} renderer_default_texture;

/**
 * @brief Represents a single entry in the internal uniform array.
 */
typedef struct shader_uniform {
    kname name;
    /** @brief The offset in bytes from the beginning of the uniform set (per-frame/per-group/per-draw) */
    u64 offset;
    /**
     * @brief The location to be used as a lookup.
     * For samplers and textures, this is the index within the internal sampler/texture array at the given frequency (per-frame/per-group/per-draw).
     * Otherwise, index into the uniform array within the shader.
     */
    u16 location;
    /** @brief Index into the internal sampler/texture array depending on type. */
    u16 tex_samp_index;
    /** @brief The size of the uniform, or 0 for samplers. */
    u16 size;
    /** @brief The update frequency of the uniform. */
    shader_update_frequency frequency;
    /** @brief The type of uniform. */
    shader_uniform_type type;
    /** @brief The length of the array if it is one; otherwise 0 */
    u32 array_length;
} shader_uniform;

/**
 * @brief Represents a single shader vertex attribute.
 */
typedef struct shader_attribute {
    /** @brief The attribute name. */
    kname name;
    /** @brief The attribute type. */
    shader_attribute_type type;
    /** @brief The attribute size in bytes. */
    u32 size;
} shader_attribute;

/**
 * @brief Various shader flag bit fields.
 */
typedef enum shader_flag_bits {
    SHADER_FLAG_NONE_BIT = 0x0000,
    // Reads from depth buffer.
    SHADER_FLAG_DEPTH_TEST_BIT = 0x0001,
    // Writes to depth buffer.
    SHADER_FLAG_DEPTH_WRITE_BIT = 0x0002,
    SHADER_FLAG_WIREFRAME_BIT = 0x0004,
    // Reads from depth buffer.
    SHADER_FLAG_STENCIL_TEST_BIT = 0x0008,
    // Writes to depth buffer.
    SHADER_FLAG_STENCIL_WRITE_BIT = 0x0010,
    // Reads from colour buffer.
    SHADER_FLAG_COLOUR_READ_BIT = 0x0020,
    // Writes to colour buffer.
    SHADER_FLAG_COLOUR_WRITE_BIT = 0x0040
} shader_flag_bits;

/** @brief A combination of topology bit flags. */
typedef u32 shader_flags;

/**
 * @brief Represents data required for a particular update frequency within a shader.
 */
typedef struct shader_frequency_data {
    /** @brief The number of non-sampler and non-texture uniforms for this frequency. */
    u8 uniform_count;
    /** @brief The number of sampler uniforms for this frequency. */
    u8 uniform_sampler_count;
    // Keeps the uniform indices of samplers for fast lookups.
    u32* sampler_indices;
    /** @brief The number of texture uniforms for this frequency. */
    u8 uniform_texture_count;
    // Keeps the uniform indices of textures for fast lookups.
    u32* texture_indices;
    /** @brief The actual size of the uniform buffer object for this frequency. */
    u64 ubo_size;

    /** @brief The identifier of the currently bound group/per_draw. Ignored for per_frame */
    u32 bound_id;

} shader_frequency_data;

/**
 * @brief Represents the current state of a given shader.
 */
typedef enum shader_state {
    /** @brief The shader is "free", and is thus unusable.*/
    SHADER_STATE_FREE,
    /** @brief The shader has not yet gone through the creation process, and is unusable.*/
    SHADER_STATE_NOT_CREATED,
    /** @brief The shader has gone through the creation process, but not initialization. It is unusable.*/
    SHADER_STATE_UNINITIALIZED,
    /** @brief The shader is created and initialized, and is ready for use.*/
    SHADER_STATE_INITIALIZED,
} shader_state;

struct kresource_text;

/**
 * @brief The configuration for a single stage of the shader.
 */
typedef struct shader_stage_config {
    /** @brief The shader stage the config is for. */
    shader_stage stage;
    /** @brief A pointer to the text resource containing the shader source. */
    struct kresource_text* resource;
    /** @brief The name of the resource. */
    kname resource_name;
    /** @brief The name of the package containing the resource. */
    kname package_name;
} shader_stage_config;

/** @brief Configuration for an attribute. */
typedef struct shader_attribute_config {
    /** @brief The name of the attribute. */
    kname name;
    /** @brief The size of the attribute. */
    u8 size;
    /** @brief The type of the attribute. */
    shader_attribute_type type;
} shader_attribute_config;

/** @brief Configuration for a uniform. */
typedef struct shader_uniform_config {
    /** @brief The name of the uniform. */
    kname name;
    /** @brief The size of the uniform. If arrayed, this is the per-element size */
    u16 size;
    /** @brief The location of the uniform. */
    u32 location;
    /** @brief The type of the uniform. */
    shader_uniform_type type;
    /** @brief The array length, if uniform is an array. */
    u32 array_length;
    /** @brief The update frequency of the uniform. */
    shader_update_frequency frequency;
} shader_uniform_config;

typedef enum kmaterial_type {
    KMATERIAL_TYPE_UNKNOWN = 0,
    KMATERIAL_TYPE_STANDARD,
    KMATERIAL_TYPE_WATER,
    KMATERIAL_TYPE_BLENDED,
    KMATERIAL_TYPE_COUNT,
    KMATERIAL_TYPE_CUSTOM = 99
} kmaterial_type;

typedef enum kmaterial_model {
    KMATERIAL_MODEL_UNLIT = 0,
    KMATERIAL_MODEL_PBR,
    KMATERIAL_MODEL_PHONG,
    KMATERIAL_MODEL_COUNT,
    KMATERIAL_MODEL_CUSTOM = 99
} kmaterial_model;

typedef enum kmaterial_texture_map {
    KMATERIAL_TEXTURE_MAP_BASE_COLOUR,
    KMATERIAL_TEXTURE_MAP_NORMAL,
    KMATERIAL_TEXTURE_MAP_METALLIC,
    KMATERIAL_TEXTURE_MAP_ROUGHNESS,
    KMATERIAL_TEXTURE_MAP_AO,
    KMATERIAL_TEXTURE_MAP_MRA,
    KMATERIAL_TEXTURE_MAP_EMISSIVE,
} kmaterial_texture_map;

typedef enum kmaterial_flag_bits {
    // Material is marked as having transparency. If not set, alpha of albedo will not be used.
    KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT = 0x0001U,
    // Material is double-sided.
    KMATERIAL_FLAG_DOUBLE_SIDED_BIT = 0x0002U,
    // Material recieves shadows.
    KMATERIAL_FLAG_RECIEVES_SHADOW_BIT = 0x0004U,
    // Material casts shadows.
    KMATERIAL_FLAG_CASTS_SHADOW_BIT = 0x0008U,
    // Material normal map enabled. A default z-up value will be used if not set.
    KMATERIAL_FLAG_NORMAL_ENABLED_BIT = 0x0010U,
    // Material AO map is enabled. A default of 1.0 (white) will be used if not set.
    KMATERIAL_FLAG_AO_ENABLED_BIT = 0x0020U,
    // Material emissive map is enabled. Emissive map is ignored if not set.
    KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT = 0x0040U,
    // Material combined MRA (metallic/roughness/ao) map is enabled. MRA map is ignored if not set.
    KMATERIAL_FLAG_MRA_ENABLED_BIT = 0x0080U,
    // Material refraction map is enabled. Refraction map is ignored if not set.
    KMATERIAL_FLAG_REFRACTION_ENABLED_BIT = 0x0100U,
    // Material uses vertex colour data as the base colour.
    KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT = 0x0200U
} kmaterial_flag_bits;

typedef u32 kmaterial_flags;

// Configuration for a material texture input.
typedef struct kmaterial_texture_input_config {
    // Name of the resource.
    kname resource_name;
    // Name of the package containing the resource.
    kname package_name;
    // Name of the custom sampler, if one.
    kname sampler_name;
    // The texture channel to sample, if relevant.
    texture_channel channel;
} kmaterial_texture_input_config;

// The configuration for a custom material sampler.
typedef struct kmaterial_sampler_config {
    kname name;
    texture_filter filter_min;
    texture_filter filter_mag;
    texture_repeat repeat_u;
    texture_repeat repeat_v;
    texture_repeat repeat_w;
} kmaterial_sampler_config;

typedef u16 kmaterial;
#define KMATERIAL_INVALID INVALID_ID_U16
#define KMATERIAL_INSTANCE_INVALID INVALID_ID_U16

/**
 * @brief A material instance, which contains handles to both
 * the base material as well as the instance itself. Every time
 * an instance is "acquired", one of these is created, and the instance
 * should be referenced using this going from that point.
 */
typedef struct kmaterial_instance {
    // Handle to the base material.
    kmaterial base_material;
    // Handle to the instance.
    u16 instance_id;
} kmaterial_instance;

typedef struct krenderbuffer_render_data {
    /** @brief The element count. */
    u32 count;
    /** @brief The offset from the beginning of the buffer. */
    u64 offset;
} krenderbuffer_render_data;

/** @brief Represents render data for arbitrary geometry. */
typedef struct kgeometry_render_data {
    struct renderbuffer* vertex_buffer;
    krenderbuffer_render_data vertex_data;
    struct renderbuffer* index_buffer;
    krenderbuffer_render_data index_data;
} kgeometry_render_data;

typedef struct kskybox_render_data {
    mat4 model;
    u32 group_id;
    u32 draw_id;
    struct kresource_texture* cubemap;
} kskybox_render_data;

/** @brief Defines flags used for rendering static meshes */
typedef enum kstatic_mesh_render_data_flag {
    /** @brief Indicates that the winding order for the given static mesh should be inverted. */
    KSTATICM_ESH_RENDER_DATA_FLAG_WINDING_INVERTED_BIT = 0x0001
} kstaticm_esh_render_data_flag;

/**
 * @brief Collection of flags for a static mesh submesh to be rendered.
 * @see kstatic_mesh_render_data_flag
 */
typedef u32 kstatic_mesh_render_data_flag_bits;

/**
 * @brief The render data for an individual static sub-mesh
 * to be rendered.
 */
typedef struct kstatic_mesh_submesh_render_data {
    /** @brief Flags for the static mesh to be rendered. */
    kstatic_mesh_render_data_flag_bits flags;

    /** @brief The vertex data. */
    krenderbuffer_render_data vertex_data;

    /** @brief The index data. */
    krenderbuffer_render_data index_data;

    /** @brief The instance of the material to use with this static mesh when rendering. */
    kmaterial_instance material;
} kstatic_mesh_submesh_render_data;

/**
 * Contains data required to render a static mesh (ultimately its submeshes).
 */
typedef struct kstatic_mesh_render_data {
    /** The identifier of the mesh instance being rendered. */
    u16 instance_id;

    /** @brief The number of submeshes to be rendered. */
    u32 submesh_count;
    /** @brief The array of submeshes to be rendered. */
    kstatic_mesh_submesh_render_data* submeshes;

    /** @brief The tint override to be used when rendering all submeshes. Typically white (1, 1, 1, 1) if not used. */
    vec4 tint;
} kstatic_mesh_render_data;

/**
 * Directional light data formatted for direct shader use.
 */
typedef struct kdirectional_light_render_data {
    /** @brief The light colour. */
    colour3 colour;
    /** @brief The direction of the light.*/
    vec3 direction;

    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} kdirectional_light_render_data;

/**
 * Point light data formatted for direct shader use.
 */
typedef struct kpoint_light_render_data {
    /** @brief The light colour. */
    colour3 colour;
    /** @brief The position of the light in the world. The w component is ignored.*/
    vec3 position;
    /** @brief Reduces light intensity linearly */
    f32 linear;
    /** @brief Makes the light fall off slower at longer distances. */
    f32 quadratic;
} kpoint_light_render_data;

typedef struct kwater_plane_render_data {
    mat4 model;
    /** @brief The vertex data. */
    krenderbuffer_render_data vertex_data;

    /** @brief The index data. */
    krenderbuffer_render_data index_data;

    /** @brief The instance of the material to use with this static mesh when rendering. */
    kmaterial_instance material;
} kwater_plane_render_data;
