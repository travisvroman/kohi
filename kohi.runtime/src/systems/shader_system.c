#include "shader_system.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core/event.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_utils.h"
#include "resources/resource_types.h"
#include "strings/kstring.h"
#include "systems/resource_system.h"
#include "systems/texture_system.h"

// The internal shader system state.
typedef struct shader_system_state {
    // A pointer to the renderer system state.
    struct renderer_system_state* renderer;
    // This system's configuration.
    shader_system_config config;
    // A lookup table for shader name->id
    hashtable lookup;
    // The memory used for the lookup table.
    void* lookup_memory;
    // A collection of created shaders.
    shader* shaders;
} shader_system_state;

// A pointer to hold the internal system state.
static shader_system_state* state_ptr = 0;

static b8 internal_attribute_add(shader* shader, const shader_attribute_config* config);
static b8 internal_sampler_add(shader* shader, shader_uniform_config* config);
static u32 generate_new_shader_id(void);
static b8 internal_uniform_add(shader* shader, const shader_uniform_config* config, u32 location);
static b8 uniform_name_valid(shader* shader, const char* uniform_name);
static b8 shader_uniform_add_state_valid(shader* shader);
static void internal_shader_destroy(shader* s);
///////////////////////

#ifdef _DEBUG
static b8 file_watch_event(u16 code, void* sender, void* listener_inst, event_context context) {
    shader_system_state* typed_state = (shader_system_state*)listener_inst;
    if (code == EVENT_CODE_WATCHED_FILE_WRITTEN) {
        u32 file_watch_id = context.data.u32[0];

        // Search shaders for the one with the changed file watch id.
        for (u32 i = 0; i < typed_state->config.max_shader_count; ++i) {
            shader* s = &typed_state->shaders[i];
            for (u32 w = 0; w < s->shader_stage_count; ++w) {
                if (s->module_watch_ids[w] == file_watch_id) {
                    if (!shader_system_reload(s->id)) {
                        KWARN("Shader hot-reload failed for shader '%s'. See logs for details.", s->name);
                        // Allow other systems to pick this up.
                        return false;
                    }
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
            KERROR("shader_system_initialize - config.max_shader_count must be greater than 0");
            return false;
        } else {
            // This is to help avoid hashtable collisions.
            KWARN("shader_system_initialize - config.max_shader_count is recommended to be at least 512.");
        }
    }

    // Figure out how large of a hashtable is needed.
    // Block of memory will contain state structure then the block for the hashtable.
    u64 struct_requirement = sizeof(shader_system_state);
    u64 hashtable_requirement = sizeof(u32) * typed_config->max_shader_count;
    u64 shader_array_requirement = sizeof(shader) * typed_config->max_shader_count;
    *memory_requirement = struct_requirement + hashtable_requirement + shader_array_requirement;

    if (!memory) {
        return true;
    }

    // Setup the state pointer, memory block, shader array, then create the hashtable.
    state_ptr = memory;
    u64 addr = (u64)memory;
    state_ptr->lookup_memory = (void*)(addr + struct_requirement);
    state_ptr->shaders = (void*)((u64)state_ptr->lookup_memory + hashtable_requirement);
    state_ptr->config = *typed_config;
    hashtable_create(sizeof(u32), typed_config->max_shader_count, state_ptr->lookup_memory, false, &state_ptr->lookup);

    // Invalidate all shader ids.
    for (u32 i = 0; i < typed_config->max_shader_count; ++i) {
        state_ptr->shaders[i].id = INVALID_ID;
    }

    // Fill the table with invalid ids.
    u32 invalid_fill_id = INVALID_ID;
    if (!hashtable_fill(&state_ptr->lookup, &invalid_fill_id)) {
        KERROR("hashtable_fill failed.");
        return false;
    }

    for (u32 i = 0; i < state_ptr->config.max_shader_count; ++i) {
        state_ptr->shaders[i].id = INVALID_ID;
    }

    // Keep a pointer to the renderer state.
    state_ptr->renderer = engine_systems_get()->renderer_system;

    // Watch for file hot reloads in debug builds.
#ifdef _DEBUG
    event_register(EVENT_CODE_WATCHED_FILE_WRITTEN, state_ptr, file_watch_event);
#endif

    return true;
}

void shader_system_shutdown(void* state) {
    if (state) {
        // Destroy any shaders still in existence.
        shader_system_state* st = (shader_system_state*)state;
        for (u32 i = 0; i < st->config.max_shader_count; ++i) {
            shader* s = &st->shaders[i];
            if (s->id != INVALID_ID) {
                internal_shader_destroy(s);
            }
        }
        hashtable_destroy(&st->lookup);
        kzero_memory(st, sizeof(shader_system_state));
    }

    state_ptr = 0;
}

b8 shader_system_create(const shader_config* config) {
    u32 id = generate_new_shader_id();
    shader* out_shader = &state_ptr->shaders[id];
    kzero_memory(out_shader, sizeof(shader));
    out_shader->id = id;
    if (out_shader->id == INVALID_ID) {
        KERROR("Unable to find free slot to create new shader. Aborting.");
        return false;
    }
    out_shader->state = SHADER_STATE_NOT_CREATED;
    out_shader->name = string_duplicate(config->name);
    out_shader->local_ubo_offset = 0;
    out_shader->local_ubo_size = 0;
    out_shader->local_ubo_stride = 0;
    out_shader->bound_instance_id = INVALID_ID;
    out_shader->attribute_stride = 0;

    // Setup arrays
    out_shader->global_texture_maps = darray_create(texture_map*);
    out_shader->uniforms = darray_create(shader_uniform);
    out_shader->attributes = darray_create(shader_attribute);

    // Create a hashtable to store uniform array indexes. This provides a direct index into the
    // 'uniforms' array stored in the shader for quick lookups by name.
    u64 element_size = sizeof(u16); // Indexes are stored as u16s.
    u64 element_count = 1023;       // This is more uniforms than we will ever need, but a bigger table reduces collision chance.
    out_shader->hashtable_block = kallocate(element_size * element_count, MEMORY_TAG_HASHTABLE);
    hashtable_create(element_size, element_count, out_shader->hashtable_block, false, &out_shader->uniform_lookup);

    // Invalidate all spots in the hashtable.
    u32 invalid = INVALID_ID;
    hashtable_fill(&out_shader->uniform_lookup, &invalid);

    // A running total of the actual global uniform buffer object size.
    out_shader->global_ubo_size = 0;
    // A running total of the actual instance uniform buffer object size.
    out_shader->ubo_size = 0;
    // NOTE: UBO alignment requirement set in renderer backend.

    // This is hard-coded because the Vulkan spec only guarantees that a _minimum_ 128 bytes of space are available,
    // and it's up to the driver to determine how much is available. Therefore, to avoid complexity, only the
    // lowest common denominator of 128B will be used.
    out_shader->local_ubo_stride = 128;

    // Take a copy of the flags.
    out_shader->flags = config->flags;

    if (!renderer_shader_create(state_ptr->renderer, out_shader, config)) {
        KERROR("Error creating shader.");
        return false;
    }

    // Ready to be initialized.
    out_shader->state = SHADER_STATE_UNINITIALIZED;

    // Process attributes
    for (u32 i = 0; i < config->attribute_count; ++i) {
        shader_attribute_config* ac = &config->attributes[i];
        if (!internal_attribute_add(out_shader, ac)) {
            KERROR("Failed to add attribute '%s' to shader '%s'.", ac->name, config->name);
            return false;
        }
    }

    // Process uniforms
    for (u32 i = 0; i < config->uniform_count; ++i) {
        shader_uniform_config* uc = &config->uniforms[i];
        if (uniform_type_is_sampler(uc->type)) {
            if (!internal_sampler_add(out_shader, uc)) {
                KERROR("Failed to add sampler '%s' to shader '%s'.", uc->name, config->name);
                return false;
            }
        } else {
            if (!internal_uniform_add(out_shader, uc, INVALID_ID)) {
                KERROR("Failed to add uniform '%s' to shader '%s'.", uc->name, config->name);
                return false;
            }
        }
    }

    // Initialize the shader.
    if (!renderer_shader_initialize(state_ptr->renderer, out_shader)) {
        KERROR("shader_system_create: initialization failed for shader '%s'.", config->name);
        // NOTE: initialize automatically destroys the shader if it fails.
        return false;
    }

    // At this point, creation is successful, so store the shader id in the hashtable
    // so this can be looked up by name later.
    if (!hashtable_set(&state_ptr->lookup, config->name, &out_shader->id)) {
        // Dangit, we got so far... welp, nuke the shader and boot.
        renderer_shader_destroy(state_ptr->renderer, out_shader);
        return false;
    }

    return true;
}

b8 shader_system_reload(u32 shader_id) {
    if (shader_id == INVALID_ID) {
        return false;
    }

    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_reload(state_ptr->renderer, s);
}

u32 shader_system_get_id(const char* shader_name) {
    u32 shader_id = INVALID_ID;
    if (!hashtable_get(&state_ptr->lookup, shader_name, &shader_id)) {
        KERROR("There is no shader registered named '%s'.", shader_name);
        return INVALID_ID;
    }
    // KTRACE("Got id %u for shader named '%s'.", shader_id, shader_name);
    return shader_id;
}

shader* shader_system_get_by_id(u32 shader_id) {
    if (shader_id == INVALID_ID) {
        KERROR("shader_system_get_by_id was passed INVALID_ID. Null will be returned.");
        return 0;
    }
    if (state_ptr->shaders[shader_id].id == INVALID_ID) {
        KERROR("shader_system_get_by_id was passed an invalid id (%u. Null will be returned.", shader_id);
        return 0;
    }
    if (shader_id >= state_ptr->config.max_shader_count) {
        KERROR("shader_system_get_by_id was passed an id (%u) out of range (0-%u). Null will be returned.", shader_id, state_ptr->config.max_shader_count);
        return 0;
    }
    return &state_ptr->shaders[shader_id];
}

shader* shader_system_get(const char* shader_name) {
    u32 shader_id = shader_system_get_id(shader_name);
    if (shader_id != INVALID_ID) {
        return shader_system_get_by_id(shader_id);
    }

    // Attempt to load the shader resource and return it.
    resource shader_config_resource;
    if (!resource_system_load(shader_name, RESOURCE_TYPE_SHADER, 0, &shader_config_resource)) {
        KERROR("Failed to load shader resource for shader '%s'.", shader_name);
        return 0;
    }
    shader_config* config = (shader_config*)shader_config_resource.data;
    if (!shader_system_create(config)) {
        KERROR("Failed to create shader '%s'.", shader_name);
        return 0;
    }
    resource_system_unload(&shader_config_resource);

    // Attempt once more to get a shader id.
    shader_id = shader_system_get_id(shader_name);
    if (shader_id != INVALID_ID) {
        return shader_system_get_by_id(shader_id);
    }

    KERROR("There is not shader available called '%s', and one by that name could also not be loaded.", shader_name);
    return 0;
}

static void internal_shader_destroy(shader* s) {
    renderer_shader_destroy(state_ptr->renderer, s);

    // Set it to be unusable right away.
    s->state = SHADER_STATE_NOT_CREATED;

    u32 sampler_count = darray_length(s->global_texture_maps);
    for (u32 i = 0; i < sampler_count; ++i) {
        kfree(s->global_texture_maps[i], sizeof(texture_map), MEMORY_TAG_RENDERER);
    }
    darray_destroy(s->global_texture_maps);

    // Free the name.
    if (s->name) {
        u32 length = string_length(s->name);
        kfree(s->name, length + 1, MEMORY_TAG_STRING);
    }
    s->name = 0;
}

void shader_system_destroy(const char* shader_name) {
    u32 shader_id = shader_system_get_id(shader_name);
    if (shader_id == INVALID_ID) {
        return;
    }

    shader* s = &state_ptr->shaders[shader_id];

    internal_shader_destroy(s);
}

b8 shader_system_set_wireframe(u32 shader_id, b8 wireframe_enabled) {
    // Disabling is always supported because it's basically a no-op.

    shader* s = &state_ptr->shaders[shader_id];
    if (!wireframe_enabled) {
        s->is_wireframe = false;
        return true;
    }

    return renderer_shader_set_wireframe(state_ptr->renderer, s, wireframe_enabled);
}

b8 shader_system_use_by_id(u32 shader_id) {
    shader* next_shader = shader_system_get_by_id(shader_id);
    if (!renderer_shader_use(state_ptr->renderer, next_shader)) {
        KERROR("Failed to use shader '%s'.", next_shader->name);
        return false;
    }
    return true;
}

u16 shader_system_uniform_location(u32 shader_id, const char* uniform_name) {
    if (shader_id == INVALID_ID) {
        KERROR("shader_system_uniform_location called with invalid shader id.");
        return INVALID_ID_U16;
    }
    shader* s = &state_ptr->shaders[shader_id];

    u16 index = INVALID_ID_U16;
    if (!hashtable_get(&s->uniform_lookup, uniform_name, &index) || index == INVALID_ID_U16) {
        KERROR("Shader '%s' does not have a registered uniform named '%s'", s->name, uniform_name);
        return INVALID_ID_U16;
    }
    return s->uniforms[index].index;
}

b8 shader_system_uniform_set(u32 shader_id, const char* uniform_name, const void* value) {
    return shader_system_uniform_set_arrayed(shader_id, uniform_name, 0, value);
}

b8 shader_system_uniform_set_arrayed(u32 shader_id, const char* uniform_name, u32 array_index, const void* value) {
    if (shader_id == INVALID_ID) {
        KERROR("shader_system_uniform_set_arrayed called with invalid shader id.");
        return false;
    }

    u16 index = shader_system_uniform_location(shader_id, uniform_name);
    return shader_system_uniform_set_by_location_arrayed(shader_id, index, array_index, value);
}

b8 shader_system_sampler_set(u32 shader_id, const char* sampler_name, const texture* t) {
    return shader_system_sampler_set_arrayed(shader_id, sampler_name, 0, t);
}

b8 shader_system_sampler_set_arrayed(u32 shader_id, const char* sampler_name, u32 array_index, const texture* t) {
    return shader_system_uniform_set_arrayed(shader_id, sampler_name, array_index, t);
}

b8 shader_system_sampler_set_by_location(u32 shader_id, u16 location, const texture* t) {
    return shader_system_uniform_set_by_location_arrayed(shader_id, location, 0, t);
}

b8 shader_system_uniform_set_by_location(u32 shader_id, u16 location, const void* value) {
    return shader_system_uniform_set_by_location_arrayed(shader_id, location, 0, value);
}

b8 shader_system_uniform_set_by_location_arrayed(u32 shader_id, u16 location, u32 array_index, const void* value) {
    shader* s = &state_ptr->shaders[shader_id];
    shader_uniform* uniform = &s->uniforms[location];
    return renderer_shader_uniform_set(state_ptr->renderer, s, uniform, array_index, value);
}

b8 shader_system_bind_instance(u32 shader_id, u32 instance_id) {
    state_ptr->shaders[shader_id].bound_instance_id = instance_id;
    return true;
}

b8 shader_system_apply_global(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_globals(state_ptr->renderer, s);
}

b8 shader_system_apply_instance(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_instance(state_ptr->renderer, s);
}

b8 shader_system_apply_local(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_local(state_ptr->renderer, s);
}

b8 shader_system_shader_instance_acquire(u32 shader_id, u32 map_count, texture_map* maps, u32* out_instance_id) {
    shader* selected_shader = shader_system_get_by_id(shader_id);

    // Ensure that configs are setup for required texture maps.
    shader_instance_resource_config instance_resource_config = {0};
    u32 instance_sampler_count = selected_shader->instance_uniform_sampler_count;

    instance_resource_config.uniform_config_count = instance_sampler_count;
    if (instance_sampler_count > 0) {
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);
    } else {
        instance_resource_config.uniform_configs = 0;
    }

    // Create a sampler config for each map.
    for (u32 i = 0; i < instance_sampler_count; ++i) {
        shader_uniform* u = &selected_shader->uniforms[selected_shader->instance_sampler_indices[i]];
        shader_instance_uniform_texture_config* uniform_config = &instance_resource_config.uniform_configs[i];
        /* uniform_config->uniform_location = u->location; */
        uniform_config->texture_map_count = KMAX(u->array_length, 1);
        uniform_config->texture_maps = kallocate(sizeof(texture_map*) * uniform_config->texture_map_count, MEMORY_TAG_ARRAY);
        for (u32 j = 0; j < uniform_config->texture_map_count; ++j) {
            uniform_config->texture_maps[j] = &maps[i];

            // Acquire resources for the map.
            if (!renderer_texture_map_resources_acquire(uniform_config->texture_maps[j])) {
                KERROR("Unable to acquire resources for texture map.");
                return false;
            }
        }
    }

    // Acquire the instance resources for this shader.
    b8 result = renderer_shader_instance_resources_acquire(state_ptr->renderer, selected_shader, &instance_resource_config, out_instance_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for shader '%s'.", selected_shader->name);
    }

    // Clean up the uniform configs.
    if (instance_resource_config.uniform_configs) {
        for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
            shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
            kfree(ucfg->texture_maps, sizeof(shader_instance_uniform_texture_config) * ucfg->texture_map_count, MEMORY_TAG_ARRAY);
            ucfg->texture_maps = 0;
        }
        kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);
    }

    return result;
}

