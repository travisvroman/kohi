#include "kasset_importer_static_mesh_obj.h"

#include "memory/kmemory.h"
#include "platform/vfs.h"
#include "serializers/obj_mtl_serializer.h"
#include "serializers/obj_serializer.h"
#include "strings/kname.h"

#include <assets/kasset_types.h>
#include <core/engine.h>
#include <logger.h>
#include <serializers/kasset_binary_static_mesh_serializer.h>
#include <serializers/kasset_material_serializer.h>
#include <strings/kstring.h>

b8 kasset_importer_static_mesh_obj_import(const struct kasset_importer* self, u64 data_size, const void* data, void* params, struct kasset* out_asset) {
    if (!self || !data_size || !data) {
        KERROR("kasset_importer_static_mesh_obj_import requires valid pointers to self and data, as well as a nonzero data_size.");
        return false;
    }
    kasset_static_mesh* typed_asset = (kasset_static_mesh*)out_asset;
    const char* material_file_name = 0;

    struct vfs_state* vfs = engine_systems_get()->vfs_system_state;

    // Handle OBJ file import.
    {
        obj_source_asset obj_asset = {0};
        if (!obj_serializer_deserialize(data, &obj_asset)) {
            KERROR("OBJ file import failed! See logs for details.");
            return false;
        }

        // Convert OBJ asset to static_mesh.

        // Header-level data.
        typed_asset->geometry_count = obj_asset.geometry_count;
        typed_asset->center = obj_asset.center;
        typed_asset->extents = obj_asset.extents;
        typed_asset->geometries = kallocate(sizeof(kasset_static_mesh_geometry) * typed_asset->geometry_count, MEMORY_TAG_ARRAY);

        // Each geometry.
        for (u32 i = 0; i < typed_asset->geometry_count; ++i) {
            obj_source_geometry* g_src = &obj_asset.geometries[i];
            kasset_static_mesh_geometry* g = &typed_asset->geometries[i];

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

        // Save off a copy of the string so the OBJ asset can be let go.
        material_file_name = string_duplicate(obj_asset.material_file_name);

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
                kfree(g_src->indices, sizeof(vertex_3d) * g_src->index_count, MEMORY_TAG_ARRAY);
            }
        }
        kfree(obj_asset.geometries, sizeof(obj_source_geometry) * obj_asset.geometry_count, MEMORY_TAG_ARRAY);
        if (obj_asset.material_file_name) {
            string_free(obj_asset.material_file_name);
        }
    }

    // Serialize static_mesh and write out ksm file.
    {
        u64 serialized_size = 0;
        void* serialized_data = kasset_binary_static_mesh_serialize(out_asset, &serialized_size);
        if (!serialized_data || !serialized_size) {
            KERROR("Failed to serialize binary static mesh.");
            return false;
        }

        // Write out .ksm file.
        if (!vfs_asset_write(0, out_asset, true, serialized_size, serialized_data)) {
            KWARN("Failed to write .ksm file. See logs for details. Static mesh asset still imported and can be used, though.");
        }
    }

    // Deserialize the material file, if there is one.
    {
        obj_mtl_source_asset mtl_asset = {0};
        if (material_file_name) {
            // Build path based on OBJ file path. The files should sit together on disk.
            const char* obj_path = kname_string_get(out_asset->meta.source_asset_path);
            char path_buf[512] = {0};
            string_directory_from_path(path_buf, obj_path);
            const char* mtl_path = string_format("%s%s", path_buf, material_file_name);

            vfs_asset_data mtl_file_data = {0};
            vfs_request_direct_from_disk_sync(vfs, mtl_path, false, 0, 0, &mtl_file_data);
            if (mtl_file_data.result == VFS_REQUEST_RESULT_SUCCESS) {
                // Deserialize the mtl file.
                if (!obj_mtl_serializer_deserialize(mtl_file_data.text, &mtl_asset)) {
                    // NOTE: Intentionally not aborting here because the mesh can still be uses sans materials.
                    KWARN("Failed to parse MTL file data. See logs for details.");
                } else {
                    for (u32 i = 0; i < mtl_asset.material_count; ++i) {
                        obj_mtl_source_material* m_src = &mtl_asset.materials[i];

                        // Convert to kasset_material.
                        kasset_material new_material = {0};

                        // Set material name and package name.
                        new_material.base.name = m_src->name;
                        new_material.base.package_name = out_asset->package_name;
                        // Since it's an import, make note of the source asset path as well.
                        new_material.base.meta.source_asset_path = kname_create(mtl_path);

                        // Imports do not use a custom shader.
                        new_material.custom_shader_name = 0;

                        new_material.type = m_src->type;

                        // Material maps.
                        new_material.map_count = m_src->texture_map_count;
                        new_material.maps = kallocate(sizeof(kasset_material_map) * new_material.map_count, MEMORY_TAG_ARRAY);
                        for (u32 j = 0; j < new_material.map_count; ++j) {
                            kasset_material_map* map = &new_material.maps[j];
                            obj_mtl_source_texture_map* map_src = &m_src->maps[j];

                            // Name of the map (really just for informational purposes)
                            map->name = map_src->name;

                            // Name for the image asset.
                            map->image_asset_name = map_src->image_asset_name;

                            // Name of the package containing the asset. In this case (import), it will always be the same package.
                            map->image_asset_package_name = out_asset->package_name;

                            // Map channel. NOTE: OBJ format _can_ specify different channels per material type. Kohi doesn't, so just use the "base" type.
                            switch (map_src->channel) {
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_NORMAL:
                            case OBJ_TEXTURE_MAP_CHANNEL_PHONG_NORMAL:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_NORMAL;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_ALBEDO:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_ALBEDO;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_METALLIC:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_METALLIC;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_ROUGHNESS:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_ROUGHNESS;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_AO:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_AO;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_EMISSIVE:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_EMISSIVE;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_CLEAR_COAT:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_CLEAR_COAT;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_CLEAR_COAT_ROUGHNESS:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_CLEAR_COAT_ROUGHNESS;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PBR_WATER:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_WATER_DUDV;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PHONG_SPECULAR:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_SPECULAR;
                                break;
                            case OBJ_TEXTURE_MAP_CHANNEL_PHONG_DIFFUSE:
                            case OBJ_TEXTURE_MAP_CHANNEL_UNLIT_COLOUR:
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_DIFFUSE;
                                break;
                            default:
                                KWARN("Unsupported map type listed. Defaulting to diffuse.");
                                map->channel = KASSET_MATERIAL_MAP_CHANNEL_DIFFUSE;
                                break;
                            }

                            // Repeats
                            map->repeat_u = map_src->repeat_u;
                            map->repeat_v = map_src->repeat_v;
                            map->repeat_w = map_src->repeat_w;

                            // Filters
                            map->filter_min = map_src->filter_min;
                            map->filter_mag = map_src->filter_mag;
                        }

                        // Material properties.
                        new_material.property_count = m_src->property_count;
                        new_material.properties = kallocate(sizeof(kasset_material_property) * new_material.property_count, MEMORY_TAG_ARRAY);
                        for (u32 j = 0; j < new_material.property_count; ++j) {
                            kasset_material_property* prop = &new_material.properties[j];
                            obj_mtl_source_property* prop_src = &m_src->properties[j];

                            prop->name = prop_src->name;
                            prop->type = prop_src->type;
                            prop->size = prop_src->size;
                            // NOTE: Not sure if this is the best way of handling this...
                            kcopy_memory(&prop->value, &prop_src->value, sizeof(mat4));
                        }

                        // Serialize the material.
                        const char* serialized_text = kasset_material_serialize((kasset*)&new_material);
                        if (!serialized_text) {
                            KWARN("Failed to serialize material '%s'. See logs for details.", kname_string_get(new_material.base.name));
                        }

                        // Write out kmt file.
                        if (!vfs_asset_write(vfs, (kasset*)&new_material, false, string_length(serialized_text), serialized_text)) {
                            KERROR("Failed to write serialized material to disk.");
                        }

                        // Cleanup MTL asset.
                        {
                            // Maps
                            kfree(m_src->maps, sizeof(obj_mtl_source_material) * m_src->texture_map_count, MEMORY_TAG_ARRAY);

                            // Properties
                            kfree(m_src->properties, sizeof(obj_mtl_source_property) * m_src->property_count, MEMORY_TAG_ARRAY);
                        }

                        // Cleanup kmt from memory since there is no mechanic from here to load it.
                        // This will just be loaded from disk later.
                        {
                            // Maps
                            kfree(new_material.maps, sizeof(obj_mtl_source_material) * new_material.map_count, MEMORY_TAG_ARRAY);

                            // Properties
                            kfree(new_material.properties, sizeof(obj_mtl_source_property) * new_material.property_count, MEMORY_TAG_ARRAY);
                        }
                    } // each material
                }
            } // success
        }
    } // end material processing.

    return true;
}
