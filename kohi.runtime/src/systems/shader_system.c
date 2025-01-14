#include "shader_system.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core/event.h"
#include "core_render_types.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"
#include "systems/texture_system.h"
#include "utils/render_type_utils.h"

/**
 * @brief Represents a shader on the frontend. This is internal to the shader system.
 */
typedef struct kshader {
    /** @brief unique identifier that is compared against a handle. */
    u64 uniqueid;

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

    // A constant pointer to the shader config resource.
    const kresource_shader* shader_resource;

    // Array of pointers to text resources, one per stage.
    kresource_text** stage_source_text_resources;
    // Array of generations of stage source text resources. Matches size of stage_source_text_resources;
    u32* stage_source_text_generations;

} kshader;

// The internal shader system state.
typedef struct shader_system_state {
    // A pointer to the renderer system state.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;

    // The max number of textures that can be bound for a single draw call, provided by the renderer.
    u16 max_bound_texture_count;
    // The max number of samplers that can be bound for a single draw call, provided by the renderer.
    u16 max_bound_sampler_count;

    // This system's configuration.
    shader_system_config config;
    // A collection of created shaders.
    kshader* shaders;

    // Convenience pointer to resource system state.
    struct kresource_system_state* resource_state;
} shader_system_state;

// A pointer to hold the internal system state.
// FIXME: Get rid of this and all references to it and use the engine_systems_get() instead where needed.
static shader_system_state* state_ptr = 0;

static b8 internal_attribute_add(kshader* shader, const shader_attribute_config* config);
static b8 internal_texture_add(kshader* shader, shader_uniform_config* config);
static b8 internal_sampler_add(kshader* shader, shader_uniform_config* config);
static khandle generate_new_shader_handle(void);
static b8 internal_uniform_add(kshader* shader, const shader_uniform_config* config, u16 tex_samp_index);
static khandle shader_create(const kresource_shader* shader_resource);
static b8 shader_reload(kshader* shader, khandle shader_handle);

// Verify the name is valid and unique.
static b8 uniform_name_valid(kshader* shader, kname uniform_name);
static b8 shader_uniform_add_state_valid(kshader* shader);
static void internal_shader_destroy(khandle* shader);
///////////////////////

