#pragma once

#include "defines.h"
#include "strings/kname.h"

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

typedef enum primitive_topology_type {
    /** Topology type not defined. Not valid for shader creation. */
    PRIMITIVE_TOPOLOGY_TYPE_NONE = 0x00,
    /** A list of triangles. The default if nothing is defined. */
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST = 0x01,
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP = 0x02,
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN = 0x04,
    PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST = 0x08,
    PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP = 0x10,
    PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST = 0x20,
    PRIMITIVE_TOPOLOGY_TYPE_MAX = PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST << 1
} primitive_topology_type;

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
    SHADER_UNIFORM_TYPE_TEXTURE_1D = 11U,
    SHADER_UNIFORM_TYPE_TEXTURE_2D = 12U,
    SHADER_UNIFORM_TYPE_TEXTURE_3D = 13U,
    SHADER_UNIFORM_TYPE_TEXTURE_CUBE = 14U,
    SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY = 15U,
    SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY = 16U,
    SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY = 17U,
    SHADER_UNIFORM_TYPE_SAMPLER = 18U,
    SHADER_UNIFORM_TYPE_CUSTOM = 255U
} shader_uniform_type;

typedef enum shader_generic_sampler {
    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT = 0,
    SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED = 1,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP = 2,
    SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER = 3,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT = 4,
    SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED = 5,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP = 6,
    SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER = 7,

    SHADER_GENERIC_SAMPLER_COUNT,
} shader_generic_sampler;

/**
 * @brief Represents a single entry in the internal uniform array.
 */
