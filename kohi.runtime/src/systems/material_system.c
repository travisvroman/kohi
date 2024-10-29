#include "material_system.h"

#include "core/console.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

#ifndef PBR_MAP_COUNT
#    define PBR_MAP_COUNT 5
#endif

#define MAX_SHADOW_CASCADE_COUNT 4

// Samplers
const u32 SAMP_ALBEDO = 0;
const u32 SAMP_NORMAL = 1;
const u32 SAMP_COMBINED = 2;
const u32 SAMP_SHADOW_MAP = 3;
const u32 SAMP_IRRADIANCE_MAP = 4;

// The number of texture maps for a PBR material
#define PBR_MATERIAL_MAP_COUNT 3

// Number of texture maps for a layered PBR material
#define LAYERED_PBR_MATERIAL_MAP_COUNT 1

// Terrain materials are now all loaded into a single array texture.
const u32 SAMP_TERRAIN_MATERIAL_ARRAY_MAP = 0;
const u32 SAMP_TERRAIN_SHADOW_MAP = 1 + SAMP_TERRAIN_MATERIAL_ARRAY_MAP;
const u32 SAMP_TERRAIN_IRRADIANCE_MAP = 1 + SAMP_TERRAIN_SHADOW_MAP;
// 1 array map for terrain materials, 1 for shadow map, 1 for irradiance map
const u32 TERRAIN_SAMP_COUNT = 3;

#define MAX_TERRAIN_MATERIAL_COUNT 4

typedef struct material_system_state {
    material_system_config config;

    kresource_material* default_unlit_material;
    kresource_material* default_phong_material;
    kresource_material* default_pbr_material;
    kresource_material* default_layered_material;

    // FIXME: remove these
    u32 terrain_shader_id;
    shader* terrain_shader;
    u32 pbr_shader_id;
    shader* pbr_shader;

    // Keep a pointer to the renderer state for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct kresource_system_state* resource_system;

} material_system_state;

static b8 create_default_pbr_material(material_system_state* state);
static b8 create_default_terrain_material(material_system_state* state);
static b8 load_material(material_system_state* state, material_config* config, material* m);
static void destroy_material(kresource_material* m);

static b8 assign_map(material_system_state* state, kresource_texture_map* map, const material_map* config, kname material_name, const kresource_texture* default_tex);
static void on_material_system_dump(console_command_context context);

