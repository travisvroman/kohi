/**
 * @file asset_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains the implementation of the asset system, which
 * is responsible for managing the lifecycle of assets.
 *
 * @details
 * @version 1.0
 * @date 2024-07-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include <assets/kasset_types.h>
#include <strings/kname.h>

typedef struct asset_system_config {
    // The maximum number of assets which may be loaded at once.
    u32 max_asset_count;

    kname application_package_name;
    const char* application_package_name_str;
} asset_system_config;

struct asset_system_state;

/**
 * @brief Deserializes configuration for the asset system from the provided string.
 *
 * @param config_str The string to deserialize.
 * @param out_config A pointer to hold the deserialized config.
 * @return True on success; otherwise false.
 */
KAPI b8 asset_system_deserialize_config(const char* config_str, asset_system_config* out_config);

/**
 * @brief Initializes the asset system. Call twice; once to get the memory requirement (pass 0 to state and config) and a second
 * time passing along the state and config once allocated.
 *
 * @param memory_requirement A pointer to hold the numeric amount of bytes needed for the state. Required.
 * @param state A pointer to the state. Pass 0 when getting memory requirement, otherwise pass the block of allocated memory.
 * @param config A constant pointer to the configuration of the system. Ignored when getting memory requirement.
 * @return True on success; otherwise false.
 */
KAPI b8 asset_system_initialize(u64* memory_requirement, struct asset_system_state* state, const asset_system_config* config);

/**
 * @brief Shuts the system down.
 *
 * @param state A pointer to the state. Required.
 */
KAPI void asset_system_shutdown(struct asset_system_state* state);

// ////////////////////////////////////
// BINARY ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_binary_loaded_callback)(void* listener, kasset_binary* asset);
// async load from game package.
KAPI kasset_binary* asset_system_request_binary(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_binary_loaded_callback callback);
// sync load from game package.
KAPI kasset_binary* asset_system_request_binary_sync(struct asset_system_state* state, const char* name);
// async load from specific package.
KAPI kasset_binary* asset_system_request_binary_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_binary_loaded_callback callback);
// sync load from specific package.
KAPI kasset_binary* asset_system_request_binary_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_binary(struct asset_system_state* state, kasset_binary* asset);

// ////////////////////////////////////
// TEXT ASSETS
// ////////////////////////////////////

// sync load from game package.
KAPI kasset_text* asset_system_request_text_sync(struct asset_system_state* state, const char* name);
// sync load from specific package.
KAPI kasset_text* asset_system_request_text_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_text(struct asset_system_state* state, kasset_text* asset);

// ////////////////////////////////////
// IMAGE ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_image_loaded_callback)(void* listener, kasset_image* asset);
// async load from game package.
KAPI kasset_image* asset_system_request_image(struct asset_system_state* state, const char* name, b8 flip_y, void* listener, PFN_kasset_image_loaded_callback callback);
// sync load from game package.
KAPI kasset_image* asset_system_request_image_sync(struct asset_system_state* state, const char* name, b8 flip_y);
// async load from specific package.
KAPI kasset_image* asset_system_request_image_from_package(struct asset_system_state* state, const char* package_name, const char* name, b8 flip_y, void* listener, PFN_kasset_image_loaded_callback callback);
// sync load from specific package.
KAPI kasset_image* asset_system_request_image_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name, b8 flip_y);

KAPI void asset_system_release_image(struct asset_system_state* state, kasset_image* asset);

// ////////////////////////////////////
// BITMAP FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
KAPI kasset_bitmap_font* asset_system_request_bitmap_font_sync(struct asset_system_state* state, const char* name);
// sync load from specific package.
KAPI kasset_bitmap_font* asset_system_request_bitmap_font_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_bitmap_font(struct asset_system_state* state, kasset_bitmap_font* asset);

// ////////////////////////////////////
// SYSTEM FONT ASSETS
// ////////////////////////////////////

// sync load from game package.
KAPI kasset_system_font* asset_system_request_system_font_sync(struct asset_system_state* state, const char* name);