typedef struct shader_uniform {
    kname name;
    /** @brief The offset in bytes from the beginning of the uniform set (global/instance/local) */
    u64 offset;
    /**
     * @brief The location to be used as a lookup. Typically the same as the index except for samplers,
     * which is used to lookup texture index within the internal array at the given scope (global/instance).
     */
    u16 location;
    /** @brief Index into the internal uniform array. */
    u16 index;
    /** @brief The size of the uniform, or 0 for samplers. */
    u16 size;
    /** @brief The index of the descriptor set the uniform belongs to (0=per_frame, 1=per_group, INVALID_ID=per_draw). */
    u8 set_index;
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

typedef enum shader_flags {
    SHADER_FLAG_NONE = 0x00,
    // Reads from depth buffer.
    SHADER_FLAG_DEPTH_TEST = 0x01,
    // Writes to depth buffer.
    SHADER_FLAG_DEPTH_WRITE = 0x02,
    SHADER_FLAG_WIREFRAME = 0x04,
    // Reads from depth buffer.
    SHADER_FLAG_STENCIL_TEST = 0x08,
    // Writes to depth buffer.
    SHADER_FLAG_STENCIL_WRITE = 0x10,
    // Reads from colour buffer.
    SHADER_FLAG_COLOUR_READ = 0x20,
    // Writes to colour buffer.
    SHADER_FLAG_COLOUR_WRITE = 0x40
} shader_flags;

typedef u32 shader_flag_bits;

/**
 * @brief Represents data required for a particular update frequency within a shader.
 */
typedef struct shader_frequency_data {
    /** @brief The number of non-sampler and non-texture uniforms for this frequency. */
    u8 uniform_count;
    /** @brief The number of sampler uniforms for this frequency. */
    u8 uniform_sampler_count;
    // darray Keeps the uniform indices of samplers for fast lookups.
    u32* sampler_indices;
    /** @brief The number of texture uniforms for this frequency. */
    u8 uniform_texture_count;
    // darray Keeps the uniform indices of textures for fast lookups.
    u32* texture_indices;
    /** @brief The actual size of the uniform buffer object for this frequency. */
    u64 ubo_size;
    /** @brief The stride of the uniform buffer object for this frequency. */
    u64 ubo_stride;
    /**
     * @brief The offset in bytes for the UBO from the beginning
     * of the uniform buffer for this frequency.
     */
    u64 ubo_offset;

    /** @brief The identifier of the currently bound group/per_draw. Ignored for per_frame */
    u32 bound_id;

} shader_frequency_data;

/**
 * @brief Represents the current state of a given shader.
 */
typedef enum shader_state {
    /** @brief The shader has not yet gone through the creation process, and is unusable.*/
    SHADER_STATE_NOT_CREATED,
    /** @brief The shader has gone through the creation process, but not initialization. It is unusable.*/
    SHADER_STATE_UNINITIALIZED,
    /** @brief The shader is created and initialized, and is ready for use.*/
    SHADER_STATE_INITIALIZED,
} shader_state;

typedef struct shader_stage_config {
    shader_stage stage;
    const char* filename;
    char* source;
} shader_stage_config;

/** @brief Configuration for an attribute. */
typedef struct shader_attribute_config {
    /** @brief The length of the name. */
    u8 name_length;
    /** @brief The name of the attribute. */
    char* name;
    /** @brief The size of the attribute. */
    u8 size;
    /** @brief The type of the attribute. */
    shader_attribute_type type;
} shader_attribute_config;

/** @brief Configuration for a uniform. */
typedef struct shader_uniform_config {
    /** @brief The length of the name. */
    u8 name_length;
    /** @brief The name of the uniform. */
    char* name;
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

/**
 * @brief Configuration for a shader. Typically created and
 * destroyed by the shader resource loader, and set to the
 * properties found in a .shadercfg resource file.
 */
typedef struct shader_config {
    /** @brief The name of the shader to be created. */
    char* name;

    /** @brief The face cull mode to be used. Default is BACK if not supplied. */
    face_cull_mode cull_mode;

    /** @brief The topology types for the shader pipeline. See primitive_topology_type. Defaults to "triangle list" if unspecified. */
    u32 topology_types;

    /** @brief The count of attributes. */
    u8 attribute_count;
    /** @brief The collection of attributes. Darray. */
    shader_attribute_config* attributes;

    /** @brief The count of uniforms. */
    u8 uniform_count;
    /** @brief The collection of uniforms. Darray. */
    shader_uniform_config* uniforms;

    /** @brief The number of stages present in the shader. */
    u8 stage_count;

    /** @brief The collection of stage configs. */
    shader_stage_config* stage_configs;

    /** @brief The maximum number of groups allowed. */
    u32 max_groups;

    /** @brief The maximum number of per-draw instances allowed. */
    u32 max_per_draw_count;

    /** @brief The flags set for this shader. */
    u32 flags;
} shader_config;

/**
 * @brief Represents a shader on the frontend.
 */
typedef struct kshader {
    /** @brief unique identifier that is compared against a handle. */
    u64 uniqueid;

    kname name;

    shader_flag_bits flags;

    /** @brief The types of topologies used by the shader and its pipeline. See primitive_topology_type. */
    u32 topology_types;

    /**
     * @brief The amount of bytes that are required for UBO alignment.
     *
     * This is used along with the UBO size to determine the ultimate
     * stride, which is how much the UBOs are spaced out in the buffer.
     * For example, a required alignment of 256 means that the stride
     * must be a multiple of 256 (true for some nVidia cards).
     */
    u64 required_ubo_alignment;

    /** @brief An array of uniforms in this shader. Darray. */
    shader_uniform* uniforms;

    /** @brief An array of attributes. Darray. */
    shader_attribute* attributes;

    /** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
    u16 attribute_stride;

    u8 shader_stage_count;
    shader_stage_config* stage_configs;

    /** @brief Per-frame frequency data. */
    shader_frequency_data per_frame;

    /** @brief Per-group frequency data. */
    shader_frequency_data per_group;

    /** @brief Per-draw frequency data. */
    shader_frequency_data per_draw;

    /** @brief The internal state of the shader. */
    shader_state state;

    /** @brief Indicates if the shader is currently flagged to use wireframe. */
    b8 is_wireframe;

    /** @brief An opaque pointer to hold renderer API specific data. Renderer is responsible for creation and destruction of this.  */
    void* internal_data;

#ifdef _DEBUG
    u32* module_watch_ids;
#endif
} kshader;

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

typedef enum kmaterial_texture_map_channel {
    KMATERIAL_TEXTURE_MAP_CHANNEL_R = 0,
    KMATERIAL_TEXTURE_MAP_CHANNEL_G = 1,
    KMATERIAL_TEXTURE_MAP_CHANNEL_B = 2,
    KMATERIAL_TEXTURE_MAP_CHANNEL_A = 3
} kmaterial_texture_map_channel;
