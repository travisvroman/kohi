#include "material_system.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "containers/hashtable.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "systems/texture_system.h"

#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/light_system.h"

typedef struct material_shader_uniform_locations {
    u16 projection;
    u16 view;
    u16 ambient_colour;
    u16 view_position;
    u16 shininess;
    u16 diffuse_colour;
    u16 diffuse_texture;
    u16 specular_texture;
    u16 normal_texture;
    u16 model;
    u16 render_mode;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;
} material_shader_uniform_locations;

typedef struct ui_shader_uniform_locations {
    u16 projection;
    u16 view;
    u16 diffuse_colour;
    u16 diffuse_texture;
    u16 model;
} ui_shader_uniform_locations;

typedef struct material_system_state {
    material_system_config config;

    material default_material;

    // Array of registered materials.
    material* registered_materials;

    // Hashtable for material lookups.
    hashtable registered_material_table;

    // Known locations for the material shader.
    material_shader_uniform_locations material_locations;
    u32 material_shader_id;

    // Known locations for the UI shader.
    ui_shader_uniform_locations ui_locations;
    u32 ui_shader_id;
} material_system_state;

typedef struct material_reference {
    u64 reference_count;
    u32 handle;
    b8 auto_release;
} material_reference;

static material_system_state* state_ptr = 0;

b8 create_default_material(material_system_state* state);
b8 load_material(material_config config, material* m);
void destroy_material(material* m);

