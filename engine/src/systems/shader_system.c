#include "shader_system.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "containers/hashtable.h"
#include "renderer/renderer_frontend.h"
#include

typedef struct shader_system_state {
    shader_system_config config;
    // Shader name->id
    hashtable lookup;
    void* lookup_memory;
} shader_system_state;

static shader_system_state* state_ptr = 0;

b8 shader_system_initialize(u64* memory_requirement, void* memory, shader_system_config config) {
    // Verify configuration.
    if (config.max_shader_count < 512) {
        if (config.max_shader_count == 0) {
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
    u64 hashtable_requirement = sizeof(u32) * config.max_shader_count;
    *memory_requirement = struct_requirement + hashtable_requirement;

    if (!memory) {
        return true;
    }

    // Setup the state pointer and memory block, then create the hashtable.
    state_ptr = memory;
    state_ptr->lookup_memory = state_ptr + struct_requirement;
    state_ptr->config = config;
    hashtable_create(sizeof(u32), config.max_shader_count, state_ptr->lookup_memory, false, &state_ptr->lookup);

    // Fill the table with invalid ids.
    u32 invalid_fill_id = INVALID_ID;
    if (!hashtable_fill(&state_ptr->lookup, &invalid_fill_id)) {
        KERROR("hashtable_fill failed.");
        return false;
    }

    return true;
}

void shader_system_shutdown(void* state) {
    if (state) {
        shader_system_state* s = (shader_system_state*)state;
        hashtable_destroy(&s->lookup);
        kzero_memory(s, sizeof(shader_system_state));
    }

    state_ptr = 0;
}

b8 shader_system_create(const char* name, const shader_config* config) {
    u32 shader_id = 0;
    if (!renderer_shader_create(name, config->renderpass_id, config->stages, config->use_instances, config->use_local, &shader_id)) {
        KERROR("Error creating shader.");
        return false;
    }

    // Add attributes.
    for (u32 i = 0; i < config->attribute_count; ++i) {
        if (!renderer_shader_add_attribute(shader_id, name, config->attributes[i].type)) {
            KERROR("shader_system_create: Error adding attribute '%s'. Shader will be destroyed.", config->attributes[i].name);
            renderer_shader_destroy(shader_id);
            return false;
        }
    }

    // Add uniforms.
    for (u32 i = 0; i < config->uniform_count; ++i) {
        if (config->uniforms[i].type == SHADER_UNIFORM_TYPE_CUSTOM) {
            // Handle custom uniforms.
            if (!renderer_shader_add_uniform_custom(shader_id, config->uniforms[i].name, config->uniforms[i].size, config->uniforms[i].scope, &config->uniforms[i].location)) {
                KERROR("shader_system_create: Error adding custom uniform '%s'", config->uniforms[i].name);
                renderer_shader_destroy(shader_id);
                return false;
            }
        } else {
            // Handle all other types of uniforms.
            if (!renderer_shader_add_uniform(shader_id, config->uniforms[i].name, config->uniforms[i].type, config->uniforms[i].scope, &config->uniforms[i].location)) {
                KERROR("shader_system_create: Error adding uniform '%s'", config->uniforms[i].name);
                renderer_shader_destroy(shader_id);
                return false;
            }
        }
    }

    // Initialize the shader.
    if (!renderer_shader_initialize(shader_id)) {
        KERROR("shader_system_create: initialization failed for shader '%s'.", name);
        // NOTE: initialize automatically destroys the shader if it fails.
        return false;
    }

    // At this point, creation is successful, so store the shader id in the hashtable
    // so this can be looked up by name later.
    if (!hashtable_set(&state_ptr->lookup, name, &shader_id)) {
        // Dangit, we got so far... welp, nuke the shader and boot.
        renderer_shader_destroy(shader_id);
        return false;
    }

    return true;
}

b8 shader_system_load_config(const char* config_file_path, shader_config* out_config) {
    if (!config_file_path || !out_config || string_length(config_file_path) < 1) {
        return false;
    }

    // TODO: open file, etc.

    // Process configuration.
    if (!renderer_renderpass_id(out_config->renderpass_name, &out_config->renderpass_id)) {
        KERROR("shader_system_load_config: No renderpass found named '%s'. Cannot load config.", out_config->renderpass_name);
        return false;
    }

    // Extract stage names
    u32 stages = 0;
    for (u32 i = 0; i < out_config->stage_count; ++i) {
        if (strings_equali(out_config->stage_names[i], "frag") || strings_equali(out_config->stage_names[i], "fragment")) {
            stages |= SHADER_STAGE_FRAGMENT;
        }
    }

    return true;
}