#include "shader_system.h"

#include "containers/darray.h"
#include "core/engine.h"
#include "core/event.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/platform.h"
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
    struct texture_system_state* texture_system;
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
// FIXME: Get rid of this and all references to it and use the engine_systems_get() instead where needed.
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
    state_ptr->texture_system = engine_systems_get()->texture_system;

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
    out_shader->attribute_stride = 0;
    out_shader->shader_stage_count = config->stage_count;
    out_shader->stage_configs = kallocate(sizeof(shader_stage_config) * config->stage_count, MEMORY_TAG_ARRAY);
    out_shader->uniforms = darray_create(shader_uniform);
    out_shader->attributes = darray_create(shader_attribute);

    out_shader->per_frame.uniform_count = 0;
    out_shader->per_frame.uniform_sampler_count = 0;
    out_shader->per_frame.sampler_indices = darray_create(u32);

    out_shader->per_group.bound_id = INVALID_ID;
    // Number of samplers in the shader, per frame. NOT the number of descriptors needed (i.e could be an array).
    out_shader->per_group.uniform_count = 0;
    // Number of samplers in the shader, per group, per frame. NOT the number of descriptors needed (i.e could be an array).
    out_shader->per_group.uniform_sampler_count = 0;
    out_shader->per_group.sampler_indices = darray_create(u32);

    out_shader->per_draw.uniform_count = 0;
    out_shader->per_draw.ubo_offset = 0;
    out_shader->per_draw.ubo_size = 0;
    out_shader->per_draw.ubo_stride = 0;
    out_shader->per_draw.bound_id = INVALID_ID;

    // Examine the uniforms and determine scope as well as a count of samplers.
    u32 total_count = darray_length(config->uniforms);
    for (u32 i = 0; i < total_count; ++i) {
        switch (config->uniforms[i].frequency) {
        case SHADER_UPDATE_FREQUENCY_PER_FRAME:
            // TODO: also track texture uniforms.
            if (uniform_type_is_sampler(config->uniforms[i].type)) {
                out_shader->per_frame.uniform_sampler_count++;
                darray_push(out_shader->per_frame.sampler_indices, i);
            } else {
                out_shader->per_frame.uniform_count++;
            }
            break;
        case SHADER_UPDATE_FREQUENCY_PER_GROUP:
            if (uniform_type_is_sampler(config->uniforms[i].type)) {
                out_shader->per_group.uniform_sampler_count++;
                darray_push(out_shader->per_group.sampler_indices, i);
            } else {
                out_shader->per_group.uniform_count++;
            }
            break;
        case SHADER_UPDATE_FREQUENCY_PER_DRAW:
            out_shader->per_draw.uniform_count++;
            break;
        }
    }

    // Create a hashtable to store uniform array indexes. This provides a direct index into the
    // 'uniforms' array stored in the shader for quick lookups by name.
    u64 element_size = sizeof(u16); // Indexes are stored as u16s.
    u64 element_count = 1023;       // This is more uniforms than we will ever need, but a bigger table reduces collision chance.
    out_shader->hashtable_block = kallocate(element_size * element_count, MEMORY_TAG_HASHTABLE);
    hashtable_create(element_size, element_count, out_shader->hashtable_block, false, &out_shader->uniform_lookup);

    // Invalidate all spots in the hashtable.
    u16 invalid = INVALID_ID_U16;
    hashtable_fill(&out_shader->uniform_lookup, &invalid);

    // A running total of the actual global uniform buffer object size.
    out_shader->per_frame.ubo_size = 0;
    // A running total of the actual instance uniform buffer object size.
    out_shader->per_group.ubo_size = 0;
    // NOTE: UBO alignment requirement set in renderer backend.

    // FIXME: This is hard-coded because the Vulkan spec only guarantees that a _minimum_ 128 bytes of space are available,
    // and it's up to the driver to determine how much is available. Therefore, to avoid complexity, only the
    // lowest common denominator of 128B will be used.
    // Should be determined by the backend and reported thusly.
    out_shader->per_draw.ubo_stride = 128;

    // Take a copy of the flags.
    out_shader->flags = config->flags;

#ifdef _DEBUG
    // NOTE: Only watch module files for debug builds.
    out_shader->module_watch_ids = kallocate(sizeof(u32) * config->stage_count, MEMORY_TAG_ARRAY);
