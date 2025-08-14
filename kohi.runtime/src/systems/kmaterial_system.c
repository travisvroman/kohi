// TODO:
// - Blended type material
// - Material models (unlit, PBR, Phong, etc.)
//
#include "kmaterial_system.h"

#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <serializers/kasset_material_serializer.h>
#include <serializers/kasset_shader_serializer.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "core/console.h"
#include "core/engine.h"
#include "core/event.h"
#include "kresources/kresource_types.h"
#include "renderer/kmaterial_renderer.h"
#include "renderer/renderer_frontend.h"
#include "runtime_defines.h"
#include "systems/asset_system.h"
#include "systems/texture_system.h"

/**
 * The structure which holds state for the entire material system.
 */
typedef struct kmaterial_system_state {
    kmaterial_system_config config;

    // collection of materials, indexed by material resource index.
    kmaterial_data* materials;
    // darray of material instances, indexed first by material index, then by instance index.
    kmaterial_instance_data** instances;

    // A default material for each type of material.
    kmaterial_data* default_standard_material;
    kmaterial_data* default_water_material;
    kmaterial_data* default_blended_material;

    // Keep a pointer to the renderer state for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;

    // Runtime package name pre-hashed and kept here for convenience.
    kname runtime_package_name;
} kmaterial_system_state;

// Holds data for a material instance request.
typedef struct kasset_material_request_listener {
    kmaterial material_handle;
    u16 instance_id;
    kmaterial_system_state* state;
    b8 needs_cleanup;
} kasset_material_request_listener;

static b8 create_default_standard_material(kmaterial_system_state* state);
static b8 create_default_water_material(kmaterial_system_state* state);
static b8 create_default_blended_material(kmaterial_system_state* state);
static void on_material_system_dump(console_command_context context);
static kmaterial material_handle_create(kmaterial_system_state* state, kname name);
static u16 kmaterial_instance_handle_create(kmaterial_system_state* state, kmaterial material_handle);
static b8 material_create(kmaterial_system_state* state, kmaterial material_handle, const kasset_material* asset);
static void material_destroy(kmaterial_system_state* state, kmaterial_data* material, u32 material_index);
static b8 kmaterial_instance_create(kmaterial_system_state* state, kmaterial base_material, u16* out_instance_id);
static void kmaterial_instance_destroy(kmaterial_system_state* state, kmaterial_data* base_material, kmaterial_instance_data* inst);
static void kasset_material_loaded(void* listener, kasset_material* asset);
static kmaterial_instance default_kmaterial_instance_get(kmaterial_system_state* state, kmaterial_data* base_material);
static kmaterial_data* get_material_data(kmaterial_system_state* state, kmaterial material_handle);
static kmaterial_instance_data* get_kmaterial_instance_data(kmaterial_system_state* state, kmaterial_instance instance);
static b8 material_on_event(u16 code, void* sender, void* listener_inst, event_context data);

