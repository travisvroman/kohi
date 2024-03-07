/**
 * @file resource_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the types for common resources the engine uses.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "core/identifier.h"
#include "math/math_types.h"

#define TERRAIN_MAX_MATERIAL_COUNT 4

/** @brief Pre-defined resource types. */
typedef enum resource_type {
    /** @brief Text resource type. */
    RESOURCE_TYPE_TEXT,
    /** @brief Binary resource type. */
    RESOURCE_TYPE_BINARY,
    /** @brief Image resource type. */
    RESOURCE_TYPE_IMAGE,
    /** @brief Material resource type. */
    RESOURCE_TYPE_MATERIAL,
    /** @brief Shader resource type (or more accurately shader config). */
    RESOURCE_TYPE_SHADER,
    /** @brief Mesh resource type (collection of geometry configs). */
    RESOURCE_TYPE_MESH,
    /** @brief Bitmap font resource type. */
    RESOURCE_TYPE_BITMAP_FONT,
    /** @brief System font resource type. */
    RESOURCE_TYPE_SYSTEM_FONT,
    /** @brief Simple scene resource type. */
    RESOURCE_TYPE_scene,
    /** @brief Terrain resource type. */
    RESOURCE_TYPE_TERRAIN,
    /** @brief Audio resource type. */
    RESOURCE_TYPE_AUDIO,
    /** @brief Custom resource type. Used by loaders outside the core engine. */
    RESOURCE_TYPE_CUSTOM
} resource_type;

/** @brief A magic number indicating the file as a kohi binary file. */
#define RESOURCE_MAGIC 0xcafebabe

/**
 * @brief The header data for binary resource types.
 */
typedef struct resource_header {
    /** @brief A magic number indicating the file as a kohi binary file. */
    u32 magic_number;
    /** @brief The resource type. Maps to the enum resource_type. */
    u8 resource_type;
    /** @brief The format version this resource uses. */
    u8 version;
    /** @brief Reserved for future header data.. */
    u16 reserved;
} resource_header;

/**
 * @brief A generic structure for a resource. All resource loaders
 * load data into these.
 */
typedef struct resource {
    /** @brief The identifier of the loader which handles this resource. */
    u32 loader_id;
    /** @brief The name of the resource. */
    const char *name;
    /** @brief The full file path of the resource. */
    char *full_path;
    /** @brief The size of the resource data in bytes. */
    u64 data_size;
    /** @brief The resource data. */
    void *data;
} resource;

/**
 * @brief A structure to hold image resource data.
 */
typedef struct image_resource_data {
    /** @brief The number of channels. */
    u8 channel_count;
    /** @brief The width of the image. */
    u32 width;
    /** @brief The height of the image. */
    u32 height;
    /** @brief The pixel data of the image. */
    u8 *pixels;
    /**
     * @brief The number of mip levels to be generated for this
     * image resource. Should be passed on to the texture using it.
     * Must always be at least 1.
     */
    u32 mip_levels;
} image_resource_data;

/** @brief Parameters used when loading an image. */
typedef struct image_resource_params {
    /** @brief Indicates if the image should be flipped on the y-axis when loaded.
     */
    b8 flip_y;
} image_resource_params;

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

/**
 * @brief The maximum length of a texture name.
 */
#define TEXTURE_NAME_MAX_LENGTH 512

typedef enum texture_flag {
    /** @brief Indicates if the texture has transparency. */
    TEXTURE_FLAG_HAS_TRANSPARENCY = 0x1,
    /** @brief Indicates if the texture can be written (rendered) to. */
    TEXTURE_FLAG_IS_WRITEABLE = 0x2,
    /** @brief Indicates if the texture was created via wrapping vs traditional
       creation. */
    TEXTURE_FLAG_IS_WRAPPED = 0x4,
    /** @brief Indicates the texture is a depth texture. */
    TEXTURE_FLAG_DEPTH = 0x8
} texture_flag;

