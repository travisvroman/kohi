#include "kasset_importer_static_mesh_obj.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <core_render_types.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <serializers/kasset_material_serializer.h>
#include <serializers/kasset_static_mesh_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "serializers/obj_serializer.h"

b8 kasset_static_mesh_obj_import(const char* target_path, const char* data, u32* out_material_file_count, const char*** out_material_file_names) {
    if (!data || !out_material_file_count || !out_material_file_names) {
        KERROR("%s requires valid pointers to data, out_material_file_count, and out_material_file_names.", __FUNCTION__);
        return false;
    }

    kasset_static_mesh asset = {0};
    obj_source_asset obj_asset = {0};
    // Handle OBJ file import.
    {
        if (!obj_serializer_deserialize(data, &obj_asset)) {
            KERROR("OBJ file import failed! See logs for details.");
            return false;
        }

        // Convert OBJ asset to static_mesh.

        // Header-level data.
        asset.geometry_count = obj_asset.geometry_count;
        asset.center = obj_asset.center;
        asset.extents = obj_asset.extents;
        asset.geometries = kallocate(sizeof(kasset_static_mesh_geometry) * asset.geometry_count, MEMORY_TAG_ARRAY);

        // Each geometry.
        for (u32 i = 0; i < asset.geometry_count; ++i) {
            obj_source_geometry* g_src = &obj_asset.geometries[i];
            kasset_static_mesh_geometry* g = &asset.geometries[i];

            // Take copies of all data.
            g->center = g_src->center;
            g->extents = g_src->extents;

            if (g_src->name) {
                g->name = kname_create(g_src->name);
            }

            if (g_src->material_asset_name) {
                g->material_asset_name = kname_create(g_src->material_asset_name);
            }

            if (g_src->index_count && g_src->indices) {
                u64 index_size = sizeof(u32) * g_src->index_count;
                g->index_count = g_src->index_count;
                g->indices = kallocate(index_size, MEMORY_TAG_ARRAY);
                kcopy_memory(g->indices, g_src->indices, index_size);
            }

            if (g_src->vertex_count && g_src->vertices) {
                u64 vertex_size = sizeof(vertex_3d) * g_src->vertex_count;
                g->vertex_count = g_src->vertex_count;
                g->vertices = kallocate(vertex_size, MEMORY_TAG_ARRAY);
                kcopy_memory(g->vertices, g_src->vertices, vertex_size);
            }
        }

        // Save off a copy material file names so the OBJ asset can be let go.
        *out_material_file_count = obj_asset.material_file_count;
        if (*out_material_file_count) {
            *out_material_file_names = KALLOC_TYPE_CARRAY(const char*, *out_material_file_count);
            KCOPY_TYPE_CARRAY(*out_material_file_names, obj_asset.material_file_names, const char*, *out_material_file_count);
        }

        // Cleanup OBJ asset.
        for (u32 i = 0; i < obj_asset.geometry_count; ++i) {
            obj_source_geometry* g_src = &obj_asset.geometries[i];

            if (g_src->name) {
                string_free(g_src->name);
            }

            if (g_src->material_asset_name) {
                string_free(g_src->material_asset_name);
            }

            if (g_src->index_count && g_src->indices) {
                kfree(g_src->indices, sizeof(u32) * g_src->index_count, MEMORY_TAG_ARRAY);
            }

            if (g_src->vertex_count && g_src->vertices) {
                kfree(g_src->vertices, sizeof(vertex_3d) * g_src->vertex_count, MEMORY_TAG_ARRAY);
            }
        }
        kfree(obj_asset.geometries, sizeof(obj_source_geometry) * obj_asset.geometry_count, MEMORY_TAG_ARRAY);
        if (obj_asset.material_file_count && obj_asset.material_file_names) {
            string_cleanup_array(obj_asset.material_file_names, obj_asset.material_file_count);
            KFREE_TYPE_CARRAY(obj_asset.material_file_names, const char*, obj_asset.material_file_count);
        }
    }

    // Serialize static_mesh and write out ksm file.

    u64 serialized_size = 0;
    void* serialized_data = kasset_static_mesh_serialize(&asset, &serialized_size);
    if (!serialized_data || !serialized_size) {
        KERROR("Failed to serialize binary static mesh.");
        return false;
    }

    // Write out .ksm file.
    b8 success = true;
    if (!filesystem_write_entire_binary_file(target_path, serialized_size, serialized_data)) {
        KWARN("Failed to write .ksm file '%s'. See logs for details.", target_path);
        success = false;
    }

    return success;
}