#ifdef _DEBUG
static b8 file_watch_event(u16 code, void* sender, void* listener_inst, event_context context) {
    shader_system_state* typed_state = (shader_system_state*)listener_inst;
    if (code == EVENT_CODE_RESOURCE_HOT_RELOADED) {

        // Search shaders for the one whose generations are out of sync.
        for (u32 i = 0; i < typed_state->config.max_shader_count; ++i) {
            kshader* shader = &typed_state->shaders[i];

            b8 reload_required = false;

            for (u32 w = 0; w < shader->shader_stage_count; ++w) {
                // Found match. If the generation is out of sync, reload the shader.
                if (shader->stage_source_text_generations[w] != shader->stage_source_text_resources[w]->base.generation) {
                    // At least one is out of sync, reload. Can boot out here.
                    reload_required = true;
                    break;
                }
            }

            // Reload if needed.
            if (reload_required) {
                khandle handle = khandle_create_with_u64_identifier(i, shader->uniqueid);
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

b8 shader_system_initialize(u64* memory_requirement, void* memory, void* config) {
    shader_system_config* typed_config = (shader_system_config*)config;
    // Verify configuration.
    if (typed_config->max_shader_count < 512) {
        if (typed_config->max_shader_count == 0) {
            KERROR("shader_system_initialize - config.max_shader_count must be greater than 0. Defaulting to 512.");
            typed_config->max_shader_count = 512;
        } else {
            KWARN("shader_system_initialize - config.max_shader_count is recommended to be at least 512.");
        }
    }

    // Block of memory will contain state structure then the block for the shader array.
    u64 struct_requirement = sizeof(shader_system_state);
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
        state_ptr->shaders[i].uniqueid = INVALID_ID_U64;
    }

    // Keep a pointer to the renderer state.
    state_ptr->renderer = engine_systems_get()->renderer_system;
    state_ptr->texture_system = engine_systems_get()->texture_system;

    // Track max texture and sampler counts.
    state_ptr->max_bound_sampler_count = renderer_max_bound_sampler_count_get(state_ptr->renderer);
    state_ptr->max_bound_texture_count = renderer_max_bound_texture_count_get(state_ptr->renderer);

    // Watch for file hot reloads in debug builds.
#ifdef _DEBUG
    event_register(EVENT_CODE_RESOURCE_HOT_RELOADED, state_ptr, file_watch_event);
#endif

    return true;
}

void shader_system_shutdown(void* state) {
    if (state) {
        // Destroy any shaders still in existence.
        shader_system_state* st = (shader_system_state*)state;
        for (u32 i = 0; i < st->config.max_shader_count; ++i) {
            kshader* s = &st->shaders[i];
            if (s->uniqueid != INVALID_ID_U64) {
                khandle temp_handle = khandle_create_with_u64_identifier(i, s->uniqueid);
                internal_shader_destroy(&temp_handle);
            }
        }
        kzero_memory(st, sizeof(shader_system_state));
    }

    state_ptr = 0;
}

khandle shader_system_get(kname name, kname package_name) {
    if (name == INVALID_KNAME) {
        return khandle_invalid();
    }

    u32 count = state_ptr->config.max_shader_count;
    for (u32 i = 0; i < count; ++i) {
        if (state_ptr->shaders[i].name == name) {
            return khandle_create_with_u64_identifier(i, state_ptr->shaders[i].uniqueid);
        }
    }

    // Not found, attempt to load the shader resource.
    kresource_shader_request_info request_info = {0};
    request_info.base.type = KRESOURCE_TYPE_SHADER;
    request_info.base.synchronous = true; // Shaders are needed immediately.
    request_info.shader_config_source_text = 0;

    // Add shader asset to resource request.
    request_info.base.assets = array_kresource_asset_info_create(1);
    kresource_asset_info* asset = &request_info.base.assets.data[0];
    asset->asset_name = name; // Resource name should match the asset name.
    asset->package_name = package_name;
    asset->type = KASSET_TYPE_SHADER;
    asset->watch_for_hot_reload = false;

    kresource_shader* shader_resource = (kresource_shader*)kresource_system_request(state_ptr->resource_state, name, (kresource_request_info*)&request_info);
    if (!shader_resource) {
        KERROR("Failed to load shader resource for shader '%s'.", kname_string_get(name));
        return khandle_invalid();
    }

    // Create the shader.
    khandle shader_handle = shader_create(shader_resource);

    if (khandle_is_invalid(shader_handle)) {
        KERROR("Failed to create shader '%s'.", kname_string_get(name));
        KERROR("There is no shader available called '%s', and one by that name could also not be loaded.", kname_string_get(name));
        return shader_handle;
    }

    return shader_handle;
}

khandle shader_system_get_from_source(kname name, const char* shader_config_source) {
    if (name == INVALID_KNAME) {
        return khandle_invalid();
    }

    // Not found, attempt to load the shader resource.
    kresource_shader_request_info request_info = {0};
    request_info.base.type = KRESOURCE_TYPE_SHADER;
    request_info.base.synchronous = true;                                            // Shaders are needed immediately.
    request_info.shader_config_source_text = string_duplicate(shader_config_source); // load from string source.

    kresource_shader* shader_resource = (kresource_shader*)kresource_system_request(state_ptr->resource_state, name, (kresource_request_info*)&request_info);
    if (!shader_resource) {
        KERROR("Failed to load shader resource for shader '%s'.", kname_string_get(name));
        return khandle_invalid();
    }

    // Create the shader.
    khandle shader_handle = shader_create(shader_resource);

    if (khandle_is_invalid(shader_handle)) {
        KERROR("Failed to create shader '%s' from config source.", kname_string_get(name));
        return shader_handle;
    }

    return shader_handle;
}

static void internal_shader_destroy(khandle* shader) {
    if (khandle_is_invalid(*shader) || khandle_is_stale(*shader, state_ptr->shaders[shader->handle_index].uniqueid)) {
        return;
    }

    renderer_shader_destroy(state_ptr->renderer, *shader);

    kshader* s = &state_ptr->shaders[shader->handle_index];

    // Set it to be unusable right away.
    s->state = SHADER_STATE_NOT_CREATED;

    s->name = INVALID_KNAME;

    // Make sure to invalidate the handle.
    khandle_invalidate(shader);
}

void shader_system_destroy(khandle* shader) {
    if (khandle_is_invalid(*shader)) {
        return;
    }

    internal_shader_destroy(shader);
}

b8 shader_system_set_wireframe(khandle shader, b8 wireframe_enabled) {
    if (khandle_is_invalid(shader)) {
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

b8 shader_system_use(khandle shader) {
    if (khandle_is_invalid(shader)) {
        KERROR("Invalid shader passed.");
        return false;
    }
    kshader* next_shader = &state_ptr->shaders[shader.handle_index];
    if (!renderer_shader_use(state_ptr->renderer, shader)) {
        KERROR("Failed to use shader '%s'.", next_shader->name);
        return false;
    }
    return true;
}

u16 shader_system_uniform_location(khandle shader, kname uniform_name) {
    if (khandle_is_invalid(shader)) {
        KERROR("Invalid shader passed.");
        return INVALID_ID_U16;
    }
    kshader* next_shader = &state_ptr->shaders[shader.handle_index];

    u32 uniform_count = darray_length(next_shader->uniforms);
    for (u32 i = 0; i < uniform_count; ++i) {
        if (next_shader->uniforms[i].name == uniform_name) {
            return next_shader->uniforms[i].location;
        }
    }

    // Not found.
    return INVALID_ID_U16;
}

b8 shader_system_uniform_set(khandle shader, kname uniform_name, const void* value) {
    return shader_system_uniform_set_arrayed(shader, uniform_name, 0, value);
}

b8 shader_system_uniform_set_arrayed(khandle shader, kname uniform_name, u32 array_index, const void* value) {
    if (khandle_is_invalid(shader)) {
        KERROR("shader_system_uniform_set_arrayed called with invalid shader handle.");
        return false;
    }

    u16 index = shader_system_uniform_location(shader, uniform_name);
    return shader_system_uniform_set_by_location_arrayed(shader, index, array_index, value);
}

b8 shader_system_texture_set(khandle shader, kname sampler_name, const kresource_texture* t) {
    return shader_system_texture_set_arrayed(shader, sampler_name, 0, t);
}

b8 shader_system_texture_set_arrayed(khandle shader, kname uniform_name, u32 array_index, const kresource_texture* t) {
    return shader_system_uniform_set_arrayed(shader, uniform_name, array_index, t);
}

b8 shader_system_texture_set_by_location(khandle shader, u16 location, const kresource_texture* t) {
    return shader_system_uniform_set_by_location_arrayed(shader, location, 0, t);
}

b8 shader_system_texture_set_by_location_arrayed(khandle shader, u16 location, u32 array_index, const kresource_texture* t) {
    return shader_system_uniform_set_by_location_arrayed(shader, location, array_index, t);
}

b8 shader_system_uniform_set_by_location(khandle shader, u16 location, const void* value) {
    return shader_system_uniform_set_by_location_arrayed(shader, location, 0, value);
}

b8 shader_system_uniform_set_by_location_arrayed(khandle shader, u16 location, u32 array_index, const void* value) {
    kshader* s = &state_ptr->shaders[shader.handle_index];
    shader_uniform* uniform = &s->uniforms[location];
    return renderer_shader_uniform_set(state_ptr->renderer, shader, uniform, array_index, value);
}

b8 shader_system_bind_frame(khandle shader) {
    if (khandle_is_invalid(shader) || khandle_is_stale(shader, state_ptr->shaders[shader.handle_index].uniqueid)) {
        KERROR("Tried to bind_frame on a shader using an invalid or stale handle. Nothing to be done.");
        return false;
    }
    return renderer_shader_bind_per_frame(state_ptr->renderer, shader);
}

b8 shader_system_bind_group(khandle shader, u32 group_id) {
    if (group_id == INVALID_ID) {
        KERROR("Cannot bind shader instance INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader.handle_index].per_group.bound_id = group_id;
    return renderer_shader_bind_per_group(state_ptr->renderer, shader, group_id);
}

b8 shader_system_bind_draw_id(khandle shader, u32 draw_id) {
    if (draw_id == INVALID_ID) {
        KERROR("Cannot bind shader local id INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader.handle_index].per_draw.bound_id = draw_id;
    return renderer_shader_bind_per_draw(state_ptr->renderer, shader, draw_id);
}

b8 shader_system_apply_per_frame(khandle shader) {
    return renderer_shader_apply_per_frame(state_ptr->renderer, shader);
}

b8 shader_system_apply_per_group(khandle shader) {
    return renderer_shader_apply_per_group(state_ptr->renderer, shader);
}

b8 shader_system_apply_per_draw(khandle shader) {
    return renderer_shader_apply_per_draw(state_ptr->renderer, shader);
}

b8 shader_system_shader_group_acquire(khandle shader, u32* out_group_id) {
    return renderer_shader_per_group_resources_acquire(state_ptr->renderer, shader, out_group_id);
}

b8 shader_system_shader_per_draw_acquire(khandle shader, u32* out_per_draw_id) {
    return renderer_shader_per_draw_resources_acquire(state_ptr->renderer, shader, out_per_draw_id);
}

b8 shader_system_shader_group_release(khandle shader, u32 group_id) {
    return renderer_shader_per_group_resources_release(state_ptr->renderer, shader, group_id);
}

b8 shader_system_shader_per_draw_release(khandle shader, u32 per_draw_id) {
    return renderer_shader_per_draw_resources_release(state_ptr->renderer, shader, per_draw_id);
}

static b8 internal_attribute_add(kshader* shader, const shader_attribute_config* config) {
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

static b8 internal_texture_add(kshader* shader, shader_uniform_config* config) {

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

static b8 internal_sampler_add(kshader* shader, shader_uniform_config* config) {

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

static khandle generate_new_shader_handle(void) {
    for (u32 i = 0; i < state_ptr->config.max_shader_count; ++i) {
        if (state_ptr->shaders[i].uniqueid == INVALID_ID_U64) {
            return khandle_create(i);
        }
    }
    return khandle_invalid();
}

static b8 internal_uniform_add(kshader* shader, const shader_uniform_config* config, u16 tex_samp_index) {
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

static b8 uniform_name_valid(kshader* shader, kname uniform_name) {
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

static b8 shader_uniform_add_state_valid(kshader* shader) {
    if (shader->state != SHADER_STATE_UNINITIALIZED) {
        KERROR("Uniforms may only be added to shaders before initialization.");
        return false;
    }
    return true;
}

static khandle shader_create(const kresource_shader* shader_resource) {
    khandle new_handle = generate_new_shader_handle();
    if (khandle_is_invalid(new_handle)) {
        KERROR("Unable to find free slot to create new shader. Aborting.");
        return new_handle;
    }

    // TODO: probably don't need to keep a copy of all the resource properties
    // since we now hold a pointer to the resource.
    kshader* out_shader = &state_ptr->shaders[new_handle.handle_index];
    kzero_memory(out_shader, sizeof(kshader));
    // Sync handle uniqueid
    out_shader->uniqueid = new_handle.unique_id.uniqueid;
    out_shader->state = SHADER_STATE_NOT_CREATED;
    out_shader->name = shader_resource->base.name;
    out_shader->attribute_stride = 0;
    out_shader->shader_stage_count = shader_resource->stage_count;
    out_shader->stage_configs = kallocate(sizeof(shader_stage_config) * shader_resource->stage_count, MEMORY_TAG_ARRAY);
    out_shader->uniforms = darray_create(shader_uniform);
    out_shader->attributes = darray_create(shader_attribute);

    // Invalidate frequency bound ids
    out_shader->per_frame.bound_id = INVALID_ID; // NOTE: per-frame doesn't have a bound id, but invalidate it anyway.
    out_shader->per_group.bound_id = INVALID_ID;
    out_shader->per_draw.bound_id = INVALID_ID;

    // Take a copy of the flags.
    out_shader->flags = shader_resource->flags;

    // Save off a pointer to the config resource.
    out_shader->shader_resource = shader_resource;

    // Create arrays to track stage "text" resources.
    out_shader->stage_source_text_resources = KALLOC_TYPE_CARRAY(kresource_text*, out_shader->shader_stage_count);
    out_shader->stage_source_text_generations = KALLOC_TYPE_CARRAY(u32, out_shader->shader_stage_count);

    // Take a copy of the array.
    KCOPY_TYPE_CARRAY(out_shader->stage_configs, shader_resource->stage_configs, shader_stage_config, out_shader->shader_stage_count);

    for (u8 i = 0; i < shader_resource->stage_count; ++i) {
        // Also snag a pointer to the resource itself.
        out_shader->stage_source_text_resources[i] = shader_resource->stage_configs[i].resource;
        // Take a copy of the generation for later comparison.
        out_shader->stage_source_text_generations[i] = shader_resource->stage_configs[i].resource->base.generation;
    }

    // Keep a copy of the topology types.
    out_shader->topology_types = shader_resource->topology_types;

    // Ready to be initialized.
    out_shader->state = SHADER_STATE_UNINITIALIZED;

    // Process attributes
    for (u32 i = 0; i < shader_resource->attribute_count; ++i) {
        shader_attribute_config* ac = &shader_resource->attributes[i];
        if (!internal_attribute_add(out_shader, ac)) {
            KERROR("Failed to add attribute '%s' to shader '%s'.", ac->name, out_shader->name);
            // Invalidate the new handle and return it.
            khandle_invalidate(&new_handle);
            return new_handle;
        }
    }

    // Process uniforms
    for (u32 i = 0; i < shader_resource->uniform_count; ++i) {
        shader_uniform_config* uc = &shader_resource->uniforms[i];
        b8 uniform_add_result = false;
        if (uniform_type_is_sampler(uc->type)) {
            uniform_add_result = internal_sampler_add(out_shader, uc);
        } else if (uniform_type_is_texture(uc->type)) {
            uniform_add_result = internal_texture_add(out_shader, uc);
        } else {
            uniform_add_result = internal_uniform_add(out_shader, uc, INVALID_ID_U16);
        }
        if (!uniform_add_result) {
            // Invalidate the new handle and return it.
            khandle_invalidate(&new_handle);
            return new_handle;
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
    for (u32 i = 0; i < shader_resource->uniform_count; ++i) {
        shader_uniform_config* uc = &shader_resource->uniforms[i];
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
    if (!renderer_shader_create(state_ptr->renderer, new_handle, shader_resource)) {
        KERROR("Error creating shader.");
        // Invalidate the new handle and return it.
        khandle_invalidate(&new_handle);
        return new_handle;
    }

    return new_handle;
}

static b8 shader_reload(kshader* shader, khandle shader_handle) {

    // Check each shader stage generation for out-of-sync.
    for (u8 i = 0; i < shader->shader_stage_count; ++i) {
        if (shader->stage_source_text_generations[i] != shader->stage_source_text_resources[i]->base.generation) {
            // Sync the generations.
            shader->stage_source_text_generations[i] = shader->stage_source_text_resources[i]->base.generation;
        }
    }

    return renderer_shader_reload(state_ptr->renderer, shader_handle, shader->shader_stage_count, shader->stage_configs);
}