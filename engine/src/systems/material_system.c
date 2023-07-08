#include "material_system.h"

#include "containers/darray.h"
#include "containers/hashtable.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "systems/light_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

typedef struct material_shader_uniform_locations {
    u16 projection;
    u16 view;
    u16 ambient_colour;
    u16 view_position;
    u16 properties;
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
    u16 properties;
    u16 diffuse_texture;
    u16 model;
} ui_shader_uniform_locations;

typedef struct terrain_shader_locations {
    b8 loaded;
    u16 projection;
    u16 view;
    u16 ambient_colour;
    u16 view_position;
    u16 model;
    u16 render_mode;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;

    u16 properties;
    u16 samplers[TERRAIN_MAX_MATERIAL_COUNT * 3];  // diffuse, spec, normal.
} terrain_shader_locations;

typedef struct material_system_state {
    material_system_config config;

    material default_material;
    material default_ui_material;
    material default_terrain_material;

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

    // Known locations for terrain shader.
    terrain_shader_locations terrain_locations;
    u32 terrain_shader_id;
} material_system_state;

typedef struct material_reference {
    u64 reference_count;
    u32 handle;
    b8 auto_release;
} material_reference;

static material_system_state* state_ptr = 0;

static b8 create_default_material(material_system_state* state);
static b8 create_default_ui_material(material_system_state* state);
static b8 create_default_terrain_material(material_system_state* state);
static b8 load_material(material_config* config, material* m);
static void destroy_material(material* m);