#endif

    // Examine shader stages and load shader source as required. This source is
    // then fed to the backend renderer, which stands up any shader program resources
    // as required.
    // TODO: Implement #include directives here at this level so it's handled the same
    // regardless of what backend is being used.
    // Each stage.
    for (u8 i = 0; i < config->stage_count; ++i) {
        out_shader->stage_configs[i].stage = config->stage_configs[i].stage;
        out_shader->stage_configs[i].filename = string_duplicate(config->stage_configs[i].filename);
        // FIXME: Convert to use the new resource system.
        // Read the resource.
        resource text_resource;
        if (!resource_system_load(out_shader->stage_configs[i].filename, RESOURCE_TYPE_TEXT, 0, &text_resource)) {
            KERROR("Unable to read shader file: %s.", out_shader->stage_configs[i].filename);
            return false;
        }
        // Take a copy of the source and length, then release the resource.
        out_shader->stage_configs[i].source_length = text_resource.data_size;
        out_shader->stage_configs[i].source = string_duplicate(text_resource.data);
        // TODO: Implement recursive #include directives here at this level so it's handled the same
        // regardless of what backend is being used.
        // This should recursively replace #includes with the file content in-place and adjust the source
        // length along the way.

#ifdef _DEBUG
        // Allow shader hot-reloading in debug builds.
        if (!platform_watch_file(text_resource.full_path, &out_shader->module_watch_ids[i])) {
            // If this fails, warn about it but there's no need to crash over it.
            KWARN("Failed to watch shader source file '%s'.", text_resource.full_path);
        }

#endif
        // Release the resource as it isn't needed anymore at this point.
        resource_system_unload(&text_resource);
    }

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

    // Examine shader stages and load shader source as required. This source is
    // then fed to the backend renderer, which stands up any shader program resources
    // as required.
    // TODO: Implement #include directives here at this level so it's handled the same
    // regardless of what backend is being used.

    // Make a copy of the stage configs in case a file fails to load.
    b8 has_error = false;
    shader_stage_config* new_stage_configs = kallocate(sizeof(shader_stage_config) * s->shader_stage_count, MEMORY_TAG_ARRAY);
    for (u8 i = 0; i < s->shader_stage_count; ++i) {
        // Read the resource.
        resource text_resource;
        if (!resource_system_load(s->stage_configs[i].filename, RESOURCE_TYPE_TEXT, 0, &text_resource)) {
            KERROR("Unable to read shader file: %s.", s->stage_configs[i].filename);
            has_error = true;
            break;
        }

        // Free the old source.
        if (s->stage_configs[i].source) {
            string_free(s->stage_configs[i].source);
        }

        // Take a copy of the source and length, then release the resource.
        new_stage_configs[i].source = string_duplicate(text_resource.data);
        // TODO: Implement recursive #include directives here at this level so it's handled the same
        // regardless of what backend is being used.
        // This should recursively replace #includes with the file content in-place and adjust the source
        // length along the way.

        // Release the resource as it isn't needed anymore at this point.
        resource_system_unload(&text_resource);
    }

    for (u8 i = 0; i < s->shader_stage_count; ++i) {
        if (has_error) {
            if (new_stage_configs[i].source) {
                string_free(new_stage_configs[i].source);
            }
        } else {
            s->stage_configs[i].source = new_stage_configs[i].source;
        }
    }
    kfree(new_stage_configs, sizeof(shader_stage_config) * s->shader_stage_count, MEMORY_TAG_ARRAY);
    if (has_error) {
        return false;
    }

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

    // Free the name.
    if (s->name) {
        u32 length = string_length(s->name);
        kfree(s->name, length + 1, MEMORY_TAG_STRING);
    }
    s->name = 0;

#ifdef _DEBUG
    if (s->module_watch_ids) {
        // Unwatch the shader files.
        for (u8 i = 0; i < s->shader_stage_count; ++i) {
            platform_unwatch_file(s->module_watch_ids[i]);
        }
    }
#endif
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

b8 shader_system_sampler_set(u32 shader_id, const char* sampler_name, const kresource_texture* t) {
    return shader_system_sampler_set_arrayed(shader_id, sampler_name, 0, t);
}

b8 shader_system_sampler_set_arrayed(u32 shader_id, const char* sampler_name, u32 array_index, const kresource_texture* t) {
    return shader_system_uniform_set_arrayed(shader_id, sampler_name, array_index, t);
}

