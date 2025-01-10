#pragma once

#include <containers/array.h>
#include <math/math_types.h>
#include <strings/kname.h>

#include "assets/kasset_types.h"
#include "core_render_types.h"
#include "identifiers/khandle.h"
#include "math/geometry.h"

/** @brief Pre-defined resource types. */
typedef enum kresource_type {
    /** @brief Unassigned resource type */
    KRESOURCE_TYPE_UNKNOWN,
    /** @brief Plain text resource type. */
    KRESOURCE_TYPE_TEXT,
    /** @brief Plain binary resource type. */
    KRESOURCE_TYPE_BINARY,
    /** @brief Texture resource type. */
    KRESOURCE_TYPE_TEXTURE,
    /** @brief Material resource type. */
    KRESOURCE_TYPE_MATERIAL,
    /** @brief Shader resource type. */
    KRESOURCE_TYPE_SHADER,
    /** @brief Static Mesh resource type (collection of geometries). */
    KRESOURCE_TYPE_STATIC_MESH,
    /** @brief Skeletal Mesh resource type (collection of geometries). */
    KRESOURCE_TYPE_SKELETAL_MESH,
    /** @brief Bitmap font resource type. */
    KRESOURCE_TYPE_BITMAP_FONT,
    /** @brief System font resource type. */
    KRESOURCE_TYPE_SYSTEM_FONT,
    /** @brief Scene resource type. */
    KRESOURCE_TYPE_SCENE,
    /** @brief Heightmap-based terrain resource type. */
    KRESOURCE_TYPE_HEIGHTMAP_TERRAIN,
    /** @brief Voxel-based terrain resource type. */
    KRESOURCE_TYPE_VOXEL_TERRAIN,
    /** @brief Sound effect resource type. */
    KRESOURCE_TYPE_SOUND_EFFECT,
    /** @brief Music resource type. */
    KRESOURCE_TYPE_MUSIC,
    KRESOURCE_TYPE_COUNT,
    // Anything beyond 128 is user-defined types.
    KRESOURCE_KNOWN_TYPE_MAX = 128
} kresource_type;

/** @brief Indicates where a resource is in its lifecycle. */
typedef enum kresource_state {
    /**
     * @brief No load operations have happened whatsoever
     * for the resource.
     * The resource is NOT in a drawable state.
     */
    KRESOURCE_STATE_UNINITIALIZED,
    /**
     * @brief The CPU-side of the resources have been
     * loaded, but no GPU uploads have happened.
     * The resource is NOT in a drawable state.
     */
    KRESOURCE_STATE_INITIALIZED,
    /**
     * @brief The GPU-side of the resources are in the
     * process of being uploaded, but the upload is not yet complete.
     * The resource is NOT in a drawable state.
     */
    KRESOURCE_STATE_LOADING,
    /**
     * @brief The GPU-side of the resources are finished
     * with the process of being uploaded.
     * The resource IS in a drawable state.
     */
    KRESOURCE_STATE_LOADED
} kresource_state;

typedef struct kresource {
    kname name;
    kresource_type type;
    kresource_state state;
    u32 generation;

    /** @brief The number of tags. */
    u32 tag_count;

    /** @brief An array of tags. */
    kname* tags;

    // darray of file watches, if relevant.
    u32* asset_file_watch_ids;
} kresource;

typedef struct kresource_asset_info {
    kname asset_name;
    kname package_name;
    kasset_type type;
    b8 watch_for_hot_reload;
} kresource_asset_info;

ARRAY_TYPE(kresource_asset_info);

typedef void (*PFN_resource_loaded_user_callback)(kresource* resource, void* listener);

typedef struct kresource_request_info {
    kresource_type type;
    // The list of assets to be loaded.
    array_kresource_asset_info assets;
    // The callback made whenever all listed assets are loaded.
    PFN_resource_loaded_user_callback user_callback;
    // Listener user data.
    void* listener_inst;
    // Force the request to be synchronous, returning a loaded and ready resource immediately.
    // NOTE: This should be used sparingly, as it is a blocking operation.
    b8 synchronous;
} kresource_request_info;

/**
 * @brief Represents various types of textures.
 */
typedef enum texture_type {
    /** @brief A standard two-dimensional texture. */
    TEXTURE_TYPE_2D,
    /** @brief A 2d array texture. */
    TEXTURE_TYPE_2D_ARRAY,
    /** @brief A cube texture, used for cubemaps. */
    TEXTURE_TYPE_CUBE,
    /** @brief A cube array texture, used for arrays of cubemaps. */
    TEXTURE_TYPE_CUBE_ARRAY,
    TEXTURE_TYPE_COUNT
} texture_type;

