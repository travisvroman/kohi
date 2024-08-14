#include "kasset_material_serializer.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core_render_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

// The current version of the material file format.
#define MATERIAL_FILE_VERSION 3

const char* kasset_material_serialize(const kasset* asset) {
    if (!asset) {
        KERROR("kasset_material_serialize requires a valid pointer to a material.");
        return 0;
    }

    kson_tree tree = {0};
    // The root of the tree.
    tree.root.type = KSON_OBJECT_TYPE_OBJECT;

    kasset_material* material = (kasset_material*)asset;

    // Material type
    kson_object_value_add_string(&tree.root, "type", kmaterial_type_to_string(material->type));

    // Material name
    kson_object_value_add_string(&tree.root, "name", kname_string_get(material->base.name));

    // Format version.
    kson_object_value_add_int(&tree.root, "version", MATERIAL_FILE_VERSION);

    // Material properties
    if (material->properties && material->property_count) {
        kson_array properties_array = {0};
        properties_array.type = KSON_OBJECT_TYPE_ARRAY;

        // Each property
        for (u32 i = 0; i < material->property_count; ++i) {
            kasset_material_property* p = &material->properties[i];

            kson_object prop = {0};
            prop.type = KSON_OBJECT_TYPE_OBJECT;

            kson_object_value_add_string(&prop, "name", kname_string_get(p->name));
            kson_object_value_add_string(&prop, "type", shader_uniform_type_to_string(p->type));

            // Add value as string for vector types. Otherwise add as int or float.
            switch (p->type) {
            case SHADER_UNIFORM_TYPE_FLOAT32_4: {
                const char* str = vec4_to_string(p->value.v4);
                kson_object_value_add_string(&prop, "value", str);
                string_free(str);
            } break;
            case SHADER_UNIFORM_TYPE_FLOAT32_3: {
                const char* str = vec3_to_string(p->value.v3);
                kson_object_value_add_string(&prop, "value", str);
                string_free(str);
            } break;
            case SHADER_UNIFORM_TYPE_FLOAT32_2: {
                const char* str = vec2_to_string(p->value.v2);
                kson_object_value_add_string(&prop, "value", str);
                string_free(str);
            } break;
            case SHADER_UNIFORM_TYPE_FLOAT32: {
                kson_object_value_add_float(&prop, "value", p->value.f32);
            } break;
            case SHADER_UNIFORM_TYPE_MATRIX_4: {
                const char* str = mat4_to_string(p->value.mat4);
                kson_object_value_add_string(&prop, "value", str);
                string_free(str);
            } break;
            case SHADER_UNIFORM_TYPE_UINT8: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.u8);
            } break;
            case SHADER_UNIFORM_TYPE_UINT16: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.u16);
            } break;
            case SHADER_UNIFORM_TYPE_UINT32: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.u32);
            } break;
            case SHADER_UNIFORM_TYPE_INT8: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.i8);
            } break;
            case SHADER_UNIFORM_TYPE_INT16: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.i16);
            } break;
            case SHADER_UNIFORM_TYPE_INT32: {
                // NOTE: Treat as i64 since that's all kson deals with.
                kson_object_value_add_int(&prop, "value", p->value.i32);
            } break;
            case SHADER_UNIFORM_TYPE_CUSTOM: {
                // TODO: Provide some sort of (de)serialization mechanic for custom property types.
                KWARN("Custom type material properties cannot have a value serialized to file when written.");
                kson_object_value_add_int(&prop, "size", p->size);
            } break;
            default:
                KERROR("Cannot serialize sampler or unknown type properties, as they are not valid.");
                break;
            }

            // Add the property object to the array.
            kson_array_value_add_object(&properties_array, prop);
        }

        // Add the properties array to the root object.
        kson_object_value_add_object(&tree.root, "properties", properties_array);
    }

    // Material maps
    if (material->maps && material->map_count) {
        kson_array maps_array = {0};
        maps_array.type = KSON_OBJECT_TYPE_ARRAY;

        // Each map
        for (u32 i = 0; i < material->map_count; ++i) {
            kasset_material_map* m = &material->maps[i];

            kson_object map = {0};
            map.type = KSON_OBJECT_TYPE_OBJECT;

            kson_object_value_add_string(&map, "name", kname_string_get(m->name));

            // Fully-qualified image asset name.
            if (m->image_asset_name) {
                kson_object_value_add_string(&map, "image_asset_name", kname_string_get(m->image_asset_name));
            }

            // Filtering
            kson_object_value_add_string(&map, "filter_min", texture_filter_mode_to_string(m->filter_min));
            kson_object_value_add_string(&map, "filter_mag", texture_filter_mode_to_string(m->filter_mag));

            // Repeats
            kson_object_value_add_string(&map, "repeat_u", texture_repeat_to_string(m->repeat_u));
            kson_object_value_add_string(&map, "repeat_v", texture_repeat_to_string(m->repeat_v));
            kson_object_value_add_string(&map, "repeat_w", texture_repeat_to_string(m->repeat_w));

            // Add the map object to the array.
            kson_array_value_add_object(&maps_array, map);
        }

        // Add the maps array to the root object.
        kson_object_value_add_object(&tree.root, "maps", maps_array);
    }

    // Tree is built, output it to a string.
    const char* serialized = kson_tree_to_string(&tree);

    // Cleanup the tree.
    kson_tree_cleanup(&tree);

    // Verify the result.
    if (!serialized) {
        KERROR("Failed to output serialized material kson structure to string. See logs for details.");
        return 0;
    }

    return serialized;
}

