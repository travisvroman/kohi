#pragma once

#include "containers/binary_string_table.h"
#include "core_render_types.h"
#include "defines.h"
#include "identifiers/identifier.h"
#include "math/math_types.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"

/** @brief A magic number indicating the file as a kohi binary asset file. */
#define ASSET_MAGIC 0xDECAFBAD

// The maximum length of the string representation of an asset type.
#define KASSET_TYPE_MAX_LENGTH 64
// The maximum name of an asset.
#define KASSET_NAME_MAX_LENGTH 256
// The maximum name length for a kpackage.
#define KPACKAGE_NAME_MAX_LENGTH 128

// The maximum length of a fully-qualified asset name, including the '.' between parts.
#define KASSET_FULLY_QUALIFIED_NAME_MAX_LENGTH = (KPACKAGE_NAME_MAX_LENGTH + KASSET_TYPE_MAX_LENGTH + KASSET_NAME_MAX_LENGTH + 2)

typedef enum kasset_type {
	KASSET_TYPE_UNKNOWN,
	/** An image, typically (but not always) used as a texture. */
	KASSET_TYPE_IMAGE = 1,
	KASSET_TYPE_MATERIAL = 2,
	KASSET_TYPE_HEIGHTFIELD_TERRAIN = 3,
	KASSET_TYPE_HEIGHTMAP_TERRAIN = 4,
	KASSET_TYPE_SCENE = 5,
	KASSET_TYPE_BITMAP_FONT = 6,
	KASSET_TYPE_SYSTEM_FONT = 7,
	KASSET_TYPE_TEXT = 8,
	KASSET_TYPE_BINARY = 9,
	KASSET_TYPE_KSON = 10,
	KASSET_TYPE_VOXEL_TERRAIN = 11,
	// TODO: use this for the next asset type
	KASSET_TYPE_RESERVED_2 = 12,
	KASSET_TYPE_AUDIO = 13,
	KASSET_TYPE_SHADER = 14,
	KASSET_TYPE_MODEL = 15,
	KASSET_TYPE_MAX
} kasset_type;

/**
 * @brief The primary header for binary assets, to be used for serialization.
 * This should be the first member of the asset-specific binary file header.
 * NOTE: Binary asset headers should be 32-bit aligned.
 */
typedef struct binary_asset_header {
	// A magic number used to identify the binary block as a Kohi asset.
	u32 magic;
	// Indicates the asset type. Cast to kasset_type.
	u32 type;
	// The asset type version, used for feature support checking for asset versions.
	u32 version;
	// The size of the data region of  the asset in bytes.
	u32 data_block_size;
} binary_asset_header;

struct kasset;
struct kasset_importer;

typedef enum asset_request_result {
	/** The asset load was a success, including any GPU operations (if required). */
	ASSET_REQUEST_RESULT_SUCCESS,
	/** The specified package name was invalid or not found. */
	ASSET_REQUEST_RESULT_INVALID_PACKAGE,
	/** The specified asset type was invalid or not found. */
	ASSET_REQUEST_RESULT_INVALID_ASSET_TYPE,
	/** The specified asset name was invalid or not found. */
	ASSET_REQUEST_RESULT_INVALID_NAME,
	/** The asset was found, but failed to load during the parsing stage. */
	ASSET_REQUEST_RESULT_PARSE_FAILED,
	/** The asset was found, but failed to load during the GPU upload stage. */
	ASSET_REQUEST_RESULT_GPU_UPLOAD_FAILED,
	/** An internal system failure has occurred. See logs for details. */
	ASSET_REQUEST_RESULT_INTERNAL_FAILURE,
	/** No handler exists for the given asset. See logs for details. */
	ASSET_REQUEST_RESULT_NO_HANDLER,
	/** No importer exists for the given asset extension. See logs for details. */
	ASSET_REQUEST_RESULT_NO_IMPORTER_FOR_SOURCE_ASSET,
	/** There was a failure at the VFS level, probably a request for an asset that doesn't exist. */
	ASSET_REQUEST_RESULT_VFS_REQUEST_FAILED,
	/** Returned by handlers who attempt (and fail) an auto-import of source asset data when the binary does not exist. */
	ASSET_REQUEST_RESULT_AUTO_IMPORT_FAILED,
	/** The total number of result options in this enumeration. Not an actual result value */
	ASSET_REQUEST_RESULT_COUNT
} asset_request_result;

/**
 * @brief A function pointer typedef to be used to provide the asset asset_system
 * with a calback function when asset loading is complete or failed. This process is asynchronus.
 *
 * @param result The result of the asset request.
 * @param asset A constant pointer to the asset that is loaded.
 * @param listener_inst A pointer to the listener, usually passed along with the original request.
 */
typedef void (*PFN_kasset_on_result)(asset_request_result result, const struct kasset *asset, void *listener_inst);

