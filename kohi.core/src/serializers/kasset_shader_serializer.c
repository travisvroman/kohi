#include "kasset_shader_serializer.h"

#include "assets/kasset_types.h"

#include "assets/kasset_utils.h"
#include "core_render_types.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

#define SHADER_ASSET_VERSION 1

const char* kasset_shader_serialize(const kasset_shader* asset) {
    if (!asset) {
        KERROR("kasset_shader_serialize requires an asset to serialize, ya dingus!");
        return 0;
    }

    kasset_shader* typed_asset = (kasset_shader*)asset;

    // Validate that there are actual stages, because these are required.
    if (!typed_asset->stage_count) {
        KERROR("kasset_shader_serializer requires at least one stage to serialize. Otherwise it's an invalid shader, ya dingus.");
        return 0;
    }

    const char* out_str = 0;

    // Setup the KSON tree to serialize below.
    kson_tree tree = {0};
    tree.root = kson_object_create();

    // version
    if (!kson_object_value_add_int(&tree.root, "version", SHADER_ASSET_VERSION)) {
        KERROR("Failed to add version, which is a required field.");
        goto cleanup_kson;
    }

    kson_object_value_add_int(&tree.root, "supports_wireframe", typed_asset->supports_wireframe);

    // Topology types
    {
        kson_array topology_types_array = kson_array_create();
        if (typed_asset->topology_types == PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT) {
            // If no types are included, default to triangle list. Bleat about it though.
            KWARN("Incoming shader asset has no topology_types set. Defaulting to triangle_list.");
            kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT));

        } else {

            // NOTE: "none" and "max" aren't valid types, so they are never written.
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT));
            }
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT));
            }
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT));
            }
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT));
            }
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT));
            }
            if (FLAG_GET(typed_asset->topology_types, PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT)) {
                kson_array_value_add_string(&topology_types_array, topology_type_to_string(PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT));
            }
        }

        kson_object_value_add_array(&tree.root, "topology_types", topology_types_array);
    }

    // Stages
    {
        kson_array stages_array = kson_array_create();
        for (u32 i = 0; i < typed_asset->stage_count; ++i) {
            kson_object stage_obj = kson_object_create();
            kasset_shader_stage* stage = &typed_asset->stages[i];

            kson_object_value_add_string(&stage_obj, "type", shader_stage_to_string(stage->type));
            if (stage->source_asset_name) {
                kson_object_value_add_string(&stage_obj, "source_asset_name", stage->source_asset_name);
            }
            if (stage->package_name) {
                kson_object_value_add_string(&stage_obj, "package_name", stage->package_name);
            }

            kson_array_value_add_object(&stages_array, stage_obj);
        }
        kson_object_value_add_array(&tree.root, "stages", stages_array);
    }

    // Attributes
    if (typed_asset->attribute_count > 0) {
        kson_array attributes_array = kson_array_create();
        for (u32 i = 0; i < typed_asset->attribute_count; ++i) {
            kson_object attribute_obj = kson_object_create();
            kasset_shader_attribute* attribute = &typed_asset->attributes[i];

            kson_object_value_add_string(&attribute_obj, "type", shader_attribute_type_to_string(attribute->type));
            kson_object_value_add_string(&attribute_obj, "name", attribute->name);

            kson_array_value_add_object(&attributes_array, attribute_obj);
        }
        kson_object_value_add_array(&tree.root, "attributes", attributes_array);
    }

    // Binding sets
    if (typed_asset->binding_set_count > 0) {
        kson_array binding_sets_array = kson_object_create();

        // Each binding set
        for (u32 i = 0; i < typed_asset->binding_set_count; ++i) {
            kasset_shader_binding_set* set = &typed_asset->sets[i];

            kson_object binding_set_obj = kson_object_create();

            kson_object_value_add_string(&binding_set_obj, "name", kname_string_get(set->name));

            kson_array bindings_array = kson_array_create();

            // Each binding within the set
            for (u32 j = 0; j < set->binding_count; ++j) {
                kasset_shader_binding* binding = &set->bindings[j];

                kson_object binding_obj = kson_object_create();

                kson_object_value_add_string(&binding_obj, "type", kasset_shader_binding_type_to_string(binding->type));

                if (binding->name != INVALID_KNAME) {
                    kson_object_value_add_string(&binding_obj, "name", kname_string_get(binding->name));
                }

                // Add size if binding is a ubo or ssbo.
                if (binding->type == KASSET_SHADER_BINDING_TYPE_UBO || binding->type == KASSET_SHADER_BINDING_TYPE_SSBO) {
                    kson_object_value_add_int(&binding_obj, "size", (i64)binding->size);
                }

                // Add array size if relevant (i.e. more than one).
                if (binding->array_size > 1) {
                    kson_object_value_add_int(&binding_obj, "array_size", (i64)binding->array_size);
                }

                kson_array_value_add_object(&bindings_array, binding_obj);
            }

            kson_object_value_add_array(&binding_set_obj, "bindings", binding_set_obj);
        }

        kson_object_value_add_array(&tree.root, "binding_sets", binding_sets_array);
    }

    // Output to string.
    out_str = kson_tree_to_string(&tree);
    if (!out_str) {
        KERROR("Failed to serialize shader to string. See logs for details.");
    }