b8 kmaterial_system_initialize(u64* memory_requirement, kmaterial_system_state* state, const kmaterial_system_config* config) {
    kmaterial_system_config* typed_config = (kmaterial_system_config*)config;
    if (typed_config->max_material_count == 0) {
        KFATAL("material_system_initialize - config.max_material_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    *memory_requirement = sizeof(kmaterial_system_state);

    if (!state) {
        return true;
    }

    // Just so it doesn't have to be rehashed all the time.
    state->runtime_package_name = kname_create(PACKAGE_NAME_RUNTIME);

    // Keep a pointer to the renderer system state for quick access.
    const engine_system_states* states = engine_systems_get();
    state->renderer = states->renderer_system;
    state->texture_system = states->texture_system;

    state->config = *typed_config;

    state->materials = darray_reserve(kmaterial_data, config->max_material_count);
    // An array for each material will be created when a material is created.
    state->instances = darray_reserve(kmaterial_instance_data*, config->max_material_count);

    // Register a console command to dump list of materials/references.
    console_command_register("material_system_dump", 0, state, on_material_system_dump);

    return true;
}

b8 kmaterial_system_setup_defaults(struct kmaterial_system_state* state) {
    // NOTE: Material shaders have to be loaded before this point, which is handled by the renderer.

    // Load up some default materials.
    if (!create_default_standard_material(state)) {
        KFATAL("Failed to create default standard material. Application cannot continue.");
        return false;
    }

    if (!create_default_water_material(state)) {
        KFATAL("Failed to create default water material. Application cannot continue.");
        return false;
    }

    // TODO: blended materials.
    if (!create_default_blended_material(state)) {
        KFATAL("Failed to create default blended material. Application cannot continue.");
        return false;
    }

    return true;
}

void kmaterial_system_shutdown(struct kmaterial_system_state* state) {
    if (state) {
        // Destroy default materials.
        material_destroy(state, state->default_standard_material, 0);
        material_destroy(state, state->default_water_material, 1);
        // TODO: destroy this when it's implemented.
        /* material_destroy(state, state->default_blended_material, 2); */
    }
}

b8 kmaterial_system_get_handle(struct kmaterial_system_state* state, kname name, kmaterial* out_material) {
    if (state) {
        u16 length = darray_length(state->materials);
        for (u16 i = 0; i < length; ++i) {
            if (state->materials[i].name == name) {
                *out_material = i;
                return true;
            }
        }
    }

    return false;
}

b8 kmaterial_is_loaded_get(struct kmaterial_system_state* state, kmaterial material) {
    if (!state || material == KMATERIAL_INVALID) {
        return false;
    }

    return state->materials[material].state == KMATERIAL_STATE_LOADED;
}

ktexture kmaterial_texture_get(struct kmaterial_system_state* state, kmaterial material, kmaterial_texture_input tex_input) {
    if (!state || material == KMATERIAL_INVALID) {
        return false;
    }

    kmaterial_data* data = &state->materials[material];

    switch (tex_input) {
    case KMATERIAL_TEXTURE_INPUT_BASE_COLOUR:
        return data->base_colour_texture;
    case KMATERIAL_TEXTURE_INPUT_NORMAL:
        return data->normal_texture;
    case KMATERIAL_TEXTURE_INPUT_METALLIC:
        return data->metallic_texture;
    case KMATERIAL_TEXTURE_INPUT_ROUGHNESS:
        return data->roughness_texture;
    case KMATERIAL_TEXTURE_INPUT_AMBIENT_OCCLUSION:
        return data->ao_texture;
    case KMATERIAL_TEXTURE_INPUT_EMISSIVE:
        return data->emissive_texture;
    case KMATERIAL_TEXTURE_INPUT_REFLECTION:
        return data->reflection_texture;
    case KMATERIAL_TEXTURE_INPUT_REFRACTION:
        return data->refraction_texture;
    case KMATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH:
        return data->reflection_depth_texture;
    case KMATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH:
        return data->refraction_depth_texture;
    case KMATERIAL_TEXTURE_INPUT_DUDV:
        return data->dudv_texture;
    case KMATERIAL_TEXTURE_INPUT_MRA:
        return data->mra_texture;
    case KMATERIAL_TEXTURE_INPUT_COUNT:
    default:
        KERROR("Unknown material texture input.");
        return 0;
    }
}

void kmaterial_texture_set(struct kmaterial_system_state* state, kmaterial material, kmaterial_texture_input tex_input, ktexture texture) {
    if (!state || material == KMATERIAL_INVALID) {
        return;
    }

    kmaterial_data* data = &state->materials[material];

    switch (tex_input) {
    case KMATERIAL_TEXTURE_INPUT_BASE_COLOUR:
        data->base_colour_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_NORMAL:
        data->normal_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_METALLIC:
        data->metallic_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_ROUGHNESS:
        data->roughness_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_AMBIENT_OCCLUSION:
        data->ao_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_EMISSIVE:
        data->emissive_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_REFLECTION:
        data->reflection_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_REFRACTION:
        data->refraction_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH:
        data->reflection_depth_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH:
        data->refraction_depth_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_DUDV:
        data->dudv_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_MRA:
        data->mra_texture = texture;
    case KMATERIAL_TEXTURE_INPUT_COUNT:
    default:
        KERROR("Unknown material texture input.");
        return;
    }
}

b8 kmaterial_has_transparency_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT);
}
void kmaterial_has_transparency_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT, value);
}

b8 kmaterial_double_sided_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_DOUBLE_SIDED_BIT);
}
void kmaterial_double_sided_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_DOUBLE_SIDED_BIT, value);
}

b8 kmaterial_recieves_shadow_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT);
}
void kmaterial_recieves_shadow_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT, value);
}

b8 kmaterial_casts_shadow_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_CASTS_SHADOW_BIT);
}
void kmaterial_casts_shadow_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_CASTS_SHADOW_BIT, value);
}

b8 kmaterial_normal_enabled_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_NORMAL_ENABLED_BIT);
}
void kmaterial_normal_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, value);
}

b8 kmaterial_ao_enabled_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_AO_ENABLED_BIT);
}
void kmaterial_ao_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_AO_ENABLED_BIT, value);
}

b8 kmaterial_emissive_enabled_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT);
}
void kmaterial_emissive_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT, value);
}

b8 kmaterial_refraction_enabled_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT);
}
void kmaterial_refraction_enabled_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT, value);
}

f32 kmaterial_refraction_scale_get(struct kmaterial_system_state* state, kmaterial material) {
    if (!state || material == KMATERIAL_INVALID) {
        return 0;
    }

    kmaterial_data* data = &state->materials[material];
    return data->refraction_scale;
}
void material_refraction_scale_set(struct kmaterial_system_state* state, kmaterial material, f32 value) {
    if (!state || material == KMATERIAL_INVALID) {
        return;
    }

    kmaterial_data* data = &state->materials[material];
    data->refraction_scale = value;
}

