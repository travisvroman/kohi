#include "mesh.h"

#include "core/identifier.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/math_types.h"
#include "renderer/renderer_types.h"
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

    out_mesh->config = config;
    out_mesh->generation = INVALID_ID_U8;
    if (config.name) {
        out_mesh->name = string_duplicate(config.name);
    }

    return true;
}

b8 mesh_initialize(mesh* m) {
    if (!m) {
        return false;
    }

    if (m->config.resource_name) {
        return true;
    } else {
        // Just verifying config.
        if (!m->config.g_configs) {
            return false;
        }

        m->geometry_count = m->config.geometry_count;
        m->geometries = kallocate(sizeof(geometry*), MEMORY_TAG_ARRAY);
    }
    return true;
}

b8 mesh_load(mesh* m) {
    if (!m) {
        return false;
    }

    m->unique_id = identifier_aquire_new_id(m);

    if (m->config.resource_name) {
        return mesh_load_from_resource(m->config.resource_name, m);
    } else {
        if (!m->config.g_configs) {
            return false;
        }

        for (u32 i = 0; i < m->config.geometry_count; ++i) {
            m->geometries[i] = geometry_system_acquire_from_config(m->config.g_configs[i], true);
            m->generation = 0;

            // Clean up the allocations for the geometry config.
            // TODO: Do this during unload/destroy
            geometry_system_config_dispose(&m->config.g_configs[i]);
        }
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

    if (m->config.name) {
        kfree(m->config.name, string_length(m->config.name) + 1, MEMORY_TAG_STRING);
        m->config.name = 0;
    }
    if (m->config.resource_name) {
        kfree(m->config.resource_name, string_length(m->config.resource_name) + 1, MEMORY_TAG_STRING);
        m->config.resource_name = 0;
    }
    if (m->config.parent_name) {
        kfree(m->config.parent_name, string_length(m->config.parent_name) + 1, MEMORY_TAG_STRING);
        m->config.parent_name = 0;
    }

    return true;
}