b8 kasset_material_deserialize(const char* file_text, kasset* out_asset) {
    if (!file_text || !out_asset) {
        KERROR("kasset_material_deserialize requires valid pointers to file_text and out_asset.");
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(file_text, &tree)) {
        KERROR("Failed to parse material file. See logs for details.");
        return 0;
    }

    b8 success = false;
    kasset_material* out_material = (kasset_material*)out_asset;

    // Extract top-level properties first.
    const char* material_name_str = 0;
    if (!kson_object_property_value_get_string(&tree.root, "name", &material_name_str)) {
        KERROR("failed to obtain name from material file, which is a required field.");
        goto cleanup;
    }
    out_material->base.name = kname_create(material_name_str);
    string_free(material_name_str);

    // Material type
    const char* type_str = 0;
    if (!kson_object_property_value_get_string(&tree.root, "type", &type_str)) {
        KERROR("failed to obtain type from material file, which is a required field.");
        goto cleanup;
    }
    out_material->type = string_to_kmaterial_type(type_str);

    i64 file_format_version = 0;
    if (!kson_object_property_value_get_int(&tree.root, "version", &file_format_version)) {
        KERROR("Unable to find file format version, a required file. Material will not be processed.");
        goto cleanup;
    }
    // Validate version
    if (file_format_version != MATERIAL_FILE_VERSION) {
        if (file_format_version == 1) {
            KERROR("Material file format version 1 no longer supported. File should be manually converted to at least version 3. Material will not be processed.");
            goto cleanup;
        } else if (file_format_version == 2) {
            KERROR("Material file format version 2 no longer supported. File should be manually converted to at least version 3. Material will not be processed.");
            goto cleanup;
        }

        if (file_format_version > MATERIAL_FILE_VERSION) {
            KERROR("Cannot process a file material version that is newer than the current version, ya dingus!");
            goto cleanup;
        }
    }

    // Extract properties. These are optional, so skip if not included.
    {
        kson_array properties_array = {0};
        if (kson_object_property_value_get_object(&tree.root, "properties", &properties_array)) {
            if (kson_array_element_count_get(&properties_array, &out_material->property_count)) {
                out_material->properties = darray_create(kasset_material_property);
                for (u32 i = 0; i < out_material->property_count; ++i) {
                    kson_object prop = {0};
                    if (kson_array_element_value_get_object(&properties_array, i, &prop)) {
                        // Extract attributes
                        kasset_material_property p = {0};

                        // Name is required.
                        const char* prop_name_str = 0;
                        if (!kson_object_property_value_get_string(&prop, "name", &prop_name_str)) {
                            KWARN("Material property has no name, but is required. Skipping property.");
                            continue;
                        }
                        p.name = kname_create(prop_name_str);
                        string_free(prop_name_str);

                        // Type is required.
                        const char* prop_type_str = 0;
                        if (!kson_object_property_value_get_string(&prop, "type", &prop_type_str)) {
                            KWARN("Material property has no type, but is required. Skipping property.");
                            continue;
                        }
                        // Convert type string into type.
                        p.type = string_to_shader_uniform_type(prop_type_str);
                        string_free(prop_type_str);

                        // NOTE: Value is optional. Attempt parsing if it is there.
                        i64 i_value = 0;
                        switch (p.type) {
                        case SHADER_UNIFORM_TYPE_FLOAT32:
                            kson_object_property_value_get_float(&prop, "value", &p.value.f32);
                            break;
                        case SHADER_UNIFORM_TYPE_FLOAT32_2: {
                            if (!kson_object_property_value_get_vec2(&prop, "value", &p.value.v2)) {
                                KWARN("Failed to extract vec2 from property. Defaulting to vec2_zero.");
                                p.value.v2 = vec2_zero();
                            }
                        } break;
                        case SHADER_UNIFORM_TYPE_FLOAT32_3: {
                            if (!kson_object_property_value_get_vec3(&prop, "value", &p.value.v3)) {
                                KWARN("Failed to extract vec3 from property. Defaulting to vec3_zero.");
                                p.value.v3 = vec3_zero();
                            }
                        } break;
                        case SHADER_UNIFORM_TYPE_FLOAT32_4: {
                            if (!kson_object_property_value_get_vec4(&prop, "value", &p.value.v4)) {
                                KWARN("Failed to extract vec4 from property. Defaulting to vec4_zero.");
                                p.value.v4 = vec4_zero();
                            }
                        } break;
                        case SHADER_UNIFORM_TYPE_MATRIX_4: {
                            if (!kson_object_property_value_get_mat4(&prop, "value", &p.value.mat4)) {
                                KWARN("Failed to extract mat4 from property. Defaulting to mat4_identity.");
                                p.value.mat4 = mat4_identity();
                            }
                        } break;
                        case SHADER_UNIFORM_TYPE_UINT32:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.u32 = (u32)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_UINT16:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.u16 = (u16)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_UINT8:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.u8 = (u8)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_INT32:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.i32 = (i32)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_INT16:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.i16 = (i16)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_INT8:
                            if (kson_object_property_value_get_int(&prop, "value", &i_value)) {
                                p.value.i8 = (i8)i_value;
                            }
                            break;
                        case SHADER_UNIFORM_TYPE_SAMPLER_1D:
                        case SHADER_UNIFORM_TYPE_SAMPLER_2D:
                        case SHADER_UNIFORM_TYPE_SAMPLER_3D:
                        case SHADER_UNIFORM_TYPE_SAMPLER_1D_ARRAY:
                        case SHADER_UNIFORM_TYPE_SAMPLER_2D_ARRAY:
                        case SHADER_UNIFORM_TYPE_SAMPLER_CUBE:
                        case SHADER_UNIFORM_TYPE_SAMPLER_CUBE_ARRAY:
                            KWARN("Samplers cannot be a material property. Use a map instead. Skipping this one.");
                            break;
                        case SHADER_UNIFORM_TYPE_CUSTOM:
                            KWARN("There is no currently-implemented way to (de)serialize a custom type property. Skipping this one.");
                            break;
                        }

                        // Push to prop array
                        darray_push(out_material->properties, p);
                    }
                }
            }
        }
    }

    // Extract maps.
    {
        kson_array maps_array = {0};
        if (kson_object_property_value_get_object(&tree.root, "maps", &maps_array)) {
            if (kson_array_element_count_get(&maps_array, &out_material->map_count)) {
                out_material->maps = darray_create(kasset_material_map);
                for (u32 i = 0; i < out_material->map_count; ++i) {
                    kson_object map = {0};
                    if (kson_array_element_value_get_object(&maps_array, i, &map)) {
                        // Extract attributes
                        kasset_material_map m = {0};

                        // name
                        const char* map_name_str = 0;
                        if (!kson_object_property_value_get_string(&map, "channel", &map_name_str)) {
                            KERROR("name, a required map field, was not found. Skipping map.");
                            continue;
                        }
                        m.name = kname_create(map_name_str);
                        string_free(map_name_str);

                        // image_asset_name
                        const char* image_asset_name_str = 0;
                        if (!kson_object_property_value_get_string(&map, "image_asset_name", &image_asset_name_str)) {
                            KERROR("image_asset_name, a required map field, was not found. Skipping map.");
                            continue;
                        }
                        m.image_asset_name = kname_create(image_asset_name_str);
                        string_free(image_asset_name_str);

                        // channel
                        const char* channel_str = 0;
                        if (!kson_object_property_value_get_string(&map, "channel", &channel_str)) {
                            KERROR("channel, a required map field, was not found. Skipping map.");
                            continue;
                        }
                        m.channel = string_to_material_map_channel(channel_str);
                        string_free(channel_str);

                        // The rest of the fields are all optional.
                        const char* value_str = 0;

                        // filters
                        m.filter_min = m.filter_mag = TEXTURE_FILTER_MODE_LINEAR;
                        if (kson_object_property_value_get_string(&map, "filter_min", &value_str)) {
                            m.filter_min = string_to_texture_filter_mode(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&map, "filter_mag", &value_str)) {
                            m.filter_mag = string_to_texture_filter_mode(value_str);
                            string_free(value_str);
                        }

                        // repeats
                        m.repeat_u = m.repeat_v = m.repeat_w = TEXTURE_REPEAT_REPEAT;
                        if (kson_object_property_value_get_string(&map, "repeat_u", &value_str)) {
                            m.repeat_u = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&map, "repeat_v", &value_str)) {
                            m.repeat_v = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&map, "repeat_w", &value_str)) {
                            m.repeat_w = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                    }
                    // Push to maps array.
                    darray_push(out_material->maps, map);
                }
            }
        }
    }

    // Done parsing!
    success = true;
cleanup:
    kson_tree_cleanup(&tree);

    return success;
}
