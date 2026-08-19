#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "strings/kname.h"

typedef vec3 colour3;
typedef vec4 colour4;

typedef vec3i colour3i;
typedef vec4i colour4i;

// clang-format off
#define KCOLOUR4_WHITE       (colour4){1.0f, 1.0f, 1.0f, 1.0f}
#define KCOLOUR4_WHITE_75    (colour4){1.0f, 1.0f, 1.0f, 0.75f}
#define KCOLOUR4_WHITE_50    (colour4){1.0f, 1.0f, 1.0f, 0.5f}
#define KCOLOUR4_WHITE_25    (colour4){1.0f, 1.0f, 1.0f, 0.25f}
#define KCOLOUR4_BLACK       (colour4){0.0f, 0.0f, 0.0f, 1.0f}
#define KCOLOUR4_GRAY        (colour4){0.5f, 0.5f, 0.5f, 1.0f}
#define KCOLOUR4_DARK_GRAY   (colour4){0.25f, 0.25f, 0.25f, 1.0f}
#define KCOLOUR4_LIGHT_GRAY  (colour4){0.75f, 0.75f, 0.75f, 1.0f}
#define KCOLOUR4_RED         (colour4){1.0f, 0.0f, 0.0f, 1.0f}
#define KCOLOUR4_GREEN       (colour4){0.0f, 1.0f, 0.0f, 1.0f}
#define KCOLOUR4_BLUE        (colour4){0.0f, 0.0f, 1.0f, 1.0f}
#define KCOLOUR4_YELLOW      (colour4){1.0f, 1.0f, 0.0f, 1.0f}
#define KCOLOUR4_CYAN        (colour4){0.0f, 1.0f, 1.0f, 1.0f}
#define KCOLOUR4_MAGENTA     (colour4){1.0f, 0.0f, 1.0f, 1.0f}
#define KCOLOUR4_ORANGE      (colour4){1.0f, 0.5f, 0.0f, 1.0f}
#define KCOLOUR4_PURPLE      (colour4){0.5f, 0.0f, 0.5f, 1.0f}
#define KCOLOUR4_PINK        (colour4){1.0f, 0.75f, 0.8f, 1.0f}
#define KCOLOUR4_BROWN       (colour4){0.6f, 0.4f, 0.2f, 1.0f}
#define KCOLOUR4_TRANSPARENT (colour4){0.0f, 0.0f, 0.0f, 0.0f}

#define KCOLOUR3_WHITE       (colour3){1.0f, 1.0f, 1.0f}
#define KCOLOUR3_BLACK       (colour3){0.0f, 0.0f, 0.0f}
#define KCOLOUR3_GRAY        (colour3){0.5f, 0.5f, 0.5f}
#define KCOLOUR3_DARK_GRAY   (colour3){0.25f, 0.25f, 0.25f}
#define KCOLOUR3_LIGHT_GRAY  (colour3){0.75f, 0.75f, 0.75f}
#define KCOLOUR3_RED         (colour3){1.0f, 0.0f, 0.0f}
#define KCOLOUR3_GREEN       (colour3){0.0f, 1.0f, 0.0f}
#define KCOLOUR3_BLUE        (colour3){0.0f, 0.0f, 1.0f}
#define KCOLOUR3_YELLOW      (colour3){1.0f, 1.0f, 0.0f}
#define KCOLOUR3_CYAN        (colour3){0.0f, 1.0f, 1.0f}
#define KCOLOUR3_MAGENTA     (colour3){1.0f, 0.0f, 1.0f}
#define KCOLOUR3_ORANGE      (colour3){1.0f, 0.5f, 0.0f}
#define KCOLOUR3_PURPLE      (colour3){0.5f, 0.0f, 0.5f}
#define KCOLOUR3_PINK        (colour3){1.0f, 0.75f, 0.8f}
#define KCOLOUR3_BROWN       (colour3){0.6f, 0.4f, 0.2f}
#define KCOLOUR3_TRANSPARENT (colou34){0.0f, 0.0f, 0.0f}
// clang-format on

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
typedef enum primitive_topology_type {
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
} primitive_topology_type;

/** @brief A combination of topology bit flags. */
typedef u32 primitive_topology_type_bits;

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
	// Depth buffer format, 4 channels, 8 bits each
	KPIXEL_FORMAT_D32,
	// Depth buffer format, 3 channels, 8 bits each
	KPIXEL_FORMAT_D24,
	// Stencil buffer format, 1 channel, 8 bits
	KPIXEL_FORMAT_S8
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
	SHADER_ATTRIB_TYPE_INT32_2 = 10U,
	SHADER_ATTRIB_TYPE_INT32_3 = 11U,
	SHADER_ATTRIB_TYPE_INT32_4 = 12U,
	SHADER_ATTRIB_TYPE_UINT32 = 13U,
	SHADER_ATTRIB_TYPE_UINT32_2 = 14U,
	SHADER_ATTRIB_TYPE_UINT32_3 = 15U,
	SHADER_ATTRIB_TYPE_UINT32_4 = 16U,
} shader_attribute_type;

/**
 * @brief Represents various types of textures.
 */
