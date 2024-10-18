#pragma once

#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "strings/kname.h"

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
struct kactor_staticmesh_system_state;

typedef struct kactor_staticmesh_system_config {
    u32 max_components;
} kactor_staticmesh_system_config;

KAPI b8 kactor_comp_staticmesh_system_initialize(u64* memory_requirement, void* state, const kactor_staticmesh_system_config* config);
KAPI void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_system_state* state);

KAPI u64 kactor_comp_staticmesh_create(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name, geometry g, kresource_material_instance material);
KAPI u64 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name);
KAPI void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_system_state* state, u64 id);

KAPI b8 kactor_comp_staticmesh_get_geometry(struct kactor_staticmesh_system_state* state, u64 id, geometry* out_geometry);
KAPI b8 kactor_comp_staticmesh_get_material(struct kactor_staticmesh_system_state* state, u64 id, kresource_material_instance* out_material);
KAPI b8 kactor_comp_staticmesh_get_geometry_material(struct kactor_staticmesh_system_state* state, u64 id, geometry* out_geometry, kresource_material_instance* out_material);
