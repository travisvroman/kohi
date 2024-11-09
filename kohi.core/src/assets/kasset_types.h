#pragma once

#include "containers/array.h"
#include "core_render_types.h"
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
    KASSET_TYPE_IMAGE,
    KASSET_TYPE_MATERIAL,
    KASSET_TYPE_STATIC_MESH,
    KASSET_TYPE_HEIGHTMAP_TERRAIN,
    KASSET_TYPE_SCENE,
    KASSET_TYPE_BITMAP_FONT,
    KASSET_TYPE_SYSTEM_FONT,
    KASSET_TYPE_TEXT,
    KASSET_TYPE_BINARY,
    KASSET_TYPE_KSON,
    KASSET_TYPE_VOXEL_TERRAIN,
    KASSET_TYPE_SKELETAL_MESH,
    KASSET_TYPE_AUDIO,
    KASSET_TYPE_MUSIC,
    KASSET_TYPE_SHADER,
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
    /** @brief The path of the originally imported file used to create this asset, stored as a kname */
    kname source_asset_path;

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
} kasset;

#define KASSET_TYPE_NAME_HEIGHTMAP_TERRAIN "HeightmapTerrain"

typedef struct kasset_heightmap_terrain {
    kasset base;
    kname heightmap_asset_name;
    u16 chunk_size;
    vec3 tile_scale;
    u8 material_count;
    kname* material_names;
} kasset_heightmap_terrain;

typedef enum kasset_image_format {
    KASSET_IMAGE_FORMAT_UNDEFINED = 0,
    // 4 channel, 8 bits per channel
    KASSET_IMAGE_FORMAT_RGBA8 = 1
    // TODO: additional formats
} kasset_image_format;

/** @brief Import options for images. */
typedef struct kasset_image_import_options {
    /** @brief Indicates if the image should be flipped on the y-axis when imported. */
    b8 flip_y;
    /** @brief The expected format of the image. */
    kasset_image_format format;
} kasset_image_import_options;

#define KASSET_TYPE_NAME_IMAGE "Image"

typedef struct kasset_image {
    kasset base;
    u32 width;
    u32 height;
    u8 channel_count;
    u8 mip_levels;
    kasset_image_format format;
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
    kasset base;
    u16 geometry_count;
    kasset_static_mesh_geometry* geometries;
    extents_3d extents;
    vec3 center;
} kasset_static_mesh;

#define KASSET_TYPE_NAME_MATERIAL "Material"

typedef enum kmaterial_type {
    KMATERIAL_TYPE_UNKNOWN = 0,
    KMATERIAL_TYPE_PBR,
    KMATERIAL_TYPE_PBR_WATER,
    KMATERIAL_TYPE_UNLIT,
    KMATERIAL_TYPE_PHONG,
    KMATERIAL_TYPE_COUNT,
    KMATERIAL_TYPE_CUSTOM = 99
} kmaterial_type;

typedef enum kasset_material_texture_map {
    KASSET_MATERIAL_TEXTURE_MAP_BASE_COLOUR,
    KASSET_MATERIAL_TEXTURE_MAP_NORMAL,
    KASSET_MATERIAL_TEXTURE_MAP_METALLIC,
    KASSET_MATERIAL_TEXTURE_MAP_ROUGHNESS,
    KASSET_MATERIAL_TEXTURE_MAP_AO,
    KASSET_MATERIAL_TEXTURE_MAP_MRA,
    KASSET_MATERIAL_TEXTURE_MAP_EMISSIVE,
} kasset_material_texture_map;

typedef enum kasset_material_texture_map_channel {
    KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_R = 0,
    KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_G = 1,
    KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_B = 2,
    KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_A = 3
} kasset_material_texture_map_channel;

typedef struct kasset_material_texture {
    kname resource_name;
    kname sampler_name;
    kname map_name;
    kasset_material_texture_map map;
    kasset_material_texture_map_channel channel;
} kasset_material_texture;

typedef struct kasset_material_sampler {
    kname name;
    texture_filter filter_min;
    texture_filter filter_mag;
    texture_repeat repeat_u;
    texture_repeat repeat_v;
    texture_repeat repeat_w;
} kasset_material_sampler;

/* typedef struct kasset_material_property {
    kname name;
    shader_uniform_type type;
    u32 size;
    union {
        vec4 v4;
        vec3 v3;
        vec2 v2;
        f32 f32;
        u32 u32;
        u16 u16;
        u8 u8;
        i32 i32;
        i16 i16;
        i8 i8;
        mat4 mat4;
    } value;
} kasset_material_property; */

