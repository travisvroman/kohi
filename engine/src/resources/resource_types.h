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

#include "math/math_types.h"
#include "renderer/renderer_types.inl"

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
    /** @brief Static mesh resource type. */
    RESOURCE_TYPE_STATIC_MESH,
    /** @brief Shader resource type (or more accurately shader config). */
    RESOURCE_TYPE_SHADER,
    /** @brief Custom resource type. Used by loaders outside the core engine. */
    RESOURCE_TYPE_CUSTOM
} resource_type;

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
} image_resource_data;

/**
 * @brief The maximum length of a texture name.
 */
#define TEXTURE_NAME_MAX_LENGTH 512

/**
 * @brief Represents a texture.
 */
typedef struct texture {
    /** @brief The unique texture identifier. */
    u32 id;
    /** @brief The texture width. */
    u32 width;
    /** @brief The texture height. */
    u32 height;
    /** @brief The number of channels in the texture. */
    u8 channel_count;
    /** @brief Indicates if the texture has transparency. */
    b8 has_transparency;
    /** @brief The texture generation. Incremented every time the data is reloaded. */
    u32 generation;
    /** @brief The texture name. */
    char name[TEXTURE_NAME_MAX_LENGTH];
    /** @brief The raw texture data (pixels). */
    void* internal_data;
} texture;

/** @brief A collection of texture uses */
typedef enum texture_use {
    /** @brief An unknown use. This is default, but should never actually be used. */
    TEXTURE_USE_UNKNOWN = 0x00,
    /** @brief The texture is used as a diffuse map. */
    TEXTURE_USE_MAP_DIFFUSE = 0x01
} texture_use;

/**
 * @brief A structure which maps a texture, use and
 * other properties.
 */
typedef struct texture_map {
    /** @brief A pointer to a texture. */
    texture* texture;
    /** @brief The use of the texture */
    texture_use use;
} texture_map;

/** @brief The maximum length of a material name. */
#define MATERIAL_NAME_MAX_LENGTH 256

/**
 * @brief A collection of material types.
 * @deprecated This should probably store a shader id instead, and be bound that way.
 */
typedef enum material_type {
    /** A material used in the world. */
    MATERIAL_TYPE_WORLD,
    /** A material used in the UI */
    MATERIAL_TYPE_UI
} material_type;

/**
 * @brief Material configuration typically loaded from
 * a file or created in code to load a material from.
 */
typedef struct material_config {
    /** @brief The name of the material. */
    char name[MATERIAL_NAME_MAX_LENGTH];
    /** @brief The material type. */
    material_type type;
    /** @brief Indicates if the material should be automatically released when no references to it remain. */
    b8 auto_release;
    /** @brief The diffuse colour of the material. */
    vec4 diffuse_colour;
    /** @brief The diffuse map name. */
    char diffuse_map_name[TEXTURE_NAME_MAX_LENGTH];
} material_config;

/**
 * @brief A material, which represents various properties
 * of a surface in the world such as texture, colour,
 * bumpiness, shininess and more.
 */
typedef struct material {
    /** @brief The material id. */
    u32 id;
    /** @brief The material generation. Incremented every time the material is changed. */
    u32 generation;
    /** @brief The internal material id. Used by the renderer backend to map to internal resources. */
    u32 internal_id;
    /** @brief The material type. */
    material_type type;
    /** @brief The material name. */
    char name[MATERIAL_NAME_MAX_LENGTH];
    /** @brief The diffuse colour. */
    vec4 diffuse_colour;
    /** @brief The diffuse texture map. */
    texture_map diffuse_map;
} material;

/** @brief The maximum length of a geometry name. */
#define GEOMETRY_NAME_MAX_LENGTH 256

/**
 * @brief Represents actual geometry in the world.
 * Typically (but not always, depending on use) paired with a material.
 */
typedef struct geometry {
    /** @brief The geometry identifier. */
    u32 id;
    /** @brief The internal geometry identifier, used by the renderer backend to map to internal resources. */
    u32 internal_id;
    /** @brief The geometry generation. Incremented every time the geometry changes. */
    u32 generation;
    /** @brief The geometry name. */
    char name[GEOMETRY_NAME_MAX_LENGTH];
    /** @brief A pointer to the material associated with this geometry.. */
    material* material;
} geometry;

typedef struct shader_attribute_config {
    u8 name_length;
    char* name;
    u8 size;
    shader_attribute_type type;
} shader_attribute_config;

typedef struct shader_uniform_config {
    u8 name_length;
    char* name;
    u8 size;
    u32 location;
    shader_uniform_type type;
    shader_scope scope;
} shader_uniform_config;

typedef struct shader_config {
    char* name;
    u8 renderpass_id;
    u32 stages;
    b8 use_instances;
    b8 use_local;

    u8 attribute_count;
    shader_attribute_config* attributes;

    u8 uniform_count;
    shader_uniform_config* uniforms;

    char* renderpass_name;

    u8 stage_count;
    char** stage_names;
    u8 stage_filename_count;
    char** stage_filenames;
} shader_config;