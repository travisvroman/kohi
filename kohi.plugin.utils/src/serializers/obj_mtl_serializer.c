#include "obj_mtl_serializer.h"

#include "core_render_types.h"

#include <containers/darray.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include <stdio.h> // sscanf

static b8 import_obj_material_library_file(const char* mtl_file_text, obj_mtl_source_asset* out_mtl_source_asset);

b8 obj_mtl_serializer_serialize(const obj_mtl_source_asset* source_asset, const char** out_file_text) {
    KASSERT_MSG(false, "Not yet implemented");
    return false;
}

b8 obj_mtl_serializer_deserialize(const char* mtl_file_text, obj_mtl_source_asset* out_mtl_source_asset) {
    if (!mtl_file_text || !out_mtl_source_asset) {
        KERROR("obj_serializer_deserialize requires valid pointers to mtl_file_text and out_mtl_source_asset.");
        return false;
    }

    return import_obj_material_library_file(mtl_file_text, out_mtl_source_asset);
}

static b8 import_obj_material_library_file(const char* mtl_file_text, obj_mtl_source_asset* out_mtl_source_asset) {
    KDEBUG("Importing obj .mtl file ...");

    obj_mtl_source_material* materials = darray_create(obj_mtl_source_material);

    const char* current_name = 0;

    b8 hit_name = false;

    char* line = 0;
    char line_buffer[512];
    char* p = &line_buffer[0];
    u32 line_length = 0;
    u8 addl_advance = 0;
    u32 start_from = 0;

    obj_mtl_source_material current_material = {0};

    while (true) {
        start_from += line_length + addl_advance;
        if (!string_line_get(mtl_file_text, 511, start_from, &p, &line_length, &addl_advance)) {
            /* if (!filesystem_read_line(mtl_file, 511, &p, &line_length)) { */
            break;
        }
        // Trim the line first.
        line = string_trim(line_buffer);
        line_length = string_length(line);

        // Skip blank lines.
        if (line_length < 1) {
            continue;
        }

        char first_char = line[0];
        switch (first_char) {
        case '#':
            // Skip comments
            continue;
        case 'K': {
            char second_char = line[1];
            switch (second_char) {
            case 'a': {
                // Ambient colour.
                char t[2];
                vec3* prop = &current_material.ambient_colour;
                sscanf(line, "%s %f %f %f", t, &prop->r, &prop->g, &prop->b);
            }
            case 'd': {
                // Diffuse colour.
                char t[2];
                vec3* prop = &current_material.diffuse_colour;
                sscanf(line, "%s %f %f %f", t, &prop->r, &prop->g, &prop->b);
            } break;
            case 's': {
                // Specular colour.
                char t[2];
                vec3* prop = &current_material.specular_colour;
                sscanf(line, "%s %f %f %f", t, &prop->r, &prop->g, &prop->b);
            } break;
            case 'e': {
                // Emissive colour.
                char t[2];
                vec3* prop = &current_material.emissive_colour;
                sscanf(line, "%s %f %f %f", t, &prop->r, &prop->g, &prop->b);
            } break;
            }
        } break;
        case 'N': {
            char second_char = line[1];
            switch (second_char) {
            case 's': {
                // Specular exponent
                char t[2];
                sscanf(line, "%s %f", t, &current_material.specular_exponent);

                // NOTE: Need to make sure this is nonzero as this will cause
                // artefacts in the rendering of objects.
                if (current_material.specular_exponent == 0) {
                    current_material.specular_exponent = 8.0f;
                }
            } break;
            }
        } break;
        case 'b': {
            // NOTE: Some implementations use 'bump' instead of 'map_bump'.
            char substr[10];
            char texture_file_name[512];

            sscanf(line, "%s %s", substr, texture_file_name);

            // Texture name
            char tex_name_buf[512] = {0};
            string_filename_no_extension_from_path(tex_name_buf, texture_file_name);

            if (strings_nequali(substr, "bump", 4)) {
                current_material.normal_image_asset_name = kname_create(tex_name_buf);
            } else {
                KWARN("Unrecognized token (expected 'bump'). Skipping.");
                continue;
            }
        } break;
        case 'm': {
            // map
            char substr[10];
            char texture_file_name[512];

            sscanf(line, "%s %s", substr, texture_file_name);

            // Texture name
            char tex_name_buf[512] = {0};
            string_filename_no_extension_from_path(tex_name_buf, texture_file_name);
            kname image_asset_name = kname_create(tex_name_buf);

            // map name/type
            if (strings_nequali(substr, "map_ka", 6)) {
                current_material.ambient_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_kd", 6)) {
                current_material.diffuse_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_ks", 6)) {
                current_material.specular_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_Pm", 6)) {
                current_material.metallic_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_Pr", 6)) {
                current_material.roughness_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_Ke", 6)) {
                current_material.emissive_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_bump", 8)) {
                current_material.normal_image_asset_name = image_asset_name;
            } else if (strings_nequali(substr, "map_rma", 7) || strings_nequali(substr, "map_orm", 7) || strings_nequali(substr, "map_mra", 7)) {
                // NOTE: Treating RMA (roughness/metallic/ao), ORM and MRA as the same MRA for now.
                current_material.mra_image_asset_name = image_asset_name;
            } else {
                KWARN("Unrecognized token. Skipping.");
                continue;
            }
        } break;
        case 'n': {
            char substr[10];
            char material_name[512];

            sscanf(line, "%s %s", substr, material_name);
            if (strings_nequali(substr, "newmtl", 6)) {
                // Is a material name.

                // If there is already a material name, then this is a new material.
                if (hit_name) {
                    // Push the current material to the collection and move on.
                    // Assuming standard material type.
                    current_material.type = KMATERIAL_TYPE_STANDARD;
                    current_material.model = KMATERIAL_MODEL_PBR; // FIXME: Defaulting to PBR, might want to find a better way to handle this.
                    // If using a PBR property, assume PBR.
                    if (current_material.roughness_image_asset_name || current_material.metallic_image_asset_name || current_material.mra_image_asset_name || current_material.metallic || current_material.roughness) {
                        current_material.model = KMATERIAL_MODEL_PBR;
                    }
                    // Take a copy of the name.
                    if (current_name) {
                        current_material.name = kname_create(current_name);
                        string_free(current_name);
                        current_name = 0;
                    } else {
                        // TODO: generate random name - maybe based on guid?
                        KASSERT_MSG(false, "Not yet implemented.");
                    }
                    darray_push(materials, current_material);

                    // Cleanup and reset for the next material.
                    kzero_memory(&current_material, sizeof(obj_mtl_source_material));
                }

                // Take a copy of the name for the next material.
                hit_name = true;
                current_name = string_duplicate(material_name);
            }
        }
        } // end switch
    } // each line

    // Write out the remaining material.
    // Assuming standard material type.
    current_material.type = KMATERIAL_TYPE_STANDARD;
    current_material.model = KMATERIAL_MODEL_PBR; // FIXME: Defaulting to PBR, might want to find a better way to handle this.
    // If using a PBR property, assume PBR.
    if (current_material.roughness_image_asset_name || current_material.metallic_image_asset_name || current_material.mra_image_asset_name || current_material.metallic || current_material.roughness) {
        current_material.model = KMATERIAL_MODEL_PBR;
    }
    // Take a copy of the name.
    if (current_name) {
        current_material.name = kname_create(current_name);
        string_free(current_name);
        current_name = 0;
    } else {
        // TODO: generate random name - maybe based on guid?
        KASSERT_MSG(false, "Not yet implemented.");
    }
    darray_push(materials, current_material);

    // Cleanup and reset for the next material.
    kzero_memory(&current_material, sizeof(obj_mtl_source_material));

    // Take a copy of the materials darray.
    out_mtl_source_asset->material_count = darray_length(materials);
    out_mtl_source_asset->materials = KALLOC_TYPE_CARRAY(obj_mtl_source_material, out_mtl_source_asset->material_count);
    KCOPY_TYPE_CARRAY(out_mtl_source_asset->materials, materials, obj_mtl_source_material, out_mtl_source_asset->material_count);

    // Cleanup
    darray_destroy(materials);
    if (current_name) {
        string_free(current_name);
        current_name = 0;
    }

    return true;
}