b8 kmaterial_use_vertex_colour_as_base_colour_get(struct kmaterial_system_state* state, kmaterial material) {
    return kmaterial_flag_get(state, material, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT);
}
void kmaterial_use_vertex_colour_as_base_colour_set(struct kmaterial_system_state* state, kmaterial material, b8 value) {
    kmaterial_flag_set(state, material, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT, value);
}

b8 kmaterial_flag_set(struct kmaterial_system_state* state, kmaterial material, kmaterial_flag_bits flag, b8 value) {
    if (!state || material == KMATERIAL_INVALID) {
        return false;
    }

    kmaterial_data* data = &state->materials[material];

    FLAG_SET(data->flags, flag, value);
    return true;
}

b8 kmaterial_flag_get(struct kmaterial_system_state* state, kmaterial material, kmaterial_flag_bits flag) {
    if (!state || material == KMATERIAL_INVALID) {
        return false;
    }

    kmaterial_data* data = &state->materials[material];

    return FLAG_GET(data->flags, (u32)flag);
}

b8 kmaterial_system_acquire(kmaterial_system_state* state, kname name, kmaterial_instance* out_instance) {
    KASSERT_MSG(out_instance, "out_instance is required.");

    u16 material_count = darray_length(state->materials);
    for (u16 i = 0; i < material_count; ++i) {
        kmaterial_data* material = &state->materials[i];
        if (material->name == name) {
            // Material exists, create an instance and boot.
            out_instance->base_material = i;

            // Request instance and set handle.
            b8 instance_result = kmaterial_instance_create(state, out_instance->base_material, &out_instance->instance_id);
            if (!instance_result) {
                KERROR("Failed to create material instance during new material creation.");
            }
            return instance_result;
        }
    }

    // Material is not yet loaded, request it.
    KTRACE("Material system - '%s' not yet loaded. Requesting...", kname_string_get(name));

    // Setup a new handle for the material.
    kmaterial new_handle = material_handle_create(state, name);
    out_instance->base_material = new_handle;

    kmaterial_data* material = &state->materials[new_handle];
    material->state = KMATERIAL_STATE_LOADING;

    // Setup a listener.
    kasset_material_request_listener* listener = KALLOC_TYPE(kasset_material_request_listener, MEMORY_TAG_MATERIAL_INSTANCE);
    listener->state = state;
    listener->material_handle = new_handle;
    listener->instance_id = out_instance->instance_id;
    listener->needs_cleanup = true;

    // Request the asset.
    kasset_material* asset = asset_system_request_material(engine_systems_get()->asset_state, kname_string_get(name), listener, kasset_material_loaded);
    return asset != 0;
}

void kmaterial_system_release(kmaterial_system_state* state, kmaterial_instance* instance) {
    if (!state) {
        return;
    }

    // Getting the material instance data successfully performs all handle checks for
    // the material and instance. This means it's safe to destroy.
    kmaterial_data* base_material = get_material_data(state, instance->base_material);
    kmaterial_instance_data* inst = get_kmaterial_instance_data(state, *instance);
    if (base_material && inst) {
        kmaterial_instance_destroy(state, base_material, inst);
        // Invalidate both handles.
        instance->instance_id = KMATERIAL_INSTANCE_INVALID;
        instance->base_material = KMATERIAL_INVALID;
    }
}

const kmaterial_data* kmaterial_get_base_material_data(kmaterial_system_state* state, kmaterial base_material) {
    return &state->materials[base_material];
}

const kmaterial_instance_data* kmaterial_get_material_instance_data(kmaterial_system_state* state, kmaterial_instance instance) {
    return &state->instances[instance.base_material][instance.instance_id];
}

b8 kmaterial_instance_flag_set(struct kmaterial_system_state* state, kmaterial_instance instance, kmaterial_flag_bits flag, b8 value) {
    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->flags = FLAG_SET(data->flags, flag, value);

    return true;
}

b8 kmaterial_instance_flag_get(struct kmaterial_system_state* state, kmaterial_instance instance, kmaterial_flag_bits flag) {
    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    return FLAG_GET(data->flags, (u32)flag);
}

b8 kmaterial_instance_base_colour_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec4* out_value) {
    if (!out_value) {
        return false;
    }

    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->base_colour;
    return true;
}
b8 kmaterial_instance_base_colour_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec4 value) {
    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->base_colour = value;
    return true;
}

b8 kmaterial_instance_uv_offset_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec3* out_value) {
    if (!out_value) {
        return false;
    }

    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->uv_offset;
    return true;
}
b8 kmaterial_instance_uv_offset_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec3 value) {
    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->uv_offset = value;
    return true;
}

b8 kmaterial_instance_uv_scale_get(struct kmaterial_system_state* state, kmaterial_instance instance, vec3* out_value) {
    if (!out_value) {
        return false;
    }

    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->uv_scale;
    return true;
}

b8 kmaterial_instance_uv_scale_set(struct kmaterial_system_state* state, kmaterial_instance instance, vec3 value) {
    kmaterial_instance_data* data = get_kmaterial_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->uv_offset = value;
    return true;
}

kmaterial_instance kmaterial_system_get_default_standard(kmaterial_system_state* state) {
    return default_kmaterial_instance_get(state, state->default_standard_material);
}