typedef enum texture_format {
    TEXTURE_FORMAT_UNKNOWN,
    TEXTURE_FORMAT_RGBA8,
    TEXTURE_FORMAT_RGB8,
} texture_format;

typedef enum texture_flag {
    /** @brief Indicates if the texture has transparency. */
    TEXTURE_FLAG_HAS_TRANSPARENCY = 0x01,
    /** @brief Indicates if the texture can be written (rendered) to. */
    TEXTURE_FLAG_IS_WRITEABLE = 0x02,
    /** @brief Indicates if the texture was created via wrapping vs traditional
       creation. */
    TEXTURE_FLAG_IS_WRAPPED = 0x04,
    /** @brief Indicates the texture is a depth texture. */
    TEXTURE_FLAG_DEPTH = 0x08,
    /** @brief Indicates the texture is a stencil texture. */
    TEXTURE_FLAG_STENCIL = 0x10,
    /** @brief Indicates that this texture should account for renderer buffering (i.e. double/triple buffering) */
    TEXTURE_FLAG_RENDERER_BUFFERING = 0x20,
} texture_flag;

/** @brief Holds bit flags for textures.. */
typedef u8 texture_flag_bits;

#define KRESOURCE_TYPE_NAME_TEXTURE "Texture"

typedef struct kresource_texture {
    kresource base;
    /** @brief The texture type. */
    texture_type type;
    /** @brief The texture width. */
    u32 width;
    /** @brief The texture height. */
    u32 height;
    /** @brief The format of the texture data. */
    texture_format format;
    /** @brief For arrayed textures, how many "layers" there are. Otherwise this is 1. */
    u16 array_size;
    /** @brief Holds various flags for this texture. */
    texture_flag_bits flags;
    /** @brief The number of mip maps the internal texture has. Must always be at least 1. */
    u8 mip_levels;
    /** @brief The the handle to renderer-specific texture data. */
    khandle renderer_texture_handle;
} kresource_texture;

typedef struct kresource_texture_pixel_data {
    u8* pixels;
    u32 pixel_array_size;
    u32 width;
    u32 height;
    u32 channel_count;
    texture_format format;
    u8 mip_levels;
} kresource_texture_pixel_data;

ARRAY_TYPE(kresource_texture_pixel_data);

typedef struct kresource_texture_request_info {
    kresource_request_info base;

    texture_type texture_type;
    u8 array_size;
    texture_flag_bits flags;

    // Optionally provide pixel data per layer. Must match array_size in length.
    // Only used where asset at index has type of undefined.
    array_kresource_texture_pixel_data pixel_data;

    // Texture width in pixels. Ignored unless there are no assets or pixel data.
    u32 width;

    // Texture height in pixels. Ignored unless there are no assets or pixel data.
    u32 height;

    // Texture format. Ignored unless there are no assets or pixel data.
    texture_format format;

    // The number of mip levels. Ignored unless there are no assets or pixel data.
    u8 mip_levels;

    // Indicates if loaded image assets should be flipped on the y-axis when loaded. Ignored for non-asset-based textures.
    b8 flip_y;
} kresource_texture_request_info;

/**
 * @brief A shader resource.
 */
typedef struct kresource_shader {
    kresource base;

    /** @brief The face cull mode to be used. Default is BACK if not supplied. */
    face_cull_mode cull_mode;

    /** @brief The topology types for the shader pipeline. See primitive_topology_type. Defaults to "triangle list" if unspecified. */
    primitive_topology_types topology_types;

    /** @brief The count of attributes. */
    u8 attribute_count;
    /** @brief The collection of attributes.*/
    shader_attribute_config* attributes;

    /** @brief The count of uniforms. */
    u8 uniform_count;
    /** @brief The collection of uniforms.*/
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
    shader_flags flags;
} kresource_shader;

/** @brief Used to request a shader resource. */
typedef struct kresource_shader_request_info {
    kresource_request_info base;
    // Optionally include shader config source text to be used as if it resided in a .ksc file.
    const char* shader_config_source_text;
} kresource_shader_request_info;

/**
 * @brief A kresource_material is really nothing more than a configuration
 * of a material to hand off to the material system. Once a material is loaded,
 * this can just be released.
 */
typedef struct kresource_material {
    kresource base;

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

    // Derivative (dudv) map. Only used for water materials.
    kmaterial_texture_input dudv_map;

    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;

    u32 custom_sampler_count;
    kmaterial_sampler_config* custom_samplers;

} kresource_material;