/** @brief Holds bit flags for textures.. */
typedef u8 texture_flag_bits;

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

/**
 * @brief Represents a texture.
 */
typedef struct texture {
    /** @brief The unique texture identifier. */
    u32 id;
    /** @brief The texture type. */
    texture_type type;
    /** @brief The texture width. */
    u32 width;
    /** @brief The texture height. */
    u32 height;
    /** @brief The number of channels in the texture. */
    u8 channel_count;
    /** @brief For arrayed textures, how many "layers" there are. Otherwise this is 1. */
    u16 array_size;
    /** @brief Holds various flags for this texture. */
    texture_flag_bits flags;
    /** @brief The texture generation. Incremented every time the data is
     * reloaded. */
    u32 generation;
    /** @brief The texture name. */
    char name[TEXTURE_NAME_MAX_LENGTH];
    /** @brief The raw texture data (pixels). */
    void *internal_data;
    /** @brief The number of mip maps the internal texture has. Must always be at least 1. */
    u32 mip_levels;
} texture;

/** @brief Represents supported texture filtering modes. */
typedef enum texture_filter {
    /** @brief Nearest-neighbor filtering. */
    TEXTURE_FILTER_MODE_NEAREST = 0x0,
    /** @brief Linear (i.e. bilinear) filtering.*/
    TEXTURE_FILTER_MODE_LINEAR = 0x1
} texture_filter;

typedef enum texture_repeat {
    TEXTURE_REPEAT_REPEAT = 0x1,
    TEXTURE_REPEAT_MIRRORED_REPEAT = 0x2,
    TEXTURE_REPEAT_CLAMP_TO_EDGE = 0x3,
    TEXTURE_REPEAT_CLAMP_TO_BORDER = 0x4
} texture_repeat;

/**
 * @brief A structure which maps a texture, use and
 * other properties.
 */
typedef struct texture_map {
    /**
     * @brief The cached generation of the assigned texture.
     * Used to determine when to regenerate this texture map's
     * resources when a texture's generation changes (as this could
     * be required if, say, a texture's mip levels change).
     * */
    u32 generation;
    /**
     * @brief Cached mip map levels. Should match assigned
     * texture. Must always be at least 1.
     */
    u32 mip_levels;
    /** @brief A pointer to a texture. */
    texture *texture;
    /** @brief Texture filtering mode for minification. */
    texture_filter filter_minify;
    /** @brief Texture filtering mode for magnification. */
    texture_filter filter_magnify;
    /** @brief The repeat mode on the U axis (or X, or S) */
    texture_repeat repeat_u;
    /** @brief The repeat mode on the V axis (or Y, or T) */
    texture_repeat repeat_v;
    /** @brief The repeat mode on the W axis (or Z, or U) */
    texture_repeat repeat_w;
    /** @brief An identifier used for internal resource lookups/management. */
    u32 internal_id;
} texture_map;

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

typedef enum font_type { FONT_TYPE_BITMAP,
                         FONT_TYPE_SYSTEM } font_type;

typedef struct font_data {
    font_type type;
    char face[256];
    u32 size;
    i32 line_height;
    i32 baseline;
    i32 atlas_size_x;
    i32 atlas_size_y;
    texture_map atlas;
    u32 glyph_count;
    font_glyph *glyphs;
    u32 kerning_count;
    font_kerning *kernings;
    f32 tab_x_advance;
    u32 internal_data_size;
    void *internal_data;
} font_data;

typedef struct bitmap_font_page {
    i8 id;
    char file[256];
} bitmap_font_page;

typedef struct bitmap_font_resource_data {
    font_data data;
    u32 page_count;
    bitmap_font_page *pages;
} bitmap_font_resource_data;

typedef struct system_font_face {
    char name[256];
} system_font_face;

typedef struct system_font_resource_data {
    // darray
    system_font_face *fonts;
    u64 binary_size;
    void *font_binary;
} system_font_resource_data;

