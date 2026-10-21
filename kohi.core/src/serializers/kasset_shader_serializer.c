#include "kasset_shader_serializer.h"

#include "assets/kasset_types.h"

#include "core_render_types.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

#define SHADER_ASSET_VERSION 1

const char *kasset_shader_serialize (const kasset_shader *asset) {
	if (!asset) {
		KERROR("kasset_shader_serialize requires an asset to serialize, ya dingus!");
		return 0;
	}

	kasset_shader *typed_asset = (kasset_shader *)asset;

	// Validate that there are actual stages, because these are required.
	if (!typed_asset->pipelines || !typed_asset->pipeline_count || !typed_asset->pipelines[0].stage_count) {
		KERROR("kasset_shader_serializer requires at least one stage to serialize. Otherwise it's an invalid shader, ya dingus.");
		return 0;
	}

	const char *out_str = 0;

	// Setup the KSON tree to serialize below.
	kson_tree tree = {0};
	tree.root = kson_object_create();

	// version
	if (!kson_object_value_add_int(&tree.root, "version", SHADER_ASSET_VERSION)) {
		KERROR("Failed to add version, which is a required field.");
		goto cleanup_kson;
	}

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

	kson_object_value_add_string(&tree.root, "default_topology", topology_type_to_string(typed_asset->default_topology));

	// Attachments. Required.
	kson_object attachments_obj = kson_object_create();
	// Colour attachments
	if (typed_asset->colour_attachment_count) {
		kson_array colour_attachments_array = kson_array_create();
		for (u8 i = 0; i < typed_asset->colour_attachment_count; ++i) {
			kasset_shader_attachment *att = &typed_asset->colour_attachments[i];

			kson_object att_obj = kson_object_create();
			if (att->name) {
				kson_object_value_add_string(&att_obj, "name", att->name);
				kson_object_value_add_string(&att_obj, "format", string_from_kpixel_format(att->format));
			}

			kson_array_value_add_object(&colour_attachments_array, att_obj);
		}

		kson_object_value_add_array(&attachments_obj, "colour", colour_attachments_array);
	}

	// Depth attachment
	if (typed_asset->depth_attachment.format != KPIXEL_FORMAT_UNKNOWN) {
		kasset_shader_attachment *att = &typed_asset->depth_attachment;
		kson_object att_obj = kson_object_create();
		if (att->name) {
			kson_object_value_add_string(&att_obj, "name", att->name);
		}
		kson_object_value_add_string(&att_obj, "format", string_from_kpixel_format(att->format));

		kson_object_value_add_object(&attachments_obj, "depth", att_obj);
	}

	// Stencil attachment
	if (typed_asset->stencil_attachment.format != KPIXEL_FORMAT_UNKNOWN) {
		kasset_shader_attachment *att = &typed_asset->stencil_attachment;
		kson_object att_obj = kson_object_create();
		if (att->name) {
			kson_object_value_add_string(&att_obj, "name", att->name);
		}
		kson_object_value_add_string(&att_obj, "format", string_from_kpixel_format(att->format));

		kson_object_value_add_object(&attachments_obj, "stencil", att_obj);
	}

	kson_object_value_add_object(&tree.root, "attachments", attachments_obj);

	// Vertex layout pipelines
	kson_array pipelines_array = kson_array_create();
	for (u8 pi = 0; pi < typed_asset->pipeline_count; ++pi) {
		kasset_shader_pipeline *pipeline = &typed_asset->pipelines[pi];

		kson_object pipeline_obj = kson_object_create();

		// Stages
		{
			kson_array stages_array = kson_array_create();
			for (u32 i = 0; i < pipeline->stage_count; ++i) {
				kson_object stage_obj = kson_object_create();
				kasset_shader_stage *stage = &pipeline->stages[i];

				kson_object_value_add_string(&stage_obj, "type", shader_stage_to_string(stage->type));
				if (stage->source_asset_name) {
					kson_object_value_add_string(&stage_obj, "source_asset_name", stage->source_asset_name);
				}
				if (stage->package_name) {
					kson_object_value_add_string(&stage_obj, "package_name", stage->package_name);
				}

				kson_array_value_add_object(&stages_array, stage_obj);
			}
			kson_object_value_add_array(&pipeline_obj, "stages", stages_array);
		}

		// Attributes
		if (pipeline->attribute_count > 0) {
			kson_array attributes_array = kson_array_create();
			for (u32 i = 0; i < pipeline->attribute_count; ++i) {
				kson_object attribute_obj = kson_object_create();
				kasset_shader_attribute *attribute = &pipeline->attributes[i];

				kson_object_value_add_string(&attribute_obj, "type", shader_attribute_type_to_string(attribute->type));
				kson_object_value_add_string(&attribute_obj, "name", attribute->name);

				kson_array_value_add_object(&attributes_array, attribute_obj);
			}
			kson_object_value_add_array(&pipeline_obj, "attributes", attributes_array);
		}

		kson_array_value_add_object(&pipelines_array, pipeline_obj);
	}
	kson_object_value_add_array(&tree.root, "pipelines", pipelines_array);

	// Binding sets
	if (typed_asset->binding_set_count > 0) {
		kson_array binding_sets_array = kson_array_create();

		for (u32 bs = 0; bs < typed_asset->binding_set_count; ++bs) {

			kson_object binding_set_obj = kson_object_create();
			shader_binding_set_config *binding_set = &typed_asset->binding_sets[bs];

			kson_object_value_add_kname_as_string(&binding_set_obj, "name", binding_set->name);
			kson_object_value_add_int(&binding_set_obj, "max_instance_count", binding_set->max_instance_count);

			kson_array bindings_array = kson_array_create();
			for (u8 b = 0; b < binding_set->binding_count; ++b) {
				shader_binding_config *binding = &binding_set->bindings[b];

				kson_object binding_obj = kson_object_create();

				kson_object_value_add_string(&binding_obj, "type", shader_binding_type_to_string(binding->binding_type));
				if (binding->name) {
					kson_object_value_add_kname_as_string(&binding_obj, "name", binding->name);
				}

				switch (binding->binding_type) {
				case SHADER_BINDING_TYPE_UBO:
					kson_object_value_add_int(&binding_obj, "data_size", binding->data_size);
					kson_object_value_add_int(&binding_obj, "offset", binding->offset);
					break;
				case SHADER_BINDING_TYPE_SSBO:
					if (binding->data_size) {
						kson_object_value_add_int(&binding_obj, "data_size", binding->data_size);
					}
					if (binding->offset) {
						kson_object_value_add_int(&binding_obj, "offset", binding->offset);
					}
					break;
				case SHADER_BINDING_TYPE_TEXTURE:
					kson_object_value_add_int(&binding_obj, "array_size", binding->array_size);
					kson_object_value_add_string(&binding_obj, "texture_type", ktexture_type_to_string(binding->texture_type));
					break;
				case SHADER_BINDING_TYPE_SAMPLER:
					kson_object_value_add_int(&binding_obj, "array_size", binding->array_size);
					kson_object_value_add_string(&binding_obj, "sampler_type", shader_sampler_type_to_string(binding->sampler_type));
					break;
				default:
					// Skip unknown types
					continue;
				}

				// Add binding to the bindings array.
				kson_array_value_add_object(&bindings_array, binding_obj);
			}

			// Add bindings array to the binding set object.
			kson_object_value_add_array(&binding_set_obj, "bindings", bindings_array);

			// Add the binding set to the binding sets array
			kson_array_value_add_object(&binding_sets_array, binding_set_obj);
		}

		// Add the binding sets array to the root
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

b8 kasset_shader_deserialize (const char *file_text, kasset_shader *out_asset) {
	if (out_asset) {
		b8 success = false;
		kasset_shader *typed_asset = (kasset_shader *)out_asset;

		// Deserialize the loaded asset data
		kson_tree tree = {0};
		if (!kson_tree_from_string(file_text, &tree)) {
			KERROR("Failed to parse asset data for shader. See logs for details.");
			goto cleanup_kson;
		}

		// version
		if (!kson_object_property_value_get_int(&tree.root, "version", (i64 *)(&typed_asset->version))) {
			KERROR("Failed to parse version, which is a required field.");
			goto cleanup_kson;
		}

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
					const char *topology_type_str = 0;
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

		// Default topology type. Uses triangle list if not set.
		typed_asset->default_topology = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
		const char *default_topology_type_str = 0;
		kson_object_property_value_get_string(&tree.root, "default_topology", &default_topology_type_str);
		if (default_topology_type_str) {
			typed_asset->default_topology = string_to_topology_type(default_topology_type_str);
			string_free(default_topology_type_str);
		}

		// Attachments. Required.
		kson_object attachments_obj;
		if (!kson_object_property_value_get_object(&tree.root, "attachments", &attachments_obj)) {
			KERROR("Property ;'attachments' is required at the root level for shader configurations. At least one attachment is required.");
			goto cleanup_kson;
		}
		u8 attachment_count = 0;
		kson_array colour_attachments_array;
		if (kson_object_property_value_get_array(&attachments_obj, "colour", &colour_attachments_array)) {
			u32 count = 0;
			kson_array_element_count_get(&colour_attachments_array, &count);

			out_asset->colour_attachment_count = (u8)count;
			out_asset->colour_attachments = KALLOC_TYPE_CARRAY(kasset_shader_attachment, count);

			for (u32 i = 0; i < count; ++i) {
				kasset_shader_attachment *att = &out_asset->colour_attachments[i];
				kson_object att_obj;
				kson_array_element_value_get_object(&colour_attachments_array, i, &att_obj);

				kson_object_property_value_get_string(&att_obj, "name", &att->name);

				const char *tmp_format = 0;
				kson_object_property_value_get_string(&att_obj, "format", &tmp_format);
				att->format = string_to_kpixel_format(tmp_format);
				string_free(tmp_format);
			}

			attachment_count += (u8)count;
		}

		// depth attachment
		{
			kson_object att_obj;
			if (kson_object_property_value_get_object(&attachments_obj, "depth", &att_obj)) {
				kasset_shader_attachment *att = &out_asset->depth_attachment;

				kson_object_property_value_get_string(&att_obj, "name", &att->name);

				const char *tmp_format = 0;
				kson_object_property_value_get_string(&att_obj, "format", &tmp_format);
				att->format = string_to_kpixel_format(tmp_format);
				string_free(tmp_format);

				attachment_count++;

				// Ensure format is valid for the attachment type.
				if (att->format != KPIXEL_FORMAT_D32 && att->format != KPIXEL_FORMAT_D24) {
					KERROR("Invalid depth format - must either be d32 or d24.");
					goto cleanup_kson;
				}
			}
		}

		// stencil attachment
		{
			kson_object att_obj;
			if (kson_object_property_value_get_object(&attachments_obj, "stencil", &att_obj)) {
				kasset_shader_attachment *att = &out_asset->stencil_attachment;

				kson_object_property_value_get_string(&att_obj, "name", &att->name);

				const char *tmp_format = 0;
				kson_object_property_value_get_string(&att_obj, "format", &tmp_format);
				att->format = string_to_kpixel_format(tmp_format);
				string_free(tmp_format);

				attachment_count++;

				// Ensure format is valid for the attachment type.
				if (att->format != KPIXEL_FORMAT_S8) {
					KERROR("Invalid stencil format - must either be s8.");
					goto cleanup_kson;
				}
			}
		}

		// Ensure there is at least one attachment.
		if (!attachment_count) {
			KERROR("A minimum of one attachment must exist in shader config.");
			goto cleanup_kson;
		}

		// Pipelines, one per vertex layout.
		kson_array pipelines_array;
		if (kson_object_property_value_get_array(&tree.root, "pipelines", &pipelines_array)) {
			u32 pipeline_count = 0;
			if (!kson_array_element_count_get(&pipelines_array, &pipeline_count) || pipeline_count == 0) {
				KERROR("Pipelines are required for shader configurations. Make sure at least one exists.");
				goto cleanup_kson;
			}

			typed_asset->pipeline_count = (u8)pipeline_count;
			typed_asset->pipelines = KALLOC_TYPE_CARRAY(kasset_shader_pipeline, typed_asset->pipeline_count);

			for (u8 pi = 0; pi < typed_asset->pipeline_count; ++pi) {
				kasset_shader_pipeline *pipeline = &typed_asset->pipelines[pi];
				kson_object pipeline_obj;
				kson_array_element_value_get_object(&pipelines_array, pi, &pipeline_obj);

				// Stages
				kson_array stages_array;
				if (kson_object_property_value_get_array(&pipeline_obj, "stages", &stages_array)) {
					u32 stage_count = 0;
					if (!kson_array_element_count_get(&stages_array, &stage_count) || stage_count == 0) {
						KERROR("Stages are required for shader configurations. Make sure at least one exists.");
						goto cleanup_kson;
					}

					pipeline->stage_count = stage_count;
					pipeline->stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, pipeline->stage_count);
					for (u8 i = 0; i < pipeline->stage_count; ++i) {
						kson_object stage_obj = {0};
						kson_array_element_value_get_object(&stages_array, i, &stage_obj);

						kasset_shader_stage *stage = &pipeline->stages[i];
						const char *temp = 0;

						kson_object_property_value_get_string(&stage_obj, "type", &temp);
						stage->type = string_to_shader_stage(temp);
						string_free(temp);

						kson_object_property_value_get_string(&stage_obj, "source_asset_name", &stage->source_asset_name);
						kson_object_property_value_get_string(&stage_obj, "package_name", &stage->package_name);
					}
				} else {
					KERROR("Stages are required for shader configurations. Make sure at least one exists.");
					goto cleanup_kson;
				}

				// Attributes
				kson_array attributes_array = {0};
				if (kson_object_property_value_get_array(&pipeline_obj, "attributes", &attributes_array)) {
					u32 attribute_count = 0;
					if (!kson_array_element_count_get(&attributes_array, &attribute_count)) {
						KERROR("Failed to get attributes_array count. See logs for details.");
						goto cleanup_kson;
					}

					pipeline->attribute_count = attribute_count;
					pipeline->attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, pipeline->attribute_count);
					for (u32 i = 0; i < pipeline->attribute_count; ++i) {
						kson_object attribute_obj = {0};
						kson_array_element_value_get_object(&attributes_array, i, &attribute_obj);
						kasset_shader_attribute *attribute = &pipeline->attributes[i];

						const char *temp = 0;
						kson_object_property_value_get_string(&attribute_obj, "type", &temp);
						attribute->type = string_to_shader_attribute_type(temp);
						string_free(temp);

						kson_object_property_value_get_string(&attribute_obj, "name", &attribute->name);
					}
				}
			}
		}

		// Binding sets
		kson_array binding_sets_array = {0};
		if (kson_object_property_value_get_array(&tree.root, "binding_sets", &binding_sets_array)) {

			u32 binding_set_count = 0;
			if (!kson_array_element_count_get(&binding_sets_array, &binding_set_count)) {
				KERROR("Failed to get count for binding sets.");
				goto cleanup_kson;
			}

			typed_asset->binding_set_count = binding_set_count;
			typed_asset->binding_sets = KALLOC_TYPE_CARRAY(shader_binding_set_config, binding_set_count);
			for (u32 bs = 0; bs < binding_set_count; ++bs) {

				kson_object binding_set_obj = {0};
				if (!kson_array_element_value_get_object(&binding_sets_array, bs, &binding_set_obj)) {
					KERROR("Failed to get binding set at index %u", bs);
					goto cleanup_kson;
				}

				shader_binding_set_config *binding_set = &typed_asset->binding_sets[bs];

				kson_object_property_value_get_string_as_kname(&binding_set_obj, "name", &binding_set->name);

				i64 max_instance_count_i = 0;
				if (!kson_object_property_value_get_int(&binding_set_obj, "max_instance_count", &max_instance_count_i)) {
					KERROR("'max_instance_count' not provided for binding set %u. Defaulting to 1, but this may cause problems.", bs);
				}
				binding_set->max_instance_count = (u32)max_instance_count_i;

				kson_array bindings_array = {0};
				if (!kson_object_property_value_get_array(&binding_set_obj, "bindings", &bindings_array)) {
					KERROR("Required field 'bindings' not present in binding set %u", bs);
					goto cleanup_kson;
				}

				u32 binding_count = 0;
				kson_array_element_count_get(&bindings_array, &binding_count);
				binding_set->binding_count = binding_count;

				binding_set->bindings = KALLOC_TYPE_CARRAY(shader_binding_config, binding_set->binding_count);

				// Extract binding properties.
				for (u8 b = 0; b < binding_set->binding_count; ++b) {
					kson_object binding_obj = {0};
					kson_array_element_value_get_object(&bindings_array, b, &binding_obj);

					shader_binding_config *binding = &binding_set->bindings[b];

					// Binding type is required.
					const char *binding_type_str = 0;
					if (!kson_object_property_value_get_string(&binding_obj, "type", &binding_type_str)) {
						KERROR("Required binding type not present - set=%u, binding=%", bs, b);
						goto cleanup_kson;
					}
					binding->binding_type = shader_binding_type_from_string(binding_type_str);
					string_free(binding_type_str);

					// Keep a running count of each type.
					switch (binding->binding_type) {
					case SHADER_BINDING_TYPE_UBO:
						binding_set->ubo_index = b;
						break;
					case SHADER_BINDING_TYPE_SSBO:
						binding_set->ssbo_count++;
						break;
					case SHADER_BINDING_TYPE_TEXTURE:
						binding_set->texture_count++;
						break;
					case SHADER_BINDING_TYPE_SAMPLER:
						binding_set->sampler_count++;
						break;
					default:
						// Skip if type is invalid.
						continue;
					}

					// Name is optional except for SSBO.
					if (!kson_object_property_value_get_string_as_kname(&binding_obj, "name", &binding->name)) {
						if (binding->binding_type == SHADER_BINDING_TYPE_SSBO) {
							KERROR("name is required for storage/SSBO binding type. set=%u, binding=%u", bs, b);
							goto cleanup_kson;
						} else {
							char name_buf[512] = {0};
							string_nformat(name_buf, 511, "%s_binding_set_%u_binding_%u", kname_string_get(typed_asset->name), bs, b);
							binding->name = kname_create(name_buf);
						}
					}

					// Data size is required for UBO.
					// For non-existant SSBOs, it's required as well, but there's no way to tell here that that's the case.
					i64 data_size = 0;
					if (!kson_object_property_value_get_int(&binding_obj, "data_size", &data_size)) {
						if (binding->binding_type == SHADER_BINDING_TYPE_UBO) {
							KERROR("data_size is required for UBO. set=%u, binding=%u", bs, b);
							goto cleanup_kson;
						}
					}
					if (!data_size && binding->binding_type == SHADER_BINDING_TYPE_UBO) {
						KERROR("A non-zero data_size is required for UBO. set=%u, binding=%u", bs, b);
						goto cleanup_kson;
					}
					binding->data_size = data_size;

					// Offset is optional, defaults to 0. Ignored other than UBO
					i64 offset = 0;
					if (kson_object_property_value_get_int(&binding_obj, "offset", &offset)) {
						if (binding->binding_type != SHADER_BINDING_TYPE_UBO) {
							KWARN("offset is ignored for types other than UBO. set=%u, binding=%u", bs, b);
						}
					}
					binding->offset = offset;

					if (binding->binding_type == SHADER_BINDING_TYPE_TEXTURE || binding->binding_type == SHADER_BINDING_TYPE_SAMPLER) {

						// Array size is only looked at for textures and samplers. Default = 1.
						i64 array_size = 1;
						kson_object_property_value_get_int(&binding_obj, "array_size", &array_size);
						binding->array_size = array_size;
					}

					if (binding->binding_type == SHADER_BINDING_TYPE_TEXTURE) {

						// texture_type is only looked at for textures. Default = 2D.
						const char *texture_type_str = 0;
						kson_object_property_value_get_string(&binding_obj, "texture_type", &texture_type_str);
						if (texture_type_str) {
							binding->texture_type = ktexture_type_from_string(texture_type_str);
							string_free(texture_type_str);
						} else {
							binding->texture_type = KTEXTURE_TYPE_2D;
						}
					}

					if (binding->binding_type == SHADER_BINDING_TYPE_SAMPLER) {

						// sampler_type is only looked at for samplers. Default = 2D.
						const char *sampler_type_str = 0;
						kson_object_property_value_get_string(&binding_obj, "sampler_type", &sampler_type_str);
						if (sampler_type_str) {
							binding->sampler_type = shader_sampler_type_from_string(sampler_type_str);
							string_free(sampler_type_str);
						} else {
							binding->sampler_type = SHADER_SAMPLER_TYPE_2D;
						}
					}
				} // binding properties

			} // binding sets
		} else {
			KERROR("No binding sets are defined. This is requied in shader config.");
			goto cleanup_kson;
		}

		success = true;
	cleanup_kson:
		kson_tree_cleanup(&tree);
		return success;
	}

	KERROR("kasset_shader_deserialize serializer requires an asset to deserialize to, ya dingus!");
	return false;
}
