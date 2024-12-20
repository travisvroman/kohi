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

    obj_mtl_source_property* current_properties = darray_create(obj_mtl_source_property);
    obj_mtl_source_texture_map* current_maps = darray_create(obj_mtl_source_texture_map);
    const char* current_name = 0;

    b8 hit_name = false;

    char* line = 0;
    char line_buffer[512];
    char* p = &line_buffer[0];
    u32 line_length = 0;
    u8 addl_advance = 0;
    u32 start_from = 0;
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
            case 'a':
            case 'd': {
                // Ambient/Diffuse colour are treated the same at this level.
                // ambient colour is determined by the level.
                char t[2];
                obj_mtl_source_property prop = {0};
                prop.name = kname_create("diffuse_colour");
                prop.type = SHADER_UNIFORM_TYPE_FLOAT32_4;
                prop.size = sizeof(vec4);

                sscanf(line, "%s %f %f %f", t, &prop.value.v4.r, &prop.value.v4.g, &prop.value.v4.b);

                // NOTE: This is only used by the colour shader, and will set to
                // max_norm by default. Transparency could be added as a material
                // property all its own at a later time.
                prop.value.v4.a = 1.0f;

                darray_push(current_properties, prop);
            } break;
            case 's': {
                // Specular colour
                char t[2];

                // NOTE: Not using this for now.
                f32 spec_rubbish = 0.0f;
                sscanf(line, "%s %f %f %f", t, &spec_rubbish, &spec_rubbish, &spec_rubbish);
            } break;
            }
        } break;
        case 'N': {
            char second_char = line[1];
            switch (second_char) {
            case 's': {
                // Specular exponent
                char t[2];

                obj_mtl_source_property prop = {0};
                prop.name = kname_create("shininess");
                prop.type = SHADER_UNIFORM_TYPE_FLOAT32;
                prop.size = sizeof(f32);

                sscanf(line, "%s %f", t, &prop.value.f32);
                // NOTE: Need to make sure this is nonzero as this will cause
                // artefacts in the rendering of objects.
                if (prop.value.f32 == 0) {
                    prop.value.f32 = 8.0f;
                }

                darray_push(current_properties, prop);
            } break;
            }
        } break;
        case 'b': // NOTE: Some implementations use 'bump' instead of 'map_bump'.
        case 'm': {
            // map
            char substr[10];
            char texture_file_name[512];

            sscanf(line, "%s %s", substr, texture_file_name);

            obj_mtl_source_texture_map map = {0};

            // Texture name
            char tex_name_buf[512] = {0};
            string_filename_no_extension_from_path(tex_name_buf, texture_file_name);
            map.image_asset_name = kname_create(tex_name_buf);

            // map name/type
            if (first_char == 'm') {
                if (strings_nequali(substr, "map_Kd", 6)) {
                    map.name = kname_create("albedo");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_ALBEDO;
                } else if (strings_nequali(substr, "map_Pm", 6)) {
                    map.name = kname_create("metallic");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_METALLIC;
                } else if (strings_nequali(substr, "map_Pr", 6)) {
                    map.name = kname_create("rougness");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_ROUGHNESS;
                } else if (strings_nequali(substr, "map_Ke", 6)) {
                    map.name = kname_create("emissive");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_EMISSIVE;
                } else if (strings_nequali(substr, "map_bump", 8)) {
                    map.name = kname_create("normal");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_NORMAL;
                } else {
                    KERROR("Unrecognized token. Skipping.");
                    continue;
                }
            } else if (first_char == 'b') {
                if (strings_nequali(substr, "bump", 4)) {
                    map.name = kname_create("normal");
                    map.channel = OBJ_TEXTURE_MAP_CHANNEL_PBR_NORMAL;
                } else {
                    KERROR("Unrecognized token. Skipping.");
                    continue;
                }
            } else {
                KERROR("Unrecognized token. Skipping.");
                continue;
            }

            darray_push(current_maps, map);
        } break;
        case 'n': {
            char substr[10];
            char material_name[512];

            sscanf(line, "%s %s", substr, material_name);
            if (strings_nequali(substr, "newmtl", 6)) {
                // Is a material name.

                // If there is already a material name, then this is a new material.
                if (hit_name) {
                    // Push a new material to the collection and move on.
                    obj_mtl_source_material new_material = {0};
                    // Assuming standard material type.
                    new_material.type = KMATERIAL_TYPE_STANDARD;
                    // NOTE: forcing PBR on there.
                    new_material.model = KMATERIAL_MODEL_PBR;
                    // Take a copy of the properties array.
                    new_material.property_count = darray_length(current_properties);
                    new_material.properties = kallocate(sizeof(obj_mtl_source_property) * new_material.property_count, MEMORY_TAG_ARRAY);
                    kcopy_memory(new_material.properties, current_properties, sizeof(obj_mtl_source_property) * new_material.property_count);
                    // Take a copy of the maps array.
                    new_material.texture_map_count = darray_length(current_maps);
                    new_material.maps = kallocate(sizeof(obj_mtl_source_property) * new_material.texture_map_count, MEMORY_TAG_ARRAY);
                    kcopy_memory(new_material.maps, current_maps, sizeof(obj_mtl_source_property) * new_material.texture_map_count);
                    // Take a copy of the name.
                    if (current_name) {
                        new_material.name = kname_create(current_name);
                    } else {
                        // TODO: generate random name - maybe based on guid?
                        KASSERT_MSG(false, "Not yet implemented.");
                    }
                    darray_push(out_mtl_source_asset->materials, new_material);

                    // Cleanup and reset for the next material.
                    darray_clear(current_properties);
                    darray_clear(current_maps);
                    if (current_name) {
                        string_free(current_name);
                        current_name = 0;
                    }
                }

                // Take a copy of the name for the next material.
                hit_name = true;
                current_name = string_duplicate(material_name);
            }
        }
        } // end switch
    } // each line

    // Write out the remaining material.
    obj_mtl_source_material new_material = {0};
    // Assuming standard material type.
    new_material.type = KMATERIAL_TYPE_STANDARD;
    // NOTE: forcing PBR on there.
    new_material.model = KMATERIAL_MODEL_PBR;
    // Take a copy of the properties array.
    new_material.property_count = darray_length(current_properties);
    new_material.properties = kallocate(sizeof(obj_mtl_source_property) * new_material.property_count, MEMORY_TAG_ARRAY);
    kcopy_memory(new_material.properties, current_properties, sizeof(obj_mtl_source_property) * new_material.property_count);
    // Take a copy of the maps array.
    new_material.texture_map_count = darray_length(current_maps);
    new_material.maps = kallocate(sizeof(obj_mtl_source_property) * new_material.texture_map_count, MEMORY_TAG_ARRAY);
    kcopy_memory(new_material.maps, current_maps, sizeof(obj_mtl_source_property) * new_material.texture_map_count);
    // Take a copy of the name.
    if (current_name) {
        new_material.name = kname_create(current_name);
    } else {
        // TODO: generate random name - maybe based on guid?
        KASSERT_MSG(false, "Not yet implemented.");
    }
    darray_push(out_mtl_source_asset->materials, new_material);

    // Cleanup
    darray_destroy(current_properties);
    darray_destroy(current_maps);
    if (current_name) {
        string_free(current_name);
        current_name = 0;
    }

    return true;
}
