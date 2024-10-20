#include "kactor.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include <debug/kassert.h>
#include <logger.h>
#include <memory/kmemory.h>

typedef struct kactor_staticmesh_system_state {
    u32 max_components;
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
    state->max_components = config->max_components;
    state->geometries = state_block + (sizeof(kactor_staticmesh_system_state));
    state->materials = (kresource_material_instance*)(((u8*)state->geometries) + (sizeof(geometry) * config->max_components));

    // Invalidate all entries in the system.
    for (u32 i = 0; i < state->max_components; ++i) {
        state->geometries[i].id = INVALID_ID;
        state->materials[i].per_draw_id = INVALID_ID;
    }

    return true;
}

void kactor_comp_staticmesh_system_shutdown(struct kactor_staticmesh_system_state* state) {
    if (state) {
        // TODO: things and stuff.
        kzero_memory(state, sizeof(kactor_staticmesh_system_state));
    }
}

static u32 get_free_index(struct kactor_staticmesh_system_state* state) {
    if (!state) {
        return INVALID_ID;
    }
    for (u32 i = 0; i < state->max_components; ++i) {
        if (state->materials[i].per_draw_id == INVALID_ID && state->geometries[i].id == INVALID_ID) {
            return i;
        }
    }

    KERROR("Failed to find free slot for static mesh load. Increase system config->max_components. Current=%u", state->max_components);
    return INVALID_ID;
}

u32 kactor_comp_staticmesh_create(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name, geometry g, kresource_material_instance material) {
    u32 index = get_free_index(state);
    if (index != INVALID_ID) {
        state->geometries[index] = g;
        state->materials[index] = material;
    }

    return index;
}

u32 kactor_comp_staticmesh_get_id(struct kactor_staticmesh_system_state* state, u64 actor_id, kname name) {
}
void kactor_comp_staticmesh_destroy(struct kactor_staticmesh_system_state* state, u32 id) {
}
b8 kactor_comp_staticmesh_load(u32 actor_id) {
}
b8 kactor_comp_staticmesh_unload(u32 actor_id) {
}

geometry* kactor_comp_staticmesh_get_geometry(struct kactor_staticmesh_system_state* state, u32 id) {
    if (!state || id == INVALID_ID) {
        return 0;
    }

    return &state->geometries[id];
}

kresource_material_instance* kactor_comp_staticmesh_get_material(struct kactor_staticmesh_system_state* state, u32 id) {
    if (!state || id == INVALID_ID) {
        return 0;
    }

    return &state->materials[id];
}

b8 kactor_comp_staticmesh_get_geometry_material(struct kactor_staticmesh_system_state* state, u32 id, geometry** out_geometry, kresource_material_instance** out_material) {
    if (!state || id == INVALID_ID) {
        return false;
    }

    *out_material = &state->materials[id];
    *out_geometry = &state->geometries[id];

    return true;
}

b8 kactor_comp_staticmesh_get_render_data(struct kactor_staticmesh_system_state* state, u32 id, struct kactor_comp_staticmesh_render_data* out_render_data) {
    if (!state || !out_render_data || id == INVALID_ID) {
        return false;
    }

    out_render_data->material = &state->materials[id];

    geometry* g = &state->geometries[id];

    // NOTE: this just fills out the data known by the actor itself. There may be other
    // fields known by other systems to fill out the rest (i.e. a scene).
    out_render_data->vertex_count = g->vertex_count;
    out_render_data->vertex_buffer_offset = g->vertex_buffer_offset;
    out_render_data->index_count = g->index_count;
    out_render_data->index_buffer_offset = g->index_buffer_offset;

    return true;
}
