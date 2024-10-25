#pragma once

#include <strings/kname.h>

#include "kresources/kresource_types.h"
#include "systems/static_mesh_system.h"

/**
 * An actor is an in-world representation of something which exists in or can be spawned in
 * the world. It may contain actor-components, which can be used to control how actors are rendered,
 * move about in the world, sound, etc. Each actor-component typically has reference to at least one resource, which
 * is generally what gets rendered (i.e. a static mesh resource), but not always (i.e. a sound effect).
 *
 * When used with a scene, these may be parented to one another via the scene's hierarchy view and
 * xform graph, when attached to a scene node.
 */
typedef struct kactor {
    u64 id;
    kname name;
    k_handle xform;
} kactor;

// staticmesh system
struct kactor_staticmesh_comp_system_state;

typedef struct kactor_staticmesh_system_config {
    // The max number of static mesh actor components that can be loaded at any one time.
    u32 max_components;
} kactor_staticmesh_system_config;

KAPI b8 kactor_comp_staticmesh_system_initialize(u64* memory_requirement, void* state, const kactor_staticmesh_system_config* config);
KAPI void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_comp_system_state* state);

KAPI u32 kactor_comp_staticmesh_create(struct kactor_staticmesh_comp_system_state* state, u64 actor_id, kname name, kname static_mesh_resource_name);
KAPI u32 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_comp_system_state* state, u64 actor_id, kname name);

/**
 * @brief Attempts to get the name of a static mesh component with the given id.
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param comp_id The component identifier.
 * @returns The component name on success; otherwise INVALID_KNAME.
 */
KAPI kname kactor_comp_staticmesh_name_get(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);

/**
 * Attempts to set the name of a static mesh component with the given id.
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param comp_id The component identifier.
 * @param name The name to be set.
 * @returns True on success; otherwise false.
 */
KAPI b8 kactor_comp_staticmesh_name_set(struct kactor_staticmesh_comp_system_state* state, u32 comp_id, kname name);

/**
 * @brief Attempts to get the tint of a static mesh component with the given id.
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param comp_id The component identifier.
 * @returns The component tint on success; otherwise a default of vec4_one (white).
 */
KAPI vec4 kactor_comp_staticmesh_tint_get(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);

/**
 * Attempts to set the tint of a static mesh component with the given id.
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param comp_id The component identifier.
 * @param name The tint to be set.
 * @returns True on success; otherwise false.
 */
KAPI b8 kactor_comp_staticmesh_tint_set(struct kactor_staticmesh_comp_system_state* state, u32 comp_id, vec4 tint);

/**
 * @brief Obtains a list of static mesh component ids for a given actor.
 *
 * NOTE: This function is designed to be called twice; once to obtain a count (passing 0/null to out_ids) and a
 * second time, passing allocated memory to hold the count of ids * sizeof(u32).
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param actor_id The identifier of the actor from which to obtain ids.
 * @param out_count A pointer to hold the number of ids of static meshes owned by the given actor.
 * @param out_comp_ids An array of u32s large enough to hold the count of actors obtained via the first call to this function.
 * @returns True if the provided actor is valid and contains static meshes; otherwise false.
 */
KAPI b8 kactor_comp_staticmesh_get_ids_for_actor(struct kactor_staticmesh_comp_system_state* state, u64 actor_id, u32* out_count, u32* out_comp_ids);

/**
 * @brief Destroys the actor with the given identifier.
 *
 * @param state A pointer to the kactor static mesh component system state.
 * @param comp_id The identifier of the component to destroy.
 */
KAPI void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);

KAPI b8 kactor_comp_staticmesh_load(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);
KAPI b8 kactor_comp_staticmesh_unload(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);

KAPI static_mesh_instance* kactor_comp_staticmesh_get_mesh_instance(struct kactor_staticmesh_comp_system_state* state, u32 comp_id);