static b8 assign_map(texture_map* map, const material_map* config, const char* material_name, texture* default_tex);

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
    state_ptr->material_locations.properties = INVALID_ID_U16;
    state_ptr->material_locations.diffuse_texture = INVALID_ID_U16;
    state_ptr->material_locations.specular_texture = INVALID_ID_U16;
    state_ptr->material_locations.normal_texture = INVALID_ID_U16;
    state_ptr->material_locations.ambient_colour = INVALID_ID_U16;
    state_ptr->material_locations.model = INVALID_ID_U16;
    state_ptr->material_locations.render_mode = INVALID_ID_U16;

    state_ptr->ui_shader_id = INVALID_ID;
    state_ptr->ui_locations.properties = INVALID_ID_U16;
    state_ptr->ui_locations.diffuse_texture = INVALID_ID_U16;
    state_ptr->ui_locations.view = INVALID_ID_U16;
    state_ptr->ui_locations.projection = INVALID_ID_U16;
    state_ptr->ui_locations.model = INVALID_ID_U16;

    state_ptr->terrain_locations.projection = INVALID_ID_U16;
    state_ptr->terrain_locations.view = INVALID_ID_U16;
    state_ptr->terrain_locations.ambient_colour = INVALID_ID_U16;
    state_ptr->terrain_locations.view_position = INVALID_ID_U16;
    state_ptr->terrain_locations.model = INVALID_ID_U16;
    state_ptr->terrain_locations.render_mode = INVALID_ID_U16;
    state_ptr->terrain_locations.dir_light = INVALID_ID_U16;
    state_ptr->terrain_locations.p_lights = INVALID_ID_U16;
    state_ptr->terrain_locations.num_p_lights = INVALID_ID_U16;
    state_ptr->terrain_locations.properties = INVALID_ID_U16;
    for (u32 i = 0; i < 12; ++i) {
        state_ptr->terrain_locations.samplers[i] = INVALID_ID_U16;
    }

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

    if (!create_default_ui_material(state_ptr)) {
        KFATAL("Failed to create default UI material. Application cannot continue.");
        return false;
    }

    if (!create_default_terrain_material(state_ptr)) {
        KFATAL("Failed to create default terrain material. Application cannot continue.");
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
    state_ptr->material_locations.properties = shader_system_uniform_index(s, "properties");
    state_ptr->material_locations.diffuse_texture = shader_system_uniform_index(s, "diffuse_texture");
    state_ptr->material_locations.specular_texture = shader_system_uniform_index(s, "specular_texture");
    state_ptr->material_locations.normal_texture = shader_system_uniform_index(s, "normal_texture");
    state_ptr->material_locations.model = shader_system_uniform_index(s, "model");
    state_ptr->material_locations.render_mode = shader_system_uniform_index(s, "mode");
    state_ptr->material_locations.dir_light = shader_system_uniform_index(s, "dir_light");
    state_ptr->material_locations.p_lights = shader_system_uniform_index(s, "p_lights");
    state_ptr->material_locations.num_p_lights = shader_system_uniform_index(s, "num_p_lights");

    s = shader_system_get("Shader.Builtin.UI");
    state_ptr->ui_shader_id = s->id;
    state_ptr->ui_locations.projection = shader_system_uniform_index(s, "projection");
    state_ptr->ui_locations.view = shader_system_uniform_index(s, "view");
    state_ptr->ui_locations.properties = shader_system_uniform_index(s, "properties");
    state_ptr->ui_locations.diffuse_texture = shader_system_uniform_index(s, "diffuse_texture");
    state_ptr->ui_locations.model = shader_system_uniform_index(s, "model");

    s = shader_system_get("Shader.Builtin.Terrain");
    state_ptr->terrain_shader_id = s->id;
    state_ptr->terrain_locations.projection = shader_system_uniform_index(s, "projection");
    state_ptr->terrain_locations.view = shader_system_uniform_index(s, "view");
    state_ptr->terrain_locations.ambient_colour = shader_system_uniform_index(s, "ambient_colour");
    state_ptr->terrain_locations.view_position = shader_system_uniform_index(s, "view_position");
    state_ptr->terrain_locations.model = shader_system_uniform_index(s, "model");
    state_ptr->terrain_locations.render_mode = shader_system_uniform_index(s, "mode");
    state_ptr->terrain_locations.dir_light = shader_system_uniform_index(s, "dir_light");
    state_ptr->terrain_locations.p_lights = shader_system_uniform_index(s, "p_lights");
    state_ptr->terrain_locations.num_p_lights = shader_system_uniform_index(s, "num_p_lights");

    state_ptr->terrain_locations.properties = shader_system_uniform_index(s, "properties");

    state_ptr->terrain_locations.samplers[0] = shader_system_uniform_index(s, "diffuse_texture_0");
    state_ptr->terrain_locations.samplers[1] = shader_system_uniform_index(s, "specular_texture_0");
    state_ptr->terrain_locations.samplers[2] = shader_system_uniform_index(s, "normal_texture_0");

    state_ptr->terrain_locations.samplers[3] = shader_system_uniform_index(s, "diffuse_texture_1");
    state_ptr->terrain_locations.samplers[4] = shader_system_uniform_index(s, "specular_texture_1");
    state_ptr->terrain_locations.samplers[5] = shader_system_uniform_index(s, "normal_texture_1");

    state_ptr->terrain_locations.samplers[6] = shader_system_uniform_index(s, "diffuse_texture_2");
    state_ptr->terrain_locations.samplers[7] = shader_system_uniform_index(s, "specular_texture_2");
    state_ptr->terrain_locations.samplers[8] = shader_system_uniform_index(s, "normal_texture_2");

    state_ptr->terrain_locations.samplers[9] = shader_system_uniform_index(s, "diffuse_texture_3");
    state_ptr->terrain_locations.samplers[10] = shader_system_uniform_index(s, "specular_texture_3");
    state_ptr->terrain_locations.samplers[11] = shader_system_uniform_index(s, "normal_texture_3");

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
        m = material_system_acquire_from_config((material_config*)material_resource.data);
    }

    // Clean up
    resource_system_unload(&material_resource);

    if (!m) {
        KERROR("Failed to load material resource, returning nullptr.");
    }

    return m;
}

