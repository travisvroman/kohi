#include "static_mesh_system.h"

#include "assets/kasset_types.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/asset_system.h"
#include "systems/material_system.h"

typedef enum kstatic_mesh_state {
    KSTATIC_MESH_STATE_UNINITIALIZED,
    KSTATIC_MESH_STATE_LOADING,
    KSTATIC_MESH_STATE_LOADED
} kstatic_mesh_state;

typedef enum kstatic_mesh_instance_state {
    KSTATIC_MESH_INSTANCE_STATE_UNINITIALIZED,
    KSTATIC_MESH_INSTANCE_STATE_ACQUIRED
} kstatic_mesh_instance_state;

//
/**
 * Represents a single static mesh, which contains geometry.
 */
typedef struct submesh {
    /** @brief The geometry data for this mesh. */
    kgeometry geometry;
    /** @brief The name of the material associated with this mesh. */
    kname material_name;
} submesh;

typedef struct static_mesh_submesh_data {
    /** @brief The number of submeshes in this static mesh resource. */
    u16 submesh_count;
    /** @brief The array of submeshes in this static mesh resource. */
    submesh* submeshes;
} static_mesh_submesh_data;

typedef struct instance_data {
    /**
     * @brief An array of material instances associated with the submeshes.
     * Elements match up to mesh_resource->submeshes index-wise. Thus the
     * count of this array is the same as mesh_resource->submesh_count.
     */
    kmaterial_instance* material_instances;

    // Tint used for all submeshes.
    vec4 tint;
} instance_data;

// The collection of instances for a base mesh.
typedef struct base_mesh_instance_data {
    u16 max_instance_count;
    // One per instance of the base mesh. Indexed by the instance id.
    instance_data* instances;
    // State, indexed by instance id.
    kstatic_mesh_instance_state* states;
} base_mesh_instance_data;

typedef struct static_mesh_system_state {
    kname application_package_name;

    // The max number of entries in the below arrays. Can be increased.
    u16 max_mesh_count;
    // Indexed by kstatic_mesh id
    kname* names;
    // "KSTATIC_MESH_STATE_UNINITIALIZED" means this slot is unused.
    kstatic_mesh_state* states;
    static_mesh_submesh_data* submesh_datas;
    // Instances for the mesh, indexed by kstatic_mesh id.
    base_mesh_instance_data* base_instance_datas;
} static_mesh_system_state;

typedef struct kstatic_mesh_asset_load_listener {
    static_mesh_system_state* state;
    kstatic_mesh m;
} kstatic_mesh_asset_load_listener;

static void ensure_arrays_allocated(static_mesh_system_state* state, u32 new_count);
static void ensure_instance_arrays_allocated(base_mesh_instance_data* base_instance_data, u32 new_count);
static kstatic_mesh_instance issue_new_instance(static_mesh_system_state* state, kstatic_mesh m);
static void mesh_asset_loaded(void* listener, kasset_static_mesh* asset);
static void release_instance(static_mesh_system_state* state, kstatic_mesh m, u16 instance_id);
static void acquire_material_instances(static_mesh_system_state* state, kstatic_mesh m, u16 instance_id);

b8 static_mesh_system_initialize(u64* memory_requirement, struct static_mesh_system_state* state, static_mesh_system_config config) {
    if (!memory_requirement) {
        return false;
    }

    *memory_requirement = sizeof(static_mesh_system_state);

    if (!state) {
        return true;
    }

    state->application_package_name = config.application_package_name;

    // Setup data arrays.
    state->max_mesh_count = 0;
    u16 new_count = 64;
    ensure_arrays_allocated(state, new_count);
    state->max_mesh_count = new_count;

    KDEBUG("Static mesh system initialized.");

    return true;
}

void static_mesh_system_shutdown(struct static_mesh_system_state* state) {
    if (state) {
        // TODO: shut down.
    }
}

kstatic_mesh_instance static_mesh_instance_acquire(struct static_mesh_system_state* state, kname asset_name) {
    return static_mesh_instance_acquire_from_package(state, asset_name, state->application_package_name);
}