struct vfs_asset_data;

/**
 * @brief A function pointer typedef to be used to provide the asset system with a callback function
 * when an asset is written to on-disk (i.e. a hot-reload). This process is synchronous.
 */
typedef void (*PFN_kasset_on_hot_reload)(const struct vfs_asset_data *asset_data, const struct kasset *asset);

/**
 * @brief Imports an asset according to the provided params and the importer's internal logic.
 * NOTE: Some importers (i.e. .obj for static meshes) can also trigger imports of other assets. Those assets are immediately
 * serialized to disk/package and not returned here though.
 *
 * @param self A constant pointer to the importer itself.
 * @param data_size The size of the data being imported.
 * @param data A constant pointer to a block of memory containing the data being imported.
 * @param params A block of memory containing parameters for the import. Optional in general, but required by some importers.
 * @param out_asset A pointer to the asset being imported.
 * @returns True on success; otherwise false.
 */
typedef b8 (*PFN_kasset_importer_import)(const struct kasset_importer *self, u64 data_size, const void *data, void *params, struct kasset *out_asset);

/**
 * @brief Represents the interface point for an importer.
 */
typedef struct kasset_importer {
	/** @brief The file type supported by the importer. */
	const char *source_type;
	/**
	 * @brief Imports an asset according to the provided params and the importer's internal logic.
	 * NOTE: Some importers (i.e. .obj for static meshes) can also trigger imports of other assets. Those assets are immediately
	 * serialized to disk/package and not returned here though.
	 *
	 * @param self A pointer to the importer itself.
	 * @param data_size The size of the data being imported.
	 * @param data A block of memory containing the data being imported.
	 * @param params A block of memory containing parameters for the import. Optional in general, but required by some importers.
	 * @param out_asset A pointer to the asset being imported.
	 * @returns True on success; otherwise false.
	 */
	PFN_kasset_importer_import import;
} kasset_importer;

/** @brief Various metadata included with the asset. */
typedef struct kasset_metadata {
	// The asset version.
	u32 version;
	/** @brief The path of the asset, stored as a kstring_id */
	kstring_id asset_path;
	/** @brief The path of the originally imported file used to create this asset, stored as a kstring_id */
	kstring_id source_asset_path;

	/** @brief The number of tags. */
	u32 tag_count;

	/** @brief An array of tags. */
	kname *tags;
	// TODO: Listing of asset-type-specific metadata

} kasset_metadata;

/**
 * @brief a structure meant to be included as the first member in the
 * struct of all asset types for quick casting purposes.
 */
typedef struct kasset {
	/** @brief A system-wide unique identifier for the asset. */
	identifier id;
	/** @brief Increments every time the asset is loaded/reloaded. Otherwise INVALID_ID. */
	u32 generation;
	// Size of the asset.
	u64 size;
	// Asset name stored as a kname.
	kname name;
	// Package name stored as a kname.
	kname package_name;
	/** @brief The asset type */
	kasset_type type;
	/** @brief Metadata for the asset */
	kasset_metadata meta;
	/** @brief The file watch id, if the asset is being watched. Otherwise INVALID_ID. */
	u32 file_watch_id;
} kasset;

#define KASSET_TYPE_NAME_HEIGHTMAP_TERRAIN "HeightmapTerrain"

typedef struct kasset_heightmap_terrain {
	kname heightmap_asset_name;
	kname heightmap_asset_package_name;
	u16 chunk_size;
	vec3 tile_scale;
	u8 material_count;
	kname *material_names;
	u32 version;
} kasset_heightmap_terrain;

#define KASSET_TYPE_NAME_IMAGE "Image"

typedef struct kasset_image {
	kname name;
	u32 width;
	u32 height;
	u32 depth;
	u8 channel_count;
	u8 mip_levels;
	kpixel_format format;
	u64 pixel_array_size;
	u8 *pixels;
} kasset_image;

#define KASSET_TYPE_NAME_MATERIAL "Material"