kmaterial_instance kmaterial_system_get_default_water(kmaterial_system_state* state) {
    return default_kmaterial_instance_get(state, state->default_water_material);
}

kmaterial_instance kmaterial_system_get_default_blended(kmaterial_system_state* state) {
    return default_kmaterial_instance_get(state, state->default_blended_material);
}

void kmaterial_system_dump(kmaterial_system_state* state) {
    u32 material_count = darray_length(state->materials);
    for (u32 i = 0; i < material_count; ++i) {
        kmaterial_data* m = &state->materials[i];
        // Skip "free" slots.
        if (m->state == KMATERIAL_STATE_UNINITIALIZED) {
            continue;
        }

        kmaterial_instance_data* instance_array = state->instances[i];
        // Get a count of active instances.
        u32 instance_count = darray_length(instance_array);
        u32 active_instance_count = 0;
        for (u32 j = 0; j < instance_count; ++j) {
            if (instance_array[j].material != KMATERIAL_INVALID) {
                active_instance_count++;
            }
        }

        KINFO("Material name: '%s', active instance count = %u", kname_string_get(m->name), active_instance_count);
    }
}

static b8 create_default_standard_material(kmaterial_system_state* state) {
    KTRACE("Creating default standard material...");
    kname material_name = kname_create(KMATERIAL_STANDARD_NAME_DEFAULT);

    // Create a fake material "asset" that can be used to load the material.
    kasset_material asset = {0};
    asset.name = material_name;
    asset.type = KMATERIAL_TYPE_STANDARD;
    asset.model = KMATERIAL_MODEL_PBR;
    asset.has_transparency = KMATERIAL_DEFAULT_HAS_TRANSPARENCY;
    asset.double_sided = KMATERIAL_DEFAULT_DOUBLE_SIDED;
    asset.recieves_shadow = KMATERIAL_DEFAULT_RECIEVES_SHADOW;
    asset.casts_shadow = KMATERIAL_DEFAULT_CASTS_SHADOW;
    asset.use_vertex_colour_as_base_colour = KMATERIAL_DEFAULT_USE_VERTEX_COLOUR_AS_BASE_COLOUR;
    asset.base_colour = KMATERIAL_DEFAULT_BASE_COLOUR_VALUE; // white
    asset.normal = KMATERIAL_DEFAULT_NORMAL_VALUE;
    asset.normal_enabled = KMATERIAL_DEFAULT_NORMAL_ENABLED;
    asset.ambient_occlusion_enabled = KMATERIAL_DEFAULT_AO_ENABLED;
    asset.mra = KMATERIAL_DEFAULT_MRA_VALUE;
    asset.use_mra = KMATERIAL_DEFAULT_MRA_ENABLED;
    asset.custom_shader_name = 0;

    // Setup a new handle for the material.
    kmaterial new_material = material_handle_create(state, material_name);

    // Setup a listener.
    kasset_material_request_listener listener = {
        .state = state,
        .material_handle = new_material,
        .instance_id = KMATERIAL_INSTANCE_INVALID, // NOTE: creation of default materials does not immediately need an instance.
        .needs_cleanup = false,                    // This is done in-line, so don't need to cleanup.
    };
    kasset_material_loaded(&listener, &asset);

    // Save off a pointer to the material.
    state->default_standard_material = &state->materials[new_material];

    KTRACE("Done.");
    return true;
}

static b8 create_default_water_material(kmaterial_system_state* state) {
    KTRACE("Creating default water material...");
    kname material_name = kname_create(KMATERIAL_WATER_NAME_DEFAULT);

    // Create a fake material "asset" that can be serialized into a string.
    kasset_material asset = {0};
    asset.name = material_name;
    asset.type = KMATERIAL_TYPE_WATER;
    asset.model = KMATERIAL_MODEL_PBR;
    asset.has_transparency = false;
    asset.double_sided = false;
    asset.recieves_shadow = true;
    asset.casts_shadow = false;
    asset.use_vertex_colour_as_base_colour = false;
    asset.base_colour = vec4_one(); // white
    asset.normal = vec3_create(0.0f, 0.0f, 1.0f);
    asset.normal_enabled = true;
    asset.tiling = 0.25f;
    asset.wave_strength = 0.02f;
    asset.wave_speed = 0.03f;
    asset.custom_shader_name = 0;

    // Use default DUDV texture.
    asset.dudv_map.resource_name = kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME);
    asset.dudv_map.package_name = state->runtime_package_name;

    // Use default water normal texture.
    asset.normal_map.resource_name = kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME);
    asset.normal_map.package_name = state->runtime_package_name;
    asset.normal_enabled = true;

    // Setup a new handle for the material.
    kmaterial new_material = material_handle_create(state, material_name);

    // Setup a listener.
    kasset_material_request_listener listener = {
        .state = state,
        .material_handle = new_material,
        .instance_id = KMATERIAL_INSTANCE_INVALID, // NOTE: creation of default materials does not immediately need an instance.
        .needs_cleanup = false,                    // This is done in-line, so don't need to cleanup.
    };
    kasset_material_loaded(&listener, &asset);

    // Save off a pointer to the material.
    state->default_water_material = &state->materials[new_material];

    KTRACE("Done.");
    return true;
}

