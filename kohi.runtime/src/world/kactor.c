#include "kactor.h"
#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include <debug/kassert.h>

typedef struct kactor_staticmesh_system_state {
    geometry* geometries;
    kresource_material_instance* materials;
} kactor_staticmesh_system_state;

b8 kactor_comp_staticmesh_system_initialize(u64* memory_requirement, void* state_block, const kactor_staticmesh_system_config* config) {
    KASSERT_MSG(memory_requirement, "kactor_comp_staticmesh_system_initialize requires a valid pointer to memory_requirement.");

    *memory_requirement = sizeof(kactor_staticmesh_system_state) + (sizeof(geometry) * config->max_components) + (sizeof(kresource_material_instance) * config->max_components);

    if (!state_block) {
        return true;
    }

    kactor_staticmesh_system_state* state = (kactor_staticmesh_system_state*)state_block;
    state->geometries = state_block + (sizeof(kactor_staticmesh_system_state));
    state->materials = (kresource_material_instance*)(((u8*)state->geometries) + (sizeof(geometry) * config->max_components));

    return true;
}

void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_system_state* state) {
    if (state) {
        // TODO: things and stuff.
    }
}

u64 kactor_comp_staticmesh_create(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name, geometry g, kresource_material_instance material) {
}
u64 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name) {
}
void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_system_state* state, u64 id) {
}

b8 kactor_comp_staticmesh_get_geometry(struct kactor_staticmesh_system_state* state, u64 id, geometry* out_geometry) {
}
b8 kactor_comp_staticmesh_get_material(struct kactor_staticmesh_system_state* state, u64 id, kresource_material_instance* out_material) {
}
b8 kactor_comp_staticmesh_get_geometry_material(struct kactor_staticmesh_system_state* state, u64 id, geometry* out_geometry, kresource_material_instance* out_material) {
}