b8 material_system_initialize(u64* memory_requirement, void* state, void* config) {
    material_system_config* typed_config = (material_system_config*)config;
    if (typed_config->max_material_count == 0) {
        KFATAL("material_system_initialize - config.max_material_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    u64 struct_requirement = sizeof(material_system_state);
    u64 array_requirement = sizeof(material) * typed_config->max_material_count;
    u64 hashtable_requirement = sizeof(material_reference) * typed_config->max_material_count;
    *memory_requirement = struct_requirement + array_requirement + hashtable_requirement;

    if (!state) {
        return true;
    }

    state_ptr = state;
    state_ptr->config = *typed_config;

    state_ptr->material_shader_id = INVALID_ID;
    state_ptr->material_locations.view = INVALID_ID_U16;
    state_ptr->material_locations.projection = INVALID_ID_U16;
    state_ptr->material_locations.diffuse_colour = INVALID_ID_U16;
    state_ptr->material_locations.diffuse_texture = INVALID_ID_U16;
    state_ptr->material_locations.specular_texture = INVALID_ID_U16;
    state_ptr->material_locations.normal_texture = INVALID_ID_U16;
    state_ptr->material_locations.ambient_colour = INVALID_ID_U16;
    state_ptr->material_locations.shininess = INVALID_ID_U16;
    state_ptr->material_locations.model = INVALID_ID_U16;
    state_ptr->material_locations.render_mode = INVALID_ID_U16;

    state_ptr->ui_shader_id = INVALID_ID;
    state_ptr->ui_locations.diffuse_colour = INVALID_ID_U16;
    state_ptr->ui_locations.diffuse_texture = INVALID_ID_U16;
    state_ptr->ui_locations.view = INVALID_ID_U16;
    state_ptr->ui_locations.projection = INVALID_ID_U16;
    state_ptr->ui_locations.model = INVALID_ID_U16;

    // The array block is after the state. Already allocated, so just set the pointer.
    void* array_block = state + struct_requirement;
    state_ptr->registered_materials = array_block;

    // Hashtable block is after array.
    void* hashtable_block = array_block + array_requirement;

    // Create a hashtable for material lookups.
    hashtable_create(sizeof(material_reference), typed_config->max_material_count, hashtable_block, false, &state_ptr->registered_material_table);

    // Fill the hashtable with invalid references to use as a default.
    material_reference invalid_ref;
    invalid_ref.auto_release = false;
    invalid_ref.handle = INVALID_ID;  // Primary reason for needing default values.
    invalid_ref.reference_count = 0;
    hashtable_fill(&state_ptr->registered_material_table, &invalid_ref);

    // Invalidate all materials in the array.
    u32 count = state_ptr->config.max_material_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->registered_materials[i].id = INVALID_ID;
        state_ptr->registered_materials[i].generation = INVALID_ID;
        state_ptr->registered_materials[i].internal_id = INVALID_ID;
        state_ptr->registered_materials[i].render_frame_number = INVALID_ID;
    }

    if (!create_default_material(state_ptr)) {
        KFATAL("Failed to create default material. Application cannot continue.");
        return false;
    }

    // Get the uniform indices.
    // Save off the locations for known types for quick lookups.
    shader* s = shader_system_get("Shader.Builtin.Material");
    state_ptr->material_shader_id = s->id;
    state_ptr->material_locations.projection = shader_system_uniform_index(s, "projection");
    state_ptr->material_locations.view = shader_system_uniform_index(s, "view");
    state_ptr->material_locations.ambient_colour = shader_system_uniform_index(s, "ambient_colour");
    state_ptr->material_locations.view_position = shader_system_uniform_index(s, "view_position");
    state_ptr->material_locations.diffuse_colour = shader_system_uniform_index(s, "diffuse_colour");
    state_ptr->material_locations.diffuse_texture = shader_system_uniform_index(s, "diffuse_texture");
    state_ptr->material_locations.specular_texture = shader_system_uniform_index(s, "specular_texture");
    state_ptr->material_locations.normal_texture = shader_system_uniform_index(s, "normal_texture");
    state_ptr->material_locations.shininess = shader_system_uniform_index(s, "shininess");
    state_ptr->material_locations.model = shader_system_uniform_index(s, "model");
    state_ptr->material_locations.render_mode = shader_system_uniform_index(s, "mode");
    state_ptr->material_locations.dir_light = shader_system_uniform_index(s, "dir_light");
    state_ptr->material_locations.p_lights = shader_system_uniform_index(s, "p_lights");
    state_ptr->material_locations.num_p_lights = shader_system_uniform_index(s, "num_p_lights");

    s = shader_system_get("Shader.Builtin.UI");
    state_ptr->ui_shader_id = s->id;
    state_ptr->ui_locations.projection = shader_system_uniform_index(s, "projection");
    state_ptr->ui_locations.view = shader_system_uniform_index(s, "view");
    state_ptr->ui_locations.diffuse_colour = shader_system_uniform_index(s, "diffuse_colour");
    state_ptr->ui_locations.diffuse_texture = shader_system_uniform_index(s, "diffuse_texture");
    state_ptr->ui_locations.model = shader_system_uniform_index(s, "model");

    return true;
}

void material_system_shutdown(void* state) {
    material_system_state* s = (material_system_state*)state;
    if (s) {
        // Invalidate all materials in the array.
        u32 count = s->config.max_material_count;
        for (u32 i = 0; i < count; ++i) {
            if (s->registered_materials[i].id != INVALID_ID) {
                destroy_material(&s->registered_materials[i]);
            }
        }

        // Destroy the default material.
        destroy_material(&s->default_material);
    }

    state_ptr = 0;
}

material* material_system_acquire(const char* name) {
    // Load material configuration from resource;
    resource material_resource;
    if (!resource_system_load(name, RESOURCE_TYPE_MATERIAL, 0, &material_resource)) {
        KERROR("Failed to load material resource, returning nullptr.");
        return 0;
    }

    // Now acquire from loaded config.
    material* m = 0;
    if (material_resource.data) {
        m = material_system_acquire_from_config(*(material_config*)material_resource.data);
    }

    // Clean up
    resource_system_unload(&material_resource);

    if (!m) {
        KERROR("Failed to load material resource, returning nullptr.");
    }

    return m;
}

material* material_system_acquire_from_config(material_config config) {
    // Return default material.
    if (strings_equali(config.name, DEFAULT_MATERIAL_NAME)) {
        return &state_ptr->default_material;
    }

    material_reference ref;
    if (state_ptr && hashtable_get(&state_ptr->registered_material_table, config.name, &ref)) {
        // This can only be changed the first time a material is loaded.
        if (ref.reference_count == 0) {
            ref.auto_release = config.auto_release;
        }
        ref.reference_count++;
        if (ref.handle == INVALID_ID) {
            // This means no material exists here. Find a free index first.
            u32 count = state_ptr->config.max_material_count;
            material* m = 0;
            for (u32 i = 0; i < count; ++i) {
                if (state_ptr->registered_materials[i].id == INVALID_ID) {
                    // A free slot has been found. Use its index as the handle.
                    ref.handle = i;
                    m = &state_ptr->registered_materials[i];
                    break;
                }
            }

            // Make sure an empty slot was actually found.
            if (!m || ref.handle == INVALID_ID) {
                KFATAL("material_system_acquire - Material system cannot hold anymore materials. Adjust configuration to allow more.");
                return 0;
            }

            // Create new material.
            if (!load_material(config, m)) {
                KERROR("Failed to load material '%s'.", config.name);
                return 0;
            }

            if (m->generation == INVALID_ID) {
                m->generation = 0;
            } else {
                m->generation++;
            }

            // Also use the handle as the material id.
            m->id = ref.handle;
            // KTRACE("Material '%s' does not yet exist. Created, and ref_count is now %i.", config.name, ref.reference_count);
        } else {
            // KTRACE("Material '%s' already exists, ref_count increased to %i.", config.name, ref.reference_count);
        }

        // Update the entry.
        hashtable_set(&state_ptr->registered_material_table, config.name, &ref);
        return &state_ptr->registered_materials[ref.handle];
    }

    // NOTE: This would only happen in the event something went wrong with the state.
    KERROR("material_system_acquire_from_config failed to acquire material '%s'. Null pointer will be returned.", config.name);
    return 0;
}

void material_system_release(const char* name) {
    // Ignore release requests for the default material.
    if (strings_equali(name, DEFAULT_MATERIAL_NAME)) {
        return;
    }
    material_reference ref;
    if (state_ptr && hashtable_get(&state_ptr->registered_material_table, name, &ref)) {
        if (ref.reference_count == 0) {
            KWARN("Tried to release non-existent material: '%s'", name);
            return;
        }

        // Take a copy of the name since it would be wiped out if destroyed,
        // (as passed in name is generally a pointer to the actual material's name).
        char name_copy[MATERIAL_NAME_MAX_LENGTH];
        string_ncopy(name_copy, name, MATERIAL_NAME_MAX_LENGTH);

        ref.reference_count--;
        if (ref.reference_count == 0 && ref.auto_release) {
            material* m = &state_ptr->registered_materials[ref.handle];

            // Destroy/reset material.
            destroy_material(m);

            // Reset the reference.
            ref.handle = INVALID_ID;
            ref.auto_release = false;
            // KTRACE("Released material '%s'., Material unloaded because reference count=0 and auto_release=true.", name_copy);
        } else {
            // KTRACE("Released material '%s', now has a reference count of '%i' (auto_release=%s).", name_copy, ref.reference_count, ref.auto_release ? "true" : "false");
        }

        // Update the entry.
        hashtable_set(&state_ptr->registered_material_table, name_copy, &ref);
    } else {
        KERROR("material_system_release failed to release material '%s'.", name);
    }
}

material* material_system_get_default(void) {
    if (state_ptr) {
        return &state_ptr->default_material;
    }

    KFATAL("material_system_get_default called before system is initialized.");
    return 0;
}

#define MATERIAL_APPLY_OR_FAIL(expr)                  \
    if (!expr) {                                      \
        KERROR("Failed to apply material: %s", expr); \
        return false;                                 \
    }

b8 material_system_apply_global(u32 shader_id, u64 renderer_frame_number, const mat4* projection, const mat4* view, const vec4* ambient_colour, const vec3* view_position, u32 render_mode) {
    shader* s = shader_system_get_by_id(shader_id);
    if (!s) {
        return false;
    }
    if (s->render_frame_number == renderer_frame_number) {
        return true;
    }
    if (shader_id == state_ptr->material_shader_id) {
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.projection, projection));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.view, view));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.ambient_colour, ambient_colour));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.view_position, view_position));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.render_mode, &render_mode));
    } else if (shader_id == state_ptr->ui_shader_id) {
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.projection, projection));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.view, view));
    } else {
        KERROR("material_system_apply_global(): Unrecognized shader id '%d' ", shader_id);
        return false;
    }
    MATERIAL_APPLY_OR_FAIL(shader_system_apply_global());

    // Sync the frame number.
    s->render_frame_number = renderer_frame_number;
    return true;
}