typedef struct kasset_material {
    kasset base;
    kmaterial_type type;
    // The asset name for a custom shader. Optional.
    kname custom_shader_name;

    vec4 base_colour;
    kasset_material_texture base_colour_map;

    f32 metallic;
    kasset_material_texture metallic_map;
    kasset_material_texture_map_channel metallic_map_source_channel;

    f32 roughness;
    kasset_material_texture roughness_map;
    kasset_material_texture_map_channel roughness_map_source_channel;

    f32 ambient_occlusion;
    kasset_material_texture ambient_occlusion_map;
    kasset_material_texture_map_channel ambient_occlusion_map_source_channel;

    // Combined metallic/roughness/ao value.
    vec3 mra;
    kasset_material_texture mra_map;
    // Indicates if the mra combined value/map should be used instead of the separate ones.
    b8 use_mra;

    vec4 emissive;
    kasset_material_texture emissive_map;

    u32 custom_sampler_count;
    kasset_material_sampler* custom_samplers;

} kasset_material;

#define KASSET_TYPE_NAME_TEXT "Text"

typedef struct kasset_text {
    kasset base;
    const char* content;
} kasset_text;

#define KASSET_TYPE_NAME_BINARY "Binary"

typedef struct kasset_binary {
    kasset base;
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

typedef enum kasset_scene_node_attachment_type {
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE,
    KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT
} kasset_scene_node_attachment_type;

static const char* kasset_scene_node_attachment_type_strings[KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT] = {
    "Skybox",           // KASSET_SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    "DirectionalLight", // KASSET_SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    "PointLight",       // KASSET_SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT,
    "StaticMesh",       // KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    "HeightmapTerrain", // KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_HEIGHTMAP_TERRAIN,
    "WaterPlane"        // KASSET_SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE,
};

// Ensure changes to scene attachment types break this if it isn't also updated.
STATIC_ASSERT(KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT == (sizeof(kasset_scene_node_attachment_type_strings) / sizeof(*kasset_scene_node_attachment_type_strings)), "Scene attachment type count does not match string lookup table count.");

//////////////////////

typedef struct kasset_scene_node_attachment {
    kasset_scene_node_attachment_type type;
    const char* name;
} kasset_scene_node_attachment;

typedef struct kasset_scene_node_attachment_skybox {
    kasset_scene_node_attachment base;
    const char* cubemap_image_asset_name;
} kasset_scene_node_attachment_skybox;

typedef struct kasset_scene_node_attachment_directional_light {
    kasset_scene_node_attachment base;
    vec4 colour;
    vec4 direction;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} kasset_scene_node_attachment_directional_light;

typedef struct kasset_scene_node_attachment_point_light {
    kasset_scene_node_attachment base;
    vec4 colour;
    vec4 position;
    f32 constant_f;
    f32 linear;
    f32 quadratic;
} kasset_scene_node_attachment_point_light;

typedef struct kasset_scene_node_attachment_static_mesh {
    kasset_scene_node_attachment base;
    const char* asset_name;
} kasset_scene_node_attachment_static_mesh;

typedef struct kasset_scene_node_attachment_heightmap_terrain {
    kasset_scene_node_attachment base;
    const char* asset_name;
} kasset_scene_node_attachment_heightmap_terrain;

typedef struct kasset_scene_node_attachment_water_plane {
    kasset_scene_node_attachment base;
    // TODO: expand configurable properties.
} kasset_scene_node_attachment_water_plane;

typedef struct kasset_scene_node {
    const char* name;
    u32 attachment_count;
    kasset_scene_node_attachment* attachments;
    u32 child_count;
    struct kasset_scene_node* children;
    // String representation of xform, processed by the scene when needed.
    const char* xform_source;
} kasset_scene_node;

typedef struct kasset_scene {
    kasset base;
    const char* description;
    u32 node_count;
    kasset_scene_node* nodes;
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

typedef struct kasset_shader_uniform {
    const char* name;
    shader_uniform_type type;
    shader_update_frequency frequency;
} kasset_shader_uniform;

typedef struct kasset_shader {
    kasset base;
    u32 stage_count;
    kasset_shader_stage* stages;
    b8 depth_test;
    b8 depth_write;
    b8 stencil_test;
    b8 stencil_write;
    u16 max_instances;

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
    kasset base;
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
    kasset base;
    kname face;
    u32 size;
    i32 line_height;
    i32 baseline;
    i32 atlas_size_x;
    i32 atlas_size_y;
    array_kasset_bitmap_font_glyph glyphs;
    array_kasset_bitmap_font_kerning kernings;
    f32 tab_x_advance;
    array_kasset_bitmap_font_page pages;
} kasset_bitmap_font;
