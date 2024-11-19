#include "kasset_material_serializer.h"

#include "assets/kasset_types.h"
#include "assets/kasset_utils.h"
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

#define INPUT_BASE_COLOUR "base_colour"
#define INPUT_NORMAL "normal"
#define INPUT_METALLIC "metallic"
#define INPUT_ROUGHNESS "roughness"
#define INPUT_AO "ao"
#define INPUT_MRA "mra"
#define INPUT_EMISSIVE "emissive"

#define INPUT_MAP "map"
#define INPUT_VALUE "value"
#define INPUT_ENABLED "enabled"

#define INPUT_MAP_RESOURCE_NAME "resource_name"
#define INPUT_MAP_PACKAGE_NAME "package_name"
#define INPUT_MAP_SAMPLER_NAME "sampler_name"
#define INPUT_MAP_SOURCE_CHANNEL "source_channel"

#define SAMPLERS "samplers"

static b8 extract_input_map_channel_or_float(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, kasset_material_texture_map_channel* out_source_channel, f32* out_value, f32 default_value);
static b8 extract_input_map_channel_or_vec4(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, vec4* out_value, vec4 default_value);
static b8 extract_input_map_channel_or_vec3(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, vec3* out_value, vec3 default_value);

static void add_map_obj(kson_object* base_obj, const char* source_channel, kasset_material_texture* texture);
static b8 extract_map(const kson_object* map_obj, kasset_material_texture* out_texture, kasset_material_texture_map_channel* out_source_channel);

