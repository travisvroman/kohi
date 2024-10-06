#include "material_system.h"

#include "containers/darray.h"
#include "containers/hashtable.h"
#include "core/console.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"
#include "systems/resource_system.h"
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

// The number of textures for a PBR material
#define PBR_MATERIAL_TEXTURE_COUNT 3

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
    kresource_material* default_terrain_material;

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

        // Destroy the default material.
        destroy_material(state->default_pbr_material);
        destroy_material(state->default_terrain_material);
    }
}

kresource_material* material_system_acquire(material_system_state* state, kname name, u32* out_local_id) {
    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    kresource* out_resource = kresource_system_request(state->resource_system, name, (kresource_request_info*)&request);
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
    if (out_resource->state == KRESOURCE_STATE_LOADED) {
        kresource_material* typed_resource = (kresource_material*)out_resource;
        if (typed_resource->type == KRESOURCE_MATERIAL_TYPE_PBR) {
            u32 pbr_shader_id = shader_system_get_id("Shader.PBRMaterial");
            // NOTE: No maps for this shader type.
            if (!shader_system_shader_local_acquire(pbr_shader_id, 1, 0, out_local_id)) {
                KASSERT_MSG(false, "Failed to acquire renderer resources for default PBR material. Application cannot continue.");
            }
        } else {
            KASSERT_MSG(false, "Unsupported material type - add local shader acquisition logic.");
        }
    }
    return (kresource_material*)out_resource;
}

void material_system_release(material_system_state* state, kresource_material* material) {
    kresource_system_release(state->resource_system, (kresource*)material);
}

const kresource_material* material_system_get_default_unlit(material_system_state* state) {
    return state->default_unlit_material;
}

const kresource_material* material_system_get_default_phong(material_system_state* state) {
    return state->default_phong_material;
}

const kresource_material* material_system_get_default_pbr(material_system_state* state) {
    return state->default_pbr_material;
}

