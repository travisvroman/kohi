#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "resources/debug/debug_grid.h"

struct frame_data;
struct render_packet;
struct directional_light;
struct point_light;
struct mesh;
struct skybox;
struct geometry_config;
struct camera;
struct simple_scene_config;
struct terrain;
struct ray;
struct raycast_result;
struct transform;
struct viewport;
struct geometry_render_data;

typedef enum simple_scene_state {
    /** @brief created, but nothing more. */
    SIMPLE_SCENE_STATE_UNINITIALIZED,
    /** @brief Configuration parsed, not yet loaded hierarchy setup. */
    SIMPLE_SCENE_STATE_INITIALIZED,
    /** @brief In the process of loading the hierarchy. */
    SIMPLE_SCENE_STATE_LOADING,
    /** @brief Everything is loaded, ready to play. */
    SIMPLE_SCENE_STATE_LOADED,
    /** @brief In the process of unloading, not ready to play. */
    SIMPLE_SCENE_STATE_UNLOADING,
    /** @brief Unloaded and ready to be destroyed.*/
    SIMPLE_SCENE_STATE_UNLOADED
} simple_scene_state;

typedef struct pending_mesh {
    struct mesh* m;

    const char* mesh_resource_name;

    u32 geometry_config_count;
    struct geometry_config** g_configs;
} pending_mesh;

typedef struct simple_scene {
    u32 id;
    simple_scene_state state;
    b8 enabled;

    char* name;
    char* description;

    transform scene_transform;

    // Singlular pointer to a directional light.
    struct directional_light* dir_light;

    // darray of point lights.
    struct point_light* point_lights;

    // darray of meshes.
    struct mesh* meshes;

    // darray of terrains.
    struct terrain* terrains;

    // darray of meshes to be loaded.`
    pending_mesh* pending_meshes;

    // Singlular pointer to a skybox.
    struct skybox* sb;

    // A grid for the scene.
    debug_grid grid;

    // A pointer to the scene configuration, if provided.
    struct simple_scene_config* config;

} simple_scene;

/**
 * @brief Creates a new scene with the given config with default values.
 * No resources are allocated. Config is not yet processed.
 *
 * @param config A pointer to the configuration. Optional.
 * @param out_scene A pointer to hold the newly created scene. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_create(void* config, simple_scene* out_scene);

/**
 * @brief Performs initialization routines on the scene, including processing
 * configuration (if provided) and scaffolding heirarchy.
 *
 * @param scene A pointer to the scene to be initialized.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_initialize(simple_scene* scene);

/**
 * @brief Performs loading routines and resource allocation on the given scene.
 *
 * @param scene A pointer to the scene to be loaded.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_load(simple_scene* scene);

/**
 * @brief Performs unloading routines and resource de-allocation on the given scene.
 * A scene is also destroyed when unloading.
 *
 * @param scene A pointer to the scene to be unloaded.
 * @param immediate Unload immediately instead of the next frame. NOTE: can have unintended side effects if used improperly.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_unload(simple_scene* scene, b8 immediate);

/**
 * @brief Performs any required scene updates for the given frame.
 *
 * @param scene A pointer to the scene to be updated.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_update(simple_scene* scene, const struct frame_data* p_frame_data);

/**
 * @brief Populate the given render packet with data from the provided scene.
 *
 * @param scene A pointer to the scene to be updated.
 * @param current_camera The current camera to use while rendering the scene.
 * @param viewport A pointer to the viewport to be used when populating the render packets.
 * @param p_frame_data A constant pointer to the current frame's data.
 * @param packet A pointer to the packet to populate.
 * @return True on success; otherwise false.
 */
KAPI b8 simple_scene_populate_render_packet(simple_scene* scene, struct camera* current_camera, struct viewport* v, struct frame_data* p_frame_data, struct render_packet* packet);

KAPI b8 simple_scene_raycast(simple_scene* scene, const struct ray* r, struct raycast_result* out_result);

KAPI b8 simple_scene_directional_light_add(simple_scene* scene, const char* name, struct directional_light* light);

KAPI b8 simple_scene_point_light_add(simple_scene* scene, const char* name, struct point_light* light);

KAPI b8 simple_scene_mesh_add(simple_scene* scene, const char* name, struct mesh* m);

KAPI b8 simple_scene_skybox_add(simple_scene* scene, const char* name, struct skybox* sb);

KAPI b8 simple_scene_terrain_add(simple_scene* scene, const char* name, struct terrain* t);

KAPI b8 simple_scene_directional_light_remove(simple_scene* scene, const char* name);

KAPI b8 simple_scene_point_light_remove(simple_scene* scene, const char* name);

KAPI b8 simple_scene_mesh_remove(simple_scene* scene, const char* name);

KAPI b8 simple_scene_skybox_remove(simple_scene* scene, const char* name);

KAPI b8 simple_scene_terrain_remove(simple_scene* scene, const char* name);

KAPI struct directional_light* simple_scene_directional_light_get(simple_scene* scene, const char* name);

KAPI struct point_light* simple_scene_point_light_get(simple_scene* scene, const char* name);

KAPI struct mesh* simple_scene_mesh_get(simple_scene* scene, const char* name);

KAPI struct skybox* simple_scene_skybox_get(simple_scene* scene, const char* name);

KAPI struct terrain* simple_scene_terrain_get(simple_scene* scene, const char* name);

KAPI struct transform* simple_scene_transform_get_by_id(simple_scene* scene, u64 unique_id);

KAPI b8 simple_scene_debug_render_data_query(simple_scene* scene, u32* data_count, struct geometry_render_data** debug_geometries);

KAPI b8 simple_scene_mesh_render_data_query(const simple_scene* scene, const frustum* f, vec3 center, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data* out_geometries);
KAPI b8 simple_scene_mesh_render_data_query_from_line(const simple_scene* scene, vec3 direction, vec3 center, f32 radius, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data* out_geometries);

KAPI b8 simple_scene_terrain_render_data_query(const simple_scene* scene, const frustum* f, vec3 center, struct frame_data* p_frame_data, u32* out_count, struct geometry_render_data* out_terrain_geometries);