static kstatic_mesh_instance issue_new_instance(static_mesh_system_state* state, kstatic_mesh m) {
    // Search for empty slot, use it if found.
    base_mesh_instance_data* base = &state->base_instance_datas[m];

    u16 instance_id = INVALID_ID_U16;
    for (u16 i = 0; i < base->max_instance_count; ++i) {
        if (base->states[i] == KSTATIC_MESH_INSTANCE_STATE_UNINITIALIZED) {
            // Found a free slot, use it.
            instance_id = i;
            break;
        }
    }

    // If not found, ensure the instance arrays are allocated/grown.
    // Use the first entry in the newly-allocated space.
    if (instance_id == INVALID_ID_U16) {
        u16 new_count = (base->max_instance_count ? base->max_instance_count : 1) * 2;
        ensure_instance_arrays_allocated(base, new_count);

        instance_id = base->max_instance_count;

        base->max_instance_count = new_count;
    }

    base->states[instance_id] = KSTATIC_MESH_INSTANCE_STATE_ACQUIRED;

    // Actually setup the instance and return it.
    acquire_material_instances(state, m, instance_id);

    kstatic_mesh_instance inst = {
        .instance_id = instance_id,
        .mesh = m};
    return inst;
}

static void mesh_asset_loaded(void* listener, kasset_static_mesh* asset) {

    if (asset->geometry_count < 1) {
        KERROR("Provided static mesh asset has no geometries, thus there is nothing to be loaded.");
        return;
    }

    kstatic_mesh_asset_load_listener* typed_listener = listener;
    kstatic_mesh m = typed_listener->m;
    static_mesh_system_state* state = typed_listener->state;

    const engine_system_states* systems = engine_systems_get();

    // Upload to GPU, etc.
    renderbuffer* geometry_vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    renderbuffer* geometry_index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    // Process submeshes from asset.
    // TODO: A reloaded asset will need to free the old data first just before this.
    state->submesh_datas[m].submesh_count = asset->geometry_count;
    state->submesh_datas[m].submeshes = KALLOC_TYPE_CARRAY(submesh, asset->geometry_count);

    for (u32 i = 0; i < asset->geometry_count; ++i) {

        kasset_static_mesh_geometry* source_geometry = &asset->geometries[i];
        submesh* s = &state->submesh_datas[m].submeshes[i];
        s->material_name = source_geometry->material_asset_name;

        // Take a copy of the geometry data from the asset.
        kgeometry* submesh_geometry = &s->geometry;
        submesh_geometry->type = KGEOMETRY_TYPE_3D_STATIC;
        submesh_geometry->name = source_geometry->name;
        submesh_geometry->center = source_geometry->center;
        submesh_geometry->extents = source_geometry->extents;
        submesh_geometry->generation = INVALID_ID_U16; // TODO: A reupload won't do this.
        // Vertex data
        submesh_geometry->vertex_count = source_geometry->vertex_count;
        submesh_geometry->vertex_element_size = sizeof(vertex_3d);
        submesh_geometry->vertices = KALLOC_TYPE_CARRAY(vertex_3d, source_geometry->vertex_count);
        KCOPY_TYPE_CARRAY(submesh_geometry->vertices, source_geometry->vertices, vertex_3d, source_geometry->vertex_count);
        // Index data
        submesh_geometry->index_count = source_geometry->index_count;
        submesh_geometry->index_element_size = sizeof(u32);
        submesh_geometry->indices = KALLOC_TYPE_CARRAY(u32, source_geometry->index_count);
        KCOPY_TYPE_CARRAY(submesh_geometry->indices, source_geometry->indices, u32, source_geometry->index_count);

        // Upload geometry data.
        b8 is_reupload = submesh_geometry->generation != INVALID_ID_U16;
        u64 vertex_size = (u64)(sizeof(vertex_3d) * submesh_geometry->vertex_count);
        u64 vertex_offset = 0;
        u64 index_size = (u64)(sizeof(u32) * submesh_geometry->index_count);
        u64 index_offset = 0;

        // Vertex data.
        if (!is_reupload) {
            // Allocate space in the buffer.
            if (!renderer_renderbuffer_allocate(geometry_vertex_buffer, vertex_size, &submesh_geometry->vertex_buffer_offset)) {
                KERROR("static mesh system failed to allocate from the renderer's vertex buffer! Submesh geometry won't be uploaded (skipped)");
                continue;
            }
        }

        // Load the data.
        // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
        if (!renderer_renderbuffer_load_range(geometry_vertex_buffer, submesh_geometry->vertex_buffer_offset + vertex_offset, vertex_size, submesh_geometry->vertices + vertex_offset, false)) {
            KERROR("static mesh system failed to upload to the renderer vertex buffer!");
            if (!renderer_renderbuffer_free(geometry_vertex_buffer, vertex_size, submesh_geometry->vertex_buffer_offset)) {
                KERROR("Failed to recover from vertex write failure while freeing vertex buffer range.");
            }
            continue;
        }

        // Index data, if applicable
        if (index_size) {
            if (!is_reupload) {
                // Allocate space in the buffer.
                if (!renderer_renderbuffer_allocate(geometry_index_buffer, index_size, &submesh_geometry->index_buffer_offset)) {
                    KERROR("static mesh system failed to allocate from the renderer index buffer!");
                    // Free vertex data
                    if (!renderer_renderbuffer_free(geometry_vertex_buffer, vertex_size, submesh_geometry->vertex_buffer_offset)) {
                        KERROR("Failed to recover from index allocation failure while freeing vertex buffer range.");
                    }
                    continue;
                }
            }

            // Load the data.
            // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
            if (!renderer_renderbuffer_load_range(geometry_index_buffer, submesh_geometry->index_buffer_offset + index_offset, index_size, submesh_geometry->indices + index_offset, false)) {
                KERROR("static mesh system failed to upload to the renderer index buffer!");
                // Free vertex data
                if (!renderer_renderbuffer_free(geometry_vertex_buffer, vertex_size, submesh_geometry->vertex_buffer_offset)) {
                    KERROR("Failed to recover from index write failure while freeing vertex buffer range.");
                }
                // Free index data
                if (!renderer_renderbuffer_free(geometry_index_buffer, index_size, submesh_geometry->index_buffer_offset)) {
                    KERROR("Failed to recover from index write failure while freeing index buffer range.");
                }
                continue;
            }
        }

        submesh_geometry->generation++;
    }

    // Update the state.
    state->states[m] = KSTATIC_MESH_STATE_LOADED;

    // Get material instances for already-existing static mesh instances.
    for (u16 instance_id = 0; instance_id < state->base_instance_datas[m].max_instance_count; ++instance_id) {
        acquire_material_instances(state, m, instance_id);
    }

    // Release the asset.
    asset_system_release_static_mesh(systems->asset_state, asset);

    // Cleanup listener.
    KFREE_TYPE(typed_listener, kstatic_mesh_asset_load_listener, MEMORY_TAG_RESOURCE);
}