const kresource_material* material_system_get_default_terrain(material_system_state* state) {
    return state->default_terrain_material;
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

/* static void destroy_material(kresource_material* m) {
    material_system_state* state = engine_systems_get()->material_system;
    // KTRACE("Destroying material '%s'...", m->name);

    u32 length = darray_length(m->maps);
    for (u32 i = 0; i < length; ++i) {
        // Release texture references.
        if (m->maps[i].texture) {
            texture_system_release_resource((kresource_texture*)m->maps[i].texture);
        }
        // Release texture map resources.
        renderer_kresource_texture_map_resources_release(state->renderer, &m->maps[i]);
    }

    // Release renderer resources.
    if (m->shader_id != INVALID_ID && m->internal_id != INVALID_ID) {
        renderer_shader_instance_resources_release(state_ptr->renderer, shader_system_get_by_id(m->shader_id), m->internal_id);
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
} */

static b8 create_default_pbr_material(material_system_state* state) {

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.material_source_text = "\
version = 3\
type = \"pbr\"\
\
maps = [\
    {\
        name = \"albedo\"\
        texture_name = \"default_diffuse\"\
    }\
]\
\
properties = [\
]";

    state->default_pbr_material = (kresource_material*)kresource_system_request(state->resource_system, kname_create("default"), (kresource_request_info*)&request);

    // FIXME: Move this material shader acquisition code to somewhere else?
    //
    // Setup a configuration to get instance resources for this material.
    material* m = &state->default_pbr_material;
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    instance_resource_config.uniform_config_count = 3; // NOTE: This includes material maps, shadow maps and irradiance map.
    instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Material textures
    shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
    /* mat_textures->uniform_location = state_ptr->pbr_locations.material_texures; */
    mat_textures->kresource_texture_map_count = PBR_MATERIAL_TEXTURE_COUNT;
    mat_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * mat_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    mat_textures->kresource_texture_maps[SAMP_ALBEDO] = &m->maps[SAMP_ALBEDO];
    mat_textures->kresource_texture_maps[SAMP_NORMAL] = &m->maps[SAMP_NORMAL];
    mat_textures->kresource_texture_maps[SAMP_COMBINED] = &m->maps[SAMP_COMBINED];

    // Shadow textures
    shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
    /* shadow_textures->uniform_location = state_ptr->pbr_locations.shadow_textures; */
    shadow_textures->kresource_texture_map_count = 1;
    shadow_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * shadow_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    shadow_textures->kresource_texture_maps[0] = &m->maps[SAMP_SHADOW_MAP];

    // IBL cube texture
    shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
    /* ibl_cube_texture->uniform_location = state_ptr->pbr_locations.ibl_cube_texture; */
    ibl_cube_texture->kresource_texture_map_count = 1;
    ibl_cube_texture->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * ibl_cube_texture->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    ibl_cube_texture->kresource_texture_maps[0] = &m->maps[SAMP_IRRADIANCE_MAP];

    shader* s = shader_system_get_by_id(state->pbr_shader_id);
    if (!renderer_shader_instance_resources_acquire(state->renderer, s, &instance_resource_config, &state->default_pbr_material->instance_id)) {
        KFATAL("Failed to acquire renderer resources for default PBR material. Application cannot continue.");
        return false;
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg->kresource_texture_maps, sizeof(kresource_texture_map*) * ucfg->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        ucfg->kresource_texture_maps = 0;
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    return true;
}

static b8 create_default_terrain_material(material_system_state* state) {
    kzero_memory(&state->default_terrain_material, sizeof(material));
    state->default_terrain_material.id = INVALID_ID;
    state->default_terrain_material.type = MATERIAL_TYPE_TERRAIN;
    state->default_terrain_material.generation = INVALID_ID;
    state->default_terrain_material.name = kname_create(DEFAULT_TERRAIN_MATERIAL_NAME);

    // Should essentially be the same thing as the defualt material, just mapped to an "array" of one material.
    state->default_terrain_material.property_struct_size = sizeof(material_terrain_properties);
    state->default_terrain_material.properties = kallocate(sizeof(material_terrain_properties), MEMORY_TAG_MATERIAL_INSTANCE);
    material_terrain_properties* properties = (material_terrain_properties*)state->default_terrain_material.properties;
    properties->num_materials = MAX_TERRAIN_MATERIAL_COUNT;
    properties->materials[0].diffuse_colour = vec4_one(); // white
    properties->materials[0].shininess = 8.0f;
    state->default_terrain_material.maps = darray_reserve(kresource_texture_map, TERRAIN_SAMP_COUNT);
    darray_length_set(state->default_terrain_material.maps, TERRAIN_SAMP_COUNT);
    // Material texture array.
    kresource_texture_map* map = &state->default_terrain_material.maps[SAMP_TERRAIN_MATERIAL_ARRAY_MAP];
    map->texture = texture_system_get_default_kresource_terrain_texture(state->texture_system);
    // NOTE: setting mode to nearest neighbor to make the chekerboard non-blurry.
    map->filter_magnify = map->filter_minify = TEXTURE_FILTER_MODE_NEAREST;
    map->generation = INVALID_ID;
    map->mip_levels = 1;
    renderer_kresource_texture_map_resources_acquire(state->renderer, map);

    state->default_terrain_material.maps[SAMP_TERRAIN_SHADOW_MAP].texture = texture_system_get_default_kresource_diffuse_texture(state->texture_system);

    // Change the clamp mode on the default shadow map to border.
    kresource_texture_map* ssm = &state->default_terrain_material.maps[SAMP_TERRAIN_SHADOW_MAP];
    ssm->repeat_u = ssm->repeat_v = ssm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    ssm->generation = INVALID_ID;
    ssm->mip_levels = 1;
    renderer_kresource_texture_map_resources_acquire(state->renderer, ssm);

    // NOTE: PBR materials are required for terrains.
    // NOTE: 4 materials * 3 maps per will still be loaded in order (albedo/norm/met/rough/ao per mat)
    // Next group will be shadow mappings
    // Last irradiance map

    // Setup a configuration to get instance resources for this material.
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    instance_resource_config.uniform_config_count = 3; // NOTE: This includes material maps, shadow maps and irradiance map.
    instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Material textures
    material* m = &state_ptr->default_terrain_material;
    shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
    /* mat_textures->uniform_location = state_ptr->terrain_locations.material_texures; */
    mat_textures->kresource_texture_map_count = 1;
    mat_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * mat_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    mat_textures->kresource_texture_maps[SAMP_TERRAIN_MATERIAL_ARRAY_MAP] = &m->maps[SAMP_TERRAIN_MATERIAL_ARRAY_MAP];

    // Shadow textures
    shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
    /* shadow_textures->uniform_location = state_ptr->terrain_locations.shadow_textures; */
    shadow_textures->kresource_texture_map_count = 1;
    shadow_textures->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * shadow_textures->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    shadow_textures->kresource_texture_maps[0] = &m->maps[SAMP_TERRAIN_SHADOW_MAP];

    // IBL cube texture
    shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
    /* ibl_cube_texture->uniform_location = state_ptr->terrain_locations.ibl_cube_texture; */
    ibl_cube_texture->kresource_texture_map_count = 1;
    ibl_cube_texture->kresource_texture_maps = kallocate(sizeof(kresource_texture_map*) * ibl_cube_texture->kresource_texture_map_count, MEMORY_TAG_ARRAY);
    ibl_cube_texture->kresource_texture_maps[0] = &m->maps[SAMP_TERRAIN_IRRADIANCE_MAP];
    kresource_texture_map* irm = &m->maps[SAMP_TERRAIN_IRRADIANCE_MAP];
    renderer_kresource_texture_map_resources_acquire(state->renderer, irm);
    irm->generation = INVALID_ID;
    irm->mip_levels = 1;

    // Acquire the resources
    shader* s = shader_system_get_by_id(state_ptr->terrain_shader_id);
    b8 result = renderer_shader_instance_resources_acquire(state_ptr->renderer, s, &instance_resource_config, &state->default_terrain_material.internal_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for default terrain material '%s'.");
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg->kresource_texture_maps, sizeof(kresource_texture_map*) * ucfg->kresource_texture_map_count, MEMORY_TAG_ARRAY);
        ucfg->kresource_texture_maps = 0;
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Make sure to assign the shader id.
    state->default_terrain_material.shader_id = s->id;

    return true;
}

static void on_material_system_dump(console_command_context context) {
    material_system_dump();
}