static b8 create_default_blended_material(kmaterial_system_state* state) {

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

    return true;
}

static void on_material_system_dump(console_command_context context) {
    kmaterial_system_dump(engine_systems_get()->material_system);
}

static kmaterial material_handle_create(kmaterial_system_state* state, kname name) {
    u32 resource_index = INVALID_ID;

    // Attempt to find a free "slot", or create a new entry if there isn't one.
    u32 material_count = darray_length(state->materials);
    for (u32 i = 0; i < material_count; ++i) {
        if (state->materials[i].state == KMATERIAL_STATE_UNINITIALIZED) {
            // free slot. An array should already exists for instances here.
            resource_index = i;
            break;
        }
    }
    if (resource_index == INVALID_ID) {
        resource_index = material_count;
        darray_push(state->materials, (kmaterial_data){0});
        // This also means a new entry needs to be created at this index for instances.
        kmaterial_instance_data* new_inst_array = darray_create(kmaterial_instance_data);
        darray_push(state->instances, new_inst_array);
    }

    KTRACE("Material system - new handle created at index: '%d'.", resource_index);

    return resource_index;
}

static u16 kmaterial_instance_handle_create(kmaterial_system_state* state, kmaterial material_handle) {
    u16 instance_index = KMATERIAL_INSTANCE_INVALID;

    // Attempt to find a free "slot", or create a new entry if there isn't one.
    u16 instance_count = darray_length(state->instances[material_handle]);
    for (u16 i = 0; i < instance_count; ++i) {
        if (state->instances[material_handle][i].material == KMATERIAL_INVALID) {
            // free slot. An array should already exists for instances here.
            instance_index = i;
            break;
        }
    }
    if (instance_index == KMATERIAL_INSTANCE_INVALID) {
        instance_index = instance_count;
        darray_push(state->instances[material_handle], (kmaterial_instance_data){0});
    }

    return instance_index;
}

