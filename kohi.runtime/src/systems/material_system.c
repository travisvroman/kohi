#include "material_system.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "core/console.h"
#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "kresources/kresource_utils.h"
#include "logger.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

// LEFTOFF: Rewrite the material in general. Needs to include a way to lookup base material
// khandle via the material instance khandle. this system should also orchestrate the gathering
// of resources (i.e. textures, shaders, samplers, etc.) for materials and material instances
// as well as setting uniforms, etc.
//
// TODO: Strip out and re-do the old "terrain" material type and replace with a new "blended"
// material type after the base material type is refactored.

typedef struct material_system_state {
    material_system_config config;

    // darray of materials, indexed by material khandle resource index.
    material_data* materials;
    // darray of material instances, indexed first by material khandle index, then by instance khandle index.
    material_instance_data** instances;

    // A default material for each "model" of material.
    material_data* default_unlit_material;
    material_data* default_phong_material;
    material_data* default_pbr_material;
    material_data* default_blended_material;

    // Cached ids for various material types' shaders.
    u32 standard_shader_id;
    u32 water_shader_id;
    u32 blended_shader_id;

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

// NEW

static khandle material_create(material_system_state* state, const kresource_material* typed_resource) {
    u32 resource_index = INVALID_ID;

    // Attempt to find a free "slot", or create a new entry if there isn't one.
    u32 material_count = darray_length(state->materials);
    for (u32 i = 0; i < material_count; ++i) {
        if (state->materials[i].unique_id == INVALID_ID_U64) {
            // free slot. An array should already exists for instances here.
            resource_index = i;
            break;
        }
    }
    if (resource_index == INVALID_ID) {
        resource_index = material_count;
        darray_push(state->materials, (material_data){0});
        // This also means a new entry needs to be created at this index for instances.
        material_instance_data* new_inst_array = darray_create(material_instance_data);
        darray_push(state->instances, new_inst_array);
    }

    material_data* material = &state->materials[resource_index];

    // Setup a handle first.
    khandle handle = khandle_create(resource_index);
    material->unique_id = handle.unique_id.uniqueid;

    // Base colour map or value
    if (typed_resource->base_colour_map.resource_name) {
        material->base_colour_texture = texture_system_request(typed_resource->base_colour_map.resource_name, typed_resource->base_colour_map.package_name, 0, 0);
    } else {
        material->base_colour = typed_resource->base_colour;
    }

    // Normal map
    if (typed_resource->normal_map.resource_name) {
        material->normal_texture = texture_system_request(typed_resource->normal_map.resource_name, typed_resource->normal_map.package_name, 0, 0);
    }
    material->flags |= typed_resource->normal_enabled ? MATERIAL_FLAG_NORMAL_ENABLED_BIT : 0;

    // Metallic map or value
    if (typed_resource->metallic_map.resource_name) {
        material->metallic_texture = texture_system_request(typed_resource->metallic_map.resource_name, typed_resource->metallic_map.package_name, 0, 0);
        material->metallic_texture_channel = kresource_texture_map_channel_to_texture_channel(typed_resource->metallic_map.channel);
    } else {
        material->metallic = typed_resource->metallic;
    }
    // Roughness map or value
    if (typed_resource->roughness_map.resource_name) {
        material->roughness_texture = texture_system_request(typed_resource->roughness_map.resource_name, typed_resource->roughness_map.package_name, 0, 0);
        material->roughness_texture_channel = kresource_texture_map_channel_to_texture_channel(typed_resource->roughness_map.channel);
    } else {
        material->roughness = typed_resource->roughness;
    }
    // Ambient occlusion map or value
    if (typed_resource->ambient_occlusion_map.resource_name) {
        material->ao_texture = texture_system_request(typed_resource->ambient_occlusion_map.resource_name, typed_resource->ambient_occlusion_map.package_name, 0, 0);
        material->ao_texture_channel = kresource_texture_map_channel_to_texture_channel(typed_resource->ambient_occlusion_map.channel);
    } else {
        material->ao = typed_resource->ambient_occlusion;
    }
    material->flags |= typed_resource->ambient_occlusion_enabled ? MATERIAL_FLAG_AO_ENABLED_BIT : 0;

    // MRA (combined metallic/roughness/ao) map or value
    if (typed_resource->mra_map.resource_name) {
        material->mra_texture = texture_system_request(typed_resource->mra_map.resource_name, typed_resource->mra_map.package_name, 0, 0);
    } else {
        material->mra = typed_resource->mra;
    }
    material->flags |= typed_resource->use_mra ? MATERIAL_FLAG_MRA_ENABLED_BIT : 0;

    // Emissive map or value
    if (typed_resource->emissive_map.resource_name) {
        material->emissive_texture = texture_system_request(typed_resource->emissive_map.resource_name, typed_resource->emissive_map.package_name, 0, 0);
    } else {
        material->emissive = typed_resource->emissive;
    }
    material->flags |= typed_resource->emissive_enabled ? MATERIAL_FLAG_EMISSIVE_ENABLED_BIT : 0;

    // Set remaining flags
    material->flags |= typed_resource->has_transparency ? MATERIAL_FLAG_HAS_TRANSPARENCY : 0;
    material->flags |= typed_resource->double_sided ? MATERIAL_FLAG_DOUBLE_SIDED_BIT : 0;
    material->flags |= typed_resource->recieves_shadow ? MATERIAL_FLAG_RECIEVES_SHADOW_BIT : 0;
    material->flags |= typed_resource->casts_shadow ? MATERIAL_FLAG_CASTS_SHADOW_BIT : 0;
    material->flags |= typed_resource->use_vertex_colour_as_base_colour ? MATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR : 0;

    // LEFTOFF: Setup shader resources, etc.
    //
    // Create a group for the material.

    return handle;
}

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

    state->materials = darray_create(material_data);
    // An array for each material will be created when a material is created.
    state->instances = darray_create(material_instance_data*);

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

static b8 create_default_standard_material(material_system_state* state) {

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.material_source_text = "\
version = 3\
type = \"standard\"\
\
albedo_texture = \"default_albedo\"\
normal_texture = \"default_normal\"\
mra_texture = \"default_mra\"\
emissive_texture = \"default_emissive\"\
emissive_intensity = 1.0\
has_transparency = false\
double_sided = false\
recieves_shadow = true\
casts_shadow = true\
normal_enabled = true\
ao_enabled = false\
emissive_enabled = false\
refraction_enabled = false\
use_vertex_colour_as_albedo = false";

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

static b8 create_default_multi_material(material_system_state* state) {

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
type = \"multi\"\
\
materials = [\
    \"default\"\
    \"default\"\
    \"default\"\
    \"default\"\
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