// sync load from specific package.
KAPI kasset_system_font* asset_system_request_system_font_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_system_font(struct asset_system_state* state, kasset_system_font* asset);

// ////////////////////////////////////
// STATIC MESH ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_static_mesh_loaded_callback)(void* listener, kasset_static_mesh* asset);

// async load from game package.
KAPI kasset_static_mesh* asset_system_request_static_mesh(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_static_mesh_loaded_callback callback);
// sync load from game package.
KAPI kasset_static_mesh* asset_system_request_static_mesh_sync(struct asset_system_state* state, const char* name);
// async load from specific package.
KAPI kasset_static_mesh* asset_system_request_static_mesh_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_static_mesh_loaded_callback callback);
// sync load from specific package.
KAPI kasset_static_mesh* asset_system_request_static_mesh_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_static_mesh(struct asset_system_state* state, kasset_static_mesh* asset);

// ////////////////////////////////////
// HEIGHTMAP TERRAIN ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_heightmap_terrain_loaded_callback)(void* listener, kasset_heightmap_terrain* asset);

// async load from game package.
KAPI kasset_heightmap_terrain* asset_system_request_heightmap_terrain(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_heightmap_terrain_loaded_callback callback);
// sync load from game package.
KAPI kasset_heightmap_terrain* asset_system_request_heightmap_terrain_sync(struct asset_system_state* state, const char* name);
// async load from specific package.
KAPI kasset_heightmap_terrain* asset_system_request_heightmap_terrain_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_heightmap_terrain_loaded_callback callback);
// sync load from specific package.
KAPI kasset_heightmap_terrain* asset_system_request_heightmap_terrain_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_heightmap_terrain(struct asset_system_state* state, kasset_heightmap_terrain* asset);

// ////////////////////////////////////
// MATERIAL ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_material_loaded_callback)(void* listener, kasset_material* asset);

// async load from game package.
KAPI kasset_material* asset_system_request_material(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_material_loaded_callback callback);
// sync load from game package.
KAPI kasset_material* asset_system_terrain_request_material_sync(struct asset_system_state* state, const char* name);
// async load from specific package.
KAPI kasset_material* asset_system_request_material_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_material_loaded_callback callback);
// sync load from specific package.
KAPI kasset_material* asset_system_request_material_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_material(struct asset_system_state* state, kasset_material* asset);

// ////////////////////////////////////
// AUDIO ASSETS
// ////////////////////////////////////

typedef void (*PFN_kasset_audio_loaded_callback)(void* listener, kasset_audio* asset);

// async load from game package.
KAPI kasset_audio* asset_system_request_audio(struct asset_system_state* state, const char* name, void* listener, PFN_kasset_audio_loaded_callback callback);
// sync load from game package.
KAPI kasset_audio* asset_system_terrain_request_audio_sync(struct asset_system_state* state, const char* name);
// async load from specific package.
KAPI kasset_audio* asset_system_request_audio_from_package(struct asset_system_state* state, const char* package_name, const char* name, void* listener, PFN_kasset_audio_loaded_callback callback);
// sync load from specific package.
KAPI kasset_audio* asset_system_request_audio_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_audio(struct asset_system_state* state, kasset_audio* asset);

// ////////////////////////////////////
// SCENE ASSETS
// ////////////////////////////////////

// sync load from game package.
KAPI kasset_scene* asset_system_terrain_request_scene_sync(struct asset_system_state* state, const char* name);
// sync load from specific package.
KAPI kasset_scene* asset_system_request_scene_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_scene(struct asset_system_state* state, kasset_scene* asset);

// ////////////////////////////////////
// SHADER ASSETS
// ////////////////////////////////////

// sync load from game package.
KAPI kasset_shader* asset_system_terrain_request_shader_sync(struct asset_system_state* state, const char* name);

// sync load from specific package.
KAPI kasset_shader* asset_system_request_shader_from_package_sync(struct asset_system_state* state, const char* package_name, const char* name);

KAPI void asset_system_release_shader(struct asset_system_state* state, kasset_shader* asset);