static b8 material_create(kmaterial_system_state* state, kmaterial material_handle, const kasset_material* asset) {
    kmaterial_data* material = &state->materials[material_handle];

    material->index = material_handle;
    KTRACE("Material system - Creating material at index '%u'...", material_handle);

    // Validate the material type and model.
    material->type = asset->type;
    material->model = asset->model;

    // Base colour map or value - used by all material types.
    if (asset->base_colour_map.resource_name) {
        material->base_colour_texture = texture_acquire_from_package(asset->base_colour_map.resource_name, asset->base_colour_map.package_name, 0, 0);
    } else {
        material->base_colour = asset->base_colour;
    }

    // Normal map - used by all material types.
    if (asset->normal_map.resource_name) {
        material->normal_texture = texture_acquire_from_package(asset->normal_map.resource_name, asset->normal_map.package_name, 0, 0);
    }
    FLAG_SET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, asset->normal_enabled);

    // Water textures require normals to be enabled and a texture to exist.
    if (material->type == KMATERIAL_TYPE_WATER) {
        FLAG_SET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, true);

        // A special normal texture is also required, if not set.
        if (!material->normal_texture) {
            material->normal_texture = texture_acquire_from_package(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME), state->runtime_package_name, 0, 0);
        }
    }

    // Inputs only used by standard materials.
    if (material->type == KMATERIAL_TYPE_STANDARD) {
        // Metallic map or value
        if (asset->metallic_map.resource_name) {
            material->metallic_texture = texture_acquire_from_package(asset->metallic_map.resource_name, asset->metallic_map.package_name, 0, 0);
            material->metallic_texture_channel = asset->metallic_map.channel;
        } else {
            material->metallic = asset->metallic;
        }
        // Roughness map or value
        if (asset->roughness_map.resource_name) {
            material->roughness_texture = texture_acquire_from_package(asset->roughness_map.resource_name, asset->roughness_map.package_name, 0, 0);
            material->roughness_texture_channel = asset->roughness_map.channel;
        } else {
            material->roughness = asset->roughness;
        }
        // Ambient occlusion map or value
        if (asset->ambient_occlusion_map.resource_name) {
            material->ao_texture = texture_acquire_from_package(asset->ambient_occlusion_map.resource_name, asset->ambient_occlusion_map.package_name, 0, 0);
            material->ao_texture_channel = asset->ambient_occlusion_map.channel;
        } else {
            material->ao = asset->ambient_occlusion;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT, asset->ambient_occlusion_enabled);

        // MRA (combined metallic/roughness/ao) map or value
        if (asset->mra_map.resource_name) {
            material->mra_texture = texture_acquire_from_package(asset->mra_map.resource_name, asset->mra_map.package_name, 0, 0);
        } else {
            material->mra = asset->mra;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT, asset->use_mra);

        // Emissive map or value
        if (asset->emissive_map.resource_name) {
            material->emissive_texture = texture_acquire_from_package(asset->emissive_map.resource_name, asset->emissive_map.package_name, 0, 0);
        } else {
            material->emissive = asset->emissive;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT, asset->emissive_enabled);

        // Refraction
        // TODO: implement refraction. Any materials implementing this would obviously need to be drawn _after_ everything else in the
        // scene (opaque, then transparent front-to-back, THEN refractive materials), and likely sample the colour buffer behind it
        // when applying the effect.
        /* if (typed_resource->refraction_map.resource_name) {
            material->refraction_texture = texture_system_request(typed_resource->refraction_map.resource_name, typed_resource->refraction_map.package_name, 0, 0);
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT, typed_resource->refraction_enabled); */

        // Invalidate unused textures.
        material->reflection_texture = INVALID_KTEXTURE;
        material->reflection_depth_texture = INVALID_KTEXTURE;
        material->refraction_texture = INVALID_KTEXTURE;
        material->refraction_depth_texture = INVALID_KTEXTURE;
        material->dudv_texture = INVALID_KTEXTURE;
    } else if (material->type == KMATERIAL_TYPE_WATER) {
        // Inputs only used by water materials.

        // Derivative (dudv) map.
        if (asset->dudv_map.resource_name) {
            material->dudv_texture = texture_acquire_from_package_sync(asset->dudv_map.resource_name, asset->dudv_map.package_name);
        } else {
            material->dudv_texture = texture_acquire_from_package_sync(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME), state->runtime_package_name);
        }

        // NOTE: This material also owns (and requests) the reflect/refract (and depth
        // textures for each) as opposed to the typical route of requesting via config.

        // Get the current window size as the dimensions of these textures will be based on this.
        kwindow* window = engine_active_window_get();
        // TODO: should probably cut this in half.
        u32 tex_width = window->width;
        u32 tex_height = window->height;

        const char* material_name = kname_string_get(material->name);

        // Create reflection/refraction textures.
        {
            char* formatted_name = string_format("__%s_reflection_colour__", material_name);
            ktexture_load_options options = {
                .name = kname_create(formatted_name),
                .type = KTEXTURE_TYPE_2D,
                .mip_levels = 1,
                .width = tex_width,
                .height = tex_height,
                .format = KPIXEL_FORMAT_RGBA8,
                .auto_release = true,
                .is_writeable = true,
                .multiframe_buffering = true,
            };
            string_free(formatted_name);

            material->reflection_texture = texture_acquire_with_options_sync(options);
            if (material->reflection_texture == INVALID_KTEXTURE) {
                return false;
            }
        }

        {
            char* formatted_name = string_format("__%s_reflection_depth__", material_name);
            ktexture_load_options options = {
                .name = kname_create(formatted_name),
                .type = KTEXTURE_TYPE_2D,
                .mip_levels = 1,
                .width = tex_width,
                .height = tex_height,
                .format = KPIXEL_FORMAT_RGBA8,
                .auto_release = true,
                .is_writeable = true,
                .multiframe_buffering = true,
                .is_depth = true,
                .is_stencil = false,
            };
            string_free(formatted_name);

            material->reflection_depth_texture = texture_acquire_with_options_sync(options);
            if (material->reflection_depth_texture == INVALID_KTEXTURE) {
                return false;
            }
        }

        {
            char* formatted_name = string_format("__%s_refraction_colour__", material_name);
            ktexture_load_options options = {
                .name = kname_create(formatted_name),
                .type = KTEXTURE_TYPE_2D,
                .mip_levels = 1,
                .width = tex_width,
                .height = tex_height,
                .format = KPIXEL_FORMAT_RGBA8,
                .auto_release = true,
                .is_writeable = true,
                .multiframe_buffering = true,
            };
            string_free(formatted_name);
            material->refraction_texture = texture_acquire_with_options_sync(options);
            if (material->refraction_texture == INVALID_KTEXTURE) {
                return false;
            }
        }

        {
            char* formatted_name = string_format("__%s_refraction_depth__", material_name);
            ktexture_load_options options = {
                .name = kname_create(formatted_name),
                .type = KTEXTURE_TYPE_2D,
                .mip_levels = 1,
                .width = tex_width,
                .height = tex_height,
                .format = KPIXEL_FORMAT_RGBA8,
                .auto_release = true,
                .is_writeable = true,
                .multiframe_buffering = true,
                .is_depth = true,
                .is_stencil = false,
            };
            string_free(formatted_name);

            material->refraction_depth_texture = texture_acquire_with_options_sync(options);
            if (material->reflection_depth_texture == INVALID_KTEXTURE) {
                return false;
            }
        }

        // Listen for window resizes, as these must trigger a resize of our reflect/refract
        // texture render targets. This should only be active while the material is loaded.
        if (!event_register(EVENT_CODE_WINDOW_RESIZED, material, material_on_event)) {
            KERROR("Unable to register material for window resize event. See logs for details.");
            return false;
        }

        // Additional properties.
        material->tiling = asset->tiling;
        material->wave_speed = asset->wave_speed;
        material->wave_strength = asset->wave_strength;
    }

    // Set remaining flags
    FLAG_SET(material->flags, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT, asset->has_transparency);
    FLAG_SET(material->flags, KMATERIAL_FLAG_DOUBLE_SIDED_BIT, asset->double_sided);
    FLAG_SET(material->flags, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT, asset->recieves_shadow);
    FLAG_SET(material->flags, KMATERIAL_FLAG_CASTS_SHADOW_BIT, asset->casts_shadow);
    FLAG_SET(material->flags, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT, asset->use_vertex_colour_as_base_colour);

    // Register the base material with the renderer.
    kmaterial_renderer_register_base(engine_systems_get()->material_renderer, material);

    material->state = KMATERIAL_STATE_LOADED;

    return true;
}

