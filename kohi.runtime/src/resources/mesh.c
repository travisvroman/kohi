#include "mesh.h"

#include "identifier.h"
#include "kmemory.h"
#include "kstring.h"
#include "logger.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"
#include "systems/geometry_system.h"
#include "systems/job_system.h"
#include "systems/resource_system.h"

// Also used as result_data from job.
typedef struct mesh_load_params {
    const char* resource_name;
    mesh* out_mesh;
    resource mesh_resource;
} mesh_load_params;

/**
 * @brief Called when the job completes successfully.
 *
 * @param params The parameters passed from the job after completion.
 */
static void mesh_load_job_success(void* params) {
    mesh_load_params* mesh_params = (mesh_load_params*)params;

    // This also handles the GPU upload. Can't be jobified until the renderer is multithreaded.
    geometry_config* configs = (geometry_config*)mesh_params->mesh_resource.data;
    mesh_params->out_mesh->geometry_count = mesh_params->mesh_resource.data_size;
    mesh_params->out_mesh->geometries = kallocate(sizeof(geometry*) * mesh_params->out_mesh->geometry_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < mesh_params->out_mesh->geometry_count; ++i) {
        mesh_params->out_mesh->geometries[i] = geometry_system_acquire_from_config(configs[i], true);

        // Calculate the geometry extents.
        extents_3d* local_extents = &mesh_params->out_mesh->geometries[i]->extents;
        vertex_3d* verts = configs[i].vertices;
        for (u32 v = 0; v < configs[i].vertex_count; ++v) {
            // Min
            if (verts[v].position.x < local_extents->min.x) {
                local_extents->min.x = verts[v].position.x;
            }
            if (verts[v].position.y < local_extents->min.y) {
                local_extents->min.y = verts[v].position.y;
            }
            if (verts[v].position.z < local_extents->min.z) {
                local_extents->min.z = verts[v].position.z;
            }

            // Max
            if (verts[v].position.x > local_extents->max.x) {
                local_extents->max.x = verts[v].position.x;
            }
            if (verts[v].position.y > local_extents->max.y) {
                local_extents->max.y = verts[v].position.y;
            }
            if (verts[v].position.z > local_extents->max.z) {
                local_extents->max.z = verts[v].position.z;
            }
        }

        // Calculate overall extents for the mesh by getting the extents for each sub-mesh.
        extents_3d* global_extents = &mesh_params->out_mesh->extents;

        // Min
        if (local_extents->min.x < global_extents->min.x) {
            global_extents->min.x = local_extents->min.x;
        }
        if (local_extents->min.y < global_extents->min.y) {
            global_extents->min.y = local_extents->min.y;
        }
        if (local_extents->min.z < global_extents->min.z) {
            global_extents->min.z = local_extents->min.z;
        }

        // Max
        if (local_extents->max.x > global_extents->max.x) {
            global_extents->max.x = local_extents->max.x;
        }
        if (local_extents->max.y > global_extents->max.y) {
            global_extents->max.y = local_extents->max.y;
        }
        if (local_extents->max.z > global_extents->max.z) {
            global_extents->max.z = local_extents->max.z;
        }
    }
    mesh_params->out_mesh->generation++;
    mesh_params->out_mesh->state = MESH_STATE_LOADED;

    KTRACE("Successfully loaded mesh '%s'.", mesh_params->resource_name);

    resource_system_unload(&mesh_params->mesh_resource);
}

/**
 * @brief Called when the job fails.
 *
 * @param params Parameters passed when a job fails.
 */
static void mesh_load_job_fail(void* params) {
    mesh_load_params* mesh_params = (mesh_load_params*)params;

    KERROR("Failed to load mesh '%s'.", mesh_params->resource_name);

    resource_system_unload(&mesh_params->mesh_resource);
}

/**
 * @brief Called when a mesh loading job begins.
 *
 * @param params Mesh loading parameters.
 * @param result_data Result data passed to the completion callback.
 * @return True on job success; otherwise false.
 */