cleanup_kson:
    kson_tree_cleanup(&tree);

    return out_str;
}

b8 kasset_shader_deserialize(const char* file_text, kasset_shader* out_asset) {
    if (out_asset) {
        b8 success = false;
        kasset_shader* typed_asset = (kasset_shader*)out_asset;

        // Deserialize the loaded asset data
        kson_tree tree = {0};
        if (!kson_tree_from_string(file_text, &tree)) {
            KERROR("Failed to parse asset data for shader. See logs for details.");
            goto cleanup_kson;
        }

        // version
        if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->version))) {
            KERROR("Failed to parse version, which is a required field.");
            goto cleanup_kson;
        }

        // Supports wireframe
        typed_asset->supports_wireframe = false;
        kson_object_property_value_get_bool(&tree.root, "supports_wireframe", &typed_asset->supports_wireframe);

        // Topology type flags
        // Default to triangle list
        typed_asset->topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        kson_array topology_types_array;
        if (kson_object_property_value_get_array(&tree.root, "topology_types", &topology_types_array)) {
            u32 topology_type_count = 0;
            if (kson_array_element_count_get(&topology_types_array, &topology_type_count) || topology_type_count == 0) {
                // If specified, clear it and process each one.
                typed_asset->topology_types = PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT;
                for (u32 i = 0; i < topology_type_count; ++i) {
                    const char* topology_type_str = 0;
                    if (!kson_array_element_value_get_string(&topology_types_array, i, &topology_type_str)) {
                        KERROR("Possible format error - unable to extract topology type at index %u. Skipping.", i);
                        continue;
                    }
                    primitive_topology_type_bits topology_type = string_to_topology_type(topology_type_str);
                    if (topology_type == PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT || topology_type >= PRIMITIVE_TOPOLOGY_TYPE_MAX_BIT) {
                        KERROR("Invalid topology type found. See logs for details. Skipping.");
                        continue;
                    }

                    typed_asset->topology_types = FLAG_SET(typed_asset->topology_types, topology_type, true);
                }
            }
        } else {
            // If nothing exists, default to triangle list
            typed_asset->topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
        }

        // Stages
        kson_array stages_array;
        if (kson_object_property_value_get_array(&tree.root, "stages", &stages_array)) {
            if (!kson_array_element_count_get(&stages_array, &typed_asset->stage_count) || typed_asset->stage_count == 0) {
                KERROR("Stages are required for shader configurations. Make sure at least one exists.");
                return false;
            }

            typed_asset->stages = kallocate(sizeof(kasset_shader_stage) * typed_asset->stage_count, MEMORY_TAG_ARRAY);
            for (u32 i = 0; i < typed_asset->stage_count; ++i) {
                kson_object stage_obj = {0};
                kson_array_element_value_get_object(&stages_array, i, &stage_obj);

                kasset_shader_stage* stage = &typed_asset->stages[i];
                const char* temp = 0;

                kson_object_property_value_get_string(&stage_obj, "type", &temp);
                stage->type = string_to_shader_stage(temp);
                string_free(temp);

                kson_object_property_value_get_string(&stage_obj, "source_asset_name", &stage->source_asset_name);
                kson_object_property_value_get_string(&stage_obj, "package_name", &stage->package_name);
            }
        } else {
            KERROR("Stages are required for shader configurations. Make sure at least one exists.");
            return false;
        }

        // Attributes
        kson_array attributes_array = {0};
        if (kson_object_property_value_get_array(&tree.root, "attributes", &attributes_array)) {
            if (!kson_array_element_count_get(&attributes_array, &typed_asset->attribute_count)) {
                KERROR("Failed to get attributes_array count. See logs for details.");
                return false;
            }

            typed_asset->attributes = kallocate(sizeof(kasset_shader_attribute) * typed_asset->attribute_count, MEMORY_TAG_ARRAY);
            for (u32 i = 0; i < typed_asset->attribute_count; ++i) {
                kson_object attribute_obj = {0};
                kson_array_element_value_get_object(&attributes_array, i, &attribute_obj);
                kasset_shader_attribute* attribute = &typed_asset->attributes[i];

                const char* temp = 0;
                kson_object_property_value_get_string(&attribute_obj, "type", &temp);
                attribute->type = string_to_shader_attribute_type(temp);
                string_free(temp);

                kson_object_property_value_get_string(&attribute_obj, "name", &attribute->name);
            }
        }

        // Binding sets
        kson_array binding_sets_array = {0};
        if (kson_object_property_value_get_array(&tree.root, "binding_sets", &binding_sets_array)) {

            kson_array_element_count_get(&binding_sets_array, &typed_asset->binding_set_count);
            if (!typed_asset->binding_set_count) {
                KERROR("There must be at least one binding set.");
            } else {

                typed_asset->sets = KALLOC_TYPE_CARRAY(kasset_shader_binding_set, typed_asset->binding_set_count);

                // Each set
                for (u32 i = 0; i < typed_asset->binding_set_count; ++i) {
                    kson_object set_obj;
                    kson_array_element_value_get_object(&binding_sets_array, i, &set_obj);

                    kasset_shader_binding_set* set = &typed_asset->sets[i];

                    kson_object_property_value_get_string_as_kname(&set_obj, "name", &set->name);

                    // Bindings
                    kson_array bindings_array;
                    if (kson_object_property_value_get_array(&set_obj, "bindings", &bindings_array)) {
                        KERROR("Invalid set at index %u - property 'bindings' is missing.", i);
                        continue;
                    }

                    kson_object_property_count_get(&bindings_array, &set->binding_count);
                    if (!set->binding_count) {
                        KERROR("There must be at least one binding in a binding set.");
                        continue;
                    }

                    set->bindings = KALLOC_TYPE_CARRAY(kasset_shader_binding, set->binding_count);

                    for (u32 j = 0; j < set->binding_count; ++j) {
                        kasset_shader_binding* binding = &set->bindings[j];

                        kson_object binding_obj;
                        kson_array_element_value_get_object(&bindings_array, i, &bindings_array);

                        // Type - required.
                        const char* type_str = 0;
                        if (!kson_object_property_value_get_string(&binding_obj, "type", &type_str)) {
                            KERROR("Invalid binding definition at index %u: Missing required 'type' field.", j);
                            continue;
                        }
                        binding->type = kasset_shader_binding_type_from_string(type_str);
                        string_free(type_str);

                        // Size - required for SSBO and UBO types
                        i64 size_i64 = 0;
                        if (!kson_object_property_value_get_int(&binding_obj, "size", &size_i64)) {
                            if (binding->type == KASSET_SHADER_BINDING_TYPE_SSBO || binding->type == KASSET_SHADER_BINDING_TYPE_UBO) {
                                KERROR("Property 'size' is required for binding types 'ssbo' and 'ubo'");
                                continue;
                            }
                        } else {
                            binding->size = (u32)size_i64;
                        }

                        // Offset (into the given buffer) - required for SSBO and UBO types
                        i64 offset_i64 = 0;
                        if (!kson_object_property_value_get_int(&binding_obj, "offset", &offset_i64)) {
                            if (binding->type == KASSET_SHADER_BINDING_TYPE_SSBO || binding->type == KASSET_SHADER_BINDING_TYPE_UBO) {
                                KERROR("Property 'offset' is required for binding types 'ssbo' and 'ubo'");
                                continue;
                            }
                        } else {
                            binding->offset = (u32)offset_i64;
                        }

                        // array_size - only valid for textures and sampler types.
                        i64 array_size_i64 = 0;
                        if (kson_object_property_value_get_int(&binding_obj, "array_size", &array_size_i64)) {
                            if (binding->type == KASSET_SHADER_BINDING_TYPE_SSBO || binding->type == KASSET_SHADER_BINDING_TYPE_UBO) {
                                KWARN("Property 'array_size' is invalid for binding types 'ssbo' and 'ubo' and will be ignored.");
                            } else {
                                binding->array_size = (u32)array_size_i64;
                            }
                        }
                    } // bindings
                } // binding sets
            }
        }

        success = true;
    cleanup_kson:
        kson_tree_cleanup(&tree);
        return success;
    }

    KERROR("kasset_shader_deserialize serializer requires an asset to deserialize to, ya dingus!");
    return false;
}