static void material_destroy(kmaterial_system_state* state, kmaterial_data* material, u32 material_index) {
    KASSERT_MSG(material, "Tried to destroy null material.");

    // Immediately mark it as unavailable for use.
    material->state = KMATERIAL_STATE_UNINITIALIZED;

    // Release texture resources/references
    if (material->base_colour_texture) {
        texture_release(material->base_colour_texture);
    }
    if (material->normal_texture) {
        texture_release(material->normal_texture);
    }
    if (material->metallic_texture) {
        texture_release(material->metallic_texture);
    }
    if (material->roughness_texture) {
        texture_release(material->roughness_texture);
    }
    if (material->ao_texture) {
        texture_release(material->ao_texture);
    }
    if (material->mra_texture) {
        texture_release(material->mra_texture);
    }
    if (material->emissive_texture) {
        texture_release(material->emissive_texture);
    }
    if (material->dudv_texture) {
        texture_release(material->dudv_texture);
    }
    if (material->reflection_texture) {
        texture_release(material->reflection_texture);
    }
    if (material->reflection_depth_texture) {
        texture_release(material->reflection_depth_texture);
    }
    if (material->refraction_texture) {
        texture_release(material->refraction_texture);
    }
    if (material->refraction_depth_texture) {
        texture_release(material->refraction_depth_texture);
    }

    if (material->type == KMATERIAL_TYPE_WATER) {
        // Immediately stop listening for resize events.
        if (!event_unregister(EVENT_CODE_WINDOW_RESIZED, material, material_on_event)) {
            // Nothing to really do about it, but warn the user.
            KWARN("Unable to unregister material for resize event. See logs for details.");
        }
    }

    // Unregister the material.
    kmaterial_renderer_unregister_base(engine_systems_get()->material_renderer, material);

    // Destroy instances.
    u32 instance_count = darray_length(state->instances[material_index]);
    for (u32 i = 0; i < instance_count; ++i) {
        kmaterial_instance_data* inst = &state->instances[material_index][i];
        if (inst->material != KMATERIAL_INVALID) {
            kmaterial_instance_destroy(state, material, inst);
        }
    }

    kzero_memory(material, sizeof(kmaterial_data));

    // Mark the material slot as free for another material to be loaded.
    material->state = KMATERIAL_STATE_UNINITIALIZED;
    material->group_id = INVALID_ID;
}

static b8 kmaterial_instance_create(kmaterial_system_state* state, kmaterial base_material, u16* out_instance_id) {
    *out_instance_id = kmaterial_instance_handle_create(state, base_material);
    if (*out_instance_id == KMATERIAL_INSTANCE_INVALID) {
        KERROR("Failed to create material instance handle. Instance will not be created.");
        return false;
    }

    kmaterial_data* material = &state->materials[base_material];
    kmaterial_instance_data* inst = &state->instances[base_material][*out_instance_id];
    inst->state = KMATERIAL_INSTANCE_STATE_UNINITIALIZED;

    // Only request resources and copy base material properties if the base material is actually loaded and ready to go.
    if (material->state == KMATERIAL_STATE_LOADED) {
        inst->state = KMATERIAL_INSTANCE_STATE_LOADING;

        // Register the material instance with the material renderer.
        kmaterial_renderer_register_instance(engine_systems_get()->material_renderer, material, inst);

        // Take a copy of the base material properties.
        inst->flags = material->flags;
        inst->uv_scale = material->uv_scale;
        inst->uv_offset = material->uv_offset;
        inst->base_colour = material->base_colour;

        inst->state = KMATERIAL_INSTANCE_STATE_LOADED;
    } else {
        // Base material NOT loaded, handle in async callback from asset system.
        inst->state = KMATERIAL_INSTANCE_STATE_LOADING;
    }

    return true;
}

static void kmaterial_instance_destroy(kmaterial_system_state* state, kmaterial_data* base_material, kmaterial_instance_data* inst) {
    if (base_material && inst && inst->material != KMATERIAL_INVALID) {

        // Unregister the material instance with the material renderer.
        kmaterial_renderer_unregister_instance(engine_systems_get()->material_renderer, base_material, inst);

        kzero_memory(inst, sizeof(kmaterial_instance_data));

        // Make sure to invalidate the entry.
        inst->material = KMATERIAL_INVALID;
        inst->per_draw_id = INVALID_ID;
    }
}