const char* kasset_material_serialize(const kasset* asset) {
    if (!asset) {
        KERROR("kasset_material_serialize requires a valid pointer to a material.");
        return 0;
    }

    kson_tree tree = {0};
    // The root of the tree.
    tree.root.type = KSON_OBJECT_TYPE_OBJECT;

    kasset_material* material = (kasset_material*)asset;

    // Format version.
    kson_object_value_add_int(&tree.root, "version", MATERIAL_FILE_VERSION);

    // Material type
    kson_object_value_add_string(&tree.root, "type", kasset_material_type_to_string(material->type));

    // Material model
    kson_object_value_add_string(&tree.root, "model", kasset_material_model_to_string(material->model));

    // Various flags
    kson_object_value_add_boolean(&tree.root, "has_transparency", material->has_transparency);
    kson_object_value_add_boolean(&tree.root, "double_sided", material->double_sided);
    kson_object_value_add_boolean(&tree.root, "recieves_shadow", material->recieves_shadow);
    kson_object_value_add_boolean(&tree.root, "casts_shadow", material->casts_shadow);
    kson_object_value_add_boolean(&tree.root, "use_vertex_colour_as_base_colour", material->use_vertex_colour_as_base_colour);

    // Material inputs
    kson_object inputs = kson_object_create();

    // Base colour
    kson_object base_colour = kson_object_create();
    if (material->base_colour_map.resource_name) {
        add_map_obj(&base_colour, 0, &material->base_colour_map);
    } else {
        kson_object_value_add_vec4(&base_colour, INPUT_VALUE, material->base_colour);
    }
    kson_object_value_add_object(&inputs, INPUT_BASE_COLOUR, base_colour);

    // Normal
    kson_object normal = kson_object_create();
    if (material->normal_map.resource_name) {
        add_map_obj(&normal, 0, &material->normal_map);
    } else {
        kson_object_value_add_vec3(&normal, INPUT_VALUE, material->normal);
    }
    kson_object_value_add_boolean(&normal, INPUT_ENABLED, material->normal_enabled);
    kson_object_value_add_object(&inputs, INPUT_NORMAL, base_colour);

    // Metallic
    kson_object metallic = kson_object_create();
    if (material->metallic_map.resource_name) {
        const char* channel = kasset_material_texture_map_channel_to_string(material->metallic_map_source_channel);
        add_map_obj(&base_colour, channel, &material->metallic_map);
    } else {
        kson_object_value_add_float(&metallic, INPUT_VALUE, material->metallic);
    }
    kson_object_value_add_object(&inputs, INPUT_METALLIC, metallic);

    // Roughness
    kson_object roughness = kson_object_create();
    if (material->roughness_map.resource_name) {
        const char* channel = kasset_material_texture_map_channel_to_string(material->roughness_map_source_channel);
        add_map_obj(&base_colour, channel, &material->roughness_map);
    } else {
        kson_object_value_add_float(&roughness, INPUT_VALUE, material->roughness);
    }
    kson_object_value_add_object(&inputs, INPUT_ROUGHNESS, roughness);

    // Roughness
    kson_object ao = kson_object_create();
    if (material->ambient_occlusion_map.resource_name) {
        const char* channel = kasset_material_texture_map_channel_to_string(material->ambient_occlusion_map_source_channel);
        add_map_obj(&base_colour, channel, &material->ambient_occlusion_map);
    } else {
        kson_object_value_add_float(&ao, INPUT_VALUE, material->ambient_occlusion);
    }
    kson_object_value_add_boolean(&ao, INPUT_ENABLED, material->ambient_occlusion_enabled);
    kson_object_value_add_object(&inputs, INPUT_AO, ao);

    // Metallic/roughness/ao combined value (mra)
    kson_object mra = kson_object_create();
    if (material->mra_map.resource_name) {
        add_map_obj(&base_colour, 0, &material->mra_map);
    } else {
        kson_object_value_add_vec3(&mra, INPUT_VALUE, material->mra);
    }
    kson_object_value_add_object(&inputs, INPUT_MRA, mra);

    // Emissive
    kson_object emissive = kson_object_create();
    if (material->emissive_map.resource_name) {
        add_map_obj(&base_colour, 0, &material->emissive_map);
    } else {
        kson_object_value_add_vec4(&emissive, INPUT_VALUE, material->emissive);
    }
    kson_object_value_add_boolean(&emissive, INPUT_ENABLED, material->emissive_enabled);
    kson_object_value_add_object(&inputs, INPUT_EMISSIVE, emissive);

    // Samplers
    if (material->custom_samplers && material->custom_sampler_count) {
        kson_array samplers_array = {0};
        samplers_array.type = KSON_OBJECT_TYPE_ARRAY;

        // Each sampler
        for (u32 i = 0; i < material->custom_sampler_count; ++i) {
            kasset_material_sampler* custom_sampler = &material->custom_samplers[i];

            kson_object sampler = {0};
            sampler.type = KSON_OBJECT_TYPE_OBJECT;

            kson_object_value_add_string(&sampler, "name", kname_string_get(custom_sampler->name));

            // Filtering
            kson_object_value_add_string(&sampler, "filter_min", texture_filter_mode_to_string(custom_sampler->filter_min));
            kson_object_value_add_string(&sampler, "filter_mag", texture_filter_mode_to_string(custom_sampler->filter_mag));

            // Repeats
            kson_object_value_add_string(&sampler, "repeat_u", texture_repeat_to_string(custom_sampler->repeat_u));
            kson_object_value_add_string(&sampler, "repeat_v", texture_repeat_to_string(custom_sampler->repeat_v));
            kson_object_value_add_string(&sampler, "repeat_w", texture_repeat_to_string(custom_sampler->repeat_w));

            // Add the sampler object to the array.
            kson_array_value_add_object(&samplers_array, sampler);
        }

        // Add the samplers array to the root object.
        kson_object_value_add_object(&tree.root, SAMPLERS, samplers_array);
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

    // Material type
    const char* type_str = 0;
    if (!kson_object_property_value_get_string(&tree.root, "type", &type_str)) {
        KERROR("failed to obtain type from material file, which is a required field.");
        goto cleanup;
    }
    out_material->type = string_to_kasset_material_type(type_str);

    // Material model. Optional, defaults to PBR
    const char* model_str = 0;
    if (!kson_object_property_value_get_string(&tree.root, "model", &model_str)) {
        out_material->model = KASSET_MATERIAL_MODEL_PBR;
    }
    out_material->model = string_to_kasset_material_model(model_str);

    // Format version.
    i64 file_format_version = 0;
    if (!kson_object_property_value_get_int(&tree.root, "version", &file_format_version)) {
        KERROR("Unable to find file format version, a required field. Material will not be processed.");
        goto cleanup;
    }
    // Validate version
    if (file_format_version != MATERIAL_FILE_VERSION) {
        if (file_format_version < 3) {
            KERROR("Material file format version %u no longer supported. File should be manually converted to at least version 3. Material will not be processed.", file_format_version);
            goto cleanup;
        }

        if (file_format_version > MATERIAL_FILE_VERSION) {
            KERROR("Cannot process a file material version that is newer than the current version, ya dingus!");
            goto cleanup;
        }
    }

    // Various flags - fall back to defaults if not provided.
    if (!kson_object_property_value_get_bool(&tree.root, "has_transparency", &out_material->has_transparency)) {
        out_material->has_transparency = false;
    }
    if (!kson_object_property_value_get_bool(&tree.root, "double_sided", &out_material->double_sided)) {
        out_material->double_sided = false;
    }
    if (!kson_object_property_value_get_bool(&tree.root, "recieves_shadow", &out_material->recieves_shadow)) {
        out_material->recieves_shadow = out_material->model != KASSET_MATERIAL_MODEL_UNLIT;
    }
    if (!kson_object_property_value_get_bool(&tree.root, "casts_shadow", &out_material->casts_shadow)) {
        out_material->casts_shadow = out_material->model != KASSET_MATERIAL_MODEL_UNLIT;
    }
    if (!kson_object_property_value_get_bool(&tree.root, "use_vertex_colour_as_base_colour", &out_material->use_vertex_colour_as_base_colour)) {
        out_material->use_vertex_colour_as_base_colour = false;
    }

    // Extract inputs. The array of inputs is required, but the actual inputs themselves are optional.
    // While technically this means no inputs could be provided, should probably warn about it since it
    // doesn't make much actual sense.
    {
        kson_object inputs_obj = {0};
        if (kson_object_property_value_get_object(&tree.root, "inputs", &inputs_obj)) {
            u32 input_count = 0;

            // Get known inputs

            // base_colour
            if (extract_input_map_channel_or_vec4(&inputs_obj, "base_colour", 0, &out_material->base_colour_map, &out_material->base_colour, vec4_one())) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            // mra
            if (extract_input_map_channel_or_vec3(&inputs_obj, INPUT_NORMAL, &out_material->normal_enabled, &out_material->normal_map, &out_material->normal, (vec3){0.0f, 0.0f, 1.0f})) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            // mra
            if (extract_input_map_channel_or_vec3(&inputs_obj, "mra", 0, &out_material->mra_map, &out_material->mra, (vec3){0.0f, 0.5f, 1.0f})) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
                // Flag to use MRA
                out_material->use_mra = true;
            }

            // metallic
            if (extract_input_map_channel_or_float(&inputs_obj, "metallic", 0, &out_material->metallic_map, &out_material->metallic_map_source_channel, &out_material->metallic, 0.0f)) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            // roughness
            if (extract_input_map_channel_or_float(&inputs_obj, "roughness", 0, &out_material->roughness_map, &out_material->roughness_map_source_channel, &out_material->roughness, 0.5f)) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            // ao
            if (extract_input_map_channel_or_float(&inputs_obj, "ao", &out_material->ambient_occlusion_enabled, &out_material->ambient_occlusion_map, &out_material->ambient_occlusion_map_source_channel, &out_material->ambient_occlusion, 1.0f)) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            // emissive
            if (extract_input_map_channel_or_vec4(&inputs_obj, "emissive", &out_material->emissive_enabled, &out_material->emissive_map, &out_material->emissive, vec4_zero())) {
                // Only count values actually included in config, as a validation check later.
                input_count++;
            }

            if (input_count < 1) {
                KWARN("This material has no inputs, which is strange. Why would you do that?");
            }
        }
    }

    // Extract samplers.
    {
        kson_array samplers_array = {0};
        if (kson_object_property_value_get_object(&tree.root, "samplers", &samplers_array)) {
            if (kson_array_element_count_get(&samplers_array, &out_material->custom_sampler_count)) {
                out_material->custom_samplers = darray_create(kasset_material_sampler);
                for (u32 i = 0; i < out_material->custom_sampler_count; ++i) {
                    kson_object sampler = {0};
                    if (kson_array_element_value_get_object(&samplers_array, i, &sampler)) {
                        // Extract sampler attributes
                        kasset_material_sampler custom_sampler = {0};

                        // name
                        if (!kson_object_property_value_get_kname(&sampler, "name", &custom_sampler.name)) {
                            KERROR("name, a required map field, was not found. Skipping sampler.");
                            continue;
                        }

                        // The rest of the fields are all optional. Setup some defaults.
                        custom_sampler.filter_mag = custom_sampler.filter_min = TEXTURE_FILTER_MODE_LINEAR;
                        custom_sampler.repeat_u = custom_sampler.repeat_v = custom_sampler.repeat_w = TEXTURE_REPEAT_REPEAT;
                        const char* value_str = 0;

                        // filters
                        // "filter" applies both. If it exists then set both.
                        if (kson_object_property_value_get_string(&sampler, "filter", &value_str)) {
                            custom_sampler.filter_min = custom_sampler.filter_mag = string_to_texture_filter_mode(value_str);
                            string_free(value_str);
                        }
                        // Individual min/max overrides higher-level filter.
                        if (kson_object_property_value_get_string(&sampler, "filter_min", &value_str)) {
                            custom_sampler.filter_min = string_to_texture_filter_mode(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&sampler, "filter_mag", &value_str)) {
                            custom_sampler.filter_mag = string_to_texture_filter_mode(value_str);
                            string_free(value_str);
                        }

                        // repeats
                        // "repeat" applies to all 3.
                        if (kson_object_property_value_get_string(&sampler, "repeat", &value_str)) {
                            custom_sampler.repeat_u = custom_sampler.repeat_v = custom_sampler.repeat_w = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        // Individual u/v/w overrides higher-level repeat.
                        if (kson_object_property_value_get_string(&sampler, "repeat_u", &value_str)) {
                            custom_sampler.repeat_u = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&sampler, "repeat_v", &value_str)) {
                            custom_sampler.repeat_v = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        if (kson_object_property_value_get_string(&sampler, "repeat_w", &value_str)) {
                            custom_sampler.repeat_w = string_to_texture_repeat(value_str);
                            string_free(value_str);
                        }
                        // Push to array.
                        darray_push(out_material->custom_samplers, custom_sampler);
                    }
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

static b8 extract_input_map_channel_or_float(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, kasset_material_texture_map_channel* out_source_channel, f32* out_value, f32 default_value) {
    kson_object input = {0};
    b8 input_found = false;
    if (kson_object_property_value_get_object(inputs_obj, input_name, &input)) {
        kson_object map_obj = {0};
        if (out_enabled) {
            kson_object_property_value_get_bool(&input, INPUT_ENABLED, out_enabled);
        }
        b8 has_map = kson_object_property_value_get_object(&input, INPUT_MAP, &map_obj);
        b8 has_value = kson_object_property_value_get_float(&input, INPUT_VALUE, out_value);
        if (has_map && has_value) {
            KWARN("Input '%s' specified both a value and a map. The map will be used.", input_name);
            *out_value = default_value;
            has_value = false;
            input_found = true;
        } else if (!has_map && !has_value) {
            KWARN("Input '%s' specified neither a value nor a map. A default value will be used.", input_name);
            *out_value = default_value;
            has_value = true;
        } else {
            // Valid case where only a map OR value was provided. Only count provided inputs.
            input_found = true;
        }

        // Texture input.
        if (has_map) {
            if (!extract_map(&map_obj, out_texture, out_source_channel)) {
                return false;
            }
        }
    } else {
        // If nothing is specified, use the default for this.
        *out_value = default_value;
    }

    return input_found;
}

static b8 extract_input_map_channel_or_vec4(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, vec4* out_value, vec4 default_value) {
    kson_object input = {0};
    b8 input_found = false;
    if (kson_object_property_value_get_object(inputs_obj, input_name, &input)) {
        kson_object map_obj = {0};
        if (out_enabled) {
            kson_object_property_value_get_bool(&input, INPUT_ENABLED, out_enabled);
        }
        b8 has_map = kson_object_property_value_get_object(&input, INPUT_MAP, &map_obj);
        b8 has_value = kson_object_property_value_get_vec4(&input, INPUT_VALUE, out_value);
        if (has_map && has_value) {
            KWARN("Input '%s' specified both a value and a map. The map will be used.", input_name);
            *out_value = default_value;
            has_value = false;
            input_found = true;
        } else if (!has_map && !has_value) {
            KWARN("Input '%s' specified neither a value nor a map. A default value will be used.", input_name);
            *out_value = default_value;
            has_value = true;
        } else {
            // Valid case where only a map OR value was provided. Only count provided inputs.
            input_found = true;
        }

        // Texture input.
        if (has_map) {
            if (!extract_map(&map_obj, out_texture, 0)) {
                return false;
            }
        }
    } else {
        // If nothing is specified, use the default for this.
        *out_value = default_value;
    }

    return input_found;
}

static b8 extract_input_map_channel_or_vec3(const kson_object* inputs_obj, const char* input_name, b8* out_enabled, kasset_material_texture* out_texture, vec3* out_value, vec3 default_value) {
    kson_object input = {0};
    b8 input_found = false;
    if (kson_object_property_value_get_object(inputs_obj, input_name, &input)) {
        kson_object map_obj = {0};
        if (out_enabled) {
            kson_object_property_value_get_bool(&input, INPUT_ENABLED, out_enabled);
        }
        b8 has_map = kson_object_property_value_get_object(&input, INPUT_MAP, &map_obj);
        b8 has_value = kson_object_property_value_get_vec3(&input, INPUT_VALUE, out_value);
        if (has_map && has_value) {
            KWARN("Input '%s' specified both a value and a map. The map will be used.", input_name);
            *out_value = default_value;
            has_value = false;
            input_found = true;
        } else if (!has_map && !has_value) {
            KWARN("Input '%s' specified neither a value nor a map. A default value will be used.", input_name);
            *out_value = default_value;
            has_value = true;
        } else {
            // Valid case where only a map OR value was provided. Only count provided inputs.
            input_found = true;
        }

        // Texture input.
        if (has_map) {
            if (!extract_map(&map_obj, out_texture, 0)) {
                return false;
            }
        }
    } else {
        // If nothing is specified, use the default for this.
        *out_value = default_value;
    }

    return input_found;
}

static void add_map_obj(kson_object* base_obj, const char* source_channel, kasset_material_texture* texture) {

    // Add map object.
    kson_object map_obj = kson_object_create();
    kson_object_value_add_kname(&map_obj, INPUT_MAP_RESOURCE_NAME, texture->resource_name);
    // Package name. Optional
    if (texture->package_name) {
        kson_object_value_add_kname(&map_obj, INPUT_MAP_PACKAGE_NAME, texture->package_name);
    }
    // Sampler name. Optional.
    if (texture->sampler_name) {
        kson_object_value_add_kname(&map_obj, INPUT_MAP_SAMPLER_NAME, texture->sampler_name);
    }
    // Source channel, if provided.
    if (source_channel) {
        kson_object_value_add_string(&map_obj, INPUT_MAP_SOURCE_CHANNEL, source_channel);
    }
    kson_object_value_add_object(base_obj, INPUT_MAP, map_obj);
}

static b8 extract_map(const kson_object* map_obj, kasset_material_texture* out_texture, kasset_material_texture_map_channel* out_source_channel) {

    // Extract the resource_name. Required.
    if (!kson_object_property_value_get_kname(map_obj, INPUT_MAP_RESOURCE_NAME, &out_texture->resource_name)) {
        KERROR("input map.resource_name is required.");
        return false;
    }

    // Attempt to extract package name, optional.
    if (!kson_object_property_value_get_kname(map_obj, INPUT_MAP_PACKAGE_NAME, &out_texture->package_name)) {
        out_texture->package_name = INVALID_KNAME;
    }

    // Optional property, so it doesn't matter if we get it or not.
    if (!kson_object_property_value_get_kname(map_obj, INPUT_MAP_SAMPLER_NAME, &out_texture->sampler_name)) {
        out_texture->sampler_name = INVALID_KNAME;
    }

    if (out_source_channel) {
        // For floats, a source channel must be chosen. Default is red.
        const char* channel = 0;
        kson_object_property_value_get_string(map_obj, INPUT_MAP_SOURCE_CHANNEL, &channel);
        if (channel) {
            *out_source_channel = string_to_kasset_material_texture_map_channel(channel);
        } else {
            // Use default of r.
            *out_source_channel = KASSET_MATERIAL_TEXTURE_MAP_CHANNEL_R;
        }
    }

    return true;
}
