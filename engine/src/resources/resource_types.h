#pragma once

#include "math/math_types.h"

#define TEXTURE_NAME_MAX_LENGTH 512

typedef struct texture {
    u32 id;
    u32 width;
    u32 height;
    u8 channel_count;
    b8 has_transparency;
    u32 generation;
    char name[TEXTURE_NAME_MAX_LENGTH];
    void* internal_data;
} texture;

typedef enum texture_use {
    TEXTURE_USE_UNKNOWN = 0x00,
    TEXTURE_USE_MAP_DIFFUSE = 0x01
} texture_use;

typedef struct texture_map {
    texture* texture;
    texture_use use;
} texture_map;

#define MATERIAL_NAME_MAX_LENGTH 256
typedef struct material {
    u32 id;
    u32 generation;
    u32 internal_id;
    char name[MATERIAL_NAME_MAX_LENGTH];
    vec4 diffuse_colour;
    texture_map diffuse_map;
} material;

#define GEOMETRY_NAME_MAX_LENGTH 256

/**
 * @brief Represents actual geometry in the world.
 * Typically (but not always, depending on use) paired with a material.
 */
typedef struct geometry {
    u32 id;
    u32 internal_id;
    u32 generation;
    char name[GEOMETRY_NAME_MAX_LENGTH];
    material* material;
} geometry;
