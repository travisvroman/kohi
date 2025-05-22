#pragma once

#include "containers/array.h"
#include "core_render_types.h"
#include "core_resource_types.h"
#include "defines.h"
#include "identifiers/identifier.h"
#include "math/math_types.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"

/** @brief A magic number indicating the file as a kohi binary asset file. */
#define ASSET_MAGIC 0xcafebabe
#define ASSET_MAGIC_U64 0xcafebabebadc0ffee

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
    KASSET_TYPE_STATIC_MESH = 3,
    KASSET_TYPE_HEIGHTMAP_TERRAIN = 4,
    KASSET_TYPE_SCENE = 5,
    KASSET_TYPE_BITMAP_FONT = 6,
    KASSET_TYPE_SYSTEM_FONT = 7,
    KASSET_TYPE_TEXT = 8,
    KASSET_TYPE_BINARY = 9,
    KASSET_TYPE_KSON = 10,
    KASSET_TYPE_VOXEL_TERRAIN = 11,
    KASSET_TYPE_SKELETAL_MESH = 12,
    KASSET_TYPE_AUDIO = 13,
    KASSET_TYPE_SHADER = 14,
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
typedef void (*PFN_kasset_on_result)(asset_request_result result, const struct kasset* asset, void* listener_inst);

struct vfs_asset_data;

/**
 * @brief A function pointer typedef to be used to provide the asset system with a callback function
 * when an asset is written to on-disk (i.e. a hot-reload). This process is synchronous.
 */
typedef void (*PFN_kasset_on_hot_reload)(const struct vfs_asset_data* asset_data, const struct kasset* asset);

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
typedef b8 (*PFN_kasset_importer_import)(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset);

/**
 * @brief Represents the interface point for an importer.
 */
typedef struct kasset_importer {
    /** @brief The file type supported by the importer. */
    const char* source_type;
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
    kname* tags;
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
    kname* material_names;
    u32 version;
} kasset_heightmap_terrain;

#define KASSET_TYPE_NAME_IMAGE "Image"

typedef struct kasset_image {
    u32 width;
    u32 height;
    u32 depth;
    u8 channel_count;
    u8 mip_levels;
    kpixel_format format;
    u64 pixel_array_size;
    u8* pixels;
} kasset_image;

#define KASSET_TYPE_NAME_STATIC_MESH "StaticMesh"

typedef struct kasset_static_mesh_geometry {
    kname name;
    kname material_asset_name;
    u32 vertex_count;
    vertex_3d* vertices;
    u32 index_count;
    u32* indices;
    extents_3d extents;
    vec3 center;
} kasset_static_mesh_geometry;

/** @brief Represents a static mesh asset. */
typedef struct kasset_static_mesh {
    u16 geometry_count;
    kasset_static_mesh_geometry* geometries;
    extents_3d extents;
    vec3 center;
} kasset_static_mesh;

#define KASSET_TYPE_NAME_MATERIAL "Material"

typedef struct kasset_material {
    kname name;
    kmaterial_type type;
    // Shading model
    kmaterial_model model;

    b8 has_transparency;
    b8 double_sided;
    b8 recieves_shadow;
    b8 casts_shadow;
    b8 use_vertex_colour_as_base_colour;

    // The asset name for a custom shader. Optional.
    kname custom_shader_name;

    vec4 base_colour;
    kmaterial_texture_input base_colour_map;

    vec4 specular_colour;
    kmaterial_texture_input specular_colour_map;

    b8 normal_enabled;
    vec3 normal;
    kmaterial_texture_input normal_map;

    f32 metallic;
    kmaterial_texture_input metallic_map;
    texture_channel metallic_map_source_channel;

    f32 roughness;
    kmaterial_texture_input roughness_map;
    texture_channel roughness_map_source_channel;

    b8 ambient_occlusion_enabled;
    f32 ambient_occlusion;
    kmaterial_texture_input ambient_occlusion_map;
    texture_channel ambient_occlusion_map_source_channel;

    // Combined metallic/roughness/ao value.
    vec3 mra;
    kmaterial_texture_input mra_map;
    // Indicates if the mra combined value/map should be used instead of the separate ones.
    b8 use_mra;

    b8 emissive_enabled;
    vec4 emissive;
    kmaterial_texture_input emissive_map;

    // DUDV map - only used for water materials.
    kmaterial_texture_input dudv_map;

    u32 custom_sampler_count;
    kmaterial_sampler_config* custom_samplers;

    // Only used in water materials.
    f32 tiling;
    // Only used in water materials.
    f32 wave_strength;
    // Only used in water materials.
    f32 wave_speed;

} kasset_material;

#define KASSET_TYPE_NAME_TEXT "Text"

typedef struct kasset_text {
    const char* content;
} kasset_text;

#define KASSET_TYPE_NAME_BINARY "Binary"

typedef struct kasset_binary {
    u64 size;
    const void* content;
} kasset_binary;

#define KASSET_TYPE_NAME_KSON "Kson"

typedef struct kasset_kson {
    kasset base;
    const char* source_text;
    kson_tree tree;
} kasset_kson;

#define KASSET_TYPE_NAME_SCENE "Scene"

typedef struct kasset_scene {
    kname name;
    u32 version;
    const char* description;
    u32 node_count;
    scene_node_config* nodes;
} kasset_scene;

#define KASSET_TYPE_NAME_SHADER "Shader"

typedef struct kasset_shader_stage {
    shader_stage type;
    const char* source_asset_name;
    const char* package_name;
} kasset_shader_stage;

typedef struct kasset_shader_attribute {
    const char* name;
    shader_attribute_type type;
} kasset_shader_attribute;

/**
 * @brief Represents a shader uniform within a shader asset.
 */
typedef struct kasset_shader_uniform {
    /** @brief The uniform name */
    const char* name;
    /** @brief The uniform type. */
    shader_uniform_type type;
    /** @brief The uniform size. Only used for struct type uniforms, ignored otherwise. */
    u32 size;
    /** @brief The number of elements for array uniforms. Treated as an array if > 1. */
    u32 array_size;
    /** @brief The uniform update frequency (i.e. per-frame, per-group, per-draw) */
    shader_update_frequency frequency;
} kasset_shader_uniform;

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
    primitive_topology_types topology_types;

    face_cull_mode cull_mode;

    u16 max_groups;
    u16 max_draw_ids;

    u32 stage_count;
    kasset_shader_stage* stages;

    u32 attribute_count;
    kasset_shader_attribute* attributes;

    u32 uniform_count;
    kasset_shader_uniform* uniforms;
} kasset_shader;

#define KASSET_TYPE_NAME_SYSTEM_FONT "SystemFont"

typedef struct kasset_system_font_face {
    kname name;
} kasset_system_font_face;

typedef struct kasset_system_font {
    kname ttf_asset_name;
    kname ttf_asset_package_name;
    u32 face_count;
    kasset_system_font_face* faces;
    u32 font_binary_size;
    void* font_binary;
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

ARRAY_TYPE(kasset_bitmap_font_glyph);
ARRAY_TYPE(kasset_bitmap_font_kerning);
ARRAY_TYPE(kasset_bitmap_font_page);

typedef struct kasset_bitmap_font {
    kname face;
    u32 size;
    i32 line_height;
    i32 baseline;
    i32 atlas_size_x;
    i32 atlas_size_y;
    array_kasset_bitmap_font_glyph glyphs;
    array_kasset_bitmap_font_kerning kernings;
    array_kasset_bitmap_font_page pages;
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
    i16* pcm_data;
} kasset_audio;