kstatic_mesh_instance static_mesh_instance_acquire_from_package(struct static_mesh_system_state* state, kname asset_name, kname package_name) {
    KASSERT_MSG(state, "State is required, ya dingus");

    const engine_system_states* systems = engine_systems_get();

    // Search for existing mesh by name.
    kstatic_mesh m = INVALID_KSTATIC_MESH;
    for (u16 i = 0; i < state->max_mesh_count; ++i) {
        if (state->names[i] == asset_name) {
            // Found a match on the name.
            m = i;

            // Issue new instance.
            return issue_new_instance(state, m);
        }
    }

    // Match by name not found, need to create/load new mesh.
    if (m == INVALID_KSTATIC_MESH) {
        // Find a 'free slot' and use it or expand the arrays and use the first available from the new array.
        for (u16 i = 0; i < state->max_mesh_count; ++i) {
            if (state->states[i] == KSTATIC_MESH_STATE_UNINITIALIZED) {
                // Found a free slot
                m = i;
                // immediately mark it as in-use.
                state->states[m] = KSTATIC_MESH_STATE_LOADING;

                // Setup instance array data.
                u16 new_count = 1;
                ensure_instance_arrays_allocated(&state->base_instance_datas[m], new_count);
                state->base_instance_datas->max_instance_count = new_count;

                break;
            }
        }
    }

    // No free slot was found, double array size and try again.
    if (m == INVALID_KSTATIC_MESH) {
        // FIXME: Make sure this doesn't overflow.
        u16 new_count = state->max_mesh_count * 2;
        ensure_arrays_allocated(state, new_count);
        // m is now at the end of the old array.
        m = state->max_mesh_count;

        // Increase the max count to match the new array size.
        state->max_mesh_count = new_count;
    }

    KASSERT_MSG(m != INVALID_KSTATIC_MESH, "Despite attempts, no static mesh could be matched or loaded. Check system logic.");

    // Issue a new instance.
    kstatic_mesh_instance new_inst = issue_new_instance(state, m);

    // Setup a listener.
    kstatic_mesh_asset_load_listener* listener = KALLOC_TYPE(kstatic_mesh_asset_load_listener, MEMORY_TAG_RESOURCE);
    listener->state = state;
    listener->m = m;

    // Request asset.
    kasset_static_mesh* asset = asset_system_request_static_mesh_from_package(
        systems->asset_state,
        kname_string_get(package_name),
        kname_string_get(asset_name),
        listener,
        mesh_asset_loaded);

    if (!asset) {
        KERROR("%s: Failed to request static mesh asset. See logs for details.", __FUNCTION__);
    }

    return new_inst;
}

