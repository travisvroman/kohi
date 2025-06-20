
#include "kasset_importer_material_obj_mtl.h"

#include <assets/kasset_types.h>
#include <core_render_types.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <serializers/kasset_material_serializer.h>
#include <serializers/kasset_static_mesh_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "serializers/obj_mtl_serializer.h"

b8 kasset_material_obj_mtl_import(const char* output_directory, const char* output_filename, const char* package_name, const char* data) {
    if (!data) {
        KERROR("%s requires a valid pointer to data.", __FUNCTION__);
        return false;
    }

    kname package_kname = kname_create(package_name);

    obj_mtl_source_asset mtl_asset = {0};
    // Deserialize the mtl file content.
    if (!obj_mtl_serializer_deserialize(data, &mtl_asset)) {
        // NOTE: Intentionally not aborting here because the mesh can still be uses sans materials.
        KERROR("%s: Failed to parse MTL file data. See logs for details.", __FUNCTION__);
        return false;
    } else {
        for (u32 i = 0; i < mtl_asset.material_count; ++i) {
            obj_mtl_source_material* m_src = &mtl_asset.materials[i];

            // Convert to kasset_material.
            kasset_material new_material = {0};

            // Set material name and package name.
            new_material.name = m_src->name;

            // Imports do not use a custom shader.
            new_material.custom_shader_name = 0;

            new_material.type = m_src->type;
            new_material.model = m_src->model;

            // Force defaults for things not considered in OBJ MTL files.
            new_material.casts_shadow = true;
            new_material.recieves_shadow = true;

            // Transparency - if there is a transparency "map" (which is usually the same as the ambient/diffuse map) or
            // the material is non-opaque (i.e ) less than 1.0f, then it should be marked as transparent.
            // FIXME: Find a reliable way to tell from the material definition if transparency should be supported for the material
            // _without_ looking up the "alpha" texture map. Assuming false always for now instead.
            new_material.has_transparency = false; // m_src->diffuse_transparency_image_asset_name || m_src->diffuse_transparency < 1.0f;

            // Material maps.
            // Base colour.
            if (new_material.model == KMATERIAL_MODEL_PBR) {
                // Base colour translates from diffuse only for PBR.
                if (m_src->diffuse_image_asset_name) {
                    new_material.base_colour_map.resource_name = m_src->diffuse_image_asset_name;
                    new_material.base_colour_map.package_name = package_kname;
                }
                new_material.base_colour = vec4_from_vec3(m_src->diffuse_colour, 1.0f);

                // Metallic
                if (m_src->metallic_image_asset_name) {
                    new_material.metallic_map.resource_name = m_src->metallic_image_asset_name;
                    new_material.metallic_map.package_name = package_kname;
                    // NOTE: Always assume red channel for OBJ MTL imports.
                    new_material.metallic_map.channel = TEXTURE_CHANNEL_R;
                }
                new_material.metallic = m_src->metallic;

                // Roughness
                if (m_src->roughness_image_asset_name) {
                    new_material.roughness_map.resource_name = m_src->roughness_image_asset_name;
                    new_material.roughness_map.package_name = package_kname;
                    // NOTE: Always assume red channel for OBJ MTL imports.
                    new_material.roughness_map.channel = TEXTURE_CHANNEL_R;
                }
                new_material.roughness = m_src->roughness;

                // Ambient occlusion NOTE: not supported for OBJ MTL imports.
                new_material.ambient_occlusion_enabled = false;
                new_material.ambient_occlusion = 1.0;

                // MRA (combined Metallic/Roughness/AO maps)
                if (m_src->mra_image_asset_name) {
                    new_material.mra_map.resource_name = m_src->mra_image_asset_name;
                    new_material.mra_map.package_name = package_kname;
                    new_material.use_mra = true;

                    // In this one scenario, enable AO since the MRA map can provide it.
                    new_material.ambient_occlusion_enabled = true;

                } else if (new_material.metallic_map.resource_name != INVALID_KNAME && new_material.metallic_map.resource_name == new_material.roughness_map.resource_name == new_material.ambient_occlusion_map.resource_name) {
                    // If metallic, roughness and ao all point to the same texture (and there _is_ a texture), switch to MRA instead.
                    new_material.mra_map.resource_name = new_material.metallic_map.resource_name;
                    new_material.mra_map.package_name = new_material.metallic_map.resource_name;
                    new_material.use_mra = true;

                    // In this one scenario, enable AO since the MRA map can provide it.
                    new_material.ambient_occlusion_enabled = true;

                } else {
                    new_material.use_mra = false;
                }
            } else if (new_material.model == KMATERIAL_MODEL_PHONG) {
                // TODO: make use of the ambient colour map.
                if (m_src->ambient_image_asset_name) {
                    KWARN("Material has ambient colour map set, but will not be imported due to engine limitations.");
                }
                if (m_src->diffuse_image_asset_name) {
                    new_material.base_colour_map.resource_name = m_src->diffuse_image_asset_name;
                    new_material.base_colour_map.package_name = package_kname;
                }
                // For phong, base colour is ambient + diffuse.
                new_material.base_colour = vec4_from_vec3(vec3_add(m_src->ambient_colour, m_src->diffuse_colour), 1.0f);

                // Specular - only used for phong.
                if (m_src->specular_image_asset_name) {
                    new_material.specular_colour_map.resource_name = m_src->specular_image_asset_name;
                    new_material.specular_colour_map.package_name = package_kname;
                }
                new_material.specular_colour = vec4_from_vec3(m_src->specular_colour, 1.0f);
            }

            // Normal
            if (m_src->normal_image_asset_name) {
                new_material.normal_map.resource_name = m_src->normal_image_asset_name;
                new_material.normal_map.package_name = package_kname;
                new_material.normal_enabled = true;
            } else {
                new_material.normal_enabled = false;
            }

            // Emissive
            if (m_src->emissive_image_asset_name) {
                new_material.emissive_map.resource_name = m_src->emissive_image_asset_name;
                new_material.emissive_map.package_name = package_kname;
            }
            new_material.emissive = vec4_from_vec3(m_src->emissive_colour, 1.0f);

            // Serialize the material.
            const char* serialized_text = kasset_material_serialize(&new_material);
            if (!serialized_text) {
                KWARN("Failed to serialize material '%s'. See logs for details.", kname_string_get(new_material.name));
                continue;
            }

            // Write out kmt file.
            const char* out_path = string_format("%s/%s.%s", output_directory, kname_string_get(new_material.name), "kmt");
            if (!filesystem_write_entire_text_file(out_path, serialized_text)) {
                KERROR("Failed to write serialized material to disk. See logs for details.");
            }

        } // each material

        // Cleanup materials
        if (mtl_asset.material_count) {
            KFREE_TYPE_CARRAY(mtl_asset.materials, obj_mtl_source_material, mtl_asset.material_count);
        }
    }

    return true;
}