typedef struct kasset_material {
	kname name;
	kmaterial_type type;
	// Shading model
	kmaterial_model model;

	b8 has_transparency;
	b8 masked;
	b8 double_sided;
	b8 recieves_shadow;
	b8 casts_shadow;
	b8 use_vertex_colour_as_base_colour;

	// The asset name for a custom shader. Optional.
	kname custom_shader_name;

	vec4 base_colour;
	kmaterial_texture_input_config base_colour_map;

	vec4 specular_colour;
	kmaterial_texture_input_config specular_colour_map;

	b8 normal_enabled;
	vec3 normal;
	kmaterial_texture_input_config normal_map;

	f32 metallic;
	kmaterial_texture_input_config metallic_map;
	texture_channel metallic_map_source_channel;

	f32 roughness;
	kmaterial_texture_input_config roughness_map;
	texture_channel roughness_map_source_channel;

	b8 ambient_occlusion_enabled;
	f32 ambient_occlusion;
	kmaterial_texture_input_config ambient_occlusion_map;
	texture_channel ambient_occlusion_map_source_channel;

	// Combined metallic/roughness/ao value.
	vec3 mra;
	kmaterial_texture_input_config mra_map;
	// Indicates if the mra combined value/map should be used instead of the separate ones.
	b8 use_mra;

	b8 emissive_enabled;
	vec4 emissive;
	kmaterial_texture_input_config emissive_map;

	// DUDV map - only used for water materials.
	kmaterial_texture_input_config dudv_map;

	u32 custom_sampler_count;
	kmaterial_sampler_config *custom_samplers;

	// Only used in water materials.
	f32 tiling;
	// Only used in water materials.
	f32 wave_strength;
	// Only used in water materials.
	f32 wave_speed;

} kasset_material;

#define KASSET_TYPE_NAME_TEXT "Text"

typedef struct kasset_text {
	const char *content;
} kasset_text;

#define KASSET_TYPE_NAME_BINARY "Binary"

typedef struct kasset_binary {
	u64 size;
	const void *content;
} kasset_binary;

#define KASSET_TYPE_NAME_KSON "Kson"

typedef struct kasset_kson {
	kasset base;
	const char *source_text;
	kson_tree tree;
} kasset_kson;

#define KASSET_TYPE_NAME_SHADER "Shader"

typedef struct kasset_shader_stage {
	shader_stage type;
	const char *source_asset_name;
	const char *package_name;
} kasset_shader_stage;

typedef struct kasset_shader_attribute {
	const char *name;
	shader_attribute_type type;
} kasset_shader_attribute;

// One per vertex layout.
typedef struct kasset_shader_pipeline {
	const char *name;

	u8 stage_count;
	kasset_shader_stage *stages;

	u8 attribute_count;
	kasset_shader_attribute *attributes;
} kasset_shader_pipeline;

typedef struct kasset_shader_attachment {
	const char *name;
	kpixel_format format;
} kasset_shader_attachment;

/**
 * @brief Represents a shader asset, typically loaded from disk.
 */
typedef struct kasset_shader {
	kname name;
	u32 version;
	b8 depth_test;
	b8 depth_write;
	b8 stencil_test;
	b8 stencil_write;
	b8 colour_read;
	b8 colour_write;
	b8 supports_wireframe;
	primitive_topology_type_bits topology_types;
	primitive_topology_type default_topology;

	u8 colour_attachment_count;
	kasset_shader_attachment *colour_attachments;

	kasset_shader_attachment depth_attachment;
	kasset_shader_attachment stencil_attachment;

	u8 pipeline_count;
	kasset_shader_pipeline *pipelines;

	u8 binding_set_count;
	shader_binding_set_config *binding_sets;
} kasset_shader;

#define KASSET_TYPE_NAME_SYSTEM_FONT "SystemFont"

typedef struct kasset_system_font_face {
	kname name;
} kasset_system_font_face;

typedef struct kasset_system_font {
	kname ttf_asset_name;
	kname ttf_asset_package_name;
	u32 face_count;
	kasset_system_font_face *faces;
	u32 font_binary_size;
	void *font_binary;
} kasset_system_font;

#define KASSET_TYPE_NAME_BITMAP_FONT "BitmapFont"

typedef struct kasset_bitmap_font_glyph {
	i32 codepoint;
	u16 x;
	u16 y;
	u16 width;
	u16 height;
	i16 x_offset;
	i16 y_offset;
	i16 x_advance;
	u8 page_id;
} kasset_bitmap_font_glyph;

typedef struct kasset_bitmap_font_kerning {
	i32 codepoint_0;
	i32 codepoint_1;
	i16 amount;
} kasset_bitmap_font_kerning;

typedef struct kasset_bitmap_font_page {
	i8 id;
	kname image_asset_name;
} kasset_bitmap_font_page;

typedef struct kasset_bitmap_font {
	kname face;
	u32 size;
	i32 line_height;
	i32 baseline;
	i32 atlas_size_x;
	i32 atlas_size_y;
	u32 glyph_count;
	kasset_bitmap_font_glyph *glyphs;
	u32 kerning_count;
	kasset_bitmap_font_kerning *kernings;
	u32 page_count;
	kasset_bitmap_font_page *pages;
} kasset_bitmap_font;

#define KASSET_TYPE_NAME_AUDIO "Audio"

/**
 * Represents a Kohi Audio asset.
 */
typedef struct kasset_audio {
	kname name;
	// The number of channels (i.e. 1 for mono or 2 for stereo)
	i32 channels;
	// The sample rate of the sound/music (i.e. 44100)
	u32 sample_rate;

	u32 total_sample_count;

	u64 pcm_data_size;
	/** Pulse-code modulation buffer, or raw data to be fed into a buffer. */
	i16 *pcm_data;
} kasset_audio;

