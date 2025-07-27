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
#include "core/event.h"
#include "kresources/kresource_types.h"
#include "renderer/renderer_frontend.h"
#include "systems/asset_system.h"
#include "systems/texture_system.h"

/**
 * @brief Represents a shader on the frontend. This is internal to the shader system.
 */
typedef struct kshader_data {

    kname name;

    shader_flag_bits flags;

    /** @brief The types of topologies used by the shader and its pipeline. See primitive_topology_type. */
    u32 topology_types;

    /** @brief An array of uniforms in this shader. Darray. */
    shader_uniform* uniforms;

    /** @brief An array of attributes. Darray. */
    shader_attribute* attributes;

    /** @brief The size of all attributes combined, a.k.a. the size of a vertex. */
    u16 attribute_stride;

    u8 shader_stage_count;
    shader_stage_config* stage_configs;

    /** @brief Per-frame frequency data. */
    shader_frequency_data per_frame;

    /** @brief Per-group frequency data. */
    shader_frequency_data per_group;

    /** @brief Per-draw frequency data. */
    shader_frequency_data per_draw;

    /** @brief The internal state of the shader. */
    shader_state state;

    // A constant pointer to the shader config asset.
    const kasset_shader* shader_asset;

    // Array of stages.
    shader_stage* stages;
    // Array of pointers to text assets, one per stage.
    kasset_text** stage_source_text_assets;
    // Array of generations of stage source text resources. Matches size of stage_source_text_resources;
    u32* stage_source_text_generations;
    // Array of names of stage assets.
    kname* stage_names;
    // Array of source text for stages. Matches size of stage_source_text_resources;
    const char** stage_sources;
    // Array of file watch ids, one per stage.
    u32* watch_ids;

} kshader_data;

// The internal shader system state.
typedef struct kshader_system_state {
    // A pointer to the renderer system state.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;

    // The max number of textures that can be bound for a single draw call, provided by the renderer.
    u16 max_bound_texture_count;
    // The max number of samplers that can be bound for a single draw call, provided by the renderer.
    u16 max_bound_sampler_count;

    // This system's configuration.
    kshader_system_config config;
    // A collection of created shaders.
    kshader_data* shaders;

    // Convenience pointer to resource system state.
    struct kresource_system_state* resource_state;
} kshader_system_state;

// A pointer to hold the internal system state.
// FIXME: Get rid of this and all references to it and use the engine_systems_get() instead where needed.
static kshader_system_state* state_ptr = 0;

static b8 internal_attribute_add(kshader_data* shader, const shader_attribute_config* config);
static b8 internal_texture_add(kshader_data* shader, shader_uniform_config* config);
static b8 internal_sampler_add(kshader_data* shader, shader_uniform_config* config);
static kshader generate_new_shader_handle(void);
static b8 internal_uniform_add(kshader_data* shader, const shader_uniform_config* config, u16 tex_samp_index);
static kshader shader_create(const kasset_shader* asset);
static b8 shader_reload(kshader_data* shader, kshader shader_handle);

// Verify the name is valid and unique.
static b8 uniform_name_valid(kshader_data* shader, kname uniform_name);
static b8 shader_uniform_add_state_valid(kshader_data* shader);
static void internal_shader_destroy(kshader* shader);
///////////////////////