/** @brief Used to request a material resource. */
typedef struct kresource_material_request_info {
    kresource_request_info base;
    // Optionally include source text to be used as if it resided in a .kmt file.
    const char* material_source_text;
} kresource_material_request_info;

/*
 * ==================================================
 * Static mesh
 * ==================================================
 */

/**
 * Represents a single static mesh, which contains geometry.
 */
typedef struct static_mesh_submesh {
    /** @brief The geometry data for this mesh. */
    kgeometry geometry;
    /** @brief The name of the material associated with this mesh. */
    kname material_name;
} static_mesh_submesh;

/**
 * @brief A mesh resource that is static in nature (i.e. it does not change over time).
 */
typedef struct kresource_static_mesh {
    kresource base;

    /** @brief The number of submeshes in this static mesh resource. */
    u16 submesh_count;
    /** @brief The array of submeshes in this static mesh resource. */
    static_mesh_submesh* submeshes;
} kresource_static_mesh;

typedef struct kresource_static_mesh_request_info {
    kresource_request_info base;
} kresource_static_mesh_request_info;

#define KRESOURCE_TYPE_NAME_TEXT "Text"

typedef struct kresource_text {
    kresource base;

    const char* text;
} kresource_text;

#define KRESOURCE_TYPE_NAME_BINARY "Binary"

typedef struct kresource_binary {
    kresource base;

    u32 size;
    void* bytes;
} kresource_binary;

#define KRESOURCE_TYPE_NAME_FONT "Font"

typedef struct font_glyph {
    i32 codepoint;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    i16 x_offset;
    i16 y_offset;
    i16 x_advance;
    u8 page_id;
} font_glyph;

typedef struct font_kerning {
    i32 codepoint_0;
    i32 codepoint_1;
    i16 amount;
} font_kerning;

typedef struct font_page {
    kname image_asset_name;
} font_page;

ARRAY_TYPE(font_glyph);
ARRAY_TYPE(font_kerning);
ARRAY_TYPE(font_page);

/**
 * @brief Represents a bitmap font resource.
 */
typedef struct kresource_bitmap_font {
    kresource base;

    kname face;
    // The font size.
    u32 size;
    i32 line_height;
    i32 baseline;
    u32 atlas_size_x;
    u32 atlas_size_y;

    array_font_glyph glyphs;
    array_font_kerning kernings;
    array_font_page pages;
} kresource_bitmap_font;

typedef struct kresource_bitmap_font_request_info {
    kresource_request_info base;
} kresource_bitmap_font_request_info;

/**
 * @brief Represents a system font resource.
 */
typedef struct kresource_system_font {
    kresource base;
    kname ttf_asset_name;
    kname ttf_asset_package_name;
    u32 face_count;
    kname* faces;
    u32 font_binary_size;
    void* font_binary;
} kresource_system_font;

typedef struct kresource_system_font_request_info {
    kresource_request_info base;
} kresource_system_font_request_info;

/**
 * @brief Represents a scene resource.
 */
typedef struct kresource_scene {
    kresource base;
    const char* description;
    u32 node_count;
    scene_node_config* nodes;
} kresource_scene;

typedef struct kresource_scene_request_info {
    kresource_request_info base;
} kresource_scene_request_info;

/**
 * @brief Represents a heightmap terrain resource.
 */
typedef struct kresource_heightmap_terrain {
    kresource base;
    kname heightmap_asset_name;
    kname heightmap_asset_package_name;
    u16 chunk_size;
    vec3 tile_scale;
    u8 material_count;
    kname* material_names;
} kresource_heightmap_terrain;

typedef struct kresource_heightmap_terrain_request_info {
    kresource_request_info base;
} kresource_heightmap_terrain_request_info;

/**
 * Represents a Kohi Audio resource.
 */
typedef struct kresource_audio {
    kresource base;
    // The number of channels (i.e. 1 for mono or 2 for stereo)
    i32 channels;
    // The sample rate of the sound/music (i.e. 44100)
    u32 sample_rate;

    u32 total_sample_count;

    u64 pcm_data_size;
    /** Pulse-code modulation buffer, or raw data to be fed into a buffer. */
    i16* pcm_data;

    // The format (i.e. 16 bit stereo)
    u32 format;
    // Used to track samples in streaming type files.
    // FIXME: Should be tracked internally by the audio system.
    u32 total_samples_left;

    /** @brief A handle to the audio internal resource. */
    khandle internal_resource;
} kresource_audio;

typedef struct kresource_audio_request_info {
    kresource_request_info base;
} kresource_audio_request_info;
