#pragma once

#include "defines.h"
#include "graphs/hierarchy_graph.h"
#include "identifiers/khandle.h"
#include "math/math_types.h"
#include "resources/debug/debug_grid.h"
#include "resources/resource_types.h"

struct frame_data;
struct render_packet;
struct directional_light;
struct point_light;
struct mesh;
struct skybox;
struct water_plane;
struct geometry_config;
struct camera;
struct scene_config;
struct terrain;
struct ray;
struct raycast_result;
struct transform;
struct viewport;
struct geometry_render_data;

typedef enum scene_state {
    /** @brief created, but nothing more. */
    SCENE_STATE_UNINITIALIZED,
    /** @brief Configuration parsed, not yet loaded hierarchy setup. */
    SCENE_STATE_INITIALIZED,
    /** @brief In the process of loading the hierarchy. */
    SCENE_STATE_LOADING,
    /** @brief Everything is loaded, ready to play. */
    SCENE_STATE_LOADED,
    /** @brief In the process of unloading, not ready to play. */
    SCENE_STATE_UNLOADING,
    /** @brief Unloaded and ready to be destroyed.*/
    SCENE_STATE_UNLOADED
} scene_state;

typedef struct scene_attachment {
    scene_node_attachment_type attachment_type;
    // Handle into the hierarchy graph.
    k_handle hierarchy_node_handle;
    // A handle indexing into the resource array of the given type (i.e. meshes).
    k_handle resource_handle;
} scene_attachment;

typedef enum scene_flag {
    SCENE_FLAG_NONE = 0,
    /* @brief Indicates if the scene can be saved once modified
     * (i.e. read-only would be used for runtime, writing would
     * be used in editor, etc.)
     */
    SCENE_FLAG_READONLY = 1
} scene_flag;

// Bitwise flags to be used on scene load, etc.
typedef u32 scene_flags;

typedef struct scene_node_metadata {
    // Metadata considered stale/non-existant if INVALID_ID
    u32 id;

    // The name of the node.
    const char* name;
} scene_node_metadata;

typedef struct scene_static_mesh_metadata {
    const char* resource_name;
} scene_static_mesh_metadata;

typedef struct scene_terrain_metadata {
    const char* name;
    const char* resource_name;
} scene_terrain_metadata;

typedef struct scene_skybox_metadata {
    const char* cubemap_name;
} scene_skybox_metadata;

typedef struct scene_water_plane_metadata {
    u32 reserved;
} scene_water_plane_metadata;

typedef struct scene {
    u32 id;
    scene_flags flags;

    scene_state state;
    b8 enabled;

    char* name;
    char* description;
    char* resource_name;
    char* resource_full_path;

    // darray of directional lights.
    struct directional_light* dir_lights;
    // Array of scene attachments for directional lights.
    scene_attachment* directional_light_attachments;

    // darray of point lights.
    struct point_light* point_lights;
    // Array of scene attachments for point lights.
    scene_attachment* point_light_attachments;

    // darray of meshes.
    struct mesh* meshes;
    // Array of scene attachments for meshes.
    scene_attachment* mesh_attachments;
    // Array of mesh metadata.
    scene_static_mesh_metadata* mesh_metadata;

    // darray of terrains.
    struct terrain* terrains;
    // Array of scene attachments for terrains.
    scene_attachment* terrain_attachments;
    // Array of terrain metadata.
    scene_terrain_metadata* terrain_metadata;

    // darray of skyboxes.
    struct skybox* skyboxes;
    // Array of scene attachments for skyboxes.
    scene_attachment* skybox_attachments;
    // Array of skybox metadata.
    scene_skybox_metadata* skybox_metadata;

    // darray of water planes.
    struct water_plane* water_planes;
    // Array of scene attachments for water planes.
    scene_attachment* water_plane_attachments;
    // Array of water plane metadata.
    scene_water_plane_metadata* water_plane_metadata;

    // A grid for the scene.
    debug_grid grid;

    // A pointer to the scene configuration, if provided.
    struct scene_config* config;

    hierarchy_graph hierarchy;

    // An array of node metadata, indexed by hierarchy graph handle.
    // Marked as unused by id == INVALID_ID
    // Size of this array is always highest id+1. Does not shrink on node destruction.
    scene_node_metadata* node_metadata;

    // The number of node_metadatas currently allocated.
    u32 node_metadata_count;

} scene;

/**
 * @brief Creates a new scene with the given config with default values.
 * No resources are allocated. Config is not yet processed.
 *
 * @param config A pointer to the configuration. Optional.
 * @param flags Flags to be used during creation (i.e. read-only, etc.).
 * @param out_scene A pointer to hold the newly created scene. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_create(scene_config* config, scene_flags flags, scene* out_scene);

/**
 * @brief Performs initialization routines on the scene, including processing
 * configuration (if provided) and scaffolding heirarchy.
 *
 * @param scene A pointer to the scene to be initialized.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_initialize(scene* scene);

/**
 * @brief Performs loading routines and resource allocation on the given scene.
 *
 * @param scene A pointer to the scene to be loaded.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_load(scene* scene);

/**
 * @brief Performs unloading routines and resource de-allocation on the given scene.
 * A scene is also destroyed when unloading.
 *
 * @param scene A pointer to the scene to be unloaded.
 * @param immediate Unload immediately instead of the next frame. NOTE: can have unintended side effects if used improperly.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_unload(scene* scene, b8 immediate);

/**
 * @brief Performs any required scene updates for the given frame.
 *
 * @param scene A pointer to the scene to be updated.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_update(scene* scene, const struct frame_data* p_frame_data);

KAPI void scene_render_frame_prepare(scene* scene, const struct frame_data* p_frame_data);

/**
 * @brief Updates LODs of items in the scene based on the given position and clipping distances.
 *
 * @param scene A pointer to the scene to be updated.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @param view_position The view position to use for LOD calculation.
 * @param near_clip The near clipping distance from the view position.
 * @param far_clip The far clipping distance from the view position.
 */
KAPI void scene_update_lod_from_view_position(scene* scene, const struct frame_data* p_frame_data, vec3 view_position, f32 near_clip, f32 far_clip);

KAPI b8 scene_raycast(scene* scene, const struct ray* r, struct raycast_result* out_result);

KAPI b8 scene_debug_render_data_query(scene* scene, u32* data_count, struct geometry_render_data** debug_geometries);

KAPI b8 scene_mesh_render_data_query(const scene* scene, const frustum* f, vec3 center, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries);
KAPI b8 scene_mesh_render_data_query_from_line(const scene* scene, vec3 direction, vec3 center, f32 radius, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries);

KAPI b8 scene_terrain_render_data_query(const scene* scene, const frustum* f, vec3 center, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_terrain_geometries);
KAPI b8 scene_terrain_render_data_query_from_line(const scene* scene, vec3 direction, vec3 center, f32 radius, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data** out_geometries);

KAPI b8 scene_water_plane_query(const scene* scene, const frustum* f, vec3 center, struct frame_data* p_frame_data, u32* out_count, struct water_plane*** out_water_planes);

KAPI b8 scene_save(scene* s);

/**
 * @brief Attempts to parse a xform config (_NOT_ an actual xform) from the provided string.
 * If the string contains 10 elements, rotation is parsed as quaternion.
 * If it contains 9 elements, rotation is parsed as euler angles and is
 * converted to quaternion. Anything else is invalid.
 *
 * @param str The string to parse from.
 * @param out_xform A pointer to the xform to write to.
 * @return True if parsed successfully, otherwise false.
 */
KAPI b8 string_to_scene_xform_config(const char* str, struct scene_xform_config* out_xform);
