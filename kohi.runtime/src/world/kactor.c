#include "kactor.h"

#include <debug/kassert.h>
#include <defines.h>
#include <kresources/kresource_types.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <strings/kname.h>

typedef struct kactor_staticmesh_comp_system_state {
    u32 max_components;
    // Owning actor ids.
    u64* actor_ids;
    // Array of static mesh instances.
    static_mesh_instance* mesh_instances;
    kname* names;
    vec4* tints;
    kname* resource_names;
} kactor_staticmesh_comp_system_state;

b8 kactor_comp_staticmesh_system_initialize(u64* memory_requirement, void* state_block, const kactor_staticmesh_system_config* config) {
    KASSERT_MSG(memory_requirement, "kactor_comp_staticmesh_system_initialize requires a valid pointer to memory_requirement.");

    *memory_requirement = sizeof(kactor_staticmesh_comp_system_state) +
                          (sizeof(u64) * config->max_components) +
                          (sizeof(static_mesh_instance) * config->max_components) +
                          (sizeof(kname) * config->max_components) +
                          (sizeof(vec4) * config->max_components) +
                          (sizeof(kname) * config->max_components);

    if (!state_block) {
        return true;
    }

    kactor_staticmesh_comp_system_state* state = (kactor_staticmesh_comp_system_state*)state_block;
    state->max_components = config->max_components;
    state->actor_ids = state_block + (sizeof(kactor_staticmesh_comp_system_state));
    state->mesh_instances = (static_mesh_instance*)(((u8*)state->actor_ids) + (sizeof(state->actor_ids[0]) * config->max_components));
    state->names = (kname*)(((u8*)state->mesh_instances) + (sizeof(state->mesh_instances[0]) * config->max_components));
    state->tints = (vec4*)(((u8*)state->names) + (sizeof(state->names[0]) * config->max_components));
    state->resource_names = (kname*)(((u8*)state->tints) + (sizeof(state->tints[0]) * config->max_components));

    // Invalidate all entries in the system.
    for (u32 i = 0; i < state->max_components; ++i) {
        state->actor_ids[i] = INVALID_ID_U64;
        state->mesh_instances[i].material_instances = 0;
        state->mesh_instances[i].instance_id = INVALID_ID_U64;
        state->mesh_instances[i].mesh_resource = 0;
        state->names[i] = INVALID_KNAME;
        state->resource_names[i] = INVALID_KNAME;
        state->tints[i] = vec4_one(); // default tint is white
    }

    return true;
}

void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_comp_system_state* state) {
    if (state) {
        // TODO: things and stuff.
        kzero_memory(state, sizeof(kactor_staticmesh_comp_system_state));
    }
}

static u32 get_free_index(struct kactor_staticmesh_comp_system_state* state) {
    if (!state) {
        return INVALID_ID;
    }
    for (u32 i = 0; i < state->max_components; ++i) {
        if (!state->mesh_instances[i].mesh_resource) {
            return i;
        }
    }

    KERROR("Failed to find free slot for static mesh load. Increase system config->max_components. Current=%u", state->max_components);
    return INVALID_ID;
}

u32 kactor_comp_staticmesh_create(struct kactor_staticmesh_comp_system_state* state, u64 actor_id, kname name, kname mesh_resource_name) {
    u32 index = get_free_index(state);
    if (index != INVALID_ID) {
        state->resource_names[index] = mesh_resource_name;
        state->tints[index] = vec4_one(); // default to white.
        state->actor_ids[index] = actor_id;
        state->names[index] = name;
    }

    return index;
}

u32 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_comp_system_state* state, u64 actor_id, kname name) {
    KASSERT_MSG(state, "state is required");
    if (actor_id == INVALID_ID_U64) {
        KERROR("Cannot get the id of a static mesh with an invalid actor id. INVALID_ID will be returned.");
        return INVALID_ID;
    }
    if (name == INVALID_KNAME) {
        KERROR("Cannot get the id of a static mesh by name when the name is invalid, ya dingus!");
        return INVALID_ID;
    }

    // NOTE: There might be a quicker way to do this, but generally these lookups probably shouldn't constantly be done anyway.
    for (u32 i = 0; i < state->max_components; ++i) {
        if (state->names[i] == name) {
            return i;
        }
    }
    return INVALID_ID;
}

kname kactor_comp_staticmesh_name_get(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    if (!state || comp_id == INVALID_ID) {
        return INVALID_KNAME;
    }

    KASSERT_MSG(comp_id < state->max_components, "kactor_comp_staticmesh_name_get - comp_id out of range");
    return state->names[comp_id];
}

b8 kactor_comp_staticmesh_name_set(struct kactor_staticmesh_comp_system_state* state, u32 comp_id, kname name) {
    if (!state || comp_id == INVALID_ID || name == INVALID_KNAME) {
        return false;
    }

    KASSERT_MSG(comp_id < state->max_components, "kactor_comp_staticmesh_name_set - comp_id out of range");
    state->names[comp_id] = name;
    return true;
}

vec4 kactor_comp_staticmesh_tint_get(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    if (!state || comp_id == INVALID_ID) {
        return vec4_one();
    }

    KASSERT_MSG(comp_id < state->max_components, "kactor_comp_staticmesh_tint_get - comp_id out of range");
    return state->tints[comp_id];
}

b8 kactor_comp_staticmesh_tint_set(struct kactor_staticmesh_comp_system_state* state, u32 comp_id, vec4 tint) {
    if (!state || comp_id == INVALID_ID) {
        return false;
    }

    KASSERT_MSG(comp_id < state->max_components, "kactor_comp_staticmesh_tint_set - comp_id out of range");
    state->tints[comp_id] = tint;
    return true;
}

void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    KASSERT_MSG(false, "Not yet implemented");
    // TODO: release resources, then free up "slot" in system.
}

b8 kactor_comp_staticmesh_load(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    if (!state || comp_id == INVALID_ID) {
        return false;
    }

    // TODO: Reach out to the static mesh system to get a static_mesh_instance
    KASSERT_MSG(false, "Not yet implemented");

    return true;
}

b8 kactor_comp_staticmesh_unload(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    KASSERT_MSG(false, "Not yet implemented");
    return false;
}

static_mesh_instance* kactor_comp_staticmesh_get_mesh_instance(struct kactor_staticmesh_comp_system_state* state, u32 comp_id) {
    if (!state || comp_id == INVALID_ID) {
        return 0;
    }

    return &state->mesh_instances[comp_id];
}