/** @brief The maximum length of a material name. */
#define MATERIAL_NAME_MAX_LENGTH 256

struct material;

/** @brief The maximum length of a geometry name. */
#define GEOMETRY_NAME_MAX_LENGTH 256

/**
 * @brief Represents actual geometry in the world.
 * Typically (but not always, depending on use) paired with a material.
 */
typedef struct geometry {
    /** @brief The geometry identifier. */
    u32 id;
    /** @brief The geometry generation. Incremented every time the geometry
     * changes. */
    u16 generation;
    /** @brief The center of the geometry in local coordinates. */
    vec3 center;
    /** @brief The extents of the geometry in local coordinates. */
    extents_3d extents;

    /** @brief The vertex count. */
    u32 vertex_count;
    /** @brief The size of each vertex. */
    u32 vertex_element_size;
    /** @brief The vertex data. */
    void *vertices;
    /** @brief The offset from the beginning of the vertex buffer. */
    u64 vertex_buffer_offset;

    /** @brief The index count. */
    u32 index_count;
    /** @brief The size of each index. */
    u32 index_element_size;
    /** @brief The index data. */
    void *indices;
    /** @brief The offset from the beginning of the index buffer. */
    u64 index_buffer_offset;

    /** @brief The geometry name. */
    char name[GEOMETRY_NAME_MAX_LENGTH];
    /** @brief A pointer to the material associated with this geometry.. */
    struct material *material;
} geometry;

struct geometry_config;
typedef struct mesh_config {
    char *resource_name;
    u16 geometry_count;
    struct geometry_config *g_configs;
} mesh_config;

typedef enum mesh_state {
    MESH_STATE_UNDEFINED,
    MESH_STATE_CREATED,
    MESH_STATE_INITIALIZED,
    MESH_STATE_LOADING,
    MESH_STATE_LOADED
} mesh_state;

typedef struct mesh {
    char *name;
    char *resource_name;
    mesh_state state;
    identifier id;
    u8 generation;
    u16 geometry_count;
    struct geometry_config *g_configs;
    geometry **geometries;
    extents_3d extents;
    void *debug_data;
} mesh;

/** @brief Shader stages available in the system. */
typedef enum shader_stage {
    SHADER_STAGE_VERTEX = 0x00000001,
    SHADER_STAGE_GEOMETRY = 0x00000002,
    SHADER_STAGE_FRAGMENT = 0x00000004,
    SHADER_STAGE_COMPUTE = 0x0000008
} shader_stage;

typedef struct shader_stage_config {
    shader_stage stage;
    const char *name;
    const char *filename;
    u32 source_length;
    char *source;
} shader_stage_config;

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
    SHADER_UNIFORM_TYPE_SAMPLER_1D = 11U,
    SHADER_UNIFORM_TYPE_SAMPLER_2D = 12U,
    SHADER_UNIFORM_TYPE_SAMPLER_3D = 13U,
    SHADER_UNIFORM_TYPE_SAMPLER_CUBE = 14U,
    SHADER_UNIFORM_TYPE_SAMPLER_1D_ARRAY = 15U,
    SHADER_UNIFORM_TYPE_SAMPLER_2D_ARRAY = 16U,
    SHADER_UNIFORM_TYPE_SAMPLER_CUBE_ARRAY = 17U,
    SHADER_UNIFORM_TYPE_CUSTOM = 255U
} shader_uniform_type;

/**
 * @brief Defines shader scope, which indicates how
 * often it gets updated.
 */
typedef enum shader_scope {
    /** @brief Global shader scope, generally updated once per frame. */
    SHADER_SCOPE_GLOBAL = 0,
    /** @brief Instance shader scope, generally updated "per-instance" of the
       shader. */
    SHADER_SCOPE_INSTANCE = 1,
    /** @brief Local shader scope, generally updated per-object */
    SHADER_SCOPE_LOCAL = 2
} shader_scope;