#define KASSET_TYPE_NAME_MODEL "Model"

#define KASSET_MODEL_CURRENT_VERSION 1

typedef struct kasset_model_key_vec3 {
	vec3 value;
	f32 time;
} kasset_model_key_vec3;

typedef struct kasset_model_key_quat {
	quat value;
	f32 time;
} kasset_model_key_quat;

typedef struct kasset_model_channel {
	kname name;
	u32 pos_count;
	kasset_model_key_vec3 *positions;
	u32 scale_count;
	kasset_model_key_vec3 *scales;
	u32 rot_count;
	kasset_model_key_quat *rotations;
} kasset_model_channel;

typedef struct kasset_model_animation {
	kname name;
	f32 duration;
	f32 ticks_per_second;
	u16 channel_count;
	kasset_model_channel *channels;
} kasset_model_animation;

// Bone data
typedef struct kasset_model_bone {
	kname name;
	// Transformation from mesh space to bone space.
	mat4 offset;
	// Index into bone array.
	u32 id;
} kasset_model_bone;

typedef struct kasset_model_node {
	kname name;
	mat4 local_transform;
	u16 parent_index; // INVALID_ID_U16 = root
	u16 child_count;
	u16 *children;
} kasset_model_node;

typedef enum kasset_model_mesh_type {
	KASSET_MODEL_MESH_TYPE_STATIC = 0,	// maps to vertex_3d
	KASSET_MODEL_MESH_TYPE_SKINNED = 1, // maps to skinned_vertex_3d

	KASSET_MODEL_MESH_TYPE_MAX
} kasset_model_mesh_type;

typedef struct kasset_model_submesh_data {
	kname name;
	kname material_name;
	u32 vertex_count;
	kasset_model_mesh_type type;
	void *vertices;
	u32 index_count;
	u32 *indices;
	vec3 center;
	extents_3d extents;
} kasset_model_submesh_data;

/**
 * Represents a Kohi Model asset. A model can contain
 * mesh data (i.e. vertices/indices), bone, node,
 * and animation data.
 */
typedef struct kasset_model {
	u16 submesh_count;
	kasset_model_submesh_data *submeshes;
	u16 bone_count;
	kasset_model_bone *bones;
	u16 node_count;
	kasset_model_node *nodes;
	u16 animation_count;
	kasset_model_animation *animations;

	mat4 global_inverse_transform;

	vec3 center;
	extents_3d extents;
} kasset_model;

#define KASSET_TYPE_NAME_HEIGHTFIELD_TERRAIN "HeightFieldTerrain"

typedef struct kasset_hf_terrain_vertex {
	f32 y_offset;
} kasset_hf_terrain_vertex;

typedef struct kasset_hf_terrain_chunk {
	u8 material_indices[5];
} kasset_hf_terrain_chunk;

#define KASSET_HF_TERRAIN_CHUNK_COUNT 256
#define KASSET_HF_SPLAT_RES 1024

typedef struct kasset_hf_terrain_block {
	kasset_hf_terrain_chunk chunks[KASSET_HF_TERRAIN_CHUNK_COUNT];

	// Number of pixels along x
	u32 splatmap_size_x;
	// Number of pixels along y
	u32 splatmap_size_y;
	// pixel count * 4 (channels, rgba)
	u8 splatmap_pixels[KASSET_HF_SPLAT_RES * KASSET_HF_SPLAT_RES * 4];
} kasset_hf_terrain_block;

typedef struct kasset_hf_terrain_material {
	u32 name_str_index;
	u32 albedo_str_index;
	u32 normal_str_index;
	u32 mra_str_index;
} kasset_hf_terrain_material;

typedef struct kasset_hf_terrain_material_OLD {
	u32 albedo_str_index;
	u32 normal_str_index;
	u32 mra_str_index;
} kasset_hf_terrain_material_OLD;

typedef struct kasset_hf_terrain_material_map_names {
	const char *albedo_str;
	const char *normal_str;
	const char *mra_str;
} kasset_hf_terrain_material_map_names;

typedef struct kasset_hf_terrain {
	u16 version;

	// Number of blocks along x axis. Must be at least 1.
	u16 block_count_x;
	// Number of blocks along z axis. Must be at least 1.
	u16 block_count_z;
	kasset_hf_terrain_block *blocks;

	u32 vertex_count;
	kasset_hf_terrain_vertex *vertices;

	u8 material_count;
	kasset_hf_terrain_material *materials;
	// Collection of map names per material
	kasset_hf_terrain_material_map_names *material_map_names;
	// Names of each material.
	const char **material_names;

} kasset_hf_terrain;