static b8 mesh_load_job_start(void* params, void* result_data) {
    mesh_load_params* load_params = (mesh_load_params*)params;
    b8 result = resource_system_load(load_params->resource_name, RESOURCE_TYPE_MESH, 0, &load_params->mesh_resource);

    // NOTE: The load params are also used as the result data here, only the mesh_resource field is populated now.
    kcopy_memory(result_data, load_params, sizeof(mesh_load_params));

    return result;
}

static b8 mesh_load_from_resource(const char* resource_name, mesh* out_mesh) {
    out_mesh->generation = INVALID_ID_U8;

    mesh_load_params params;
    params.resource_name = resource_name;
    params.out_mesh = out_mesh;
    params.mesh_resource = (resource){};

    job_info job = job_create(mesh_load_job_start, mesh_load_job_success, mesh_load_job_fail, &params, sizeof(mesh_load_params), sizeof(mesh_load_params));
    job_system_submit(job);

    return true;
}

b8 mesh_create(mesh_config config, mesh* out_mesh) {
    if (!out_mesh) {
        return false;
    }

    kzero_memory(out_mesh, sizeof(mesh));

    if (config.resource_name) {
        out_mesh->resource_name = string_duplicate(config.resource_name);
    }
    if (config.g_configs && config.geometry_count > 0) {
        out_mesh->geometry_count = config.geometry_count;
        out_mesh->g_configs = kallocate(sizeof(geometry_config) * out_mesh->geometry_count, MEMORY_TAG_ARRAY);
        kcopy_memory(out_mesh->g_configs, config.g_configs, sizeof(geometry_config) * out_mesh->geometry_count);
    }
    out_mesh->generation = INVALID_ID_U8;
    out_mesh->state = MESH_STATE_CREATED;

    return true;
}

b8 mesh_initialize(mesh* m) {
    if (!m) {
        return false;
    }

    if (m->resource_name) {
        return true;
    } else {
        // Just verifying config.
        if (!m->g_configs) {
            KERROR("Cannot initialize a mesh without either a resource name or at least one geometry configuration.");
            return false;
        }
    }

    m->state = MESH_STATE_INITIALIZED;
    return true;
}

b8 mesh_load(mesh* m) {
    if (!m) {
        return false;
    }

    m->state = MESH_STATE_LOADING;

    m->id = identifier_create();

    if (m->resource_name) {
        return mesh_load_from_resource(m->resource_name, m);
    } else {
        if (!m->g_configs) {
            KERROR("Cannot load a mesh without either a resource name or at least one geometry configuration.");
            return false;
        }

        for (u32 i = 0; i < m->geometry_count; ++i) {
            m->geometries[i] = geometry_system_acquire_from_config(m->g_configs[i], true);
            m->generation = 0;

            // Clean up the allocations for the geometry config.
            // TODO: Do this during unload/destroy
            geometry_system_config_dispose(&m->g_configs[i]);
        }

        m->state = MESH_STATE_LOADED;
    }

    return true;
}

b8 mesh_unload(mesh* m) {
    if (m) {
        for (u32 i = 0; i < m->geometry_count; ++i) {
            geometry_system_release(m->geometries[i]);
        }

        kfree(m->geometries, sizeof(geometry*) * m->geometry_count, MEMORY_TAG_ARRAY);
        kzero_memory(m, sizeof(mesh));

        // For good measure, invalidate the geometry so it doesn't attempt to be rendered.
        m->generation = INVALID_ID_U8;
        m->state = MESH_STATE_UNDEFINED;

        return true;
    }
    return false;
}

b8 mesh_destroy(mesh* m) {
    if (!m) {
        return false;
    }

    if (m->geometries) {
        if (!mesh_unload(m)) {
            KERROR("mesh_destroy - failed to unload mesh.");
            return false;
        }
    }

    if (m->name) {
        kfree(m->name, string_length(m->name) + 1, MEMORY_TAG_STRING);
        m->name = 0;
    }

    if (m->resource_name) {
        kfree(m->resource_name, string_length(m->resource_name) + 1, MEMORY_TAG_STRING);
        m->resource_name = 0;
    }

    return true;
}
