#pragma once

#include "kresources/kresource_types.h"
#include "math/geometry.h"

typedef struct skybox_config {
    /** @brief The name of the cubemap to be used for the skybox. */
    const char* cubemap_name;
} skybox_config;

typedef enum skybox_state {
    SKYBOX_STATE_UNDEFINED,
    SKYBOX_STATE_CREATED,
    SKYBOX_STATE_INITIALIZED,
    SKYBOX_STATE_LOADING,
    SKYBOX_STATE_LOADED
} skybox_state;

typedef struct skybox {
    skybox_state state;

    kname cubemap_name;
    kresource_texture* cubemap;

    kgeometry geometry;
    u32 group_id;
    /** @brief The skybox shader's group data generation. */
    u16 skybox_shader_group_data_generation;

    u32 draw_id;
    /** @brief The skybox shader's draw data generation. */
    u16 skybox_shader_draw_data_generation;
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