b8 shader_system_sampler_set_by_location(u32 shader_id, u16 location, const kresource_texture* t) {
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

b8 shader_system_bind_group(u32 shader_id, u32 group_id) {
    if (group_id == INVALID_ID) {
        KERROR("Cannot bind shader instance INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader_id].per_group.bound_id = group_id;
    return true;
}

b8 shader_system_bind_draw_id(u32 shader_id, u32 draw_id) {
    if (draw_id == INVALID_ID) {
        KERROR("Cannot bind shader local id INVALID_ID.");
        return false;
    }
    state_ptr->shaders[shader_id].per_draw.bound_id = draw_id;
    return true;
}

b8 shader_system_apply_per_frame(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_per_frame(state_ptr->renderer, s);
}

b8 shader_system_apply_per_group(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_per_group(state_ptr->renderer, s);
}

b8 shader_system_apply_per_draw(u32 shader_id) {
    shader* s = &state_ptr->shaders[shader_id];
    return renderer_shader_apply_per_draw(state_ptr->renderer, s);
}

static b8 per_group_or_per_draw_acquire(u32 shader_id, shader_update_frequency frequency, u32 map_count, kresource_texture_map** maps, u32* out_id) {
    shader* selected_shader = shader_system_get_by_id(shader_id);

    // Ensure that configs are setup for required texture maps.
    shader_texture_resource_config config = {0};
    u32 sampler_count = selected_shader->per_group.uniform_sampler_count;

    config.uniform_config_count = sampler_count;
    if (sampler_count > 0) {
        config.uniform_configs = kallocate(sizeof(shader_frequency_uniform_texture_config) * config.uniform_config_count, MEMORY_TAG_ARRAY);
    } else {
        config.uniform_configs = 0;
    }

    // Create a sampler config for each map.
    for (u32 i = 0; i < sampler_count; ++i) {
        shader_uniform* u = &selected_shader->uniforms[selected_shader->per_group.sampler_indices[i]];
        shader_frequency_uniform_texture_config* uniform_config = &config.uniform_configs[i];
        /* uniform_config->uniform_location = u->location; */
        uniform_config->kresource_texture_map_count = KMAX(u->array_length, 1);
        uniform_config->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * uniform_config->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        for (u32 j = 0; j < uniform_config->kresource_texture_map_count; ++j) {
            uniform_config->kresource_texture_maps[j] = maps[i];

            // Acquire resources for the map, but only if a texture is assigned.
            if (uniform_config->kresource_texture_maps[j]->texture) {
                if (!renderer_kresource_texture_map_resources_acquire(state_ptr->renderer, uniform_config->kresource_texture_maps[j])) {
                    KERROR("Unable to acquire resources for texture map.");
                    return false;
                }
            }
        }
    }

    // Acquire the instance resources for this shader.
    b8 result = false;
    if (frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
        // FIXME: rename these
        result = renderer_shader_per_group_resources_acquire(state_ptr->renderer, selected_shader, &config, out_id);
    } else if (frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        result = renderer_shader_per_draw_resources_acquire(state_ptr->renderer, selected_shader, &config, out_id);
    } else {
        KASSERT_MSG(false, "Global scope does not require resource acquisition, ya dingus.");
        return false;
    }

    if (!result) {
        KERROR("Failed to acquire %s renderer resources for shader '%s'.", frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP ? "group" : "per-draw", selected_shader->name);
    }

    // Clean up the uniform configs.
    if (config.uniform_configs) {
        for (u32 i = 0; i < config.uniform_config_count; ++i) {
            shader_frequency_uniform_texture_config* ucfg = &config.uniform_configs[i];
            if (ucfg->kresource_texture_maps) {
                kfree(ucfg->kresource_texture_maps, sizeof(shader_frequency_uniform_texture_config) * ucfg->kresource_texture_map_count, MEMORY_TAG_ARRAY);
                ucfg->kresource_texture_maps = 0;
            }
        }
        kfree(config.uniform_configs, sizeof(shader_frequency_uniform_texture_config) * config.uniform_config_count, MEMORY_TAG_ARRAY);
    }

    return result;
}

b8 shader_system_shader_instance_acquire(u32 shader_id, u32 map_count, kresource_texture_map** maps, u32* out_instance_id) {
    return per_group_or_per_draw_acquire(shader_id, SHADER_UPDATE_FREQUENCY_PER_GROUP, map_count, maps, out_instance_id);
}

b8 shader_system_shader_per_draw_acquire(u32 shader_id, u32 map_count, kresource_texture_map** maps, u32* out_local_id) {
    return per_group_or_per_draw_acquire(shader_id, SHADER_UPDATE_FREQUENCY_PER_DRAW, map_count, maps, out_local_id);
}

static b8 per_group_or_per_draw_release(u32 shader_id, shader_update_frequency frequency, u32 id, u32 map_count, kresource_texture_map* maps) {
    shader* selected_shader = shader_system_get_by_id(shader_id);

    // Release texture map resources.
    for (u32 i = 0; i < map_count; ++i) {
        renderer_kresource_texture_map_resources_release(state_ptr->renderer, &maps[i]);
    }

    b8 result = false;
    if (frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
        // FIXME: rename these
        result = renderer_shader_per_group_resources_release(state_ptr->renderer, selected_shader, id);
    } else if (frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        result = renderer_shader_per_draw_resources_release(state_ptr->renderer, selected_shader, id);
    } else {
        KASSERT_MSG(false, "Per-frame shader update frequency should not be used when releasing resources.");
    }

    if (!result) {
        KERROR("Failed to acquire %s renderer resources for shader '%s'.", frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP ? "group" : "per-draw", selected_shader->name);
    }

    return result;
}

b8 shader_system_shader_group_release(u32 shader_id, u32 group_id, u32 map_count, kresource_texture_map* maps) {
    return per_group_or_per_draw_release(shader_id, SHADER_UPDATE_FREQUENCY_PER_GROUP, group_id, map_count, maps);
}

b8 shader_system_shader_per_draw_release(u32 shader_id, u32 per_draw_id, u32 map_count, kresource_texture_map* maps) {
    return per_group_or_per_draw_release(shader_id, SHADER_UPDATE_FREQUENCY_PER_DRAW, per_draw_id, map_count, maps);
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
    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        // FIXME: This should work.
        KERROR("add_sampler cannot add a sampler at per-draw scope.");
        return false;
    }

    // Verify the name is valid and unique.
    if (!uniform_name_valid(shader, config->name) || !shader_uniform_add_state_valid(shader)) {
        return false;
    }

    // If per-frame, push into the per-frame list.
    u32 location = 0;
    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
        shader->per_frame.texture_count = darray_length(shader->per_frame_texture_maps);
        if (shader->per_frame.texture_count + 1 > state_ptr->config.max_per_frame_textures) {
            KERROR("Shader per-frame texture count %i exceeds max of %i", shader->per_frame.texture_count, state_ptr->config.max_per_frame_textures);
            return false;
        }
        location = shader->per_frame.texture_count;
        shader->per_draw.texture_count++;

        // FIXME: Convert to use sampler instead of texture map.
        // NOTE: creating a default texture map to be used here. Can always be updated later.
        kresource_texture_map default_map = {};
        default_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
        default_map.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
        default_map.repeat_u = default_map.repeat_v = default_map.repeat_w = TEXTURE_REPEAT_REPEAT;

        // Allocate a pointer assign the texture, and push into global texture maps.
        // NOTE: This allocation is only done for global texture maps.
        kresource_texture_map* map = kallocate(sizeof(kresource_texture_map), MEMORY_TAG_RENDERER);
        *map = default_map;
        map->texture = texture_system_get_default_kresource_texture(state_ptr->texture_system);

        if (!renderer_kresource_texture_map_resources_acquire(state_ptr->renderer, map)) {
            KERROR("Failed to acquire resources for per-frame texture map during shader creation.");
            return false;
        }

        darray_push(shader->per_frame_texture_maps, map);
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
        // Per-group, so keep count of how many need to be added during the resource acquisition.
        if (shader->per_group.texture_count + 1 > state_ptr->config.max_per_group_textures) {
            KERROR("Shader per_group texture count %i exceeds max of %i", shader->per_group.texture_count, state_ptr->config.max_per_group_textures);
            return false;
        }
        location = shader->per_group.texture_count;
        shader->per_group.texture_count++;
    } else if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        // Per-draw, so keep count of how many need to be added during the resource acquisition.
        if (shader->per_group.texture_count + 1 > state_ptr->config.max_per_draw_textures) {
            KERROR("Shader per_draw texture count %i exceeds max of %i", shader->per_draw.texture_count, state_ptr->config.max_per_draw_textures);
            return false;
        }
        location = shader->per_draw.texture_count;
        shader->per_draw.texture_count++;
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
    entry.frequency = config->frequency;
    entry.type = config->type;
    entry.array_length = config->array_length;
    b8 is_global = (config->frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME);
    if (is_sampler) {
        // Just use the passed in location
        entry.location = location;
    } else {
        entry.location = entry.index;
    }

    if (config->frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
        entry.set_index = 2; // NOTE: set 2 doesn't exist in Vulkan, it's a push constant.
        entry.offset = shader->per_draw.ubo_size;
        entry.size = config->size;
    } else {
        entry.set_index = (u32)config->frequency;
        entry.offset = is_sampler ? 0 : is_global ? shader->per_frame.ubo_size
                                                  : shader->per_group.ubo_size;
        entry.size = is_sampler ? 0 : config->size;
    }

    if (!hashtable_set(&shader->uniform_lookup, config->name, &entry.index)) {
        KERROR("Failed to add uniform.");
        return false;
    }
    darray_push(shader->uniforms, entry);

    if (!is_sampler) {
        if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_FRAME) {
            shader->per_frame.ubo_size += (entry.size * entry.array_length);
        } else if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_GROUP) {
            shader->per_group.ubo_size += (entry.size * entry.array_length);
        } else if (entry.frequency == SHADER_UPDATE_FREQUENCY_PER_DRAW) {
            shader->per_draw.ubo_size += (entry.size * entry.array_length);
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