static material* material_system_acquire_reference(const char* name, b8 auto_release, b8* needs_creation) {
    material_reference ref;
    if (state_ptr && hashtable_get(&state_ptr->registered_material_table, name, &ref)) {
        // This can only be changed the first time a material is loaded.
        if (ref.reference_count == 0) {
            ref.auto_release = auto_release;
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

            *needs_creation = true;

            // Also use the handle as the material id.
            m->id = ref.handle;
            // KTRACE("Material '%s' does not yet exist. Created, and ref_count is now %i.", config.name, ref.reference_count);
        } else {
            // KTRACE("Material '%s' already exists, ref_count increased to %i.", config.name, ref.reference_count);
            *needs_creation = false;
        }

        // Update the entry.
        hashtable_set(&state_ptr->registered_material_table, name, &ref);
        return &state_ptr->registered_materials[ref.handle];
    }

    // NOTE: This would only happen in the event something went wrong with the state.
    KERROR("material_system_acquire_from_config failed to acquire material '%s'. Null pointer will be returned.", name);
    return 0;
}

material* material_system_acquire_terrain_material(const char* material_name, u32 material_count, const char** material_names, b8 auto_release) {
    // Return default terrain material.
    if (strings_equali(material_name, DEFAULT_TERRAIN_MATERIAL_NAME)) {
        return &state_ptr->default_terrain_material;
    }

    b8 needs_creation = false;
    material* m = material_system_acquire_reference(material_name, auto_release, &needs_creation);
    if (!m) {
        KERROR("Failed to acquire terrain material '%s'", material_name);
        return 0;
    }

    if (needs_creation) {
        // Get all materials by name;
        material** materials = kallocate(sizeof(material*) * material_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < material_count; ++i) {
            materials[i] = material_system_acquire(material_names[i]);
        }

        // Create new material.
        // NOTE: terrain-specific load_material
        kzero_memory(m, sizeof(material));
        string_ncopy(m->name, material_name, MATERIAL_NAME_MAX_LENGTH);

        shader* s = shader_system_get("Shader.Builtin.Terrain");
        m->shader_id = s->id;
        m->type = MATERIAL_TYPE_TERRAIN;

        // Allocate maps and properties memory.
        m->property_struct_size = sizeof(material_terrain_properties);
        m->properties = kallocate(m->property_struct_size, MEMORY_TAG_MATERIAL_INSTANCE);
        material_terrain_properties* properties = m->properties;
        properties->num_materials = material_count;
        properties->padding = vec3_zero();
        properties->padding2 = vec4_zero();

        // 3 maps per material. Allocate enough slots for all materials.
        // u32 map_count = material_count * 3;
        u32 max_map_count = TERRAIN_MAX_MATERIAL_COUNT * 3;
        m->maps = darray_reserve(texture_map, max_map_count);
        darray_length_set(m->maps, max_map_count);

        // Map names and default fallback textures.
        const char* map_names[3] = {"diffuse", "specular", "normal"};
        texture* default_textures[3] = {
            texture_system_get_default_diffuse_texture(),
            texture_system_get_default_specular_texture(),
            texture_system_get_default_normal_texture()};
        // Use the default material for unassigned slots.
        material* default_material = material_system_get_default();

        // Phong properties and maps for each material.
        for (u32 material_idx = 0; material_idx < TERRAIN_MAX_MATERIAL_COUNT; ++material_idx) {
            // Properties.
            material_phong_properties* mat_props = &properties->materials[material_idx];
            // Use default material unless within the material count.
            material* ref_mat = default_material;
            if (material_idx < material_count) {
                ref_mat = materials[material_idx];
            }

            material_phong_properties* props = ref_mat->properties;
            mat_props->diffuse_colour = props->diffuse_colour;
            mat_props->shininess = props->shininess;
            mat_props->padding = vec3_zero();

            // Maps, 3 for phong. Diffuse, spec, normal.
            for (u32 map_idx = 0; map_idx < 3; ++map_idx) {
                material_map map_config = {0};
                char buf[MATERIAL_NAME_MAX_LENGTH] = {0};
                map_config.name = buf;
                string_copy(map_config.name, map_names[map_idx]);
                map_config.repeat_u = ref_mat->maps[map_idx].repeat_u;
                map_config.repeat_v = ref_mat->maps[map_idx].repeat_v;
                map_config.repeat_w = ref_mat->maps[map_idx].repeat_w;
                map_config.filter_min = ref_mat->maps[map_idx].filter_minify;
                map_config.filter_mag = ref_mat->maps[map_idx].filter_magnify;
                map_config.texture_name = ref_mat->maps[map_idx].texture->name;
                if (!assign_map(&m->maps[(material_idx * 3) + map_idx], &map_config, m->name, default_textures[map_idx])) {
                    KERROR("Failed to assign '%s' texture map for terrain material index %u", map_names[map_idx], material_idx);
                    return false;
                }
            }
        }

        kfree(materials, sizeof(material*) * material_count, MEMORY_TAG_ARRAY);

        // Acquire instance resources for all maps.

        texture_map** maps = kallocate(sizeof(texture_map*) * max_map_count, MEMORY_TAG_ARRAY);
        // Assign material maps.
        for (u32 i = 0; i < max_map_count; ++i) {
            maps[i] = &m->maps[i];
        }

        b8 result = renderer_shader_instance_resources_acquire(s, max_map_count, maps, &m->internal_id);
        if (!result) {
            KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
        }

        if (maps) {
            kfree(maps, sizeof(texture_map*) * max_map_count, MEMORY_TAG_ARRAY);
        }
        // NOTE: end terrain-specific load_material

        if (m->generation == INVALID_ID) {
            m->generation = 0;
        } else {
            m->generation++;
        }
    }

    return m;
}