void static_mesh_instance_release(struct static_mesh_system_state* state, kstatic_mesh_instance* instance) {
    release_instance(state, instance->mesh, instance->instance_id);
}

b8 static_mesh_is_loaded(struct static_mesh_system_state* state, kstatic_mesh m) {
    if (!state) {
        return false;
    }

    return state->states[m] == KSTATIC_MESH_STATE_LOADED;
}

b8 static_mesh_tint_get(struct static_mesh_system_state* state, kstatic_mesh_instance instance, vec4* out_tint) {
    if (!state || !out_tint) {
        return false;
    }

    *out_tint = state->base_instance_datas[instance.mesh].instances[instance.instance_id].tint;
    return true;
}

b8 static_mesh_tint_set(struct static_mesh_system_state* state, kstatic_mesh_instance instance, vec4 tint) {
    if (!state) {
        return false;
    }

    state->base_instance_datas[instance.mesh].instances[instance.instance_id].tint = tint;
    return true;
}

b8 static_mesh_extents_get(struct static_mesh_system_state* state, kstatic_mesh m, extents_3d* out_extents) {
    if (!state || !out_extents) {
        return false;
    }
    if (state->states[m] != KSTATIC_MESH_STATE_LOADED) {
        return false;
    }

    // FIXME: This just selects the first geometry's extents. Need to add extents to the whole
    // thing based on all submeshes.
    *out_extents = state->submesh_datas->submeshes[0].geometry.extents;

    return true;
}

b8 static_mesh_submesh_count_get(static_mesh_system_state* state, kstatic_mesh m, u16* out_count) {
    if (!state || !out_count || m == INVALID_KSTATIC_MESH) {
        return false;
    }

    *out_count = state->submesh_datas[m].submesh_count;
    return true;
}

const kgeometry* static_mesh_submesh_geometry_get_at(struct static_mesh_system_state* state, kstatic_mesh m, u16 index) {
    if (!state || m == INVALID_KSTATIC_MESH || index == INVALID_ID_U16) {
        return 0;
    }

    return &state->submesh_datas[m].submeshes[index].geometry;
}
const kmaterial_instance* static_mesh_submesh_material_instance_get_at(struct static_mesh_system_state* state, kstatic_mesh_instance instance, u16 index) {
    if (!state || instance.mesh == INVALID_KSTATIC_MESH || instance.instance_id == INVALID_ID_U16 || index == INVALID_ID_U16) {
        return 0;
    }

    return &state->base_instance_datas[instance.mesh].instances[instance.instance_id].material_instances[index];
}

b8 static_mesh_render_data_generate(struct static_mesh_system_state* state, kstatic_mesh_instance instance, kstatic_mesh_render_data_flag_bits flags, kstatic_mesh_render_data* out_render_data) {
    // Only loaded meshes.
    if (!state || !out_render_data || state->states[instance.mesh] != KSTATIC_MESH_STATE_LOADED) {
        return false;
    }
    static_mesh_submesh_data* submesh_data = &state->submesh_datas[instance.mesh];
    if (!submesh_data->submesh_count) {
        // Nothing to render.
        return false;
    }

    // TODO: instance tint
    out_render_data->tint = vec4_one(); // instance->tint;
    out_render_data->instance_id = instance.instance_id;
    out_render_data->submesh_count = submesh_data->submesh_count;
    // FIXME: Need a way to filter down this list by view frustum if we want that granular control.
    // For now though either every submesh gets rendered when this is called, or this isn't called and nothing is rendered.
    out_render_data->submeshes = KALLOC_TYPE_CARRAY(kstatic_mesh_submesh_render_data, out_render_data->submesh_count);
    for (u32 i = 0; i < out_render_data->submesh_count; ++i) {
        submesh* submesh = &submesh_data->submeshes[i];
        kstatic_mesh_submesh_render_data* submesh_rd = &out_render_data->submeshes[i];

        submesh_rd->material = state->base_instance_datas[instance.mesh].instances[instance.instance_id].material_instances[i];
        submesh_rd->vertex_data.count = submesh->geometry.vertex_count;
        submesh_rd->vertex_data.offset = submesh->geometry.vertex_buffer_offset;
        submesh_rd->index_data.count = submesh->geometry.index_count;
        submesh_rd->index_data.offset = submesh->geometry.index_buffer_offset;
        // TODO: Need a way to provide these flags per submesh.
        submesh_rd->flags = flags;
    }

    return true;
}