static void kasset_material_loaded(void* listener, kasset_material* asset) {
    kasset_material_request_listener* listener_inst = (kasset_material_request_listener*)listener;
    kmaterial_system_state* state = listener_inst->state;

    KTRACE("Material system - Resource '%s' loaded. Creating material...", kname_string_get(asset->name));

    // Create the base material.
    if (!material_create(state, listener_inst->material_handle, asset)) {
        KERROR("Failed to create material. See logs for details.");
        return;
    }

    // Create an instance of it if one is required.
    if (listener_inst->instance_id != KMATERIAL_INSTANCE_INVALID) {
        if (!kmaterial_instance_create(state, listener_inst->material_handle, &listener_inst->instance_id)) {
            KERROR("Failed to create material instance during new material creation.");
        }
    }

    // Iterate the instances of the material and see if any were waiting on the asset to load.
    kmaterial_data* material = &state->materials[listener_inst->material_handle];

    u32 instance_count = darray_length(state->instances[listener_inst->material_handle]);
    for (u32 i = 0; i < instance_count; ++i) {
        kmaterial_instance_data* inst = &state->instances[listener_inst->material_handle][i];
        if (inst->state == KMATERIAL_INSTANCE_STATE_LOADING) {

            // Register the material instance with the material renderer.
            kmaterial_renderer_register_instance(engine_systems_get()->material_renderer, material, inst);

            // Take a copy of the base material properties.
            inst->flags = material->flags;
            inst->uv_scale = material->uv_scale;
            inst->uv_offset = material->uv_offset;
            inst->base_colour = material->base_colour;

            inst->state = KMATERIAL_INSTANCE_STATE_LOADED;
        }
    }

    // Free the listener if needed.
    if (listener_inst->needs_cleanup) {
        KFREE_TYPE(listener_inst, kasset_material_request_listener, MEMORY_TAG_MATERIAL_INSTANCE);
    }
}

static kmaterial_instance default_kmaterial_instance_get(kmaterial_system_state* state, kmaterial_data* base_material) {
    kmaterial_instance instance = {0};
    instance.base_material = base_material->index;

    // Get an instance of it.
    if (!kmaterial_instance_create(state, instance.base_material, &instance.instance_id)) {
        // Fatal here because if this happens on a default material, something is seriously borked.
        KFATAL("Failed to obtain an instance of the default '%s' material.", kname_string_get(base_material->name));

        // Invalidate the handles.
        instance.base_material = KMATERIAL_INVALID;
        instance.instance_id = KMATERIAL_INSTANCE_INVALID;
    }

    return instance;
}

static kmaterial_data* get_material_data(kmaterial_system_state* state, kmaterial material_handle) {
    if (!state) {
        return 0;
    }

    // Verify handle first.
    if (material_handle == KMATERIAL_INVALID) {
        KWARN("Attempted to get material data with an invalid base material. Nothing to do.");
        return 0;
    }

    return &state->materials[material_handle];
}

static kmaterial_instance_data* get_kmaterial_instance_data(kmaterial_system_state* state, kmaterial_instance instance) {
    if (!state) {
        return 0;
    }

    kmaterial_data* material = get_material_data(state, instance.base_material);
    if (!material) {
        KERROR("Attempted to get material instance data for a non-existant material. See logs for details.");
        return 0;
    }

    // Verify handle first.
    if (instance.instance_id == KMATERIAL_INSTANCE_INVALID) {
        KWARN("Attempted to get material instance with an invalid instance handle. Nothing to do.");
        return 0;
    }

    return &state->instances[instance.base_material][instance.instance_id];
}

static b8 material_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_WINDOW_RESIZED) {
        // Resize textures to match new frame buffer.
        // TODO: Scale texture to be smaller based on some global setting.
        u16 width = context.data.u16[0];
        u16 height = context.data.u16[1];

        // const kwindow* window = sender;
        kmaterial_data* material = listener_inst;

        if (material->reflection_texture != INVALID_KTEXTURE) {
            if (!texture_resize(material->reflection_texture, width, height, true)) {
                KERROR("Failed to resize reflection colour texture for material.");
            }
        }
        if (material->reflection_depth_texture != INVALID_KTEXTURE) {
            if (!texture_resize(material->reflection_depth_texture, width, height, true)) {
                KERROR("Failed to resize reflection depth texture for material.");
            }
        }

        if (material->refraction_texture != INVALID_KTEXTURE) {
            if (!texture_resize(material->refraction_texture, width, height, true)) {
                KERROR("Failed to resize refraction colour texture for material.");
            }
        }
        if (material->refraction_depth_texture != INVALID_KTEXTURE) {
            if (!texture_resize(material->refraction_depth_texture, width, height, true)) {
                KERROR("Failed to resize refraction depth texture for material.");
            }
        }
    }

    // Allow other systems to pick up event.
    return false;
}
