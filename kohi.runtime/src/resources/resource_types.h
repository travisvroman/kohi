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

#include "identifiers/identifier.h"
#include "kresources/kresource_types.h"
#include "math/math_types.h"
#include "strings/kname.h"

#include <core_render_types.h>

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
    const char* name;
    /** @brief The full file path of the resource. */
    char* full_path;
    /** @brief The size of the resource data in bytes. */
    u64 data_size;
    /** @brief The resource data. */
    void* data;
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
    u8* pixels;
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
    TEXTURE_FLAG_HAS_TRANSPARENCY = 0x01,
    /** @brief Indicates if the texture can be written (rendered) to. */
    TEXTURE_FLAG_IS_WRITEABLE = 0x02,
    /** @brief Indicates if the texture was created via wrapping vs traditional
       creation. */
    TEXTURE_FLAG_IS_WRAPPED = 0x04,
    /** @brief Indicates the texture is a depth texture. */
    TEXTURE_FLAG_DEPTH = 0x08,
    /** @brief Indicates that this texture should account for renderer buffering (i.e. double/triple buffering) */
    TEXTURE_FLAG_RENDERER_BUFFERING = 0x10,
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

/** @brief The maximum length of a material name. */
#define MATERIAL_NAME_MAX_LENGTH 256

struct material;

struct geometry_config;
typedef struct mesh_config {
    char* resource_name;
    u16 geometry_count;
    struct geometry_config* g_configs;
} mesh_config;

typedef enum mesh_state {
    MESH_STATE_UNDEFINED,
    MESH_STATE_CREATED,
    MESH_STATE_INITIALIZED,
    MESH_STATE_LOADING,
    MESH_STATE_LOADED
} mesh_state;

struct geometry;
typedef struct mesh {
    char* name;
    char* resource_name;
    mesh_state state;
    identifier id;
    u8 generation;
    u16 geometry_count;
    struct geometry_config* g_configs;
    struct geometry** geometries;
    extents_3d extents;
    void* debug_data;
} mesh;

typedef struct shader_stage_config {
    shader_stage stage;
    const char* name;
    const char* filename;
    u32 source_length;
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

    /** @brief The maximum number of instances allowed. */
    u32 max_instances;

    /** @brief The maximum number of local thingies allowed. */
    u32 max_local_count;

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
    char* name;
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
    char* name;
    char* texture_name;
    texture_filter filter_min;
    texture_filter filter_mag;
    texture_repeat repeat_u;
    texture_repeat repeat_v;
    texture_repeat repeat_w;
} material_map;

typedef struct material_config {
    u8 version;
    char* name;
    material_type type;
    char* shader_name;
    // darray
    material_config_prop* properties;
    // darray
    material_map* maps;
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
    kname name;
    /** @brief The name of the package containing this material. */
    kname package_name;

    /** @brief An array of texture maps. */
    struct kresource_texture_map* maps;

    /** @brief property structure size. */
    u32 property_struct_size;

    /** @brief array of material property structures, which varies based on material type. e.g. material_phong_properties */
    void* properties;

    /**
     * @brief An explicitly-set irradiance texture for this material. Should only be set
     * in limited circumstances. Ideally a scene should set it through material manager.
     */
    kresource_texture* irradiance_texture;

    // /** @brief The diffuse colour. */
    // vec4 diffuse_colour;

    // /** @brief The material shininess, determines how concentrated the specular
    //  * lighting is. */
    // f32 shininess;

    u32 shader_id;

} material;

typedef enum scene_node_attachment_type {
    SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN,
    SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    SCENE_NODE_ATTACHMENT_TYPE_TERRAIN,
    SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT,
    SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE
} scene_node_attachment_type;

// Static mesh attachment.
typedef struct scene_node_attachment_static_mesh {
    char* resource_name;
} scene_node_attachment_static_mesh;

// Terrain attachment.
typedef struct scene_node_attachment_terrain {
    char* name;
    char* resource_name;
} scene_node_attachment_terrain;

// Skybox attachment
typedef struct scene_node_attachment_skybox {
    char* cubemap_name;
} scene_node_attachment_skybox;

// Directional light attachment
typedef struct scene_node_attachment_directional_light {
    vec4 colour;
    vec4 direction;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} scene_node_attachment_directional_light;

typedef struct scene_node_attachment_point_light {
    vec4 colour;
    vec4 position;
    f32 constant_f;
    f32 linear;
    f32 quadratic;
} scene_node_attachment_point_light;

// Skybox attachment
typedef struct scene_node_attachment_water_plane {
    u32 reserved;
} scene_node_attachment_water_plane;

typedef struct scene_node_attachment_config {
    scene_node_attachment_type type;
    void* attachment_data;
} scene_node_attachment_config;

typedef struct scene_xform_config {
    vec3 position;
    quat rotation;
    vec3 scale;
} scene_xform_config;

typedef struct scene_node_config {
    char* name;

    // Pointer to a config if one exists, otherwise 0
    scene_xform_config* xform;
    // darray
    scene_node_attachment_config* attachments;
    // darray
    struct scene_node_config* children;
} scene_node_config;

typedef struct scene_config {
    u32 version;
    char* name;
    char* description;
    char* resource_name;
    char* resource_full_path;

    // darray
    scene_node_config* nodes;
} scene_config;
