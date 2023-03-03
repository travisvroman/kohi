#pragma once

#include "math/math_types.h"
#include "resources/resource_types.h"
#include "renderer/renderer_types.inl"

typedef struct skybox_config {
    /** @brief The name of the cubemap to be used for the skybox. */
    const char* cubemap_name;
    geometry_config g_config;
} skybox_config;

typedef struct skybox {
    skybox_config config;
    texture_map cubemap;
    geometry* g;
    u32 instance_id;
    /** @brief Synced to the renderer's current frame number when the material has been applied that frame. */
    u64 render_frame_number;
} skybox;

/**
 * @brief Attempts to create a skybox using the specified parameters.
 * 
 * @param config The configuration to be used when creating the skybox.
 * @param out_skybox A pointer to hold the newly-created skybox.
 * @return True on success; otherwise false.
 */
KAPI b8 skybox_create(skybox_config config, skybox* out_skybox);

KAPI b8 skybox_initialize(skybox* sb);

KAPI b8 skybox_load(skybox* sb);

KAPI b8 skybox_unload(skybox* sb);

/**
 * @brief Destroys the provided skybox.
 * 
 * @param sb A pointer to the skybox to be destroyed.
 */
KAPI void skybox_destroy(skybox* sb);