b8 material_system_apply_instance(material* m, b8 needs_update) {
    // Apply instance-level uniforms.
    MATERIAL_APPLY_OR_FAIL(shader_system_bind_instance(m->internal_id));
    if (needs_update) {
        if (m->shader_id == state_ptr->material_shader_id) {
            // Material shader
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.diffuse_colour, &m->diffuse_colour));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.diffuse_texture, &m->diffuse_map));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.specular_texture, &m->specular_map));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.normal_texture, &m->normal_map));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.shininess, &m->shininess));

            // Directional light.
            directional_light* dir_light = light_system_directional_light_get();
            if (dir_light) {
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.dir_light, &dir_light->data));
            } else {
                directional_light_data data = {0};
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.dir_light, &data));
            }
            // Point lights.
            u32 p_light_count = light_system_point_light_count();
            if (p_light_count) {
                // TODO: frame allocator?
                point_light* p_lights = kallocate(sizeof(point_light) * p_light_count, MEMORY_TAG_ARRAY);
                light_system_point_lights_get(p_lights);

                point_light_data* p_light_datas = kallocate(sizeof(point_light_data) * p_light_count, MEMORY_TAG_ARRAY);
                for (u32 i = 0; i < p_light_count; ++i) {
                    p_light_datas[i] = p_lights[i].data;
                }

                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.p_lights, p_light_datas));
                kfree(p_light_datas, sizeof(point_light_data), MEMORY_TAG_ARRAY);
                kfree(p_lights, sizeof(point_light), MEMORY_TAG_ARRAY);
            }

            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.num_p_lights, &p_light_count));

        } else if (m->shader_id == state_ptr->ui_shader_id) {
            // UI shader
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.diffuse_colour, &m->diffuse_colour));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.diffuse_texture, &m->diffuse_map));
        } else {
            KERROR("material_system_apply_instance(): Unrecognized shader id '%d' on shader '%s'.", m->shader_id, m->name);
            return false;
        }
    }
    MATERIAL_APPLY_OR_FAIL(shader_system_apply_instance(needs_update));

    return true;
}