b8 shader_system_shader_instance_release(u32 shader_id, u32 instance_id) {
    shader* selected_shader = shader_system_get_by_id(shader_id);

    // Release the instance resources for this shader.
    b8 result = renderer_shader_instance_resources_release(state_ptr->renderer, selected_shader, instance_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for shader '%s'.", selected_shader->name);
    }

    return result;
}

static b8 internal_attribute_add(shader* shader, const shader_attribute_config* config) {
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
    attrib.name = string_duplicate(config->name);
    attrib.size = size;
    attrib.type = config->type;
    darray_push(shader->attributes, attrib);

    return true;
}

static b8 internal_sampler_add(shader* shader, shader_uniform_config* config) {
    // Samples can't be used for push constants.
    if (config->scope == SHADER_SCOPE_LOCAL) {
        KERROR("add_sampler cannot add a sampler at local scope.");
        return false;
    }

    // Verify the name is valid and unique.
    if (!uniform_name_valid(shader, config->name) || !shader_uniform_add_state_valid(shader)) {
        return false;
    }

    // If global, push into the global list.
    u32 location = 0;
    if (config->scope == SHADER_SCOPE_GLOBAL) {
        u32 global_texture_count = darray_length(shader->global_texture_maps);
        if (global_texture_count + 1 > state_ptr->config.max_global_textures) {
            KERROR("Shader global texture count %i exceeds max of %i", global_texture_count, state_ptr->config.max_global_textures);
            return false;
        }
        location = global_texture_count;

        // NOTE: creating a default texture map to be used here. Can always be updated later.
        texture_map default_map = {};
        default_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
        default_map.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
        default_map.repeat_u = default_map.repeat_v = default_map.repeat_w = TEXTURE_REPEAT_REPEAT;

        // Allocate a pointer assign the texture, and push into global texture maps.
        // NOTE: This allocation is only done for global texture maps.
        texture_map* map = kallocate(sizeof(texture_map), MEMORY_TAG_RENDERER);
        *map = default_map;
        map->texture = texture_system_get_default_texture();

        if (!renderer_texture_map_resources_acquire(map)) {
            KERROR("Failed to acquire resources for global texture map during shader creation.");
            return false;
        }

        darray_push(shader->global_texture_maps, map);
    } else {
        // Otherwise, it's instance-level, so keep count of how many need to be added during the resource acquisition.
        if (shader->instance_texture_count + 1 > state_ptr->config.max_instance_textures) {
            KERROR("Shader instance texture count %i exceeds max of %i", shader->instance_texture_count, state_ptr->config.max_instance_textures);
            return false;
        }
        location = shader->instance_texture_count;
        shader->instance_texture_count++;
    }

    // Treat it like a uniform. NOTE: In the case of samplers, out_location is used to determine the
    // hashtable entry's 'location' field value directly, and is then set to the index of the uniform array.
    // This allows location lookups for samplers as if they were uniforms as well (since technically they are).
    // TODO: might need to store this elsewhere
    if (!internal_uniform_add(shader, config, location)) {
        KERROR("Unable to add sampler uniform.");
        return false;
    }

    return true;
}