b8 material_system_initialize(u64* memory_requirement, material_system_state* state, const material_system_config* config) {
    material_system_config* typed_config = (material_system_config*)config;
    if (typed_config->max_material_count == 0) {
        KFATAL("material_system_initialize - config.max_material_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    *memory_requirement = sizeof(material_system_state);

    if (!state) {
        return true;
    }

    // Keep a pointer to the renderer system state for quick access.
    const engine_system_states* states = engine_systems_get();
    state->renderer = states->renderer_system;
    state->resource_system = states->kresource_state;
    state->texture_system = states->texture_system;

    state->config = *typed_config;

    // FIXME: remove these
    // Get the uniform indices.
    // Save off the locations for known types for quick lookups.
    state->pbr_shader = shader_system_get("Shader.PBRMaterial");
    state->pbr_shader_id = state->pbr_shader->id;
    state->terrain_shader = shader_system_get("Shader.Builtin.Terrain");
    state->terrain_shader_id = state->terrain_shader->id;

    // Load up some default materials.
    if (!create_default_pbr_material(state)) {
        KFATAL("Failed to create default PBR material. Application cannot continue.");
        return false;
    }

    if (!create_default_terrain_material(state)) {
        KFATAL("Failed to create default terrain material. Application cannot continue.");
        return false;
    }

    // Register a console command to dump list of materials/references.
    console_command_register("material_system_dump", 0, on_material_system_dump);

    return true;
}

void material_system_shutdown(struct material_system_state* state) {
    if (state) {

        // Release default materials.
        kresource_system_release(state->resource_system, state->default_pbr_material->base.name);
        kresource_system_release(state->resource_system, state->default_layered_material->base.name);
    }
}

static void material_resource_loaded(kresource* resource, void* listener) {
    kresource_material* typed_resource = (kresource_material*)resource;
    material_instance* instance = (material_instance*)listener;

    // TODO: In this case, the texture map should probably actually be stored on the "probe" itself,
    // this would reduce the number of samplers required. Scenes can either have a probe or not.
    // If there is no probe, whatever is rendering the scene (i.e the forward rendergraph node) should have
    // a default sampler in this case.
    // Additionally, the "IBL cubemap" should be converted to a sampler array with a max number of samplers
    // (say, 4 for example), and a local index should be passed indicating which one should be used per render.
    // The IBL cubemap sampler array should be global.
    // This will eliminate the need for local samplers which were just added, but should work best.
    // LEFTOFF: If the resource is already loaded, then new local resources from the shader it is associated
    // with must be obtained here before returning. If the resource is not yet loaded, then this
    // should happen when the resource is finally loaded. This means the pointer to the local id
    // will need to be passed along in the context of the request.
    if (resource->state == KRESOURCE_STATE_LOADED) {
        if (typed_resource->type == KRESOURCE_MATERIAL_TYPE_PBR) {
            // FIXME: use kname instead
            u32 pbr_shader_id = shader_system_get_id("Shader.PBRMaterial");
            // NOTE: No maps for this shader type.
            if (!shader_system_shader_per_draw_acquire(pbr_shader_id, 1, 0, &instance->per_draw_id)) {
                KASSERT_MSG(false, "Failed to acquire renderer resources for default PBR material. Application cannot continue.");
            }
        } else {
            KASSERT_MSG(false, "Unsupported material type - add local shader acquisition logic.");
        }
    }
}

b8 material_system_acquire(material_system_state* state, kname name, material_instance* out_instance) {
    KASSERT_MSG(out_instance, "out_instance is required.");

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.base.user_callback = material_resource_loaded;
    request.base.listener_inst = out_instance;
    out_instance->material = (kresource_material*)kresource_system_request(state->resource_system, name, (kresource_request_info*)&request);
    return true;
}

void material_system_release_instance(material_system_state* state, material_instance* instance) {
    if (instance) {
        u32 shader_id;
        b8 do_release = true;
        switch (instance->material->type) {
        default:
        case KRESOURCE_MATERIAL_TYPE_UNKNOWN:
            KWARN("Unknown material type - per-draw resources cannot be released.");
            return;
        case KRESOURCE_MATERIAL_TYPE_UNLIT:
            shader_id = shader_system_get_id("Shader.Unlit");
            do_release = instance->material != state->default_unlit_material;
            break;
        case KRESOURCE_MATERIAL_TYPE_PHONG:
            shader_id = shader_system_get_id("Shader.Phong");
            do_release = instance->material != state->default_phong_material;
            break;
        case KRESOURCE_MATERIAL_TYPE_PBR:
            shader_id = shader_system_get_id("Shader.PBRMaterial");
            do_release = instance->material != state->default_pbr_material;
            break;
        case KRESOURCE_MATERIAL_TYPE_LAYERED_PBR:
            shader_id = shader_system_get_id("Shader.LayeredPBRMaterial");
            do_release = instance->material != state->default_layered_material;
            break;
        }

        // Release per-draw resources.
        shader_system_shader_per_draw_release(shader_id, instance->per_draw_id, 0, 0);

        // Only release if not a default material.
        if (do_release) {
            kresource_system_release(state->resource_system, instance->material->base.name);
        }

        instance->material = 0;
        instance->per_draw_id = INVALID_ID;
    }
}

material_instance material_system_get_default_unlit(material_system_state* state) {
    material_instance instance = {0};
    // FIXME: use kname instead
    u32 shader_id = shader_system_get_id("Shader.Unlit");
    // NOTE: No maps for this shader type.
    if (!shader_system_shader_per_draw_acquire(shader_id, 0, 0, &instance.per_draw_id)) {
        KASSERT_MSG(false, "Failed to acquire per-draw renderer resources for default Unlit material. Application cannot continue.");
    }
    instance.material = state->default_unlit_material;
    return instance;
}

material_instance material_system_get_default_phong(material_system_state* state) {
    material_instance instance = {0};
    // FIXME: use kname instead
    u32 shader_id = shader_system_get_id("Shader.Phong");
    // NOTE: No maps for this shader type.
    if (!shader_system_shader_per_draw_acquire(shader_id, 0, 0, &instance.per_draw_id)) {
        KASSERT_MSG(false, "Failed to acquire per-draw renderer resources for default Phong material. Application cannot continue.");
    }
    instance.material = state->default_phong_material;
    return instance;
}

material_instance material_system_get_default_pbr(material_system_state* state) {
    material_instance instance = {0};
    // FIXME: use kname instead
    u32 shader_id = shader_system_get_id("Shader.PBRMaterial");
    // NOTE: No maps for this shader type.
    if (!shader_system_shader_per_draw_acquire(shader_id, 0, 0, &instance.per_draw_id)) {
        KASSERT_MSG(false, "Failed to acquire per-draw renderer resources for default PBR material. Application cannot continue.");
    }
    instance.material = state->default_pbr_material;
    return instance;
}

material_instance material_system_get_default_layered_pbr(material_system_state* state) {
    material_instance instance = {0};
    // FIXME: use kname instead
    u32 shader_id = shader_system_get_id("Shader.LayeredPBRMaterial");
    // NOTE: No maps for this shader type.
    if (!shader_system_shader_per_draw_acquire(shader_id, 0, 0, &instance.per_draw_id)) {
        KASSERT_MSG(false, "Failed to acquire per-draw renderer resources for default LayeredPBR material. Application cannot continue.");
    }
    instance.material = state->default_layered_material;
    return instance;
}

void material_system_dump(material_system_state* state) {
    // FIXME: find a way to query this from the kresource system.
    //
    /* material_reference* refs = (material_reference*)state_ptr->registered_material_table.memory;
    for (u32 i = 0; i < state_ptr->registered_material_table.element_count; ++i) {
        material_reference* r = &refs[i];
        if (r->reference_count > 0 || r->handle != INVALID_ID) {
            KTRACE("Found material ref (handle/refCount): (%u/%u)", r->handle, r->reference_count);
            if (r->handle != INVALID_ID) {
                KTRACE("Material name: %s", state_ptr->registered_materials[r->handle].name);
            }
        }
    } */
}

static b8 assign_map(material_system_state* state, kresource_texture_map* map, const material_map* config, kname material_name, const kresource_texture* default_tex) {
    map->filter_minify = config->filter_min;
    map->filter_magnify = config->filter_mag;
    map->repeat_u = config->repeat_u;
    map->repeat_v = config->repeat_v;
    map->repeat_w = config->repeat_w;
    map->mip_levels = 1;
    map->generation = INVALID_ID;

    if (config->texture_name && string_length(config->texture_name) > 0) {

        map->texture = texture_system_request(
            kname_create(config->texture_name),
            INVALID_KNAME, // Use the resource from the package where it is first found. TODO: configurable within material config - include material's package name here first.
            0,             // no listener
            0              // no callback
        );
        if (!map->texture) {
            // Use default texture instead if provided.
            if (default_tex) {
                KWARN("Failed to request material texture '%s'. Using default '%s'.", config->texture_name, kname_string_get(default_tex->base.name));
                map->texture = default_tex;
            } else {
                KERROR("Failed to request material texture '%s', and no default was provided.", config->texture_name);
                return false;
            }
        }

    } else {
        // This is done when a texture is not configured, as opposed to when it is configured and not found (above).
        map->texture = default_tex;
    }

    // Acquire texture map resources.
    if (!renderer_kresource_texture_map_resources_acquire(state->renderer, map)) {
        KERROR("Unable to acquire resources for texture map.");
        return false;
    }
    return true;
}

/* static b8 load_material(material_system_state* state, material_config* config, material* m) {
    kzero_memory(m, sizeof(material));

    // name
    m->name = kname_create(config->name);

    m->type = config->type;
    shader* selected_shader = 0;
    shader_instance_resource_config instance_resource_config = {0};

    // Process the material config by type.
    if (config->type == MATERIAL_TYPE_PBR) {
        selected_shader = state->pbr_shader;
        m->shader_id = state->pbr_shader_id;
        // PBR-specific properties.
        u32 prop_count = darray_length(config->properties);

        // Defaults
        // TODO: PBR properties
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

        // Maps. PBR expects a albedo, normal, and combined (metallic, roughness, AO).
        m->maps = darray_reserve(kresource_texture_map, PBR_MAP_COUNT);
        darray_length_set(m->maps, PBR_MAP_COUNT);
        u32 configure_map_count = darray_length(config->maps);

        // Setup tracking for what maps are/are not assigned.
        b8 mat_maps_assigned[PBR_MATERIAL_TEXTURE_COUNT];
        for (u32 i = 0; i < PBR_MATERIAL_TEXTURE_COUNT; ++i) {
            mat_maps_assigned[i] = false;
        }
        b8 ibl_cube_assigned = false;
        const char* map_names[PBR_MATERIAL_TEXTURE_COUNT] = {"albedo", "normal", "combined"};
        const kresource_texture* default_textures[PBR_MATERIAL_TEXTURE_COUNT] = {
            texture_system_get_default_kresource_diffuse_texture(state->texture_system),
            texture_system_get_default_kresource_normal_texture(state->texture_system),
            texture_system_get_default_kresource_combined_texture(state->texture_system)};

        // Attempt to match configured names to those required by PBR materials.
        // This also ensures the maps are in the proper order.
        for (u32 i = 0; i < configure_map_count; ++i) {
            b8 found = false;
            for (u32 tex_slot = 0; tex_slot < PBR_MATERIAL_TEXTURE_COUNT; ++tex_slot) {
                if (strings_equali(config->maps[i].name, map_names[tex_slot])) {
                    if (!assign_map(state, &m->maps[tex_slot], &config->maps[i], m->name, default_textures[tex_slot])) {
                        return false;
                    }
                    mat_maps_assigned[tex_slot] = true;
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
            // See if it is a configured IBL cubemap.
            // TODO: May not want this to be configurable as a map, but rather provided by the scene from a reflection probe.
            if (strings_equali(config->maps[i].name, "ibl_cube")) {
                // TODO: just loading a default cube map for now. Need to get this from the probe instead.
                if (!assign_map(state, &m->maps[SAMP_IRRADIANCE_MAP], &config->maps[i], m->name, texture_system_get_default_kresource_cube_texture(state->texture_system))) {
                    return false;
                }
                ibl_cube_assigned = true;
            } else {
                // NOTE: Ignore unexpected maps, but warn about it.
                KWARN("Configuration for material '%s' contains a map named '%s', which will be ignored for PBR material types.", config->name, config->maps[i].name);
            }
        }

        // Ensure all maps are always assigned, even if only with defaults.
        for (u32 i = 0; i < PBR_MATERIAL_TEXTURE_COUNT; ++i) {
            if (!mat_maps_assigned[i]) {
                material_map map_config = {0};
                map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
                map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
                map_config.name = string_duplicate(map_names[i]);
                map_config.texture_name = "";
                b8 assign_result = assign_map(state, &m->maps[i], &map_config, m->name, default_textures[i]);
                string_free(map_config.name);
                if (!assign_result) {
                    return false;
                }
            }
        }

        // Also make sure the cube map is always assigned.
        // TODO: May not want this to be configurable as a map, but rather provided by the scene from a reflection probe.
        if (!ibl_cube_assigned) {
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "ibl_cube";
            map_config.texture_name = "";
            if (!assign_map(state, &m->maps[SAMP_IRRADIANCE_MAP], &map_config, m->name, texture_system_get_default_kresource_cube_texture(state->texture_system))) {
                return false;
            }
        }

        // Shadow maps can't be configured, so set them up here.
        {
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
            map_config.name = "shadow_map";
            map_config.texture_name = "";
            if (!assign_map(state, &m->maps[SAMP_SHADOW_MAP], &map_config, m->name, texture_system_get_default_kresource_diffuse_texture(state->texture_system))) {
                return false;
            }
        }

        // Gather a list of pointers to texture maps;
        // Send it off to the renderer to acquire resources.
        // Map count for this type is known.
        instance_resource_config.uniform_config_count = 3; // NOTE: This includes material maps, shadow maps and irradiance map.
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

        // Material textures
        shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
        // mat_textures->uniform_location = state_ptr->pbr_locations.material_texures;
        mat_textures->kresource_texture_map_count = PBR_MATERIAL_TEXTURE_COUNT;
        mat_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * mat_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        mat_textures->kresource_texture_maps[SAMP_ALBEDO] = &m->maps[SAMP_ALBEDO];
        mat_textures->kresource_texture_maps[SAMP_NORMAL] = &m->maps[SAMP_NORMAL];
        mat_textures->kresource_texture_maps[SAMP_COMBINED] = &m->maps[SAMP_COMBINED];

        // Shadow textures
        shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
        // shadow_textures->uniform_location = state_ptr->pbr_locations.shadow_textures;
        shadow_textures->kresource_texture_map_count = 1;
        shadow_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * shadow_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        shadow_textures->kresource_texture_maps[0] = &m->maps[SAMP_SHADOW_MAP];

        // IBL cube texture
        shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
        // ibl_cube_texture->uniform_location = state_ptr->pbr_locations.ibl_cube_texture;
        ibl_cube_texture->kresource_texture_map_count = 1;
        ibl_cube_texture->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * ibl_cube_texture->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        ibl_cube_texture->kresource_texture_maps[0] = &m->maps[SAMP_IRRADIANCE_MAP];

    } else if (config->type == MATERIAL_TYPE_CUSTOM) {
        // Gather a list of pointers to texture maps;
        // Send it off to the renderer to acquire resources.
        // Custom materials.
        if (!config->shader_name) {
            KERROR("Shader name is required for custom material types. Material '%s' failed to load", m->name);
            return false;
        }
        selected_shader = shader_system_get(config->shader_name);
        m->shader_id = selected_shader->id;
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
        m->maps = darray_reserve(kresource_texture_map, map_count);
        darray_length_set(m->maps, map_count);
        for (u32 i = 0; i < map_count; ++i) {
            // No known mapping, so just map them in order.
            // Invalid textures will use the default texture because map type isn't known.
            if (!assign_map(state, &m->maps[i], &config->maps[i], m->name, texture_system_get_default_kresource_texture(state->texture_system))) {
                return false;
            }
        }

        u32 global_sampler_count = selected_shader->global_uniform_sampler_count;
        u32 instance_sampler_count = selected_shader->instance_uniform_sampler_count;

        // NOTE: The map order for custom materials must match the uniform sampler order defined in the shader. This is
        // always processed by global first, then instance.
        instance_resource_config.uniform_config_count = global_sampler_count + instance_sampler_count;
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

        // Track the number of maps used by global uniforms first and offset by that.
        u32 map_offset = 0;
        for (u32 i = 0; i < global_sampler_count; ++i) {
            map_offset++;
        }
        for (u32 i = 0; i < instance_sampler_count; ++i) {
            shader_uniform* u = &selected_shader->uniforms[selected_shader->instance_sampler_indices[i]];
            shader_instance_uniform_texture_config* uniform_config = &instance_resource_config.uniform_configs[i];
            // uniform_config->uniform_location = u->location;
            uniform_config->kresource_texture_map_count = KMAX(u->array_length, 1);
            uniform_config->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * uniform_config->kresource_texture_map_count, MEMORY_TAG_ARRAY);
            for (u32 j = 0; j < uniform_config->kresource_texture_map_count; ++j) {
                uniform_config->kresource_texture_maps[j] = &m->maps[i + map_offset];
            }
        }
    } else {
        KERROR("Unknown material type: %d. Material '%s' cannot be loaded.", config->type, m->name);
        return false;
    }

    // Acquire the instance resources for this material.
    b8 result = renderer_shader_instance_resources_acquire(state_ptr->renderer, selected_shader, &instance_resource_config, &m->internal_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg->kresource_texture_maps, sizeof(shader_instance_uniform_texture_config) * ucfg->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        ucfg->kresource_texture_maps = 0;
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    return result;
} */

static b8 create_default_pbr_material(material_system_state* state) {

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.material_source_text = "\
version = 3\
type = \"pbr\"\
blend_mode = \"translucent\"\
\
inputs = [\
    {\
        name = \"albedo\"\
        type = \"colour\"\
    }\
    {\
        name = \"normal\"\
        type = \"colour\"\
    }\
    {\
        name = \"metallic\"\
        type = \"scalar\"\
    }\
    {\
        name = \"roughness\"\
        type = \"scalar\"\
    }\
    {\
        name = \"ao\"\
        type = \"scalar\"\
    }\
    {\
        name = \"emissive\"\
        type = \"colour\"\
    }\
]\
\
maps = [\
    {\
        name = \"albedo\"\
        input = \"albedo\"\
        texture_name = \"default_diffuse\"\
    }\
    {\
        name = \"normal\"\
        input = \"normal\"\
        texture_name = \"default_normal\"\
    }\
    {\
        name = \"metallic\"\
        input = \"metallic\"\
        channel = \"r\"\
        texture_name = \"default_metallic\"\
    }\
    {\
        name = \"roughness\"\
        input = \"roughness\"\
        channel = \"r\"\
        texture_name = \"default_roughness\"\
    }\
    {\
        name = \"ao\"\
        input = \"ao\"\
        channel = \"r\"\
        texture_name = \"default_ao\"\
    }\
    {\
        name = \"opacity\"\
        input = \"opacity\"\
        texture_name = \"default_opacity\"\
    }\
    {\
        name = \"opacity_mask\"\
        input = \"opacity_mask\"\
        texture_name = \"default_opacity_mask\"\
    }\
    {\
        name = \"emissive\"\
        input = \"emissive\"\
        texture_name = \"default_emissive\"\
    }\
]\
\
properties = [\
]";

    state->default_pbr_material = (kresource_material*)kresource_system_request(state->resource_system, kname_create("default"), (kresource_request_info*)&request);

    u32 shader_id = shader_system_get_id("Shader.PBRMaterial");
    kresource_material* m = state->default_pbr_material;

    kresource_texture_map* maps[PBR_MATERIAL_MAP_COUNT] = {
        &m->albedo_diffuse_map,
        &m->normal_map,
        &m->metallic_roughness_ao_map
        // TODO: emissive
    };

    // Acquire group resources.
    if (!shader_system_shader_group_acquire(shader_id, PBR_MATERIAL_MAP_COUNT, maps, &m->group_id)) {
        KERROR("Unable to acquire group resources for default PBR material.");
        return false;
    }

    return true;
}

static b8 create_default_layered_material(material_system_state* state) {

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    // FIXME: figure out how the layers should look for this material type.
    //
    // TODO: Need to add "channel" property to each map separate from the name of
    // the map to indicate its usage.
    //
    // TODO: Layered materials will work somewhat differently than standard (see below
    // for example). Each "channel" will be represented by a arrayed texture whose number
    // of elements is equal to the number of layers in the material. This keeps the sampler
    // count low and also allows the loading of many textures for the terrain at once. The
    // mesh using this material should indicate the layer to be used at the vertex level (as
    // sampling this from an image limits to 4 layers (RGBA)).
    //
    // TODO: The size of all layers is determined by the channel_size_x/y in the material config,
    // OR by not specifying it and using the default of 1024. Texture data will be loaded into the
    // array by copying when the dimensions of the source texture match the channel_size_x/y, or by
    // blitting the texture onto the layer when it does not match. This gets around the requirement
    // of having all textures be the same size in an arrayed texture.
    //
    // TODO: This process will also be utilized by the metallic_roughness_ao_map (formerly "combined"),
    // but instead targeting a single channel of the target texture as opposed to a layer of it.
    request.material_source_text = "\
version = 3\
type = \"layered_pbr\"\
channel_size_x = 1024\
channel_size_y = 1024\
\
layers = [\
    {\
        name = \"layer_0\"\
        maps = [\
            {\
                name = \"albedo\"\
                channel = \"albedo\"\
                texture_name = \"default_diffuse\"\
            }\
            {\
                name = \"normal\"\
                channel = \"normal\"\
                texture_name = \"default_normal\"\
            }\
            {\
                name = \"metallic\"\
                channel = \"mra\"\
                source_channel = \"r\"\
                texture_name = \"default_metallic\"\
            }\
            {\
                name = \"roughness\"\
                channel = \"roughness\"\
                texture_name = \"default_roughness\"\
            }\
            {\
                name = \"ao\"\
                channel = \"ao\"\
                texture_name = \"default_ao\"\
            }\
            {\
                name = \"emissive\"\
                channel = \"emissive\"\
                texture_name = \"default_emissive\"\
            }\
        ]\
    }\
    {\
        name = \"layer_1\"\
        maps = [\
            {\
                name = \"albedo\"\
                channel = \"albedo\"\
                texture_name = \"default_diffuse\"\
            }\
            {\
                name = \"normal\"\
                channel = \"normal\"\
                texture_name = \"default_normal\"\
            }\
            {\
                name = \"metallic\"\
                channel = \"metallic\"\
                texture_name = \"default_metallic\"\
            }\
            {\
                name = \"roughness\"\
                channel = \"roughness\"\
                texture_name = \"default_roughness\"\
            }\
            {\
                name = \"ao\"\
                channel = \"ao\"\
                texture_name = \"default_ao\"\
            }\
            {\
                name = \"emissive\"\
                channel = \"emissive\"\
                texture_name = \"default_emissive\"\
            }\
        ]\
    }\
    {\
        name = \"layer_2\"\
        maps = [\
            {\
                name = \"albedo\"\
                channel = \"albedo\"\
                texture_name = \"default_diffuse\"\
            }\
            {\
                name = \"normal\"\
                channel = \"normal\"\
                texture_name = \"default_normal\"\
            }\
            {\
                name = \"metallic\"\
                channel = \"metallic\"\
                texture_name = \"default_metallic\"\
            }\
            {\
                name = \"roughness\"\
                channel = \"roughness\"\
                texture_name = \"default_roughness\"\
            }\
            {\
                name = \"ao\"\
                channel = \"ao\"\
                texture_name = \"default_ao\"\
            }\
            {\
                name = \"emissive\"\
                channel = \"emissive\"\
                texture_name = \"default_emissive\"\
            }\
        ]\
    }\
    {\
        name = \"layer_3\"\
        maps = [\
            {\
                name = \"albedo\"\
                channel = \"albedo\"\
                texture_name = \"default_diffuse\"\
            }\
            {\
                name = \"normal\"\
                channel = \"normal\"\
                texture_name = \"default_normal\"\
            }\
            {\
                name = \"metallic\"\
                channel = \"metallic\"\
                texture_name = \"default_metallic\"\
            }\
            {\
                name = \"roughness\"\
                channel = \"roughness\"\
                texture_name = \"default_roughness\"\
            }\
            {\
                name = \"ao\"\
                channel = \"ao\"\
                texture_name = \"default_ao\"\
            }\
            {\
                name = \"emissive\"\
                channel = \"emissive\"\
                texture_name = \"default_emissive\"\
            }\
        ]\
    }\
]\
\
properties = [\
]";

    state->default_layered_material = (kresource_material*)kresource_system_request(state->resource_system, kname_create("default_layered"), (kresource_request_info*)&request);

    // TODO: change to layered material shader.
    u32 shader_id = shader_system_get_id("Shader.Builtin.Terrain");
    kresource_material* m = state->default_layered_material;

    // NOTE: This is an array that includes 3 maps (albedo, normal, met/roughness/ao) per layer.
    kresource_texture_map* maps[LAYERED_PBR_MATERIAL_MAP_COUNT] = {&m->layered_material_map};

    // Acquire group resources.
    if (!shader_system_shader_group_acquire(shader_id, LAYERED_PBR_MATERIAL_MAP_COUNT, maps, &m->group_id)) {
        KERROR("Unable to acquire group resources for default layered PBR material.");
        return false;
    }

    return true;
}

static void on_material_system_dump(console_command_context context) {
    material_system_dump(engine_systems_get()->material_system);
}