b8 material_system_apply_local(material* m, const mat4* model) {
    if (m->shader_id == state_ptr->material_shader_id) {
        return shader_system_uniform_set_by_index(state_ptr->material_locations.model, model);
    } else if (m->shader_id == state_ptr->ui_shader_id) {
        return shader_system_uniform_set_by_index(state_ptr->ui_locations.model, model);
    }

    KERROR("Unrecognized shader id '%d'", m->shader_id);
    return false;
}

void material_system_dump(void) {
    material_reference* refs = (material_reference*)state_ptr->registered_material_table.memory;
    for (u32 i = 0; i < state_ptr->registered_material_table.element_count; ++i) {
        material_reference* r = &refs[i];
        if (r->reference_count > 0 || r->handle != INVALID_ID) {
            KDEBUG("Found material ref (handle/refCount): (%u/%u)", r->handle, r->reference_count);
            if (r->handle != INVALID_ID) {
                KTRACE("Material name: %s", state_ptr->registered_materials[r->handle].name);
            }
        }
    }
}

b8 load_material(material_config config, material* m) {
    kzero_memory(m, sizeof(material));

    // name
    string_ncopy(m->name, config.name, MATERIAL_NAME_MAX_LENGTH);

    m->shader_id = shader_system_get_id(config.shader_name);

    // Diffuse colour
    m->diffuse_colour = config.diffuse_colour;
    m->shininess = config.shininess;

    // Diffuse map
    // TODO: Make this configurable.
    // TODO: DRY
    m->diffuse_map.filter_minify = m->diffuse_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
    m->diffuse_map.repeat_u = m->diffuse_map.repeat_v = m->diffuse_map.repeat_w = TEXTURE_REPEAT_REPEAT;
    
    if (string_length(config.diffuse_map_name) > 0) {
        m->diffuse_map.use = TEXTURE_USE_MAP_DIFFUSE;
        m->diffuse_map.texture = texture_system_acquire(config.diffuse_map_name, true);
        if (!m->diffuse_map.texture) {
            // Configured, but not found.
            KWARN("Unable to load texture '%s' for material '%s', using default.", config.diffuse_map_name, m->name);
            m->diffuse_map.texture = texture_system_get_default_texture();
        }
    } else {
        // This is done when a texture is not configured, as opposed to when it is configured and not found (above).
        m->diffuse_map.use = TEXTURE_USE_MAP_DIFFUSE;
        m->diffuse_map.texture = texture_system_get_default_diffuse_texture();
    }
    if (!renderer_texture_map_resources_acquire(&m->diffuse_map)) {
        KERROR("Unable to acquire resources for diffuse texture map.");
        return false;
    }

    // Specular map
    // TODO: Make this configurable.
    m->specular_map.filter_minify = m->specular_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
    m->specular_map.repeat_u = m->specular_map.repeat_v = m->specular_map.repeat_w = TEXTURE_REPEAT_REPEAT;
    
    if (string_length(config.specular_map_name) > 0) {
        m->specular_map.use = TEXTURE_USE_MAP_SPECULAR;
        m->specular_map.texture = texture_system_acquire(config.specular_map_name, true);
        if (!m->specular_map.texture) {
            KWARN("Unable to load specular texture '%s' for material '%s', using default.", config.specular_map_name, m->name);
            m->specular_map.texture = texture_system_get_default_specular_texture();
        }
    } else {
        // NOTE: Only set for clarity, as call to kzero_memory above does this already.
        m->specular_map.use = TEXTURE_USE_MAP_SPECULAR;
        m->specular_map.texture = texture_system_get_default_specular_texture();
    }
    if (!renderer_texture_map_resources_acquire(&m->specular_map)) {
        KERROR("Unable to acquire resources for specular texture map.");
        return false;
    }

    // Normal map
    // TODO: Make this configurable.
    m->normal_map.filter_minify = m->normal_map.filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
    m->normal_map.repeat_u = m->normal_map.repeat_v = m->normal_map.repeat_w = TEXTURE_REPEAT_REPEAT;
    
    if (string_length(config.normal_map_name) > 0) {
        m->normal_map.use = TEXTURE_USE_MAP_NORMAL;
        m->normal_map.texture = texture_system_acquire(config.normal_map_name, true);
        if (!m->normal_map.texture) {
            KWARN("Unable to load normal texture '%s' for material '%s', using default.", config.normal_map_name, m->name);
            m->normal_map.texture = texture_system_get_default_normal_texture();
        }
    } else {
        // Use default
        m->normal_map.use = TEXTURE_USE_MAP_NORMAL;
        m->normal_map.texture = texture_system_get_default_normal_texture();
    }
    if (!renderer_texture_map_resources_acquire(&m->normal_map)) {
        KERROR("Unable to acquire resources for normal texture map.");
        return false;
    }

    // TODO: other maps

    // Send it off to the renderer to acquire resources.
    shader* s = shader_system_get(config.shader_name);
    if (!s) {
        KERROR("Unable to load material because its shader was not found: '%s'. This is likely a problem with the material asset.", config.shader_name);
        return false;
    }

    // Gather a list of pointers to texture maps;
    texture_map* maps[3] = {&m->diffuse_map, &m->specular_map, &m->normal_map};
    if (!renderer_shader_instance_resources_acquire(s, maps, &m->internal_id)) {
        KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
        return false;
    }

    return true;
}