typedef enum ktexture_type {
	/** @brief Undefined texture type as the default, useful for catching default-zero scenarios */
	KTEXTURE_TYPE_UNDEFINED,
	/** @brief A one-dimensional texture. */
	KTEXTURE_TYPE_1D,
	/** @brief A standard two-dimensional texture. */
	KTEXTURE_TYPE_2D,
	/** @brief A three-dimensional texture. */
	KTEXTURE_TYPE_3D,
	/** @brief A cube texture, used for cubemaps. */
	KTEXTURE_TYPE_CUBE,
	/** @brief A 1d array texture. */
	KTEXTURE_TYPE_1D_ARRAY,
	/** @brief A 2d array texture. */
	KTEXTURE_TYPE_2D_ARRAY,
	/** @brief A cube array texture, used for arrays of cubemaps. */
	KTEXTURE_TYPE_CUBE_ARRAY,
	KTEXTURE_TYPE_COUNT
} ktexture_type;

typedef enum shader_sampler_type {
	SHADER_SAMPLER_TYPE_1D,
	SHADER_SAMPLER_TYPE_2D,
	SHADER_SAMPLER_TYPE_3D,
	SHADER_SAMPLER_TYPE_CUBE,
	SHADER_SAMPLER_TYPE_1D_ARRAY,
	SHADER_SAMPLER_TYPE_2D_ARRAY,
	SHADER_SAMPLER_TYPE_CUBE_ARRAY,
} shader_sampler_type;

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

typedef enum shader_binding_type {
	SHADER_BINDING_TYPE_UBO,
	SHADER_BINDING_TYPE_SSBO,
	SHADER_BINDING_TYPE_TEXTURE,
	SHADER_BINDING_TYPE_SAMPLER,
	SHADER_BINDING_TYPE_COUNT
} shader_binding_type;

typedef struct shader_binding_config {
	shader_binding_type binding_type;
	kname name;
	u64 data_size;
	u64 offset;
	union {
		ktexture_type texture_type;
		shader_sampler_type sampler_type;
	};
	// Array size for arrayed textures or samplers. Assumes a array_size of 1 unless set to > 1.
	u8 array_size;
} shader_binding_config;

typedef struct shader_binding_set_config {
	kname name;
	u32 max_instance_count;
	u8 binding_count;
	u8 sampler_count;
	u8 texture_count;
	// binding index of the UBO. INVALID_ID_U8 if none.
	u8 ubo_index;
	u8 ssbo_count;
	shader_binding_config *bindings;
} shader_binding_set_config;

typedef struct shader_pipeline_config {

	u8 attribute_count;
	/** @brief An array of attributes. */
	shader_attribute *attributes;

	/** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
	u16 attribute_stride;

	u8 stage_count;

	// Array of stages.
	shader_stage *stages;
	// Array of names of stage assets.
	kname *stage_names;
	// Array of source text for stages. Matches size of stage_source_text_resources;
	const char **stage_sources;
} shader_pipeline_config;

/**
 * @brief Represents a texture to be used for rendering purposes,
 * stored on the GPU (VRAM)
 */
typedef u16 ktexture;

// The id representing an invalid texture.
#define INVALID_KTEXTURE INVALID_ID_U16

typedef enum ktexture_flag {
	/** @brief Indicates if the texture has transparency. */
	KTEXTURE_FLAG_HAS_TRANSPARENCY = 0x01,
	/** @brief Indicates if the texture can be written (rendered) to. */
	KTEXTURE_FLAG_IS_WRITEABLE = 0x02,
	/** @brief Indicates if the texture was created via wrapping vs traditional
	   creation. */
	KTEXTURE_FLAG_IS_WRAPPED = 0x04,
	/** @brief Indicates the texture is a depth texture. */
	KTEXTURE_FLAG_DEPTH = 0x08,
	/** @brief Indicates the texture is a stencil texture. */
	KTEXTURE_FLAG_STENCIL = 0x10,
	/** @brief Indicates that this texture should account for renderer buffering (i.e. double/triple buffering) */
	KTEXTURE_FLAG_RENDERER_BUFFERING = 0x20,
} ktexture_flag;

/** @brief Holds bit flags for textures.. */
typedef u8 ktexture_flag_bits;

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
	KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT = 0x0200U,
	// The material uses a mask for transparency. If both this and transparency bits are set, fragments below a given threshold are discarded.
	KMATERIAL_FLAG_MASKED_BIT = 0x0400U,
} kmaterial_flag_bits;

typedef u32 kmaterial_flags;

// The configuration for a custom material sampler.
typedef struct kmaterial_sampler_config {
	kname name;
	texture_filter filter_min;
	texture_filter filter_mag;
	texture_repeat repeat_u;
	texture_repeat repeat_v;
	texture_repeat repeat_w;
} kmaterial_sampler_config;

// Configuration for a material texture input.
typedef struct kmaterial_texture_input_config {
	// Name of the resource.
	kname resource_name;
	// Name of the package containing the resource.
	kname package_name;
	// Sampler config for this input map.
	kmaterial_sampler_config sampler;
	// The texture channel to sample, if relevant.
	texture_channel channel;
} kmaterial_texture_input_config;

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
