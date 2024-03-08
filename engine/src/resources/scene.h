#pragma once

#include "core/khandle.h"
#include "defines.h"
#include "graphs/hierarchy_graph.h"
#include "math/math_types.h"
#include "resources/debug/debug_grid.h"
#include "resources/resource_types.h"

struct frame_data;
struct render_packet;
struct directional_light;
struct point_light;
struct mesh;
struct skybox;
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
    k_handle hierarchy_node_handle;
    k_handle resource_handle;
} scene_attachment;

typedef struct scene {
    u32 id;
    scene_state state;
    b8 enabled;

    char* name;
    char* description;

    scene_attachment* mesh_attachments;
    scene_attachment* terrain_attachments;
    scene_attachment* point_light_attachments;
    scene_attachment* directional_light_attachments;
    scene_attachment* skybox_attachments;

    // darray of directional lights.
    struct directional_light* dir_lights;
    // Indices into the attachment array for xform lookups.
    u32* directional_light_attachment_indices;

    // darray of point lights.
    struct point_light* point_lights;
    // Indices into the attachment array for xform lookups.
    u32* point_light_attachment_indices;

    // darray of meshes.
    struct mesh* meshes;
    // Indices into the attachment array for xform lookups.
    u32* mesh_attachment_indices;

    // darray of terrains.
    struct terrain* terrains;
    // Indices into the attachment array for xform lookups.
    u32* terrain_attachment_indices;

    // darray of skyboxes.
    struct skybox* skyboxes;
    // Indices into the attachment array for xform lookups.
    u32* skybox_attachment_indices;

    // A grid for the scene.
    debug_grid grid;

    // A pointer to the scene configuration, if provided.
    struct scene_config* config;

    hierarchy_graph hierarchy;

} scene;

/**
 * @brief Creates a new scene with the given config with default values.
 * No resources are allocated. Config is not yet processed.
 *
 * @param config A pointer to the configuration. Optional.
 * @param out_scene A pointer to hold the newly created scene. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 scene_create(void* config, scene* out_scene);

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