void destroy_material(material* m) {
    // KTRACE("Destroying material '%s'...", m->name);

    // Release texture references.
    if (m->diffuse_map.texture) {
        texture_system_release(m->diffuse_map.texture->name);
    }
    if (m->specular_map.texture) {
        texture_system_release(m->specular_map.texture->name);
    }
    if (m->normal_map.texture) {
        texture_system_release(m->normal_map.texture->name);
    }

    // Release texture map resources.
    renderer_texture_map_resources_release(&m->diffuse_map);
    renderer_texture_map_resources_release(&m->specular_map);
    renderer_texture_map_resources_release(&m->normal_map);

    // Release renderer resources.
    if (m->shader_id != INVALID_ID && m->internal_id != INVALID_ID) {
        renderer_shader_instance_resources_release(shader_system_get_by_id(m->shader_id), m->internal_id);
        m->shader_id = INVALID_ID;
    }

    // Zero it out, invalidate IDs.
    kzero_memory(m, sizeof(material));
    m->id = INVALID_ID;
    m->generation = INVALID_ID;
    m->internal_id = INVALID_ID;
    m->render_frame_number = INVALID_ID;
}

b8 create_default_material(material_system_state* state) {
    kzero_memory(&state->default_material, sizeof(material));
    state->default_material.id = INVALID_ID;
    state->default_material.generation = INVALID_ID;
    string_ncopy(state->default_material.name, DEFAULT_MATERIAL_NAME, MATERIAL_NAME_MAX_LENGTH);
    state->default_material.diffuse_colour = vec4_one();  // white
    state->default_material.diffuse_map.use = TEXTURE_USE_MAP_DIFFUSE;
    state->default_material.diffuse_map.texture = texture_system_get_default_texture();

    state->default_material.specular_map.use = TEXTURE_USE_MAP_SPECULAR;
    state->default_material.specular_map.texture = texture_system_get_default_specular_texture();

    state->default_material.normal_map.use = TEXTURE_USE_MAP_SPECULAR;
    state->default_material.normal_map.texture = texture_system_get_default_normal_texture();

    texture_map* maps[3] = {&state->default_material.diffuse_map, &state->default_material.specular_map, &state->default_material.normal_map};

    shader* s = shader_system_get("Shader.Builtin.Material");
    if (!renderer_shader_instance_resources_acquire(s, maps, &state->default_material.internal_id)) {
        KFATAL("Failed to acquire renderer resources for default material. Application cannot continue.");
        return false;
    }

    // Make sure to assign the shader id.
    state->default_material.shader_id = s->id;

    return true;
}
