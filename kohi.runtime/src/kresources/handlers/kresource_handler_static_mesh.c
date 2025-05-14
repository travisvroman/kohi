#include "kresource_handler_static_mesh.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <logger.h>
#include <memory/kmemory.h>

#include "defines.h"
#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "strings/kname.h"
#include "systems/asset_system.h"

/**
 * @brief A listener struct with data needed for static mesh asset requests.
 */
typedef struct static_mesh_asset_request_listener {
    // A pointer to the mesh resource associated with the request.
    kresource_static_mesh* mesh_resource;
    // User callback to be made once all resource assets are loaded.
    PFN_resource_loaded_user_callback user_callback;
    void* listener_inst;
} static_mesh_asset_request_listener;

// Callback for when an asset loads.
static void kasset_static_mesh_on_result(void* listener, kasset_static_mesh* asset);

b8 kresource_handler_static_mesh_request(struct kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    kresource_static_mesh* typed_resource = (kresource_static_mesh*)resource;

    typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;

    // Exactly one asset is required.
    // TODO: Perhaps additional info to pass geometry written in code would be useful here too.
    if (info->assets.base.length != 1) {
        KERROR("A static mesh resource request must have exactly one asset listed.");
        typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;
        return false;
    }

    kresource_asset_info* asset_info = &info->assets.data[0];
    if (asset_info->type == KASSET_TYPE_STATIC_MESH) {
        // Setup a listener.
        static_mesh_asset_request_listener* listener = KALLOC_TYPE(static_mesh_asset_request_listener, MEMORY_TAG_RESOURCE);
        listener->mesh_resource = (kresource_static_mesh*)resource;
        listener->user_callback = info->user_callback;
        listener->listener_inst = info->listener_inst;

        typed_resource->base.state = KRESOURCE_STATE_LOADING;

        kasset_static_mesh* asset = asset_system_request_static_mesh_from_package(
            engine_systems_get()->asset_state,
            kname_string_get(asset_info->package_name),
            kname_string_get(asset_info->asset_name),
            listener,
            kasset_static_mesh_on_result);
        if (!asset) {
            KERROR("Error loading static mesh asset. See logs for details.");
            return false;
        }

    } else {
        KERROR("Unexpected asset type in asset listing: %u", asset_info->type);
        typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;
        return false;
    }

    typed_resource->base.generation++;

    return true;
}

void kresource_handler_static_mesh_release(struct kresource_handler* self, kresource* resource) {
    kresource_static_mesh* typed_resource = (kresource_static_mesh*)resource;

    // Release data from renderbuffers
    renderbuffer* geometry_vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    renderbuffer* geometry_index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    // Process submeshes.
    if (typed_resource->submeshes) {
        for (u32 i = 0; i < typed_resource->submesh_count; ++i) {
            static_mesh_submesh* submesh = &typed_resource->submeshes[i];
            kgeometry* g = &submesh->geometry;

            // Free uploaded geometry data.
            u64 vertex_size = (u64)(sizeof(vertex_3d) * g->vertex_count);
            u64 index_size = (u64)(sizeof(u32) * g->index_count);

            // Vertex buffer
            if (!renderer_renderbuffer_free(geometry_vertex_buffer, vertex_size, g->vertex_buffer_offset)) {
                KERROR("Failed to free vertex buffer range while releasing geometry of static mesh.");
            }
            // Index buffer
            if (!renderer_renderbuffer_free(geometry_index_buffer, index_size, g->index_buffer_offset)) {
                KERROR("Failed to free index buffer range while releasing geometry of static mesh.");
            }

            // Cleanup the geometry index and vertex arrays.
            // Everything else will be taken care of when the geometry array is freed.
            if (g->vertices) {
                KFREE_TYPE_CARRAY(g->vertices, vertex_3d, g->vertex_count);
                g->vertices = 0;
            }
            if (g->indices) {
                KFREE_TYPE_CARRAY(g->indices, u32, g->index_count);
                g->indices = 0;
            }
        }

        KFREE_TYPE_CARRAY(typed_resource->submeshes, static_mesh_submesh, typed_resource->submesh_count);
        typed_resource->submeshes = 0;
    }

    typed_resource->submesh_count = 0;
}

static void kasset_static_mesh_on_result(void* listener, kasset_static_mesh* asset) {
    static_mesh_asset_request_listener* typed_listener = listener;
    kasset_static_mesh* typed_asset = (kasset_static_mesh*)asset;

    if (typed_asset->geometry_count < 1) {
        KERROR("Provided static mesh asset has no geometries, thus there is nothing to be loaded.");
        return;
    }

    renderbuffer* geometry_vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    renderbuffer* geometry_index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    // Process submeshes from asset.
    // TODO: A reloaded asset will need to free the old data first just before this.
    typed_listener->mesh_resource->submesh_count = typed_asset->geometry_count;
    typed_listener->mesh_resource->submeshes = KALLOC_TYPE_CARRAY(static_mesh_submesh, typed_asset->geometry_count);

    for (u32 i = 0; i < typed_asset->geometry_count; ++i) {

        kasset_static_mesh_geometry* source_geometry = &typed_asset->geometries[i];
        static_mesh_submesh* submesh = &typed_listener->mesh_resource->submeshes[i];
        submesh->material_name = source_geometry->material_asset_name;

        // Take a copy of the geometry data from the asset.
        kgeometry* submesh_geometry = &submesh->geometry;
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

    if (typed_listener->user_callback) {
        typed_listener->user_callback((kresource*)typed_listener->mesh_resource, typed_listener->listener_inst);
    }

    typed_listener->mesh_resource->base.state = KRESOURCE_STATE_LOADED;

    // Cleanup listener.
    KFREE_TYPE(listener, static_mesh_asset_request_listener, MEMORY_TAG_RESOURCE);
}
