#include "kshader_system.h"

#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <core_render_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_shader_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <utils/render_type_utils.h>

#include "core/engine.h"
#if KOHI_HOT_RELOAD
#	include "core/event.h"
#endif
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "systems/asset_system.h"
#include "systems/texture_system.h"

typedef struct kshader_pipeline_data {

	u8 attribute_count;
	/** @brief An array of attributes. */
	shader_attribute *attributes;

	/** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
	u16 attribute_stride;

	u8 shader_stage_count;

	// Array of stages.
	shader_stage *stages;
	// Array of pointers to text assets, one per stage.
	kasset_text **stage_source_text_assets;
	// Array of generations of stage source text resources. Matches size of stage_source_text_resources;
	u32 *stage_source_text_generations;
	// Array of names of stage assets.
	kname *stage_names;
	// Array of source text for stages. Matches size of stage_source_text_resources;
	const char **stage_sources;
#if KOHI_DEBUG
	// Array of file watch ids, one per stage.
	u32 *watch_ids;
#endif
} kshader_pipeline_data;

typedef struct kshader_attachment {
	kname name;
	kpixel_format format;
} kshader_attachment;

/**
 * @brief Represents a shader on the frontend. This is internal to the shader system.
 */
typedef struct kshader_data {

	kname name;

	shader_flag_bits flags;

	/** @brief The types of topologies used by the shader and its pipeline. See primitive_topology_type. */
	primitive_topology_type_bits topology_types;
	primitive_topology_type default_topology;

	/** @brief The internal state of the shader. */
	shader_state state;

	// A constant pointer to the shader config asset.
	const kasset_shader *shader_asset;

	u8 colour_attachment_count;
	kshader_attachment *colour_attachments;

	kshader_attachment depth_attachment;
	kshader_attachment stencil_attachment;

	u8 pipeline_count;
	kshader_pipeline_data *pipelines;

} kshader_data;

// The internal shader system state.
typedef struct kshader_system_state {
	// A pointer to the renderer system state.
	struct renderer_system_state *renderer;
	struct texture_system_state *texture_system;

	// The max number of textures that can be bound for a single draw call, provided by the renderer.
	u16 max_bound_texture_count;
	// The max number of samplers that can be bound for a single draw call, provided by the renderer.
	u16 max_bound_sampler_count;

	// This system's configuration.
	kshader_system_config config;
	// A collection of created shaders.
	kshader_data *shaders;

} kshader_system_state;

// A pointer to hold the internal system state.
// FIXME: Get rid of this and all references to it and use the engine_systems_get() instead where needed.
static kshader_system_state *state_ptr = 0;

static kshader generate_new_shader_handle (void);
static kshader shader_create (const kasset_shader *asset);
static b8 shader_reload (kshader_data *shader, kshader shader_handle);

static void internal_shader_destroy (kshader *shader);
///////////////////////

#if KOHI_HOT_RELOAD
static b8 file_watch_event (u16 code, void *sender, void *listener_inst, event_context context) {
	kshader_system_state *typed_state = (kshader_system_state *)listener_inst;

	u32 watch_id = context.data.u32[0];
	if (code == EVENT_CODE_ASSET_HOT_RELOADED) {

		// TODO: more verification to make sure this is correct.
		kasset_text *shader_source_asset = (kasset_text *)sender;

		// Search shaders for the one whose generations are out of sync.
		for (u32 i = 0; i < typed_state->config.max_shader_count; ++i) {
			kshader_data *shader = &typed_state->shaders[i];

			b8 reload_required = false;

			for (u8 pi = 0; pi < shader->pipeline_count; ++pi) {
				kshader_pipeline_data *p = &shader->pipelines[pi];

				for (u32 w = 0; w < p->shader_stage_count; ++w) {
					if (p->watch_ids[w] == watch_id) {
						// Replace the existing shader stage source with the new.
						string_free(p->stage_sources[w]);
						p->stage_sources[w] = KNULL;
						p->stage_sources[w] = string_duplicate(shader_source_asset->content);

						// Release the asset.
						asset_system_release_text(engine_systems_get()->asset_state, shader_source_asset);
						reload_required = true;
						break;
					}
				}
			}

			// Reload if needed.
			if (reload_required) {
				kshader handle = i;
				if (!shader_reload(shader, handle)) {
					KWARN("Shader hot-reload failed for shader '%s'. See logs for details.", kname_string_get(shader->name));
				}
			}
		}
	}

	// Return as unhandled to allow other systems to pick it up.
	return false;
}
#endif

b8 kshader_system_initialize (u64 *memory_requirement, void *memory, void *config) {
	kshader_system_config *typed_config = (kshader_system_config *)config;
	// Verify configuration.
	if (typed_config->max_shader_count < 512) {
		if (typed_config->max_shader_count == 0) {
			KERROR("kshader_system_initialize - config.max_shader_count must be greater than 0. Defaulting to 512.");
			typed_config->max_shader_count = 512;
		} else {
			KWARN("kshader_system_initialize - config.max_shader_count is recommended to be at least 512.");
		}
	}

	// Block of memory will contain state structure then the block for the shader array.
	u64 struct_requirement = sizeof(kshader_system_state);
	u64 shader_array_requirement = sizeof(kshader_data) * typed_config->max_shader_count;
	*memory_requirement = struct_requirement + shader_array_requirement;

	if (!memory) {
		return true;
	}

	// Setup the state pointer, memory block, shader array, etc.
	state_ptr = memory;
	u64 addr = (u64)memory;
	state_ptr->shaders = (void *)(addr + struct_requirement);
	state_ptr->config = *typed_config;

	// Invalidate all shader ids.
	for (u32 i = 0; i < typed_config->max_shader_count; ++i) {
		state_ptr->shaders[i].state = SHADER_STATE_FREE;
	}

	// Keep a pointer to the renderer state.
	state_ptr->renderer = engine_systems_get()->renderer_system;
	state_ptr->texture_system = engine_systems_get()->texture_system;

	// Track max texture and sampler counts.
	state_ptr->max_bound_sampler_count = renderer_max_bound_sampler_count_get(state_ptr->renderer);
	state_ptr->max_bound_texture_count = renderer_max_bound_texture_count_get(state_ptr->renderer);

	// Watch for file hot reloads in debug builds.
#if KOHI_HOT_RELOAD
	event_register(EVENT_CODE_ASSET_HOT_RELOADED, state_ptr, file_watch_event);
#endif

	return true;
}

void kshader_system_shutdown (void *state) {
	if (state) {
		// Destroy any shaders still in existence.
		kshader_system_state *st = (kshader_system_state *)state;
		for (u32 i = 0; i < st->config.max_shader_count; ++i) {
			kshader_data *s = &st->shaders[i];
			if (s->state != SHADER_STATE_FREE) {
				kshader temp_handle = i;
				internal_shader_destroy(&temp_handle);
			}
		}
		kzero_memory(st, sizeof(kshader_system_state));
	}

	state_ptr = 0;
}

kshader kshader_system_get (kname name, kname package_name) {
	if (name == INVALID_KNAME) {
		return KSHADER_INVALID;
	}

	u32 count = state_ptr->config.max_shader_count;
	for (u16 i = 0; i < count; ++i) {
		if (state_ptr->shaders[i].name == name) {
			return i;
		}
	}

	// Not found, attempt to load the shader asset.
	kasset_shader *shader_asset = asset_system_request_shader_from_package_sync(engine_systems_get()->asset_state, kname_string_get(package_name), kname_string_get(name));
	if (!shader_asset) {
		KERROR("Failed to load shader resource for shader '%s'.", kname_string_get(name));
		return KSHADER_INVALID;
	}

	// Create the shader.
	kshader shader_handle = shader_create(shader_asset);

	if (shader_handle == KSHADER_INVALID) {
		KERROR("Failed to create shader '%s'.", kname_string_get(name));
		KERROR("There is no shader available called '%s', and one by that name could also not be loaded.", kname_string_get(name));
		return shader_handle;
	}

	return shader_handle;
}

kshader kshader_system_get_from_source (kname name, const char *shader_config_source) {
	if (name == INVALID_KNAME) {
		return KSHADER_INVALID;
	}

	kasset_shader *temp_asset = KALLOC_TYPE(kasset_shader, MEMORY_TAG_ASSET);
	if (!kasset_shader_deserialize(shader_config_source, temp_asset)) {
		return KSHADER_INVALID;
	}
	temp_asset->name = name;

	// Create the shader.
	kshader shader_handle = shader_create(temp_asset);

	kshader_data *s = &state_ptr->shaders[shader_handle];
	// Clear the shader asset on shaders loaded directly from source.
	s->shader_asset = KNULL;

	asset_system_release_shader(engine_systems_get()->asset_state, temp_asset);

	if (shader_handle == KSHADER_INVALID) {
		KERROR("Failed to create shader '%s' from config source.", kname_string_get(name));
		return shader_handle;
	}

	return shader_handle;
}

static void internal_shader_destroy (kshader *shader) {
	if (*shader == KSHADER_INVALID) {
		return;
	}

	kshader_data *s = &state_ptr->shaders[*shader];
	if (s->state == SHADER_STATE_FREE) {
		return;
	}

	// Set it to be unusable right away.
	s->state = SHADER_STATE_FREE;

	renderer_shader_destroy(state_ptr->renderer, *shader);

	struct asset_system_state *asset_state = engine_systems_get()->asset_state;

	if (s->pipeline_count && s->pipelines) {
		for (u8 i = 0; i < s->pipeline_count; ++i) {
			kshader_pipeline_data *p = &s->pipelines[i];

			if (p->shader_stage_count) {
				for (u8 si = 0; si < p->shader_stage_count; ++si) {
					if (p->stage_sources) {
						string_free(p->stage_sources[si]);
						p->stage_sources[si] = KNULL;
					}
					asset_system_release_text(asset_state, p->stage_source_text_assets[si]);
					p->stage_source_text_assets[si] = KNULL;
				}
				KFREE_TYPE_CARRAY(p->stage_source_text_assets, kasset_text *, p->shader_stage_count);
				KFREE_TYPE_CARRAY(p->stage_sources, const char *, p->shader_stage_count);
				KFREE_TYPE_CARRAY(p->stage_source_text_generations, u32, p->shader_stage_count);
				KFREE_TYPE_CARRAY(p->stage_names, kname, p->shader_stage_count);
				KFREE_TYPE_CARRAY(p->stages, shader_stage, p->shader_stage_count);
#if KOHI_DEBUG
				KFREE_TYPE_CARRAY(p->watch_ids, u32, p->shader_stage_count);
#endif
			}

			KFREE_TYPE_CARRAY(p->attributes, shader_attribute, p->attribute_count);
		}
		KFREE_TYPE_CARRAY(s->pipelines, kshader_pipeline_data, s->pipeline_count);
	}

	if (s->colour_attachment_count && s->colour_attachments) {
		KFREE_TYPE_CARRAY(s->colour_attachments, kshader_attachment, s->colour_attachment_count);
	}

	asset_system_release_shader(asset_state, (kasset_shader *)s->shader_asset);

	s->name = INVALID_KNAME;

	// Make sure to invalidate the handle.
	*shader = KSHADER_INVALID;
}

void kshader_system_destroy (kshader *shader) {
	if (*shader == KSHADER_INVALID) {
		return;
	}

	internal_shader_destroy(shader);
}

b8 kshader_system_set_wireframe (kshader shader, b8 wireframe_enabled) {
	if (shader == KSHADER_INVALID) {
		KERROR("Invalid shader passed.");
		return false;
	}

	if (!wireframe_enabled) {
		renderer_shader_flag_set(state_ptr->renderer, shader, SHADER_FLAG_WIREFRAME_BIT, false);
		return true;
	}

	if (renderer_shader_supports_wireframe(state_ptr->renderer, shader)) {
		renderer_shader_flag_set(state_ptr->renderer, shader, SHADER_FLAG_WIREFRAME_BIT, true);
	}
	return true;
}

b8 kshader_system_use (kshader shader, u8 vertex_layout_index) {
	if (shader == KSHADER_INVALID) {
		KERROR("Invalid shader passed.");
		return false;
	}
	kshader_data *next_shader = &state_ptr->shaders[shader];
	if (!renderer_shader_use(state_ptr->renderer, shader, vertex_layout_index)) {
		KERROR("Failed to use shader '%s'.", next_shader->name);
		return false;
	}
	return true;
}

b8 kshader_system_use_with_topology (kshader shader, primitive_topology_type topology, u8 vertex_layout_index) {
	if (shader == KSHADER_INVALID) {
		KERROR("Invalid shader passed.");
		return false;
	}
	kshader_data *next_shader = &state_ptr->shaders[shader];
	if (!renderer_shader_use_with_topology(state_ptr->renderer, shader, topology, vertex_layout_index)) {
		KERROR("Failed to use shader '%s'.", next_shader->name);
		return false;
	}
	return true;
}

void kshader_set_immediate_data (kshader shader, const void *data, u8 size) {
	renderer_shader_set_immediate_data(engine_systems_get()->renderer_system, shader, data, size);
}
void kshader_set_binding_data (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size) {
	renderer_shader_set_binding_data(engine_systems_get()->renderer_system, shader, binding_set, instance_id, binding_index, offset, data, size);
}
void kshader_set_binding_texture (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture texture) {
	renderer_shader_set_binding_texture(engine_systems_get()->renderer_system, shader, binding_set, instance_id, binding_index, array_index, texture);
}
void kshader_set_binding_sampler (kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler) {
	renderer_shader_set_binding_sampler(engine_systems_get()->renderer_system, shader, binding_set, instance_id, binding_index, array_index, sampler);
}
u32 kshader_acquire_binding_set_instance (kshader shader, u8 binding_set) {
	return renderer_shader_acquire_binding_set_instance(engine_systems_get()->renderer_system, shader, binding_set);
}
void kshader_release_binding_set_instance (kshader shader, u8 binding_set, u32 instance_id) {
	renderer_shader_release_binding_set_instance(engine_systems_get()->renderer_system, shader, binding_set, instance_id);
}

u32 kshader_binding_set_instance_count_get (kshader shader, u8 binding_set) {
	return renderer_shader_binding_set_get_max_instance_count(engine_systems_get()->renderer_system, shader, binding_set);
}

b8 kshader_apply_binding_set (kshader shader, u8 binding_set, u32 instance_id) {
	return renderer_shader_apply_binding_set(engine_systems_get()->renderer_system, shader, binding_set, instance_id);
}

static kshader generate_new_shader_handle (void) {
	for (u32 i = 0; i < state_ptr->config.max_shader_count; ++i) {
		if (state_ptr->shaders[i].state == SHADER_STATE_FREE) {
			state_ptr->shaders[i].state = SHADER_STATE_NOT_CREATED;
			return i;
		}
	}
	return KSHADER_INVALID;
}

static kshader shader_create (const kasset_shader *asset) {
	kshader new_handle = generate_new_shader_handle();
	if (new_handle == KSHADER_INVALID) {
		KERROR("Unable to find free slot to create new shader. Aborting.");
		return new_handle;
	}

	struct asset_system_state *asset_state = engine_systems_get()->asset_state;

	kshader_data *out_shader = &state_ptr->shaders[new_handle];
	kzero_memory(out_shader, sizeof(kshader));
	// Sync handle uniqueid
	out_shader->state = SHADER_STATE_NOT_CREATED;
	out_shader->name = asset->name;

	// Take a copy of the flags.
	// Build up flags.
	out_shader->flags = SHADER_FLAG_NONE_BIT;
	if (asset->depth_test) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_DEPTH_TEST_BIT, true);
	}
	if (asset->depth_write) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_DEPTH_WRITE_BIT, true);
	}

	if (asset->stencil_test) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_STENCIL_TEST_BIT, true);
	}
	if (asset->stencil_write) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_STENCIL_WRITE_BIT, true);
	}

	if (asset->colour_read) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_COLOUR_READ_BIT, true);
	}
	if (asset->colour_write) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_COLOUR_WRITE_BIT, true);
	}

	if (asset->supports_wireframe) {
		out_shader->flags = FLAG_SET(out_shader->flags, SHADER_FLAG_WIREFRAME_BIT, true);
	}

	// Keep a copy of the topology types.
	out_shader->topology_types = asset->topology_types;
	out_shader->default_topology = asset->default_topology;

	// Save off a pointer to the config resource.
	out_shader->shader_asset = asset;

	// Attachments
	// Colour
	if (asset->colour_attachment_count && asset->colour_attachments) {
		out_shader->colour_attachment_count = asset->colour_attachment_count;
		out_shader->colour_attachments = KALLOC_TYPE_CARRAY(kshader_attachment, out_shader->colour_attachment_count);
		for (u8 i = 0; i < out_shader->colour_attachment_count; ++i) {
			out_shader->colour_attachments[i].format = asset->colour_attachments[i].format;
			out_shader->colour_attachments[i].name = kname_create(asset->colour_attachments[i].name);
		}
	}

	// Depth attachment
	out_shader->depth_attachment.format = asset->depth_attachment.format;
	out_shader->depth_attachment.name = kname_create(asset->depth_attachment.name);

	// Stencil attachment
	out_shader->stencil_attachment.format = asset->stencil_attachment.format;
	out_shader->stencil_attachment.name = kname_create(asset->stencil_attachment.name);

	out_shader->pipeline_count = asset->pipeline_count;
	out_shader->pipelines = KALLOC_TYPE_CARRAY(kshader_pipeline_data, out_shader->pipeline_count);
	shader_pipeline_config *pipeline_configs = KALLOC_TYPE_CARRAY(shader_pipeline_config, out_shader->pipeline_count);
	for (u8 pi = 0; pi < out_shader->pipeline_count; ++pi) {
		kasset_shader_pipeline *ap = &asset->pipelines[pi];
		kshader_pipeline_data *p = &out_shader->pipelines[pi];
		shader_pipeline_config *pc = &pipeline_configs[pi];

		p->attribute_stride = 0;
		p->attribute_count = ap->attribute_count;
		p->attributes = KALLOC_TYPE_CARRAY(shader_attribute, p->attribute_count);

		// Create arrays to track stage "text" resources.
		p->shader_stage_count = ap->stage_count;
		p->stages = KALLOC_TYPE_CARRAY(shader_stage, ap->stage_count);
		p->stage_source_text_assets = KALLOC_TYPE_CARRAY(kasset_text *, ap->stage_count);
		p->stage_source_text_generations = KALLOC_TYPE_CARRAY(u32, ap->stage_count);
		p->stage_names = KALLOC_TYPE_CARRAY(kname, ap->stage_count);
		p->stage_sources = KALLOC_TYPE_CARRAY(const char *, ap->stage_count);
#if KOHI_DEBUG
		p->watch_ids = KALLOC_TYPE_CARRAY(u32, ap->stage_count);
#endif

		// Process stages.
		for (u8 i = 0; i < ap->stage_count; ++i) {
			p->stages[i] = ap->stages[i].type;
			// Request the text asset for each stage synchronously.
			p->stage_source_text_assets[i] = asset_system_request_text_from_package_sync(asset_state, ap->stages[i].package_name, ap->stages[i].source_asset_name);
			// Take a copy of the generation for later comparison.
			p->stage_source_text_generations[i] = 0; // TODO: generation? // out_shader->stage_source_text_assets[i].generation;

			p->stage_names[i] = kname_create(ap->stages[i].source_asset_name);
			p->stage_sources[i] = string_duplicate(p->stage_source_text_assets[i]->content);

			// Watch source file for hot-reload.
#if KOHI_DEBUG
			p->watch_ids[i] = asset_system_watch_for_reload(asset_state, KASSET_TYPE_TEXT, p->stage_names[i], kname_create(ap->stages[i].package_name));
#endif
		}

		// Process attributes
		for (u32 i = 0; i < p->attribute_count; ++i) {
			kasset_shader_attribute *aa = &ap->attributes[i];

			shader_attribute *a = &p->attributes[i];
			a->name = kname_create(aa->name);
			a->type = aa->type;
			a->size = size_from_shader_attribute_type(a->type);

			p->attribute_stride += a->size;
		}

		pc->attribute_count = p->attribute_count;
		KDUPLICATE_TYPE_CARRAY(pc->attributes, p->attributes, shader_attribute, p->attribute_count);
		pc->attribute_stride = p->attribute_stride;

		pc->stage_count = p->shader_stage_count;
		KDUPLICATE_TYPE_CARRAY(pc->stages, p->stages, shader_stage, p->shader_stage_count);
		KDUPLICATE_TYPE_CARRAY(pc->stage_names, p->stage_names, kname, p->shader_stage_count);
		// Shallow copy of the array of strings.
		KDUPLICATE_TYPE_CARRAY(pc->stage_sources, p->stage_sources, const char *, p->shader_stage_count);
	}

	// Ready to be initialized.
	out_shader->state = SHADER_STATE_UNINITIALIZED;

	kpixel_format *colour_formats = 0;
	if (out_shader->colour_attachment_count) {
		colour_formats = KALLOC_TYPE_CARRAY(kpixel_format, out_shader->colour_attachment_count);
		for (u8 i = 0; i < out_shader->colour_attachment_count; ++i) {
			colour_formats[i] = out_shader->colour_attachments[i].format;
		}
	}

	// Create renderer-internal resources.
	b8 result = renderer_shader_create(
		state_ptr->renderer,
		new_handle,
		out_shader->name,
		out_shader->flags,
		out_shader->topology_types,
		out_shader->default_topology,
		out_shader->colour_attachment_count,
		colour_formats,
		out_shader->depth_attachment.format,
		out_shader->stencil_attachment.format,
		out_shader->pipeline_count,
		pipeline_configs,
		asset->binding_set_count,
		asset->binding_sets);

	KFREE_TYPE_CARRAY(colour_formats, kpixel_format, out_shader->colour_attachment_count);

	// Cleanup config.
	for (u8 pi = 0; pi < out_shader->pipeline_count; ++pi) {
		shader_pipeline_config *pc = &pipeline_configs[pi];
		KFREE_TYPE_CARRAY(pc->attributes, shader_attribute, pc->attribute_count);

		KFREE_TYPE_CARRAY(pc->stages, shader_stage, pc->stage_count);
		KFREE_TYPE_CARRAY(pc->stage_names, kname, pc->stage_count);
		// NOTE: was just a shallow copy of strings that need to be kept, so only get rid of the array.
		KFREE_TYPE_CARRAY(pc->stage_sources, const char *, pc->stage_count);
	}
	KFREE_TYPE_CARRAY(pipeline_configs, shader_pipeline_config, out_shader->pipeline_count);

	if (!result) {
		KERROR("Error creating shader.");
		new_handle = KSHADER_INVALID;
	}
	return new_handle;
}

