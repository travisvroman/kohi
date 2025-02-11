#pragma once

#include "containers/array.h"
#include "core_physics_types.h"
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
    SCENE_NODE_ATTACHMENT_TYPE_PHYSICS_BODY,
    SCENE_NODE_ATTACHMENT_TYPE_COUNT,
    SCENE_NODE_ATTACHMENT_TYPE_USER_DEFINED
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
    "physics_body"       // SCENE_NODE_ATTACHMENT_TYPE_PHYSICS_BODY,
};

// Ensure changes to scene attachment types break this if it isn't also updated.
STATIC_ASSERT(SCENE_NODE_ATTACHMENT_TYPE_COUNT == (sizeof(scene_node_attachment_type_strings) / sizeof(*scene_node_attachment_type_strings)), "Scene attachment type count does not match string lookup table count.");

//////////////////////

typedef struct scene_node_attachment_config {
    scene_node_attachment_type type;
    kname name;
} scene_node_attachment_config;

typedef struct scene_node_attachment_user_defined_config {
    scene_node_attachment_config base;
    const char* config_source;
} scene_node_attachment_user_defined_config;

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

typedef struct scene_node_attachment_physics_body_config {
    scene_node_attachment_config base;
    kphysics_shape_type shape_type;
    f32 mass;
    f32 inertia;
    vec3 extents;
    f32 radius;
    kname mesh_resource_name;
} scene_node_attachment_physics_body_config;

// NEW
// TODO: remove all old types

/**
 * @brief Represents the base configuration structure for a kscene attachment.
 */
typedef struct kscene_attachment_config {
    // The name of the attachment type. (i.e. kname_create("static_mesh"))
    kname type_name;
    // Name of the attachment
    kname name;
    // String representation of the config for the underlying type.
    const char* config;
} kscene_attachment_config;

/**
 *  @brief Represents the configuration for a scene node.
 */
typedef struct scene_node_config {
    /** @brief The name of node. */
    kname name;

    /** @brief The number of attachments for this node. */
    u32 attachment_count;
    /** @brief Array of generic scene attachment configs. */
    kscene_attachment_config* attachments;

    /** @brief The number of children within this node. */
    u32 child_count;
    /** @brief An array of of children within this node. */
    struct scene_node_config* children;
    // String representation of xform, processed by the scene when needed.
    const char* xform_source;
} scene_node_config;