/** @brief Configuration for an attribute. */
typedef struct shader_attribute_config {
    /** @brief The length of the name. */
    u8 name_length;
    /** @brief The name of the attribute. */
    char *name;
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
    char *name;
    /** @brief The size of the uniform. If arrayed, this is the per-element size */
    u16 size;
    /** @brief The location of the uniform. */
    u32 location;
    /** @brief The type of the uniform. */
    shader_uniform_type type;
    /** @brief The array length, if uniform is an array. */
    u32 array_length;
    /** @brief The scope of the uniform. */
    shader_scope scope;
} shader_uniform_config;

/**
 * @brief Configuration for a shader. Typically created and
 * destroyed by the shader resource loader, and set to the
 * properties found in a .shadercfg resource file.
 */
typedef struct shader_config {
    /** @brief The name of the shader to be created. */
    char *name;

    /** @brief The face cull mode to be used. Default is BACK if not supplied. */
    face_cull_mode cull_mode;

    /** @brief The topology types for the shader pipeline. See primitive_topology_type. Defaults to "triangle list" if unspecified. */
    u32 topology_types;

    /** @brief The count of attributes. */
    u8 attribute_count;
    /** @brief The collection of attributes. Darray. */
    shader_attribute_config *attributes;

    /** @brief The count of uniforms. */
    u8 uniform_count;
    /** @brief The collection of uniforms. Darray. */
    shader_uniform_config *uniforms;

    /** @brief The number of stages present in the shader. */
    u8 stage_count;

    /** @brief The collection of stage configs. */
    shader_stage_config *stage_configs;

    /** @brief The maximum number of instances allowed. */
    u32 max_instances;

    /** @brief The flags set for this shader. */
    u32 flags;
} shader_config;

typedef enum material_type {
    // Invalid.
    MATERIAL_TYPE_UNKNOWN = 0,
    MATERIAL_TYPE_PBR = 1,
    MATERIAL_TYPE_TERRAIN = 2,
    MATERIAL_TYPE_CUSTOM = 99
} material_type;

typedef struct material_config_prop {
    char *name;
    shader_uniform_type type;
    u32 size;
    // FIXME: This seems like a colossal waste of memory... perhaps a union or
    // something better?
    vec4 value_v4;
    vec3 value_v3;
    vec2 value_v2;
    f32 value_f32;
    u32 value_u32;
    u16 value_u16;
    u8 value_u8;
    i32 value_i32;
    i16 value_i16;
    i8 value_i8;
    mat4 value_mat4;
} material_config_prop;

typedef struct material_map {
    char *name;
    char *texture_name;
    texture_filter filter_min;
    texture_filter filter_mag;
    texture_repeat repeat_u;
    texture_repeat repeat_v;
    texture_repeat repeat_w;
} material_map;

typedef struct material_config {
    u8 version;
    char *name;
    material_type type;
    char *shader_name;
    // darray
    material_config_prop *properties;
    // darray
    material_map *maps;
    /** @brief Indicates if the material should be automatically released when no
     * references to it remain. */
    b8 auto_release;
} material_config;

typedef struct material_phong_properties {
    /** @brief The diffuse colour. */
    vec4 diffuse_colour;

    vec3 padding;
    /** @brief The material shininess, determines how concentrated the specular
     * lighting is. */
    f32 shininess;
} material_phong_properties;

typedef struct material_terrain_properties {
    material_phong_properties materials[4];
    vec3 padding;
    i32 num_materials;
    vec4 padding2;
} material_terrain_properties;

/**
 * @brief A material, which represents various properties
 * of a surface in the world such as texture, colour,
 * bumpiness, shininess and more.
 */