static b8 shader_reload (kshader_data *shader, kshader shader_handle) {

	shader_pipeline_config *pipeline_configs = KALLOC_TYPE_CARRAY(shader_pipeline_config, shader->pipeline_count);
	for (u8 pi = 0; pi < shader->pipeline_count; ++pi) {
		kshader_pipeline_data *p = &shader->pipelines[pi];
		shader_pipeline_config *pc = &pipeline_configs[pi];

		pc->attribute_count = p->attribute_count;
		KDUPLICATE_TYPE_CARRAY(pc->attributes, p->attributes, shader_attribute, p->attribute_count);
		pc->attribute_stride = p->attribute_stride;

		pc->stage_count = p->shader_stage_count;
		KDUPLICATE_TYPE_CARRAY(pc->stages, p->stages, shader_stage, p->shader_stage_count);
		KDUPLICATE_TYPE_CARRAY(pc->stage_names, p->stage_names, kname, p->shader_stage_count);
		// Shallow copy of the array of strings.
		KDUPLICATE_TYPE_CARRAY(pc->stage_sources, p->stage_sources, const char *, p->shader_stage_count);
	}

	b8 result = renderer_shader_reload(state_ptr->renderer, shader_handle, shader->pipeline_count, pipeline_configs);

	// Cleanup config.
	for (u8 pi = 0; pi < shader->pipeline_count; ++pi) {
		shader_pipeline_config *pc = &pipeline_configs[pi];
		KFREE_TYPE_CARRAY(pc->attributes, shader_attribute, pc->attribute_count);

		KFREE_TYPE_CARRAY(pc->stages, shader_stage, pc->stage_count);
		KFREE_TYPE_CARRAY(pc->stage_names, kname, pc->stage_count);
		// NOTE: was just a shallow copy of strings that need to be kept, so only get rid of the array.
		KFREE_TYPE_CARRAY(pc->stage_sources, const char *, pc->stage_count);
	}
	KFREE_TYPE_CARRAY(pipeline_configs, shader_pipeline_config, shader->pipeline_count);

	return result;
}
