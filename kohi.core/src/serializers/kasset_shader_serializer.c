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

    // max_instances
    kson_object_value_add_int(&tree.root, "max_instances", typed_asset->max_instances);

    // Depth test
    kson_object_value_add_boolean(&tree.root, "depth_test", typed_asset->depth_test);

    // Depth write
    kson_object_value_add_boolean(&tree.root, "depth_write", typed_asset->depth_write);

    // Stencil test
    kson_object_value_add_boolean(&tree.root, "stencil_test", typed_asset->stencil_test);

    // Stencil write
    kson_object_value_add_boolean(&tree.root, "stencil_write", typed_asset->stencil_write);

    // Stages
    {
        kson_array stages_array = kson_array_create();
        for (u32 i = 0; i < typed_asset->stage_count; ++i) {
            kson_object stage_obj = kson_object_create();
            kasset_shader_stage* stage = &typed_asset->stages[i];

            kson_object_value_add_string(&stage_obj, "type", shader_stage_to_string(stage->type));
            kson_object_value_add_string(&stage_obj, "source_asset_name", stage->source_asset_name);
            kson_object_value_add_string(&stage_obj, "package_name", stage->package_name);

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
            if (per_frame_count) {
                kson_object_value_add_array(&uniforms_obj, "global", per_frame_array);
            }
            if (per_group_count) {
                kson_object_value_add_array(&uniforms_obj, "instance", per_group_array);
            }
            if (per_draw_count) {
                kson_object_value_add_array(&uniforms_obj, "local", per_draw_array);
            }
            kson_object_value_add_object(&tree.root, "uniforms", uniforms_obj);
        }
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

        // max_instances
        i64 max_instances = 0;
        kson_object_property_value_get_int(&tree.root, "max_instances", &max_instances);
        typed_asset->max_instances = (u16)max_instances;

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

        // Stages
        kson_array stages_array;
        if (kson_object_property_value_get_object(&tree.root, "stages", &stages_array)) {
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
        if (kson_object_property_value_get_object(&tree.root, "attributes", &attributes_array)) {
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

            kson_array global_array = {0};
            kson_array instance_array = {0};
            kson_array local_array = {0};
            u32 global_count = 0;
            u32 instance_count = 0;
            u32 local_count = 0;

            if (kson_object_property_value_get_object(&uniforms_obj, "global", &global_array)) {
                kson_array_element_count_get(&global_array, &global_count);
            }
            if (kson_object_property_value_get_object(&uniforms_obj, "instance", &instance_array)) {
                kson_array_element_count_get(&instance_array, &instance_count);
            }
            if (kson_object_property_value_get_object(&uniforms_obj, "local", &local_array)) {
                kson_array_element_count_get(&local_array, &local_count);
            }

            typed_asset->uniform_count = global_count + instance_count + local_count;
            typed_asset->uniforms = kallocate(sizeof(kasset_shader_uniform) * typed_asset->uniform_count, MEMORY_TAG_ARRAY);
            u32 uniform_index = 0;

            // Globals
            for (u32 i = 0; i < global_count; ++i) {
                kson_object uniform_obj = {0};
                kson_array_element_value_get_object(&global_array, i, &uniforms_obj);
                kasset_shader_uniform* uniform = &typed_asset->uniforms[uniform_index];

                const char* temp = 0;
                kson_object_property_value_get_string(&uniform_obj, "type", &temp);
                uniform->type = string_to_shader_uniform_type(temp);
                string_free(temp);

                kson_object_property_value_get_string(&uniform_obj, "name", &uniform->name);

                uniform_index++;
            }

            // Instance
            for (u32 i = 0; i < instance_count; ++i) {
                kson_object uniform_obj = {0};
                kson_array_element_value_get_object(&instance_array, i, &uniforms_obj);
                kasset_shader_uniform* uniform = &typed_asset->uniforms[uniform_index];

                const char* temp = 0;
                kson_object_property_value_get_string(&uniform_obj, "type", &temp);
                uniform->type = string_to_shader_uniform_type(temp);
                string_free(temp);

                kson_object_property_value_get_string(&uniform_obj, "name", &uniform->name);

                uniform_index++;
            }

            // Local
            for (u32 i = 0; i < local_count; ++i) {
                kson_object uniform_obj = {0};
                kson_array_element_value_get_object(&local_array, i, &uniforms_obj);
                kasset_shader_uniform* uniform = &typed_asset->uniforms[uniform_index];

                const char* temp = 0;
                kson_object_property_value_get_string(&uniform_obj, "type", &temp);
                uniform->type = string_to_shader_uniform_type(temp);
                string_free(temp);

                kson_object_property_value_get_string(&uniform_obj, "name", &uniform->name);

                uniform_index++;
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