typedef struct material {
    /** @brief The material id. */
    u32 id;
    /** @brief The material type. */
    material_type type;
    /** @brief The material generation. Incremented every time the material is
     * changed. */
    u32 generation;
    /** @brief The internal material id. Used by the renderer backend to map to
     * internal resources. */
    u32 internal_id;
    /** @brief The material name. */
    char name[MATERIAL_NAME_MAX_LENGTH];

    /** @brief An array of texture maps. */
    texture_map *maps;

    /** @brief property structure size. */
    u32 property_struct_size;

    /** @brief array of material property structures, which varies based on material type. e.g. material_phong_properties */
    void *properties;

    /**
     * @brief An explicitly-set irradiance texture for this material. Should only be set
     * in limited circumstances. Ideally a scene should set it through material manager.
     */
    texture *irradiance_texture;

    // /** @brief The diffuse colour. */
    // vec4 diffuse_colour;

    // /** @brief The material shininess, determines how concentrated the specular
    //  * lighting is. */
    // f32 shininess;

    u32 shader_id;

    /** @brief Synced to the renderer's current frame number when the material has
     * been applied that frame. */
    u64 render_frame_number;
    u8 render_draw_index;
} material;

typedef struct skybox_scene_config {
    char *name;
    char *cubemap_name;
} skybox_scene_config;

typedef struct directional_light_scene_config {
    char *name;
    vec4 colour;
    vec4 direction;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} directional_light_scene_config;

typedef struct point_light_scene_config {
    char *name;
    vec4 colour;
    vec4 position;
    f32 constant_f;
    f32 linear;
    f32 quadratic;
} point_light_scene_config;

typedef struct mesh_scene_config {
    char *name;
    char *resource_name;
    transform transform;
} mesh_scene_config;

typedef struct terrain_scene_config {
    char *name;
    char *resource_name;
    transform xform;
} terrain_scene_config;

typedef struct scene_config_remove {
    char *name;
    char *description;
    skybox_scene_config skybox_config;
    directional_light_scene_config directional_light_config;

    // darray
    point_light_scene_config *point_lights;

    // darray
    mesh_scene_config *meshes;

    // darray
    terrain_scene_config *terrains;
} scene_config_remove;

typedef enum scene_node_attachment_type {
    SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN,
    SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    SCENE_NODE_ATTACHMENT_TYPE_TERRAIN,
    SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT
} scene_node_attachment_type;

// Static mesh attachment.
typedef struct scene_node_attachment_static_mesh {
    scene_node_attachment_type attachment_type;
    char *resource_name;
} scene_node_attachment_static_mesh;

// Terrain attachment.
typedef struct scene_node_attachment_terrain {
    scene_node_attachment_type attachment_type;
    char *resource_name;
} scene_node_attachment_terrain;

// Skybox attachment
typedef struct scene_node_attachment_skybox {
    scene_node_attachment_type attachment_type;
    char *cubemap_name;
} scene_node_attachment_skybox;

// Directional light attachment
typedef struct scene_node_attachment_directional_light {
    scene_node_attachment_type attachment_type;
    vec4 colour;
    vec4 direction;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} scene_node_attachment_directional_light;

typedef struct scene_node_attachment_point_light {
    scene_node_attachment_type attachment_type;
    vec4 colour;
    vec4 position;
    f32 constant_f;
    f32 linear;
    f32 quadratic;
} scene_node_attachment_point_light;

typedef struct scene_node_attachment_config {
    scene_node_attachment_type type;
    void *attachment;
} scene_node_attachment_config;

typedef struct scene_xform_config {
    vec3 position;
    quat rotation;
    vec3 scale;
} scene_xform_config;

typedef struct scene_node_config {
    char *name;

    // Pointer to a config if one exists, otherwise 0
    scene_xform_config *xform;
    // darray
    scene_node_attachment_config *attachments;
    // darray
    struct scene_node_config *children;
} scene_node_config;

typedef struct scene_config {
    u32 version;
    char *name;
    char *description;

    // darray
    scene_node_config *nodes;
} scene_config;
