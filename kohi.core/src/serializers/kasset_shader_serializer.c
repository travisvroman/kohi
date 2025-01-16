#include "kasset_shader_serializer.h"

#include "assets/kasset_types.h"

#include "core_render_types.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

#define SHADER_ASSET_VERSION 1

static b8 extract_frequency_uniforms(shader_update_frequency frequency, u32 frequency_uniform_count, kson_array* frequency_array, kasset_shader* typed_asset, u32* uniform_index);

const char* kasset_shader_serialize(const kasset* asset) {
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

    // max_groups
    kson_object_value_add_int(&tree.root, "max_groups", typed_asset->max_groups);

    kson_object_value_add_int(&tree.root, "max_draw_ids", typed_asset->max_draw_ids);

    kson_object_value_add_int(&tree.root, "supports_wireframe", typed_asset->supports_wireframe);

    // Depth test
    kson_object_value_add_boolean(&tree.root, "depth_test", typed_asset->depth_test);

    // Depth write
    kson_object_value_add_boolean(&tree.root, "depth_write", typed_asset->depth_write);

    // Stencil test
    kson_object_value_add_boolean(&tree.root, "stencil_test", typed_asset->stencil_test);

    // Stencil write
    kson_object_value_add_boolean(&tree.root, "stencil_write", typed_asset->stencil_write);

    // Colour read
    kson_object_value_add_boolean(&tree.root, "colour_read", typed_asset->colour_read);

    // Colour write
    kson_object_value_add_boolean(&tree.root, "colour_write", typed_asset->colour_write);

    // Cull mode
    kson_object_value_add_string(&tree.root, "cull_mode", face_cull_mode_to_string(typed_asset->cull_mode));

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

    // Uniforms
    if (typed_asset->uniform_count > 0) {
        kson_object uniforms_obj = kson_object_create();

        kson_array per_frame_array = kson_array_create();
        kson_array per_group_array = kson_array_create();
        kson_array per_draw_array = kson_array_create();
        u32 per_frame_count = 0;
        u32 per_group_count = 0;
        u32 per_draw_count = 0;
        for (u32 i = 0; i < typed_asset->uniform_count; ++i) {
            kson_object uniform_obj = kson_object_create();
            kasset_shader_uniform* uniform = &typed_asset->uniforms[i];

            kson_object_value_add_string(&uniform_obj, "type", shader_uniform_type_to_string(uniform->type));
            kson_object_value_add_string(&uniform_obj, "name", uniform->name);

            // Add size if uniform is a struct.
            if (uniform->type == SHADER_UNIFORM_TYPE_STRUCT) {
                kson_object_value_add_int(&uniform_obj, "size", (i64)uniform->size);
            }

            // Add array size if relevant (i.e. more than one).
            if (uniform->array_size > 1) {
                kson_object_value_add_int(&uniform_obj, "array_size", (i64)uniform->array_size);
            }

            switch (uniform->frequency) {
            default:
            case SHADER_UPDATE_FREQUENCY_PER_FRAME:
                kson_array_value_add_object(&per_frame_array, uniform_obj);
                per_frame_count++;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_GROUP:
                kson_array_value_add_object(&per_group_array, uniform_obj);
                per_group_count++;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_DRAW:
                kson_array_value_add_object(&per_draw_array, uniform_obj);
                per_draw_count++;
                break;
            }
        }

        if (per_frame_count) {
            kson_object_value_add_array(&uniforms_obj, "per_frame", per_frame_array);
        }
        if (per_group_count) {
            kson_object_value_add_array(&uniforms_obj, "per_group", per_group_array);
        }
        if (per_draw_count) {
            kson_object_value_add_array(&uniforms_obj, "per_draw", per_draw_array);
        }
        kson_object_value_add_object(&tree.root, "uniforms", uniforms_obj);
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

b8 kasset_shader_deserialize(const char* file_text, kasset* out_asset) {
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
        if (!kson_object_property_value_get_int(&tree.root, "version", (i64*)(&typed_asset->base.meta.version))) {
            KERROR("Failed to parse version, which is a required field.");
            goto cleanup_kson;
        }

        // max_groups
        i64 max_groups = 0;
        kson_object_property_value_get_int(&tree.root, "max_groups", &max_groups);
        typed_asset->max_groups = (u16)max_groups;

        // max_draw_ids
        i64 max_draw_ids = 0;
        kson_object_property_value_get_int(&tree.root, "max_draw_ids", &max_draw_ids);
        typed_asset->max_draw_ids = (u16)max_draw_ids;

        // Depth test
        typed_asset->depth_test = false;
        kson_object_property_value_get_bool(&tree.root, "depth_test", &typed_asset->depth_test);

        // Depth write
        typed_asset->depth_write = false;
        kson_object_property_value_get_bool(&tree.root, "depth_write", &typed_asset->depth_write);

        // Stencil test
        typed_asset->stencil_test = false;
        kson_object_property_value_get_bool(&tree.root, "stencil_test", &typed_asset->stencil_test);

        // Stencil write
        typed_asset->stencil_write = false;
        kson_object_property_value_get_bool(&tree.root, "stencil_write", &typed_asset->stencil_write);

        // Supports wireframe
        typed_asset->supports_wireframe = false;
        kson_object_property_value_get_bool(&tree.root, "supports_wireframe", &typed_asset->supports_wireframe);

        // Colour read.
        if (!kson_object_property_value_get_bool(&tree.root, "colour_read", &typed_asset->colour_read)) {
            typed_asset->colour_read = true; // NOTE: colour read is on by default if not specified.
        }

        // Colour write.
        if (!kson_object_property_value_get_bool(&tree.root, "colour_write", &typed_asset->colour_write)) {
            typed_asset->colour_write = true; // NOTE: colour write is on by default if not specified.
        }

        // Cull mode.
        const char* cull_mode = 0;
        if (kson_object_property_value_get_string(&tree.root, "cull_mode", &cull_mode) && cull_mode) {
            typed_asset->cull_mode = string_to_face_cull_mode(cull_mode);
        } else {
            // Defaults to backface culling when not provided.
            typed_asset->cull_mode = FACE_CULL_MODE_BACK;
        }

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

        // Uniforms
        kson_object uniforms_obj = {0};
        if (kson_object_property_value_get_object(&tree.root, "uniforms", &uniforms_obj)) {

            kson_array per_frame_array = {0};
            kson_array per_group_array = {0};
            kson_array per_draw_array = {0};
            u32 per_frame_count = 0;
            u32 per_group_count = 0;
            u32 per_draw_count = 0;

            if (kson_object_property_value_get_array(&uniforms_obj, "per_frame", &per_frame_array)) {
                kson_array_element_count_get(&per_frame_array, &per_frame_count);
            }
            if (kson_object_property_value_get_array(&uniforms_obj, "per_group", &per_group_array)) {
                kson_array_element_count_get(&per_group_array, &per_group_count);
            }
            if (kson_object_property_value_get_array(&uniforms_obj, "per_draw", &per_draw_array)) {
                kson_array_element_count_get(&per_draw_array, &per_draw_count);
            }

            typed_asset->uniform_count = per_frame_count + per_group_count + per_draw_count;
            typed_asset->uniforms = kallocate(sizeof(kasset_shader_uniform) * typed_asset->uniform_count, MEMORY_TAG_ARRAY);
            u32 uniform_index = 0;

            // Per-frame
            if (!extract_frequency_uniforms(SHADER_UPDATE_FREQUENCY_PER_FRAME, per_frame_count, &per_frame_array, typed_asset, &uniform_index)) {
                KERROR("Failed to extract per-frame uniforms. See logs for details.");
                return false;
            }

            // per-group
            if (!extract_frequency_uniforms(SHADER_UPDATE_FREQUENCY_PER_GROUP, per_group_count, &per_group_array, typed_asset, &uniform_index)) {
                KERROR("Failed to extract per-group uniforms. See logs for details.");
                return false;
            }

            // per-draw
            if (!extract_frequency_uniforms(SHADER_UPDATE_FREQUENCY_PER_DRAW, per_draw_count, &per_draw_array, typed_asset, &uniform_index)) {
                KERROR("Failed to extract per-draw uniforms. See logs for details.");
                return false;
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

static b8 extract_frequency_uniforms(shader_update_frequency frequency, u32 frequency_uniform_count, kson_array* frequency_array, kasset_shader* typed_asset, u32* uniform_index) {
    for (u32 i = 0; i < frequency_uniform_count; ++i) {
        kson_object uniform_obj = {0};
        kson_array_element_value_get_object(frequency_array, i, &uniform_obj);
        kasset_shader_uniform* uniform = &typed_asset->uniforms[(*uniform_index)];

        // Type is required.
        const char* temp = 0;
        if (!kson_object_property_value_get_string(&uniform_obj, "type", &temp)) {
            KERROR("Uniform type is required (uniform index=%u, freq=%s, freq index=%u)", *uniform_index, shader_update_frequency_to_string(frequency), i);
            return false;
        }
        uniform->type = string_to_shader_uniform_type(temp);
        string_free(temp);

        // For struct types, the size is also required.
        if (uniform->type == SHADER_UNIFORM_TYPE_STRUCT) {
            i64 temp_size = 0;
            if (!kson_object_property_value_get_int(&uniform_obj, "size", &temp_size)) {
                KERROR("Size is required for struct uniform types (uniform index=%u, freq=%s, freq index=%u)", *uniform_index, shader_update_frequency_to_string(frequency), i);
                return false;
            }
            if (temp_size < 0) {
                KERROR("Struct size must be positive. Struct uniform cannot be processed. (uniform index=%u, freq=%s, freq index=%u, size=%lli.)", *uniform_index, shader_update_frequency_to_string(frequency), i, temp_size);
                return false;
            }
            uniform->size = (u32)temp_size;
        }

        // Check for an optional array size.
        i64 temp_array_size = 0;

        kson_object_property_value_get_int(&uniform_obj, "array_size", &temp_array_size);
        if (temp_array_size < 0) {
            KERROR("array_size must be positive. Value will be ignored, and uniform will be treated as a non-array. (uniform index=%u, freq=%s, freq index=%u, array_size=%lli.)", *uniform_index, shader_update_frequency_to_string(frequency), i, temp_array_size);
            temp_array_size = 0;
        }
        uniform->array_size = (u32)temp_array_size;

        // Uniform name.
        kson_object_property_value_get_string(&uniform_obj, "name", &uniform->name);

        // Also set frequency itself.
        uniform->frequency = frequency;

        (*uniform_index)++;
    }

    return true;
}