#if KOHI_HOT_RELOAD
static b8 file_watch_event(u16 code, void* sender, void* listener_inst, event_context context) {
    kshader_system_state* typed_state = (kshader_system_state*)listener_inst;

    u32 watch_id = context.data.u32[0];
    if (code == EVENT_CODE_ASSET_HOT_RELOADED) {

        // TODO: more verification to make sure this is correct.
        kasset_text* shader_source_asset = (kasset_text*)sender;

        // Search shaders for the one whose generations are out of sync.
        for (u32 i = 0; i < typed_state->config.max_shader_count; ++i) {
            kshader_data* shader = &typed_state->shaders[i];

            b8 reload_required = false;

            for (u32 w = 0; w < shader->shader_stage_count; ++w) {
                if (shader->watch_ids[w] == watch_id) {
                    // Replace the existing shader stage source with the new.
                    if (shader->stage_sources[w]) {
                        string_free(shader->stage_sources[w]);
                    }
                    shader->stage_sources[w] = string_duplicate(shader_source_asset->content);

                    // Release the asset.
                    asset_system_release_text(engine_systems_get()->asset_state, shader_source_asset);
                    reload_required = true;
                    break;
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

b8 kshader_system_initialize(u64* memory_requirement, void* memory, void* config) {
    kshader_system_config* typed_config = (kshader_system_config*)config;
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
    u64 shader_array_requirement = sizeof(kshader) * typed_config->max_shader_count;
    *memory_requirement = struct_requirement + shader_array_requirement;

    if (!memory) {
        return true;
    }

    // Setup the state pointer, memory block, shader array, etc.
    state_ptr = memory;
    u64 addr = (u64)memory;
    state_ptr->shaders = (void*)(addr + struct_requirement);
    state_ptr->config = *typed_config;

    state_ptr->resource_state = engine_systems_get()->kresource_state;

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

void kshader_system_shutdown(void* state) {
    if (state) {
        // Destroy any shaders still in existence.
        kshader_system_state* st = (kshader_system_state*)state;
        for (u32 i = 0; i < st->config.max_shader_count; ++i) {
            kshader_data* s = &st->shaders[i];
            if (s->state != SHADER_STATE_FREE) {
                kshader temp_handle = i;
                internal_shader_destroy(&temp_handle);
            }
        }
        kzero_memory(st, sizeof(kshader_system_state));
    }

    state_ptr = 0;
}

kshader kshader_system_get(kname name, kname package_name) {
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
    kasset_shader* shader_asset = asset_system_request_shader_from_package_sync(engine_systems_get()->asset_state, kname_string_get(package_name), kname_string_get(name));
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

kshader kshader_system_get_from_source(kname name, const char* shader_config_source) {
    if (name == INVALID_KNAME) {
        return KSHADER_INVALID;
    }

    kasset_shader* temp_asset = KALLOC_TYPE(kasset_shader, MEMORY_TAG_ASSET);
    if (!kasset_shader_deserialize(shader_config_source, temp_asset)) {
        return KSHADER_INVALID;
    }
    temp_asset->name = name;

    // Create the shader.
    kshader shader_handle = shader_create(temp_asset);

    asset_system_release_shader(engine_systems_get()->asset_state, temp_asset);

    if (shader_handle == KSHADER_INVALID) {
        KERROR("Failed to create shader '%s' from config source.", kname_string_get(name));
        return shader_handle;
    }

    return shader_handle;
}

static void internal_shader_destroy(kshader* shader) {
    if (*shader == KSHADER_INVALID) {
        return;
    }

    renderer_shader_destroy(state_ptr->renderer, *shader);

    kshader_data* s = &state_ptr->shaders[*shader];

    // Set it to be unusable right away.
    s->state = SHADER_STATE_FREE;

    s->name = INVALID_KNAME;

    // Make sure to invalidate the handle.
    *shader = KSHADER_INVALID;
}

void kshader_system_destroy(kshader* shader) {
    if (*shader == KSHADER_INVALID) {
        return;
    }

    internal_shader_destroy(shader);
}

b8 kshader_system_set_wireframe(kshader shader, b8 wireframe_enabled) {
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

b8 kshader_system_use(kshader shader) {
    if (shader == KSHADER_INVALID) {
        KERROR("Invalid shader passed.");
        return false;
    }
    kshader_data* next_shader = &state_ptr->shaders[shader];
    if (!renderer_shader_use(state_ptr->renderer, shader)) {
        KERROR("Failed to use shader '%s'.", next_shader->name);
        return false;
    }
    return true;
}

u16 kshader_system_uniform_location(kshader shader, kname uniform_name) {
    if (shader == KSHADER_INVALID) {
        KERROR("Invalid shader passed.");
        return INVALID_ID_U16;
    }
    kshader_data* next_shader = &state_ptr->shaders[shader];

    u32 uniform_count = darray_length(next_shader->uniforms);
    for (u32 i = 0; i < uniform_count; ++i) {
        if (next_shader->uniforms[i].name == uniform_name) {
            return next_shader->uniforms[i].location;
        }
    }

    // Not found.
    return INVALID_ID_U16;
}

b8 kshader_system_uniform_set(kshader shader, kname uniform_name, const void* value) {
    return kshader_system_uniform_set_arrayed(shader, uniform_name, 0, value);
}

b8 kshader_system_uniform_set_arrayed(kshader shader, kname uniform_name, u32 array_index, const void* value) {
    if (shader == KSHADER_INVALID) {
        KERROR("kshader_system_uniform_set_arrayed called with invalid shader handle.");
        return false;
    }

    u16 index = kshader_system_uniform_location(shader, uniform_name);
    return kshader_system_uniform_set_by_location_arrayed(shader, index, array_index, value);
}

b8 kshader_system_texture_set(kshader shader, kname sampler_name, ktexture t) {
    return kshader_system_texture_set_arrayed(shader, sampler_name, 0, t);
}

b8 kshader_system_texture_set_arrayed(kshader shader, kname uniform_name, u32 array_index, ktexture t) {
    return kshader_system_uniform_set_arrayed(shader, uniform_name, array_index, &t);
}

b8 kshader_system_texture_set_by_location(kshader shader, u16 location, ktexture t) {
    return kshader_system_uniform_set_by_location_arrayed(shader, location, 0, &t);
}

b8 kshader_system_texture_set_by_location_arrayed(kshader shader, u16 location, u32 array_index, ktexture t) {
    return kshader_system_uniform_set_by_location_arrayed(shader, location, array_index, &t);
}

b8 kshader_system_uniform_set_by_location(kshader shader, u16 location, const void* value) {
    return kshader_system_uniform_set_by_location_arrayed(shader, location, 0, value);
}

b8 kshader_system_uniform_set_by_location_arrayed(kshader shader, u16 location, u32 array_index, const void* value) {
    kshader_data* s = &state_ptr->shaders[shader];
    shader_uniform* uniform = &s->uniforms[location];
    return renderer_shader_uniform_set(state_ptr->renderer, shader, uniform, array_index, value);
}

b8 kshader_system_bind_frame(kshader shader) {
    if (shader == KSHADER_INVALID) {
        KERROR("Tried to bind_frame on a shader using an invalid or stale handle. Nothing to be done.");
        return false;
    }
    return renderer_shader_bind_per_frame(state_ptr->renderer, shader);
}

b8 kshader_system_bind_group(kshader shader, u32 group_id) {
    if (group_id == INVALID_ID) {
        KERROR("Cannot bind shader instance INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader].per_group.bound_id = group_id;
    return renderer_shader_bind_per_group(state_ptr->renderer, shader, group_id);
}

b8 kshader_system_bind_draw_id(kshader shader, u32 draw_id) {
    if (draw_id == INVALID_ID) {
        KERROR("Cannot bind shader local id INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader].per_draw.bound_id = draw_id;
    return renderer_shader_bind_per_draw(state_ptr->renderer, shader, draw_id);
}

b8 kshader_system_apply_per_frame(kshader shader) {
    return renderer_shader_apply_per_frame(state_ptr->renderer, shader);
}

b8 kshader_system_apply_per_group(kshader shader) {
    return renderer_shader_apply_per_group(state_ptr->renderer, shader);
}

b8 kshader_system_apply_per_draw(kshader shader) {
    return renderer_shader_apply_per_draw(state_ptr->renderer, shader);
}

b8 kshader_system_shader_group_acquire(kshader shader, u32* out_group_id) {
    return renderer_shader_per_group_resources_acquire(state_ptr->renderer, shader, out_group_id);
}

b8 kshader_system_shader_per_draw_acquire(kshader shader, u32* out_per_draw_id) {
    return renderer_shader_per_draw_resources_acquire(state_ptr->renderer, shader, out_per_draw_id);
}

b8 kshader_system_shader_group_release(kshader shader, u32 group_id) {
    return renderer_shader_per_group_resources_release(state_ptr->renderer, shader, group_id);
}

b8 kshader_system_shader_per_draw_release(kshader shader, u32 per_draw_id) {
    return renderer_shader_per_draw_resources_release(state_ptr->renderer, shader, per_draw_id);
}

static b8 internal_attribute_add(kshader_data* shader, const shader_attribute_config* config) {
    u32 size = 0;
    switch (config->type) {
    case SHADER_ATTRIB_TYPE_INT8:
    case SHADER_ATTRIB_TYPE_UINT8:
        size = 1;
        break;
    case SHADER_ATTRIB_TYPE_INT16:
    case SHADER_ATTRIB_TYPE_UINT16:
        size = 2;
        break;
    case SHADER_ATTRIB_TYPE_FLOAT32:
    case SHADER_ATTRIB_TYPE_INT32:
    case SHADER_ATTRIB_TYPE_UINT32:
        size = 4;
        break;
    case SHADER_ATTRIB_TYPE_FLOAT32_2:
        size = 8;
        break;
    case SHADER_ATTRIB_TYPE_FLOAT32_3:
        size = 12;
        break;
    case SHADER_ATTRIB_TYPE_FLOAT32_4:
        size = 16;
        break;
    default:
        KERROR("Unrecognized type %d, defaulting to size of 4. This probably is not what is desired.");
        size = 4;
        break;
    }

    shader->attribute_stride += size;

    // Create/push the attribute.
    shader_attribute attrib = {};
    attrib.name = config->name;
    attrib.size = size;
    attrib.type = config->type;
    darray_push(shader->attributes, attrib);

    return true;
}

static b8 internal_texture_add(kshader_data* shader, shader_uniform_config* config) {

    // Verify the name is valid and unique.
    if (!uniform_name_valid(shader, config->name) || !shader_uniform_add_state_valid(shader)) {
        return false;
    }

    // Verify that there are not too many textures present across all frequencies.
    u16 current_texture_count = shader->per_frame.uniform_texture_count + shader->per_group.uniform_texture_count + shader->per_draw.uniform_texture_count;
    if (current_texture_count + 1 > state_ptr->max_bound_texture_count) {
        KERROR("Cannot add another texture uniform to shader '%s' as it has already reached the maximum per-draw bound total of %hu", kname_string_get(shader->name), state_ptr->max_bound_texture_count);
        return false;
    }

    // Get the appropriate index.
    u32 tex_samp_index = 0;
    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
        tex_samp_index = shader->per_frame.uniform_texture_count;
        shader->per_frame.uniform_texture_count++;
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
        tex_samp_index = shader->per_group.uniform_texture_count;
        shader->per_group.uniform_texture_count++;
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        tex_samp_index = shader->per_draw.uniform_texture_count;
        shader->per_draw.uniform_texture_count++;
    }

    // Treat it like a uniform.
    if (!internal_uniform_add(shader, config, tex_samp_index)) {
        KERROR("Unable to add texture uniform.");
        return false;
    }

    return true;
}

static b8 internal_sampler_add(kshader_data* shader, shader_uniform_config* config) {

    // Verify the name is valid and unique.
    if (!uniform_name_valid(shader, config->name) || !shader_uniform_add_state_valid(shader)) {
        return false;
    }

    // Verify that there are not too many samplers present across all frequencies.
    u16 current_sampler_count = shader->per_frame.uniform_sampler_count + shader->per_group.uniform_sampler_count + shader->per_draw.uniform_sampler_count;
    if (current_sampler_count + 1 > state_ptr->max_bound_sampler_count) {
        KERROR("Cannot add another sampler uniform to shader '%s' as it has already reached the maximum per-draw bound total of %hu", kname_string_get(shader->name), state_ptr->max_bound_sampler_count);
        return false;
    }

    // If per-frame, push into the per-frame list.
    u32 tex_samp_index = 0;
    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
        tex_samp_index = shader->per_frame.uniform_sampler_count;
        shader->per_frame.uniform_sampler_count++;
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
        tex_samp_index = shader->per_group.uniform_sampler_count;
        shader->per_group.uniform_sampler_count++;
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        tex_samp_index = shader->per_draw.uniform_sampler_count;
        shader->per_draw.uniform_sampler_count++;
    }

    // Treat it like a uniform.
    if (!internal_uniform_add(shader, config, tex_samp_index)) {
        KERROR("Unable to add sampler uniform.");
        return false;
    }

    return true;
}

static kshader generate_new_shader_handle(void) {
    for (u32 i = 0; i < state_ptr->config.max_shader_count; ++i) {
        if (state_ptr->shaders[i].state == SHADER_STATE_FREE) {
            return i;
        }
    }
    return KSHADER_INVALID;
}

static b8 internal_uniform_add(kshader_data* shader, const shader_uniform_config* config, u16 tex_samp_index) {
    if (!shader_uniform_add_state_valid(shader) || !uniform_name_valid(shader, config->name)) {
        return false;
    }
    u32 uniform_count = darray_length(shader->uniforms);
    if (uniform_count + 1 > state_ptr->config.max_uniform_count) {
        KERROR("A shader can only accept a combined maximum of %d uniforms and samplers at global, instance and local scopes.", state_ptr->config.max_uniform_count);
        return false;
    }
    b8 is_sampler_or_texture = uniform_type_is_sampler(config->type) || uniform_type_is_texture(config->type);
    shader_uniform entry = {0};
    entry.frequency = config->frequency;
    entry.type = config->type;
    entry.array_length = config->array_length;
    entry.location = uniform_count;
    entry.tex_samp_index = tex_samp_index;

    b8 is_per_frame = (config->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME);

    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        entry.offset = shader->per_draw.ubo_size;
        entry.size = config->size;
    } else {
        entry.offset = is_sampler_or_texture ? 0 : is_per_frame ? shader->per_frame.ubo_size
                                                                : shader->per_group.ubo_size;
        entry.size = is_sampler_or_texture ? 0 : config->size;
    }

    entry.name = config->name;

    darray_push(shader->uniforms, entry);

    // Count regular uniforms only, as the others are counted in the functions called before this for
    // textures and samplers.
    if (!is_sampler_or_texture) {
        shader_frequency_data* frequency = 0;
        if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
            frequency = &shader->per_frame;
        } else if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
            frequency = &shader->per_group;
        } else if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
            frequency = &shader->per_draw;
        }
        if (!frequency) {
            KFATAL("No frequency found - investigate this!");
            return false;
        }
        frequency->ubo_size += (entry.size * (entry.array_length ? entry.array_length : 1));
        frequency->uniform_count++;
    }

    return true;
}

static b8 uniform_name_valid(kshader_data* shader, kname uniform_name) {
    if (uniform_name == INVALID_KNAME) {
        KERROR("Uniform name is invalid.");
        return false;
    }
    u32 uniform_count = darray_length(shader->uniforms);
    for (u32 i = 0; i < uniform_count; ++i) {
        if (shader->uniforms[i].name == uniform_name) {
            KERROR("A uniform by the name '%s' already exists on shader '%s'.", uniform_name, shader->name);
            return false;
        }
    }

    return true;
}

static b8 shader_uniform_add_state_valid(kshader_data* shader) {
    if (shader->state != SHADER_STATE_UNINITIALIZED) {
        KERROR("Uniforms may only be added to shaders before initialization.");
        return false;
    }
    return true;
}

static kshader shader_create(const kasset_shader* asset) {
    kshader new_handle = generate_new_shader_handle();
    if (new_handle == KSHADER_INVALID) {
        KERROR("Unable to find free slot to create new shader. Aborting.");
        return new_handle;
    }

    kshader_data* out_shader = &state_ptr->shaders[new_handle];
    kzero_memory(out_shader, sizeof(kshader));
    // Sync handle uniqueid
    out_shader->state = SHADER_STATE_NOT_CREATED;
    out_shader->name = asset->name;
    out_shader->attribute_stride = 0;
    out_shader->shader_stage_count = asset->stage_count;
    out_shader->stage_configs = kallocate(sizeof(shader_stage_config) * asset->stage_count, MEMORY_TAG_ARRAY);
    out_shader->uniforms = darray_create(shader_uniform);
    out_shader->attributes = darray_create(shader_attribute);

    // Invalidate frequency bound ids
    out_shader->per_frame.bound_id = INVALID_ID; // NOTE: per-frame doesn't have a bound id, but invalidate it anyway.
    out_shader->per_group.bound_id = INVALID_ID;
    out_shader->per_draw.bound_id = INVALID_ID;

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

    // Save off a pointer to the config resource.
    out_shader->shader_asset = asset;

    // Create arrays to track stage "text" resources.
    out_shader->stages = KALLOC_TYPE_CARRAY(shader_stage, out_shader->shader_stage_count);
    out_shader->stage_source_text_assets = KALLOC_TYPE_CARRAY(kasset_text*, out_shader->shader_stage_count);
    out_shader->stage_source_text_generations = KALLOC_TYPE_CARRAY(u32, out_shader->shader_stage_count);
    out_shader->stage_names = KALLOC_TYPE_CARRAY(kname, out_shader->shader_stage_count);
    out_shader->stage_sources = KALLOC_TYPE_CARRAY(const char*, out_shader->shader_stage_count);
    out_shader->watch_ids = KALLOC_TYPE_CARRAY(u32, out_shader->shader_stage_count);

    struct asset_system_state* asset_state = engine_systems_get()->asset_state;

    // Process stages.
    for (u8 i = 0; i < asset->stage_count; ++i) {
        out_shader->stages[i] = asset->stages[i].type;
        // Request the text asset for each stage synchronously.
        out_shader->stage_source_text_assets[i] = asset_system_request_text_from_package_sync(asset_state, asset->stages[i].package_name, asset->stages[i].source_asset_name);
        // Take a copy of the generation for later comparison.
        out_shader->stage_source_text_generations[i] = 0; // TODO: generation? // out_shader->stage_source_text_assets[i].generation;

        out_shader->stage_names[i] = kname_create(asset->stages[i].source_asset_name);
        out_shader->stage_sources[i] = string_duplicate(out_shader->stage_source_text_assets[i]->content);

        // Watch source file for hot-reload.
        out_shader->watch_ids[i] = asset_system_watch_for_reload(asset_state, KASSET_TYPE_TEXT, out_shader->stage_names[i], kname_create(asset->stages[i].package_name));
    }

    // Keep a copy of the topology types.
    out_shader->topology_types = asset->topology_types;

    // Ready to be initialized.
    out_shader->state = SHADER_STATE_UNINITIALIZED;

    // Process attributes
    for (u32 i = 0; i < asset->attribute_count; ++i) {
        kasset_shader_attribute* a = &asset->attributes[i];
        shader_attribute_config ac = {
            .type = a->type,
            .name = kname_create(a->name),
            .size = size_from_shader_attribute_type(a->type)};
        if (!internal_attribute_add(out_shader, &ac)) {
            KERROR("Failed to add attribute '%s' to shader '%s'.", ac.name, out_shader->name);
            return KSHADER_INVALID;
        }
    }

    // Process uniforms
    for (u32 i = 0; i < asset->uniform_count; ++i) {
        kasset_shader_uniform* u = &asset->uniforms[i];
        shader_uniform_config uc = {
            .type = u->type,
            .name = kname_create(u->name),
            .array_length = u->array_size,
            .frequency = u->frequency,
            .size = u->size};
        if (u->type != SHADER_UNIFORM_TYPE_STRUCT && u->type != SHADER_UNIFORM_TYPE_CUSTOM) {
            uc.size = size_from_shader_uniform_type(u->type);
        }

        b8 uniform_add_result = false;
        if (uniform_type_is_sampler(uc.type)) {
            uniform_add_result = internal_sampler_add(out_shader, &uc);
        } else if (uniform_type_is_texture(uc.type)) {
            uniform_add_result = internal_texture_add(out_shader, &uc);
        } else {
            uniform_add_result = internal_uniform_add(out_shader, &uc, INVALID_ID_U16);
        }
        if (!uniform_add_result) {
            return KSHADER_INVALID;
        }
    }

    // Now that uniforms are processed, take note of the indices of textures and samplers.
    // These are used for fast lookups later by type.
    if (out_shader->per_frame.uniform_sampler_count) {
        out_shader->per_frame.sampler_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_frame.uniform_sampler_count);
    }
    if (out_shader->per_group.uniform_sampler_count) {
        out_shader->per_group.sampler_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_group.uniform_sampler_count);
    }
    if (out_shader->per_draw.uniform_sampler_count) {
        out_shader->per_draw.sampler_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_draw.uniform_sampler_count);
    }
    if (out_shader->per_frame.uniform_texture_count) {
        out_shader->per_frame.texture_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_frame.uniform_texture_count);
    }
    if (out_shader->per_group.uniform_texture_count) {
        out_shader->per_group.texture_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_group.uniform_texture_count);
    }
    if (out_shader->per_draw.uniform_texture_count) {
        out_shader->per_draw.texture_indices = KALLOC_TYPE_CARRAY(u32, out_shader->per_draw.uniform_texture_count);
    }
    u32 frame_textures = 0, frame_samplers = 0;
    u32 group_textures = 0, group_samplers = 0;
    u32 draw_textures = 0, draw_samplers = 0;
    for (u32 i = 0; i < asset->uniform_count; ++i) {
        kasset_shader_uniform* uc = &asset->uniforms[i];
        if (uniform_type_is_sampler(uc->type)) {
            switch (uc->frequency) {
            case SHADER_UPDATE_FREQUENCY_PER_FRAME:
                out_shader->per_frame.sampler_indices[frame_samplers] = i;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_GROUP:
                out_shader->per_group.sampler_indices[group_samplers] = i;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_DRAW:
                out_shader->per_draw.sampler_indices[draw_samplers] = i;
                break;
            }
        } else if (uniform_type_is_texture(uc->type)) {
            switch (uc->frequency) {
            case SHADER_UPDATE_FREQUENCY_PER_FRAME:
                out_shader->per_frame.texture_indices[frame_textures] = i;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_GROUP:
                out_shader->per_group.texture_indices[group_textures] = i;
                break;
            case SHADER_UPDATE_FREQUENCY_PER_DRAW:
                out_shader->per_draw.texture_indices[draw_textures] = i;
                break;
            }
        }
    }

    // Create renderer-internal resources.
    if (!renderer_shader_create(
            state_ptr->renderer,
            new_handle,
            out_shader->name,
            out_shader->flags,
            out_shader->topology_types,
            asset->cull_mode,
            out_shader->shader_stage_count,
            out_shader->stages,
            out_shader->stage_names,
            out_shader->stage_sources,
            asset->max_groups,
            asset->max_draw_ids,
            darray_length(out_shader->attributes),
            out_shader->attributes,
            darray_length(out_shader->uniforms),
            out_shader->uniforms)) {
        KERROR("Error creating shader.");
        return KSHADER_INVALID;
    }

    return new_handle;
}

static b8 shader_reload(kshader_data* shader, kshader shader_handle) {

    // Check each shader stage generation for out-of-sync.
    for (u8 i = 0; i < shader->shader_stage_count; ++i) {
        // FIXME: shader generation sync.
        /* if (shader->stage_source_text_generations[i] != shader->stage_source_text_assets[i]->base.generation) {
            // Sync the generations.
            shader->stage_source_text_generations[i] = shader->stage_source_text_assets[i]->base.generation;
        } */
    }

    return renderer_shader_reload(state_ptr->renderer, shader_handle, shader->shader_stage_count, shader->stages, shader->stage_names, shader->stage_sources);
}