material* material_system_acquire_from_config(material_config* config) {
    // Return default material.
    if (strings_equali(config->name, DEFAULT_MATERIAL_NAME)) {
        return &state_ptr->default_material;
    }

    // Return default UI material.
    if (strings_equali(config->name, DEFAULT_UI_MATERIAL_NAME)) {
        return &state_ptr->default_ui_material;
    }

    // Return default terrain material.
    if (strings_equali(config->name, DEFAULT_TERRAIN_MATERIAL_NAME)) {
        return &state_ptr->default_terrain_material;
    }

    b8 needs_creation = false;
    material* m = material_system_acquire_reference(config->name, config->auto_release, &needs_creation);

    if (needs_creation) {
        // Create new material.
        if (!load_material(config, m)) {
            KERROR("Failed to load material '%s'.", config->name);
            return 0;
        }

        if (m->generation == INVALID_ID) {
            m->generation = 0;
        } else {
            m->generation++;
        }
    }

    return m;
}

void material_system_release(const char* name) {
    // Ignore release requests for the default material.
    if (strings_equali(name, DEFAULT_MATERIAL_NAME) || strings_equali(name, DEFAULT_UI_MATERIAL_NAME) || strings_equali(name, DEFAULT_TERRAIN_MATERIAL_NAME)) {
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

material* material_system_get_default_ui(void) {
    if (state_ptr) {
        return &state_ptr->default_ui_material;
    }

    KFATAL("material_system_get_default_ui called before system is initialized.");
    return 0;
}

material* material_system_get_default_terrain(void) {
    if (state_ptr) {
        return &state_ptr->default_terrain_material;
    }

    KFATAL("material_system_get_default_terrain called before system is initialized.");
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
    if (shader_id == state_ptr->material_shader_id || shader_id == state_ptr->terrain_shader_id) {
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
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.properties, m->properties));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.diffuse_texture, &m->maps[0]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.specular_texture, &m->maps[1]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->material_locations.normal_texture, &m->maps[2]));

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
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.properties, m->properties));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->ui_locations.diffuse_texture, &m->maps[0]));
        } else if (m->shader_id == state_ptr->terrain_shader_id) {
            // Apply maps
            u32 map_count = darray_length(m->maps);
            for (u32 i = 0; i < map_count; ++i) {
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->terrain_locations.samplers[i], &m->maps[i]));
            }

            // Apply properties.
            shader_system_uniform_set_by_index(state_ptr->terrain_locations.properties, m->properties);

            // TODO: Duplicating above... move this to its own function, perhaps.
            // Directional light.
            directional_light* dir_light = light_system_directional_light_get();
            if (dir_light) {
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->terrain_locations.dir_light, &dir_light->data));
            } else {
                directional_light_data data = {0};
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->terrain_locations.dir_light, &data));
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

                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->terrain_locations.p_lights, p_light_datas));
                kfree(p_light_datas, sizeof(point_light_data), MEMORY_TAG_ARRAY);
                kfree(p_lights, sizeof(point_light), MEMORY_TAG_ARRAY);
            }

            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_index(state_ptr->terrain_locations.num_p_lights, &p_light_count));
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
    } else if (m->shader_id == state_ptr->terrain_shader_id) {
        return shader_system_uniform_set_by_index(state_ptr->terrain_locations.model, model);
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

static b8 assign_map(texture_map* map, const material_map* config, const char* material_name, texture* default_tex) {
    map->filter_minify = config->filter_min;
    map->filter_magnify = config->filter_mag;
    map->repeat_u = config->repeat_u;
    map->repeat_v = config->repeat_v;
    map->repeat_w = config->repeat_w;

    if (string_length(config->texture_name) > 0) {
        map->texture = texture_system_acquire(config->texture_name, true);
        if (!map->texture) {
            // Configured, but not found.
            KWARN("Unable to load texture '%s' for material '%s', using default.", config->texture_name, material_name);
            map->texture = default_tex;
        }
    } else {
        // This is done when a texture is not configured, as opposed to when it is configured and not found (above).
        map->texture = default_tex;
    }
    if (!renderer_texture_map_resources_acquire(map)) {
        KERROR("Unable to acquire resources for texture map.");
        return false;
    }
    return true;
}

static b8 load_material(material_config* config, material* m) {
    kzero_memory(m, sizeof(material));

    // name
    string_ncopy(m->name, config->name, MATERIAL_NAME_MAX_LENGTH);

    m->shader_id = shader_system_get_id(config->shader_name);
    m->type = config->type;

    // Phong properties and maps.
    if (config->type == MATERIAL_TYPE_PHONG) {
        // Phong-specific properties.
        u32 prop_count = darray_length(config->properties);

        // Defaults
        m->property_struct_size = sizeof(material_phong_properties);
        m->properties = kallocate(sizeof(material_phong_properties), MEMORY_TAG_MATERIAL_INSTANCE);
        material_phong_properties* properties = (material_phong_properties*)m->properties;
        properties->diffuse_colour = vec4_one();
        properties->shininess = 32.0f;
        properties->padding = vec3_zero();
        for (u32 i = 0; i < prop_count; ++i) {
            if (strings_equali(config->properties[i].name, "diffuse_colour")) {
                // Diffuse colour
                properties->diffuse_colour = config->properties[i].value_v4;
            } else if (strings_equali(config->properties[i].name, "shininess")) {
                // Shininess
                properties->shininess = config->properties[i].value_f32;
            }
        }

        // Maps. Phong expects a diffuse, specular and normal.
        m->maps = darray_reserve(texture_map, 3);
        darray_length_set(m->maps, 3);
        u32 map_count = darray_length(config->maps);
        for (u32 i = 0; i < map_count; ++i) {
            if (strings_equali(config->maps[i].name, "diffuse")) {
                if (!assign_map(&m->maps[0], &config->maps[i], m->name, texture_system_get_default_diffuse_texture())) {
                    return false;
                }
            } else if (strings_equali(config->maps[i].name, "specular")) {
                if (!assign_map(&m->maps[1], &config->maps[i], m->name, texture_system_get_default_specular_texture())) {
                    return false;
                }
            } else if (strings_equali(config->maps[i].name, "normal")) {
                if (!assign_map(&m->maps[2], &config->maps[i], m->name, texture_system_get_default_normal_texture())) {
                    return false;
                }
            }
            // TODO: other maps
            // NOTE: Ignore unexpected maps.
        }
    } else if (config->type == MATERIAL_TYPE_UI) {
        // NOTE: only one map and property, so just use the first.
        // TODO: If this changes, update this to work as it does above.
        m->maps = darray_reserve(texture_map, 1);
        darray_length_set(m->maps, 1);
        m->property_struct_size = sizeof(material_ui_properties);
        m->properties = kallocate(sizeof(material_ui_properties), MEMORY_TAG_MATERIAL_INSTANCE);
        material_ui_properties* properties = m->properties;
        properties->diffuse_colour = config->properties[0].value_v4;
        if (!assign_map(&m->maps[0], &config->maps[0], m->name, texture_system_get_default_diffuse_texture())) {
            return false;
        }
    } else if (config->type == MATERIAL_TYPE_CUSTOM) {
        // Properties.
        u32 prop_count = darray_length(config->properties);
        // Start by getting a total size of all properties.
        m->property_struct_size = 0;
        for (u32 i = 0; i < prop_count; ++i) {
            if (config->properties[i].size > 0) {
                m->property_struct_size += config->properties[i].size;
            }
        }
        // Allocate enough space for the struct.
        m->properties = kallocate(m->property_struct_size, MEMORY_TAG_MATERIAL_INSTANCE);

        // Loop again and copy values to the struct. NOTE: There are no defaults for custom material uniforms.
        u32 offset = 0;
        for (u32 i = 0; i < prop_count; ++i) {
            if (config->properties[i].size > 0) {
                void* data = 0;
                switch (config->properties[i].type) {
                    case SHADER_UNIFORM_TYPE_INT8:
                        data = &config->properties[i].value_i8;
                        break;
                    case SHADER_UNIFORM_TYPE_UINT8:
                        data = &config->properties[i].value_u8;
                        break;
                    case SHADER_UNIFORM_TYPE_INT16:
                        data = &config->properties[i].value_i16;
                        break;
                    case SHADER_UNIFORM_TYPE_UINT16:
                        data = &config->properties[i].value_u16;
                        break;
                    case SHADER_UNIFORM_TYPE_INT32:
                        data = &config->properties[i].value_i32;
                        break;
                    case SHADER_UNIFORM_TYPE_UINT32:
                        data = &config->properties[i].value_u32;
                        break;
                    case SHADER_UNIFORM_TYPE_FLOAT32:
                        data = &config->properties[i].value_f32;
                        break;
                    case SHADER_UNIFORM_TYPE_FLOAT32_2:
                        data = &config->properties[i].value_v2;
                        break;
                    case SHADER_UNIFORM_TYPE_FLOAT32_3:
                        data = &config->properties[i].value_v3;
                        break;
                    case SHADER_UNIFORM_TYPE_FLOAT32_4:
                        data = &config->properties[i].value_v4;
                        break;
                    case SHADER_UNIFORM_TYPE_MATRIX_4:
                        data = &config->properties[i].value_mat4;
                        break;
                    default:
                        // TODO: custom size?
                        KWARN("Unable to process shader uniform type %d (index %u) for material '%s'. Skipping.", config->properties[i].type, i, m->name);
                        continue;
                }

                // Copy the block and move up.
                kcopy_memory(m->properties + offset, data, config->properties[i].size);
                offset += config->properties[i].size;
            }
        }

        // Maps. Custom materials can have any number of maps.
        u32 map_count = darray_length(config->maps);
        m->maps = darray_reserve(texture_map, map_count);
        darray_length_set(m->maps, map_count);
        for (u32 i = 0; i < map_count; ++i) {
            // No known mapping, so just map them in order.
            // Invalid textures will use the default texture because map type isn't known.
            if (!assign_map(&m->maps[i], &config->maps[i], m->name, texture_system_get_default_texture())) {
                return false;
            }
        }
    }

    // Send it off to the renderer to acquire resources.
    shader* s = 0;
    if (config->type == MATERIAL_TYPE_PHONG) {
        s = shader_system_get(config->shader_name ? config->shader_name : "Shader.Builtin.Material");
    } else if (config->type == MATERIAL_TYPE_UI) {
        s = shader_system_get(config->shader_name ? config->shader_name : "Shader.Builtin.UI");
    } else if (config->type == MATERIAL_TYPE_TERRAIN) {
        s = shader_system_get(config->shader_name ? config->shader_name : "Shader.Builtin.Terrain");
    } else if (config->type == MATERIAL_TYPE_PBR) {
        KFATAL("PBR not yet supported.");
        return false;
    } else if (config->type == MATERIAL_TYPE_CUSTOM) {
        if (!config->shader_name) {
            KERROR("Shader name is required for custom material types. Material '%s' failed to load", m->name);
            return false;
        }
        s = shader_system_get(config->shader_name);
    } else {
        KERROR("Unknown material type: %d. Material '%s' cannot be loaded.", config->type, m->name);
        return false;
    }
    if (!s) {
        KERROR("Unable to load material because its shader was not found: '%s'. This is likely a problem with the material asset.", config->shader_name);
        return false;
    }

    // Gather a list of pointers to texture maps;
    u32 map_count = 0;
    if (config->type == MATERIAL_TYPE_PHONG) {
        // Map count for this type is known.
        map_count = 3;
    } else if (config->type == MATERIAL_TYPE_UI) {
        // Map count for this type is known.
        map_count = 1;
    } else if (config->type == MATERIAL_TYPE_PBR) {
        KFATAL("PBR not yet supported.");
        return false;
    } else if (config->type == MATERIAL_TYPE_CUSTOM) {
        // Map count provided by config.
        map_count = darray_length(config->maps);
    }

    texture_map** maps = kallocate(sizeof(texture_map*) * map_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < map_count; ++i) {
        maps[i] = &m->maps[i];
    }

    b8 result = renderer_shader_instance_resources_acquire(s, map_count, maps, &m->internal_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
    }

    if (maps) {
        kfree(maps, sizeof(texture_map*) * map_count, MEMORY_TAG_ARRAY);
    }

    return result;
}

static void destroy_material(material* m) {
    // KTRACE("Destroying material '%s'...", m->name);

    u32 length = darray_length(m->maps);
    for (u32 i = 0; i < length; ++i) {
        // Release texture references.
        texture_system_release(m->maps[i].texture->name);
        // Release texture map resources.
        renderer_texture_map_resources_release(&m->maps[i]);
    }

    // Release renderer resources.
    if (m->shader_id != INVALID_ID && m->internal_id != INVALID_ID) {
        renderer_shader_instance_resources_release(shader_system_get_by_id(m->shader_id), m->internal_id);
        m->shader_id = INVALID_ID;
    }

    // Release properties
    if (m->properties && m->property_struct_size) {
        kfree(m->properties, m->property_struct_size, MEMORY_TAG_MATERIAL_INSTANCE);
    }

    // Zero it out, invalidate IDs.
    kzero_memory(m, sizeof(material));
    m->id = INVALID_ID;
    m->generation = INVALID_ID;
    m->internal_id = INVALID_ID;
    m->render_frame_number = INVALID_ID;
}

static b8 create_default_material(material_system_state* state) {
    kzero_memory(&state->default_material, sizeof(material));
    state->default_material.id = INVALID_ID;
    state->default_material.type = MATERIAL_TYPE_PHONG;
    state->default_material.generation = INVALID_ID;
    string_ncopy(state->default_material.name, DEFAULT_MATERIAL_NAME, MATERIAL_NAME_MAX_LENGTH);
    state->default_material.property_struct_size = sizeof(material_phong_properties);
    state->default_material.properties = kallocate(sizeof(material_phong_properties), MEMORY_TAG_MATERIAL_INSTANCE);
    material_phong_properties* properties = (material_phong_properties*)state->default_material.properties;
    properties->diffuse_colour = vec4_one();  // white
    properties->shininess = 8.0f;
    state->default_material.maps = darray_reserve(texture_map, 3);
    darray_length_set(state->default_material.maps, 3);
    for (u32 i = 0; i < 3; ++i) {
        texture_map* map = &state->default_material.maps[i];
        map->filter_magnify = map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
        map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_REPEAT;
    }
    state->default_material.maps[0].texture = texture_system_get_default_texture();

    state->default_material.maps[1].texture = texture_system_get_default_specular_texture();

    state->default_material.maps[2].texture = texture_system_get_default_normal_texture();

    // NOTE: Phong material is default.
    texture_map* maps[3] = {&state->default_material.maps[0], &state->default_material.maps[1], &state->default_material.maps[2]};

    shader* s = shader_system_get("Shader.Builtin.Material");
    if (!renderer_shader_instance_resources_acquire(s, 3, maps, &state->default_material.internal_id)) {
        KFATAL("Failed to acquire renderer resources for default material. Application cannot continue.");
        return false;
    }

    // Make sure to assign the shader id.
    state->default_material.shader_id = s->id;

    return true;
}

static b8 create_default_ui_material(material_system_state* state) {
    kzero_memory(&state->default_ui_material, sizeof(material));
    state->default_ui_material.id = INVALID_ID;
    state->default_ui_material.type = MATERIAL_TYPE_UI;
    state->default_ui_material.generation = INVALID_ID;
    string_ncopy(state->default_ui_material.name, DEFAULT_UI_MATERIAL_NAME, MATERIAL_NAME_MAX_LENGTH);
    state->default_ui_material.property_struct_size = sizeof(material_ui_properties);
    state->default_ui_material.properties = kallocate(sizeof(material_ui_properties), MEMORY_TAG_MATERIAL_INSTANCE);
    material_ui_properties* properties = (material_ui_properties*)state->default_ui_material.properties;
    properties->diffuse_colour = vec4_one();  // white
    state->default_ui_material.maps = darray_reserve(texture_map, 1);
    darray_length_set(state->default_ui_material.maps, 1);
    state->default_ui_material.maps[0].texture = texture_system_get_default_texture();

    texture_map* maps[1] = {&state->default_ui_material.maps[0]};

    shader* s = shader_system_get("Shader.Builtin.UI");
    if (!renderer_shader_instance_resources_acquire(s, 1, maps, &state->default_ui_material.internal_id)) {
        KFATAL("Failed to acquire renderer resources for default UI material. Application cannot continue.");
        return false;
    }

    // Make sure to assign the shader id.
    state->default_ui_material.shader_id = s->id;

    return true;
}

static b8 create_default_terrain_material(material_system_state* state) {
    kzero_memory(&state->default_terrain_material, sizeof(material));
    state->default_terrain_material.id = INVALID_ID;
    state->default_terrain_material.type = MATERIAL_TYPE_TERRAIN;
    state->default_terrain_material.generation = INVALID_ID;
    string_ncopy(state->default_terrain_material.name, DEFAULT_TERRAIN_MATERIAL_NAME, MATERIAL_NAME_MAX_LENGTH);

    // Should essentially be the same thing as the defualt material, just mapped to an "array" of one material.
    state->default_terrain_material.property_struct_size = sizeof(material_terrain_properties);
    state->default_terrain_material.properties = kallocate(sizeof(material_terrain_properties), MEMORY_TAG_MATERIAL_INSTANCE);
    material_terrain_properties* properties = (material_terrain_properties*)state->default_terrain_material.properties;
    properties->num_materials = 1;
    properties->materials[0].diffuse_colour = vec4_one();  // white
    properties->materials[0].shininess = 8.0f;
    state->default_terrain_material.maps = darray_reserve(texture_map, 12);
    darray_length_set(state->default_terrain_material.maps, 12);
    state->default_terrain_material.maps[0].texture = texture_system_get_default_texture();
    state->default_terrain_material.maps[1].texture = texture_system_get_default_specular_texture();
    state->default_terrain_material.maps[2].texture = texture_system_get_default_normal_texture();

    // NOTE: Phong material is default.
    texture_map* maps[3] = {
        &state->default_terrain_material.maps[0],
        &state->default_terrain_material.maps[1],
        &state->default_terrain_material.maps[2],
    };

    shader* s = shader_system_get("Shader.Builtin.Terrain");
    if (!renderer_shader_instance_resources_acquire(s, 3, maps, &state->default_terrain_material.internal_id)) {
        KFATAL("Failed to acquire renderer resources for default terrain material. Application cannot continue.");
        return false;
    }

    // Make sure to assign the shader id.
    state->default_terrain_material.shader_id = s->id;

    return true;
}