void static_mesh_render_data_destroy(kstatic_mesh_render_data* render_data) {
    if (render_data) {
        if (render_data->submeshes) {
            KFREE_TYPE_CARRAY(render_data->submeshes, kstatic_mesh_submesh_render_data, render_data->submesh_count);
        }
    }
    kzero_memory(render_data, sizeof(kstatic_mesh_submesh_render_data));
}

static void ensure_arrays_allocated(static_mesh_system_state* state, u32 new_count) {
    if (state) {
        KRESIZE_ARRAY(state->names, kname, state->max_mesh_count, new_count);
        KRESIZE_ARRAY(state->states, kstatic_mesh_state, state->max_mesh_count, new_count);
        KRESIZE_ARRAY(state->submesh_datas, static_mesh_submesh_data, state->max_mesh_count, new_count);
        KRESIZE_ARRAY(state->base_instance_datas, base_mesh_instance_data, state->max_mesh_count, new_count);
    }
}

static void ensure_instance_arrays_allocated(base_mesh_instance_data* base_instance_data, u32 new_count) {
    if (base_instance_data) {
        KRESIZE_ARRAY(base_instance_data->instances, instance_data, base_instance_data->max_instance_count, new_count);
        KRESIZE_ARRAY(base_instance_data->states, kstatic_mesh_instance_state, base_instance_data->max_instance_count, new_count);
    }
}

static void release_instance(static_mesh_system_state* state, kstatic_mesh m, u16 instance_id) {
    struct material_system_state* material_system = engine_systems_get()->material_system;

    u16 submesh_count = state->submesh_datas[m].submesh_count;

    // Release material instances.
    for (u16 i = 0; i < submesh_count; ++i) {
        material_system_release(material_system, &state->base_instance_datas[m].instances[instance_id].material_instances[i]);
    }

    // Cleanup the material instances array.
    KFREE_TYPE_CARRAY(state->base_instance_datas[m].instances[instance_id].material_instances, kmaterial_instance, submesh_count);
    state->base_instance_datas[m].instances[instance_id].material_instances = 0;

    // Mark the slot as free.
    state->base_instance_datas[m].states[instance_id] = KSTATIC_MESH_INSTANCE_STATE_UNINITIALIZED;
}

static void acquire_material_instances(static_mesh_system_state* state, kstatic_mesh m, u16 instance_id) {
    if (state->states[m] != KSTATIC_MESH_STATE_LOADED) {
        return;
    }

    base_mesh_instance_data* base_instance_data = &state->base_instance_datas[m];
    static_mesh_submesh_data* submesh_data = &state->submesh_datas[m];
    struct material_system_state* material_system = engine_systems_get()->material_system;
    instance_data* instance = &base_instance_data->instances[instance_id];

    // Only "issued" instances.
    if (base_instance_data->states[instance_id] == KSTATIC_MESH_INSTANCE_STATE_ACQUIRED) {
        instance->material_instances = KALLOC_TYPE_CARRAY(kmaterial_instance, submesh_data->submesh_count);
        KTRACE("Material instances array created.");

        // Process submeshes.
        for (u32 i = 0; i < submesh_data->submesh_count; ++i) {
            submesh* s = &submesh_data->submeshes[i];

            // Request material instance.
            b8 acquisition_result = material_system_acquire(material_system, s->material_name, &instance->material_instances[i]);
            if (!acquisition_result) {
                KWARN(
                    "Failed to load material '%s' for static mesh '%s', submesh '%s'.",
                    kname_string_get(s->material_name),
                    kname_string_get(state->names[m]),
                    kname_string_get(s->geometry.name));
            }
        }
    }
}