static u32 generate_new_shader_id(void) {
    for (u32 i = 0; i < state_ptr->config.max_shader_count; ++i) {
        if (state_ptr->shaders[i].id == INVALID_ID) {
            return i;
        }
    }
    return INVALID_ID;
}

static b8 internal_uniform_add(shader* shader, const shader_uniform_config* config, u32 location) {
    if (!shader_uniform_add_state_valid(shader) || !uniform_name_valid(shader, config->name)) {
        return false;
    }
    u32 uniform_count = darray_length(shader->uniforms);
    if (uniform_count + 1 > state_ptr->config.max_uniform_count) {
        KERROR("A shader can only accept a combined maximum of %d uniforms and samplers at global, instance and local scopes.", state_ptr->config.max_uniform_count);
        return false;
    }
    b8 is_sampler = uniform_type_is_sampler(config->type);
    shader_uniform entry;
    entry.index = uniform_count; // Index is saved to the hashtable for lookups.
    entry.scope = config->scope;
    entry.type = config->type;
    entry.array_length = config->array_length;
    b8 is_global = (config->scope == SHADER_SCOPE_GLOBAL);
    if (is_sampler) {
        // Just use the passed in location
        entry.location = location;
    } else {
        entry.location = entry.index;
    }

    if (config->scope == SHADER_SCOPE_LOCAL) {
        entry.set_index = 2; // NOTE: set 2 doesn't exist in Vulkan, it's a push constant.
        entry.offset = shader->local_ubo_size;
        entry.size = config->size;
    } else {
        entry.set_index = (u32)config->scope;
        entry.offset = is_sampler ? 0 : is_global ? shader->global_ubo_size
                                                  : shader->ubo_size;
        entry.size = is_sampler ? 0 : config->size;
    }

    if (!hashtable_set(&shader->uniform_lookup, config->name, &entry.index)) {
        KERROR("Failed to add uniform.");
        return false;
    }
    darray_push(shader->uniforms, entry);

    if (!is_sampler) {
        if (entry.scope == SHADER_SCOPE_GLOBAL) {
            shader->global_ubo_size += (entry.size * entry.array_length);
        } else if (entry.scope == SHADER_SCOPE_INSTANCE) {
            shader->ubo_size += (entry.size * entry.array_length);
        } else if (entry.scope == SHADER_SCOPE_LOCAL) {
            shader->local_ubo_size += (entry.size * entry.array_length);
        }
    }

    return true;
}

static b8 uniform_name_valid(shader* shader, const char* uniform_name) {
    if (!uniform_name || !string_length(uniform_name)) {
        KERROR("Uniform name must exist.");
        return false;
    }
    u16 location;
    if (hashtable_get(&shader->uniform_lookup, uniform_name, &location) && location != INVALID_ID_U16) {
        KERROR("A uniform by the name '%s' already exists on shader '%s'.", uniform_name, shader->name);
        return false;
    }
    return true;
}

static b8 shader_uniform_add_state_valid(shader* shader) {
    if (shader->state != SHADER_STATE_UNINITIALIZED) {
        KERROR("Uniforms may only be added to shaders before initialization.");
        return false;
    }
    return true;
}
