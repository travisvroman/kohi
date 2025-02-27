#pragma once

#include "containers/array.h"
#include "math/math_types.h"
#include "strings/kname.h"

typedef enum scene_node_attachment_type {
    SCENE_NODE_ATTACHMENT_TYPE_UNKNOWN,
    SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT,
    SCENE_NODE_ATTACHMENT_TYPE_AUDIO_EMITTER,
    SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN,
    SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE,
    SCENE_NODE_ATTACHMENT_TYPE_VOLUME,
    SCENE_NODE_ATTACHMENT_TYPE_COUNT
} scene_node_attachment_type;

static const char* scene_node_attachment_type_strings[SCENE_NODE_ATTACHMENT_TYPE_COUNT] = {
    "unknown",
    "skybox",            // SCENE_NODE_ATTACHMENT_TYPE_SKYBOX,
    "directional_light", // SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT,
    "point_light",       // SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT,
    "audio_emitter",     // SCENE_NODE_ATTACHMENT_TYPE_AUDIO_EMITTER,
    "static_mesh",       // SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH,
    "heightmap_terrain", // SCENE_NODE_ATTACHMENT_TYPE_STATIC_HEIGHTMAP_TERRAIN,
    "water_plane",       // SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE,
    "volume"             // SCENE_NODE_ATTACHMENT_TYPE_VOLUME,
};

// Ensure changes to scene attachment types break this if it isn't also updated.
STATIC_ASSERT(SCENE_NODE_ATTACHMENT_TYPE_COUNT == (sizeof(scene_node_attachment_type_strings) / sizeof(*scene_node_attachment_type_strings)), "Scene attachment type count does not match string lookup table count.");

//////////////////////

typedef struct scene_node_attachment_config {
    scene_node_attachment_type type;
    kname name;
} scene_node_attachment_config;

typedef struct scene_node_attachment_skybox_config {
    scene_node_attachment_config base;
    kname cubemap_image_asset_name;
    kname cubemap_image_asset_package_name;
} scene_node_attachment_skybox_config;

typedef struct scene_node_attachment_directional_light_config {
    scene_node_attachment_config base;
    vec4 colour;
    vec4 direction;
    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
} scene_node_attachment_directional_light_config;

typedef struct scene_node_attachment_point_light_config {
    scene_node_attachment_config base;
    vec4 colour;
    vec4 position;
    f32 constant_f;
    f32 linear;
    f32 quadratic;
} scene_node_attachment_point_light_config;

typedef struct scene_node_attachment_audio_emitter_config {
    scene_node_attachment_config base;
    b8 is_looping;
    f32 volume;
    f32 inner_radius;
    f32 outer_radius;
    f32 falloff;
    kname audio_resource_name;
    kname audio_resource_package_name;
    b8 is_streaming;
} scene_node_attachment_audio_emitter_config;

typedef struct scene_node_attachment_static_mesh_config {
    scene_node_attachment_config base;
    kname asset_name;
    kname package_name;
} scene_node_attachment_static_mesh_config;

typedef struct scene_node_attachment_heightmap_terrain_config {
    scene_node_attachment_config base;
    kname asset_name;
    kname package_name;
} scene_node_attachment_heightmap_terrain_config;

typedef struct scene_node_attachment_water_plane_config {
    scene_node_attachment_config base;
    // TODO: expand configurable properties.
} scene_node_attachment_water_plane_config;

typedef enum scene_volume_type {
    SCENE_VOLUME_TYPE_TRIGGER
} scene_volume_type;

typedef enum scene_volume_shape_type {
    SCENE_VOLUME_SHAPE_TYPE_SPHERE,
    SCENE_VOLUME_SHAPE_TYPE_RECTANGLE
} scene_volume_shape_type;

typedef struct scene_node_attachment_volume_config {
    scene_node_attachment_config base;

    scene_volume_type volume_type;
    scene_volume_shape_type shape_type;

    union {
        f32 radius;
        vec3 extents;
    } shape_config;

    const char* on_enter_command;
    const char* on_leave_command;
    const char* on_update_command;
} scene_node_attachment_volume_config;

/**
 *  @brief Represents the configuration for a scene node.
 */
typedef struct scene_node_config {
    /** @brief The name of node. */
    kname name;

    /** @brief Darray of skybox attachment configs. */
    scene_node_attachment_skybox_config* skybox_configs;
    /** @brief Darray of directional light attachment configs. */
    scene_node_attachment_directional_light_config* dir_light_configs;
    /** @brief Darray of point light attachment configs. */
    scene_node_attachment_point_light_config* point_light_configs;
    /** @brief Darray of audio emitter attachment configs. */
    scene_node_attachment_audio_emitter_config* audio_emitter_configs;
    /** @brief Darray of static mesh attachment configs. */
    scene_node_attachment_static_mesh_config* static_mesh_configs;
    /** @brief Darray of heightmap terrain attachment configs. */
    scene_node_attachment_heightmap_terrain_config* heightmap_terrain_configs;
    /** @brief Darray of water plane attachment configs. */
    scene_node_attachment_water_plane_config* water_plane_configs;
    /** @brief Darray of volume attachment configs. */
    scene_node_attachment_volume_config* volume_configs;

    /** @brief The number of children within this node. */
    u32 child_count;
    /** @brief An array of of children within this node. */
    struct scene_node_config* children;
    // String representation of xform, processed by the scene when needed.
    const char* xform_source;
} scene_node_config;
