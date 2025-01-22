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
