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
    kgeometry* geometries;
    extents_3d extents;
    void* debug_data;
} mesh;

/**
 * @brief A material, which represents various properties
 * of a surface in the world such as texture, colour,
 * bumpiness, shininess and more.
 */
typedef struct material {
    /** @brief The material id. */
    u32 id;
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
