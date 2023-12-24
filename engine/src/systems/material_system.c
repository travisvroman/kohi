#include "material_system.h"

#include "containers/darray.h"
#include "containers/hashtable.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/kvar.h"
#include "core/logger.h"
#include "defines.h"
#include "math/kmath.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.h"
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

#ifndef PBR_MAP_COUNT
#define PBR_MAP_COUNT 10
#endif

// Samplers
const u32 SAMP_ALBEDO = 0;
const u32 SAMP_NORMAL = 1;
const u32 SAMP_METALLIC = 2;
const u32 SAMP_ROUGHNESS = 3;
const u32 SAMP_AO = 4;
// The number of textures for a PBR material
#define PBR_MATERIAL_TEXTURE_COUNT 5
const u32 SAMP_SHADOW_MAP = 5;
const u32 SAMP_SHADOW_MAP_0 = 6;
const u32 SAMP_SHADOW_MAP_1 = 7;
const u32 SAMP_SHADOW_MAP_2 = 8;
// The number of shadow maps for a PBR material. TODO: Should be configurable.
#define PBR_SHADOW_MAP_TEXTURE_COUNT 4
const u32 SAMP_IBL_CUBE = 9;

#define MAX_SHADOW_CASCADE_COUNT 4

#define TERRAIN_PER_MATERIAL_SAMP_COUNT 5
// 5 maps per material for PBR. Allocate enough slots for all materials. Also one more for irradiance map.
const u32 TERRAIN_SAMP_COUNT = 5 + (TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT);
const u32 SAMP_TERRAIN_SHADOW_MAP = TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT;
const u32 SAMP_TERRAIN_IRRADIANCE_MAP = 4 + (TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT);

typedef struct pbr_shader_uniform_locations {
    u16 projection;
    u16 view;
    u16 cascade_splits;
    u16 view_position;
    u16 properties;
    u16 ibl_cube_texture;
    u16 material_texures;
    u16 shadow_textures;
    u16 light_space_0;
    u16 light_space_1;
    u16 light_space_2;
    u16 light_space_3;
    u16 model;
    u16 render_mode;
    u16 use_pcf;
    u16 bias;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;
} pbr_shader_uniform_locations;

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
    u16 cascade_splits;
    u16 view_position;
    u16 model;
    u16 render_mode;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;

    u16 properties;
    u16 ibl_cube_texture;
    u16 shadow_textures;
    u16 light_space_0;
    u16 light_space_1;
    u16 light_space_2;
    u16 light_space_3;
    u16 material_texures;
    u16 use_pcf;
    u16 bias;
} terrain_shader_locations;

typedef struct material_system_state {
    material_system_config config;

    material default_pbr_material;
    material default_terrain_material;

    // Array of registered materials.
    material* registered_materials;

    // Hashtable for material lookups.
    hashtable registered_material_table;

    // Known locations for terrain shader.
    terrain_shader_locations terrain_locations;
    u32 terrain_shader_id;

    // Known locations for the PBR shader.
    pbr_shader_uniform_locations pbr_locations;
    u32 pbr_shader_id;

    // The current irradiance cubemap texture to be used.
    texture* irradiance_cube_texture;

    // The current shadow textures to be used for the next draw.
    texture* shadow_textures[MAX_SHADOW_CASCADE_COUNT];

    mat4 directional_light_space[MAX_SHADOW_CASCADE_COUNT];

    i32 use_pcf;
} material_system_state;

typedef struct material_reference {
    u64 reference_count;
    u32 handle;
    b8 auto_release;
} material_reference;

static material_system_state* state_ptr = 0;

static b8 create_default_pbr_material(material_system_state* state);
static b8 create_default_terrain_material(material_system_state* state);
static b8 load_material(material_config* config, material* m);
static void destroy_material(material* m);

static b8 assign_map(texture_map* map, const material_map* config, const char* material_name, texture* default_tex);

static b8 material_system_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_KVAR_CHANGED) {
        if (strings_equali("use_pcf", context.data.c)) {
            kvar_int_get("use_pcf", &state_ptr->use_pcf);
            return true;
        }
    }

    return false;
}

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

    state_ptr->pbr_shader_id = INVALID_ID;
    state_ptr->pbr_locations.view = INVALID_ID_U16;
    state_ptr->pbr_locations.projection = INVALID_ID_U16;
    state_ptr->pbr_locations.properties = INVALID_ID_U16;
    state_ptr->pbr_locations.ibl_cube_texture = INVALID_ID_U16;
    state_ptr->pbr_locations.material_texures = INVALID_ID_U16;
    state_ptr->pbr_locations.shadow_textures = INVALID_ID_U16;
    state_ptr->pbr_locations.cascade_splits = INVALID_ID_U16;
    state_ptr->pbr_locations.model = INVALID_ID_U16;
    state_ptr->pbr_locations.render_mode = INVALID_ID_U16;
    state_ptr->pbr_locations.properties = INVALID_ID_U16;
    state_ptr->pbr_locations.light_space_0 = INVALID_ID_U16;
    state_ptr->pbr_locations.light_space_1 = INVALID_ID_U16;
    state_ptr->pbr_locations.light_space_2 = INVALID_ID_U16;
    state_ptr->pbr_locations.light_space_3 = INVALID_ID_U16;
    state_ptr->pbr_locations.use_pcf = INVALID_ID_U16;
    state_ptr->pbr_locations.bias = INVALID_ID_U16;

    state_ptr->terrain_locations.projection = INVALID_ID_U16;
    state_ptr->terrain_locations.view = INVALID_ID_U16;
    state_ptr->terrain_locations.cascade_splits = INVALID_ID_U16;
    state_ptr->terrain_locations.view_position = INVALID_ID_U16;
    state_ptr->terrain_locations.model = INVALID_ID_U16;
    state_ptr->terrain_locations.render_mode = INVALID_ID_U16;
    state_ptr->terrain_locations.dir_light = INVALID_ID_U16;
    state_ptr->terrain_locations.p_lights = INVALID_ID_U16;
    state_ptr->terrain_locations.num_p_lights = INVALID_ID_U16;
    state_ptr->terrain_locations.properties = INVALID_ID_U16;
    state_ptr->terrain_locations.material_texures = INVALID_ID_U16;
    state_ptr->terrain_locations.ibl_cube_texture = INVALID_ID_U16;
    state_ptr->terrain_locations.shadow_textures = INVALID_ID_U16;
    state_ptr->terrain_locations.light_space_0 = INVALID_ID_U16;
    state_ptr->terrain_locations.light_space_1 = INVALID_ID_U16;
    state_ptr->terrain_locations.light_space_2 = INVALID_ID_U16;
    state_ptr->terrain_locations.light_space_3 = INVALID_ID_U16;
    state_ptr->terrain_locations.use_pcf = INVALID_ID_U16;
    state_ptr->terrain_locations.bias = INVALID_ID_U16;

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

    if (!create_default_pbr_material(state_ptr)) {
        KFATAL("Failed to create default PBR material. Application cannot continue.");
        return false;
    }

    if (!create_default_terrain_material(state_ptr)) {
        KFATAL("Failed to create default terrain material. Application cannot continue.");
        return false;
    }

    // Get the uniform indices.
    // Save off the locations for known types for quick lookups.
    shader* s = shader_system_get("Shader.PBRMaterial");
    state_ptr->pbr_shader_id = s->id;
    state_ptr->pbr_locations.projection = shader_system_uniform_location(s, "projection");
    state_ptr->pbr_locations.view = shader_system_uniform_location(s, "view");
    state_ptr->pbr_locations.light_space_0 = shader_system_uniform_location(s, "light_space_0");
    state_ptr->pbr_locations.light_space_1 = shader_system_uniform_location(s, "light_space_1");
    state_ptr->pbr_locations.light_space_2 = shader_system_uniform_location(s, "light_space_2");
    state_ptr->pbr_locations.light_space_3 = shader_system_uniform_location(s, "light_space_3");
    state_ptr->pbr_locations.cascade_splits = shader_system_uniform_location(s, "cascade_splits");
    state_ptr->pbr_locations.view_position = shader_system_uniform_location(s, "view_position");
    state_ptr->pbr_locations.properties = shader_system_uniform_location(s, "properties");
    state_ptr->pbr_locations.material_texures = shader_system_uniform_location(s, "material_textures");
    state_ptr->pbr_locations.shadow_textures = shader_system_uniform_location(s, "shadow_textures");
    state_ptr->pbr_locations.ibl_cube_texture = shader_system_uniform_location(s, "ibl_cube_texture");
    state_ptr->pbr_locations.model = shader_system_uniform_location(s, "model");
    state_ptr->pbr_locations.render_mode = shader_system_uniform_location(s, "mode");
    state_ptr->pbr_locations.dir_light = shader_system_uniform_location(s, "dir_light");
    state_ptr->pbr_locations.p_lights = shader_system_uniform_location(s, "p_lights");
    state_ptr->pbr_locations.num_p_lights = shader_system_uniform_location(s, "num_p_lights");
    state_ptr->pbr_locations.use_pcf = shader_system_uniform_location(s, "use_pcf");
    state_ptr->pbr_locations.bias = shader_system_uniform_location(s, "bias");

    s = shader_system_get("Shader.Builtin.Terrain");
    state_ptr->terrain_shader_id = s->id;
    state_ptr->terrain_locations.projection = shader_system_uniform_location(s, "projection");
    state_ptr->terrain_locations.view = shader_system_uniform_location(s, "view");
    state_ptr->terrain_locations.light_space_0 = shader_system_uniform_location(s, "light_space_0");
    state_ptr->terrain_locations.light_space_1 = shader_system_uniform_location(s, "light_space_1");
    state_ptr->terrain_locations.light_space_2 = shader_system_uniform_location(s, "light_space_2");
    state_ptr->terrain_locations.light_space_3 = shader_system_uniform_location(s, "light_space_3");
    state_ptr->terrain_locations.cascade_splits = shader_system_uniform_location(s, "cascade_splits");
    state_ptr->terrain_locations.view_position = shader_system_uniform_location(s, "view_position");
    state_ptr->terrain_locations.model = shader_system_uniform_location(s, "model");
    state_ptr->terrain_locations.render_mode = shader_system_uniform_location(s, "mode");
    state_ptr->terrain_locations.dir_light = shader_system_uniform_location(s, "dir_light");
    state_ptr->terrain_locations.p_lights = shader_system_uniform_location(s, "p_lights");
    state_ptr->terrain_locations.num_p_lights = shader_system_uniform_location(s, "num_p_lights");

    state_ptr->terrain_locations.properties = shader_system_uniform_location(s, "properties");
    state_ptr->terrain_locations.material_texures = shader_system_uniform_location(s, "material_texures");
    state_ptr->terrain_locations.shadow_textures = shader_system_uniform_location(s, "shadow_textures");
    state_ptr->terrain_locations.ibl_cube_texture = shader_system_uniform_location(s, "ibl_cube_texture");
    state_ptr->terrain_locations.use_pcf = shader_system_uniform_location(s, "use_pcf");
    state_ptr->terrain_locations.bias = shader_system_uniform_location(s, "bias");

    // Grab the default cubemap texture as the irradiance texture.
    state_ptr->irradiance_cube_texture = texture_system_get_default_cube_texture();

    // Assign some defualts.
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        state_ptr->directional_light_space[i] = mat4_identity();
    }

    // Add a kvar to track PCF filtering enabled/disabled.
    kvar_int_create("use_pcf", 1);  // On by default.
    kvar_int_get("use_pcf", &state_ptr->use_pcf);

    event_register(EVENT_CODE_KVAR_CHANGED, 0, material_system_on_event);

    return true;
}

void material_system_shutdown(void* state) {
    material_system_state* s = (material_system_state*)state;
    if (s) {
        event_unregister(EVENT_CODE_KVAR_CHANGED, 0, material_system_on_event);

        // Invalidate all materials in the array.
        u32 count = s->config.max_material_count;
        for (u32 i = 0; i < count; ++i) {
            if (s->registered_materials[i].id != INVALID_ID) {
                destroy_material(&s->registered_materials[i]);
            }
        }

        // Destroy the default material.
        destroy_material(&s->default_pbr_material);
        destroy_material(&s->default_terrain_material);
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

        // 5 maps per material for PBR. Allocate enough slots for all materials. Also one more for irradiance map.
        m->maps = darray_reserve(texture_map, TERRAIN_SAMP_COUNT);
        darray_length_set(m->maps, TERRAIN_SAMP_COUNT);

        // Map names and default fallback textures.
        const char* map_names[TERRAIN_PER_MATERIAL_SAMP_COUNT] = {"diffuse", "normal", "metallic", "roughness", "ao"};
        texture* default_textures[TERRAIN_PER_MATERIAL_SAMP_COUNT] = {
            texture_system_get_default_diffuse_texture(),
            texture_system_get_default_normal_texture(),
            texture_system_get_default_metallic_texture(),
            texture_system_get_default_roughness_texture(),
            texture_system_get_default_ao_texture(),
        };
        // Use the default material for unassigned slots.
        material* default_material = material_system_get_default_pbr();

        // PBR properties and maps for each material.
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

            // Maps, 5 for PBR. Diffuse, spec, normal.
            for (u32 map_idx = 0; map_idx < TERRAIN_PER_MATERIAL_SAMP_COUNT; ++map_idx) {
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
                if (!assign_map(&m->maps[(material_idx * TERRAIN_PER_MATERIAL_SAMP_COUNT) + map_idx], &map_config, m->name, default_textures[map_idx])) {
                    KERROR("Failed to assign '%s' texture map for terrain material index %u", map_names[map_idx], material_idx);
                    return false;
                }
            }
        }

        // Shadow maps can't be configured, so set them up here.
        for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
            map_config.name = "shadow_map";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_TERRAIN_SHADOW_MAP + i], &map_config, m->name, texture_system_get_default_diffuse_texture())) {
                KERROR("Failed to assign '%s' texture map for terrain shadow map.", map_config.name);
                return false;
            }
        }

        // IBL - cubemap for irradiance
        {
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "ibl_cube";
            map_config.texture_name = "";
            // Always assigned to the last index.
            if (!assign_map(&m->maps[SAMP_TERRAIN_IRRADIANCE_MAP], &map_config, m->name, texture_system_get_default_cube_texture())) {
                KERROR("Failed to assign '%s' texture map for terrain irradiance map.", map_config.name);
                return false;
            }
        }

        // Release reference materials.
        for (u32 i = 0; i < material_count; ++i) {
            material_system_release(material_names[i]);
        }
        kfree(materials, sizeof(material*) * material_count, MEMORY_TAG_ARRAY);

        // NOTE: 4 materials * 5 maps per will still be loaded in order (albedo/norm/met/rough/ao per mat)
        // Next group will be shadow mappings
        // Last irradiance map

        // Setup a configuration to get instance resources for this material.
        shader_instance_resource_config instance_resource_config = {0};
        // Map count for this type is known.
        instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

        // Material textures
        shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
        mat_textures->uniform_location = state_ptr->terrain_locations.material_texures;
        mat_textures->texture_map_count = TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT;
        mat_textures->texture_maps = kallocate(sizeof(texture_map*) * mat_textures->texture_map_count, MEMORY_TAG_ARRAY);
        // Per material
        for (u32 i = 0; i < TERRAIN_MAX_MATERIAL_COUNT; ++i) {
            mat_textures->texture_maps[SAMP_ALBEDO + i] = &m->maps[SAMP_ALBEDO + i];
            mat_textures->texture_maps[SAMP_NORMAL + i] = &m->maps[SAMP_NORMAL + i];
            mat_textures->texture_maps[SAMP_METALLIC + i] = &m->maps[SAMP_METALLIC + i];
            mat_textures->texture_maps[SAMP_ROUGHNESS + i] = &m->maps[SAMP_ROUGHNESS + i];
            mat_textures->texture_maps[SAMP_AO + i] = &m->maps[SAMP_AO + i];
        }

        // Shadow textures
        shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
        shadow_textures->uniform_location = state_ptr->terrain_locations.shadow_textures;
        shadow_textures->texture_map_count = PBR_SHADOW_MAP_TEXTURE_COUNT;
        shadow_textures->texture_maps = kallocate(sizeof(texture_map*) * shadow_textures->texture_map_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < 4; ++i) {
            shadow_textures->texture_maps[i] = &m->maps[SAMP_TERRAIN_SHADOW_MAP + i];
        }

        // IBL cube texture
        shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
        ibl_cube_texture->uniform_location = state_ptr->terrain_locations.ibl_cube_texture;
        ibl_cube_texture->texture_map_count = 1;
        ibl_cube_texture->texture_maps = kallocate(sizeof(texture_map*) * ibl_cube_texture->texture_map_count, MEMORY_TAG_ARRAY);
        ibl_cube_texture->texture_maps[0] = &m->maps[SAMP_TERRAIN_IRRADIANCE_MAP];
        // Map count for this type is known.
        instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

        // Acquire the resources
        b8 result = renderer_shader_instance_resources_acquire(s, &instance_resource_config, &m->internal_id);
        if (!result) {
            KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
        }

        // Clean up the uniform configs.
        for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
            shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
            kfree(ucfg, sizeof(shader_instance_uniform_texture_config) * ucfg->texture_map_count, MEMORY_TAG_ARRAY);
        }
        kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

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
    if (strings_equali(config->name, DEFAULT_PBR_MATERIAL_NAME)) {
        return &state_ptr->default_pbr_material;
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
    if (strings_equali(name, DEFAULT_PBR_MATERIAL_NAME) || strings_equali(name, DEFAULT_TERRAIN_MATERIAL_NAME)) {
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
    return material_system_get_default_pbr();
}

material* material_system_get_default_pbr(void) {
    if (state_ptr) {
        return &state_ptr->default_pbr_material;
    }

    KFATAL("material_system_get_default_pbr called before system is initialized.");
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

b8 material_system_apply_global(u32 shader_id, const struct frame_data* p_frame_data, const mat4* projection, const mat4* view, const vec4* ambient_colour, const vec3* view_position, u32 render_mode) {
    shader* s = shader_system_get_by_id(shader_id);
    if (!s) {
        return false;
    }
    if (s->render_frame_number == p_frame_data->renderer_frame_number && s->draw_index == p_frame_data->draw_index) {
        return true;
    }

    if (shader_id == state_ptr->terrain_shader_id) {
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.projection, projection));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.view, view));
        // TODO: set cascade splits like dir lights and shadow map, etc.
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.cascade_splits, ambient_colour));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.view_position, view_position));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.render_mode, &render_mode));
        // Light space for shadow mapping. Per cascade
        for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.light_space_0 + i, &state_ptr->directional_light_space[i]));
        }
        // Directional light - global for this shader..
        directional_light* dir_light = light_system_directional_light_get();
        if (dir_light) {
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.dir_light, &dir_light->data));
        } else {
            directional_light_data data = {0};
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.dir_light, &data));
        }
        // Global shader options.
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.use_pcf, &state_ptr->use_pcf));

        // HACK: Read this in from somewhere (or have global setter?);
        f32 bias = 0.00005f;
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.bias, &bias));
    } else if (shader_id == state_ptr->pbr_shader_id) {
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.projection, projection));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.view, view));
        // TODO: set cascade splits like dir lights and shadow map, etc.
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.cascade_splits, ambient_colour));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.view_position, view_position));
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.render_mode, &render_mode));
        // Light space for shadow mapping. Per cascade
        for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.light_space_0 + i, &state_ptr->directional_light_space[i]));
        }
        // Global shader options.
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.use_pcf, &state_ptr->use_pcf));

        // HACK: Read this in from somewhere (or have global setter?);
        f32 bias = 0.00005f;
        MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.bias, &bias));
    } else {
        KERROR("material_system_apply_global(): Unrecognized shader id '%d' ", shader_id);
        return false;
    }
    MATERIAL_APPLY_OR_FAIL(shader_system_apply_global(true));

    // Sync the frame number.
    s->render_frame_number = p_frame_data->renderer_frame_number;
    return true;
}

b8 material_system_apply_instance(material* m, struct frame_data* p_frame_data, b8 needs_update) {
    // Apply instance-level uniforms.
    MATERIAL_APPLY_OR_FAIL(shader_system_bind_instance(m->internal_id));
    if (needs_update) {
        if (m->shader_id == state_ptr->pbr_shader_id) {
            // PBR shader
            // Properties
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.properties, m->properties));
            // Maps
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.material_texures, SAMP_ALBEDO, &m->maps[SAMP_ALBEDO]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.material_texures, SAMP_NORMAL, &m->maps[SAMP_NORMAL]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.material_texures, SAMP_METALLIC, &m->maps[SAMP_METALLIC]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.material_texures, SAMP_ROUGHNESS, &m->maps[SAMP_ROUGHNESS]));
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.material_texures, SAMP_AO, &m->maps[SAMP_AO]));

            // Shadow Maps
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                u32 index = SAMP_SHADOW_MAP + i;
                m->maps[index].texture = state_ptr->shadow_textures[i] ? state_ptr->shadow_textures[i] : texture_system_get_default_diffuse_texture();
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->pbr_locations.shadow_textures, index, &m->maps[index]));
            }

            // Irradience map
            m->maps[SAMP_IBL_CUBE].texture = m->irradiance_texture ? m->irradiance_texture : state_ptr->irradiance_cube_texture;
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.ibl_cube_texture, &m->maps[SAMP_IBL_CUBE]));

            // Directional light.
            directional_light* dir_light = light_system_directional_light_get();
            if (dir_light) {
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.dir_light, &dir_light->data));
            } else {
                directional_light_data data = {0};
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.dir_light, &data));
            }
            // Point lights.
            u32 p_light_count = light_system_point_light_count();
            if (p_light_count) {
                point_light* p_lights = p_frame_data->allocator.allocate(sizeof(point_light) * p_light_count);
                light_system_point_lights_get(p_lights);

                point_light_data* p_light_datas = p_frame_data->allocator.allocate(sizeof(point_light_data) * p_light_count);
                for (u32 i = 0; i < p_light_count; ++i) {
                    p_light_datas[i] = p_lights[i].data;
                }

                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.p_lights, p_light_datas));
            }

            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->pbr_locations.num_p_lights, &p_light_count));

        } else if (m->shader_id == state_ptr->terrain_shader_id) {
            // Apply material maps
            u32 material_map_count = TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT;
            for (u32 i = 0; i < material_map_count; ++i) {
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->terrain_locations.material_texures, i, &m->maps[i]));
            }

            // NOTE: apply other maps separately.

            // Shadow Maps
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                u32 index = SAMP_TERRAIN_SHADOW_MAP + i;
                m->maps[index].texture = state_ptr->shadow_textures[i] ? state_ptr->shadow_textures[i] : texture_system_get_default_diffuse_texture();
                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(state_ptr->terrain_locations.shadow_textures, index, &m->maps[index]));
            }

            // Irradience map
            m->maps[SAMP_TERRAIN_IRRADIANCE_MAP].texture = m->irradiance_texture ? m->irradiance_texture : state_ptr->irradiance_cube_texture;
            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.ibl_cube_texture, &m->maps[SAMP_TERRAIN_IRRADIANCE_MAP]));

            // Apply properties.
            shader_system_uniform_set_by_location(state_ptr->terrain_locations.properties, m->properties);

            // TODO: Duplicating above... move this to its own function, perhaps.

            // Point lights.
            u32 p_light_count = light_system_point_light_count();
            if (p_light_count) {
                point_light* p_lights = p_frame_data->allocator.allocate(sizeof(point_light) * p_light_count);
                light_system_point_lights_get(p_lights);

                point_light_data* p_light_datas = p_frame_data->allocator.allocate(sizeof(point_light_data) * p_light_count);
                for (u32 i = 0; i < p_light_count; ++i) {
                    p_light_datas[i] = p_lights[i].data;
                }

                MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.p_lights, p_light_datas));
            }

            MATERIAL_APPLY_OR_FAIL(shader_system_uniform_set_by_location(state_ptr->terrain_locations.num_p_lights, &p_light_count));
        } else {
            KERROR("material_system_apply_instance(): Unrecognized shader id '%d' on shader '%s'.", m->shader_id, m->name);
            return false;
        }
    }
    MATERIAL_APPLY_OR_FAIL(shader_system_apply_instance(needs_update));

    return true;
}

b8 material_system_apply_local(material* m, const mat4* model) {
    if (m->shader_id == state_ptr->pbr_shader_id) {
        return shader_system_uniform_set_by_location(state_ptr->pbr_locations.model, model);
    } else if (m->shader_id == state_ptr->terrain_shader_id) {
        return shader_system_uniform_set_by_location(state_ptr->terrain_locations.model, model);
    }

    KERROR("Unrecognized shader id '%d'", m->shader_id);
    return false;
}

b8 material_system_shadow_map_set(texture* shadow_texture, u8 index) {
    if (shadow_texture) {
        state_ptr->shadow_textures[index] = shadow_texture;
    }

    return true;
}

b8 material_system_irradiance_set(texture* irradiance_cube_texture) {
    if (irradiance_cube_texture) {
        if (irradiance_cube_texture->type != TEXTURE_TYPE_CUBE) {
            KWARN("material_system_irradiance_set requires parameter irradiance_cube_texture to be a cubemap type texture. Nothing was done.");
            return false;
        }

        state_ptr->irradiance_cube_texture = irradiance_cube_texture;
    } else {
        // Null sets us back to default state.
        state_ptr->irradiance_cube_texture = texture_system_get_default_cube_texture();
    }
    return true;
}

void material_system_directional_light_space_set(mat4 directional_light_space, u8 index) {
    state_ptr->directional_light_space[index] = directional_light_space;
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

    if (config->type == MATERIAL_TYPE_PBR) {
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

        // Maps. PBR expects a albedo, normal, metallic, roughness and AO.
        m->maps = darray_reserve(texture_map, PBR_MAP_COUNT);
        darray_length_set(m->maps, PBR_MAP_COUNT);
        u32 configure_map_count = darray_length(config->maps);

        b8 albedo_assigned = false;
        b8 norm_assigned = false;
        b8 metallic_assigned = false;
        b8 roughness_assigned = false;
        b8 ao_assigned = false;
        b8 ibl_cube_assigned = false;
        for (u32 i = 0; i < configure_map_count; ++i) {
            if (strings_equali(config->maps[i].name, "albedo")) {
                if (!assign_map(&m->maps[SAMP_ALBEDO], &config->maps[i], m->name, texture_system_get_default_diffuse_texture())) {
                    return false;
                }
                albedo_assigned = true;
            } else if (strings_equali(config->maps[i].name, "normal")) {
                if (!assign_map(&m->maps[SAMP_NORMAL], &config->maps[i], m->name, texture_system_get_default_normal_texture())) {
                    return false;
                }
                norm_assigned = true;
            } else if (strings_equali(config->maps[i].name, "metallic")) {
                if (!assign_map(&m->maps[SAMP_METALLIC], &config->maps[i], m->name, texture_system_get_default_metallic_texture())) {
                    return false;
                }
                metallic_assigned = true;
            } else if (strings_equali(config->maps[i].name, "roughness")) {
                if (!assign_map(&m->maps[SAMP_ROUGHNESS], &config->maps[i], m->name, texture_system_get_default_roughness_texture())) {
                    return false;
                }
                roughness_assigned = true;
            } else if (strings_equali(config->maps[i].name, "ao")) {
                if (!assign_map(&m->maps[SAMP_AO], &config->maps[i], m->name, texture_system_get_default_ao_texture())) {
                    return false;
                }
                ao_assigned = true;
            } else if (strings_equali(config->maps[i].name, "ibl_cube")) {
                // TODO: just loading a default cube map for now. Need to get this from the probe instead.
                if (!assign_map(&m->maps[SAMP_IBL_CUBE], &config->maps[i], m->name, texture_system_get_default_cube_texture())) {
                    return false;
                }
                ibl_cube_assigned = true;
            }

            // NOTE: Ignore unexpected maps.
        }
        if (!albedo_assigned) {
            // Make sure the diffuse map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "albedo";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_ALBEDO], &map_config, m->name, texture_system_get_default_diffuse_texture())) {
                return false;
            }
        }
        if (!norm_assigned) {
            // Make sure the normal map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "normal";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_NORMAL], &map_config, m->name, texture_system_get_default_normal_texture())) {
                return false;
            }
        }

        if (!metallic_assigned) {
            // Make sure the metallic map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "metallic";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_METALLIC], &map_config, m->name, texture_system_get_default_metallic_texture())) {
                return false;
            }
        }
        if (!roughness_assigned) {
            // Make sure the roughness map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "roughness";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_ROUGHNESS], &map_config, m->name, texture_system_get_default_roughness_texture())) {
                return false;
            }
        }
        if (!ao_assigned) {
            // Make sure the AO map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "ao";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_AO], &map_config, m->name, texture_system_get_default_ao_texture())) {
                return false;
            }
        }
        if (!ibl_cube_assigned) {
            // Make sure the cube map is always assigned.
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_REPEAT;
            map_config.name = "ibl_cube";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_IBL_CUBE], &map_config, m->name, texture_system_get_default_cube_texture())) {
                return false;
            }
        }

        // Shadow maps can't be configured, so set them up here.
        for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
            material_map map_config = {0};
            map_config.filter_mag = map_config.filter_min = TEXTURE_FILTER_MODE_LINEAR;
            map_config.repeat_u = map_config.repeat_v = map_config.repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
            map_config.name = "shadow_map";
            map_config.texture_name = "";
            if (!assign_map(&m->maps[SAMP_SHADOW_MAP + i], &map_config, m->name, texture_system_get_default_diffuse_texture())) {
                return false;
            }
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

    // Gather a list of pointers to texture maps;
    // Send it off to the renderer to acquire resources.
    shader* s = 0;
    shader_instance_resource_config instance_resource_config = {0};
    if (config->type == MATERIAL_TYPE_PBR) {
        s = shader_system_get(config->shader_name ? config->shader_name : "Shader.PBRMaterial");
        // Map count for this type is known.
        instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
        instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

        // Material textures
        shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
        mat_textures->uniform_location = state_ptr->pbr_locations.material_texures;
        mat_textures->texture_map_count = PBR_MATERIAL_TEXTURE_COUNT;
        mat_textures->texture_maps = kallocate(sizeof(texture_map*) * mat_textures->texture_map_count, MEMORY_TAG_ARRAY);
        mat_textures->texture_maps[SAMP_ALBEDO] = &m->maps[SAMP_ALBEDO];
        mat_textures->texture_maps[SAMP_NORMAL] = &m->maps[SAMP_NORMAL];
        mat_textures->texture_maps[SAMP_METALLIC] = &m->maps[SAMP_METALLIC];
        mat_textures->texture_maps[SAMP_ROUGHNESS] = &m->maps[SAMP_ROUGHNESS];
        mat_textures->texture_maps[SAMP_AO] = &m->maps[SAMP_AO];

        // Shadow textures
        shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
        shadow_textures->uniform_location = state_ptr->pbr_locations.shadow_textures;
        shadow_textures->texture_map_count = PBR_SHADOW_MAP_TEXTURE_COUNT;
        shadow_textures->texture_maps = kallocate(sizeof(texture_map*) * shadow_textures->texture_map_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < 4; ++i) {
            shadow_textures->texture_maps[i] = &m->maps[SAMP_SHADOW_MAP + i];
        }

        // IBL cube texture
        shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
        ibl_cube_texture->uniform_location = state_ptr->pbr_locations.ibl_cube_texture;
        ibl_cube_texture->texture_map_count = 1;
        ibl_cube_texture->texture_maps = kallocate(sizeof(texture_map*) * ibl_cube_texture->texture_map_count, MEMORY_TAG_ARRAY);
        ibl_cube_texture->texture_maps[0] = &m->maps[SAMP_IBL_CUBE];
    } else if (config->type == MATERIAL_TYPE_CUSTOM) {
        // Custom materials.
        if (!config->shader_name) {
            KERROR("Shader name is required for custom material types. Material '%s' failed to load", m->name);
            return false;
        }
        s = shader_system_get(config->shader_name);
        u32 global_sampler_count = s->global_uniform_sampler_count;
        u32 instance_sampler_count = s->instance_uniform_sampler_count;

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
            shader_uniform* u = &s->uniforms[s->instance_sampler_indices[i]];
            shader_instance_uniform_texture_config* uniform_config = &instance_resource_config.uniform_configs[i];
            uniform_config->uniform_location = u->location;
            uniform_config->texture_map_count = KMAX(u->array_length, 1);
            uniform_config->texture_maps = kallocate(sizeof(texture_map*) * uniform_config->texture_map_count, MEMORY_TAG_ARRAY);
            for (u32 j = 0; j < uniform_config->texture_map_count; ++j) {
                uniform_config->texture_maps[j] = &m->maps[i + map_offset];
            }
        }
    } else {
        KERROR("Unknown material type: %d. Material '%s' cannot be loaded.", config->type, m->name);
        return false;
    }

    // Acquire the resources.
    b8 result = renderer_shader_instance_resources_acquire(s, &instance_resource_config, &m->internal_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for material '%s'.", m->name);
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg, sizeof(shader_instance_uniform_texture_config) * ucfg->texture_map_count, MEMORY_TAG_ARRAY);
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    return result;
}

static void destroy_material(material* m) {
    // KTRACE("Destroying material '%s'...", m->name);

    u32 length = darray_length(m->maps);
    for (u32 i = 0; i < length; ++i) {
        // Release texture references.
        if (m->maps[i].texture) {
            texture_system_release(m->maps[i].texture->name);
        }
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

static b8 create_default_pbr_material(material_system_state* state) {
    kzero_memory(&state->default_pbr_material, sizeof(material));
    state->default_pbr_material.id = INVALID_ID;
    state->default_pbr_material.type = MATERIAL_TYPE_PBR;
    state->default_pbr_material.generation = INVALID_ID;
    string_ncopy(state->default_pbr_material.name, DEFAULT_PBR_MATERIAL_NAME, MATERIAL_NAME_MAX_LENGTH);
    // TODO: material PBR properties
    state->default_pbr_material.property_struct_size = sizeof(material_phong_properties);
    state->default_pbr_material.properties = kallocate(sizeof(material_phong_properties), MEMORY_TAG_MATERIAL_INSTANCE);
    material_phong_properties* properties = (material_phong_properties*)state->default_pbr_material.properties;
    properties->diffuse_colour = vec4_one();  // white
    properties->shininess = 8.0f;
    state->default_pbr_material.maps = darray_reserve(texture_map, PBR_MAP_COUNT);
    darray_length_set(state->default_pbr_material.maps, PBR_MAP_COUNT);
    for (u32 i = 0; i < PBR_MAP_COUNT; ++i) {
        texture_map* map = &state->default_pbr_material.maps[i];
        map->filter_magnify = map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
        map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_REPEAT;
    }

    // Change the clamp mode on the default shadow map to border.
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        texture_map* ssm = &state->default_pbr_material.maps[SAMP_SHADOW_MAP + i];
        ssm->repeat_u = ssm->repeat_v = ssm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    }

    state->default_pbr_material.maps[SAMP_ALBEDO].texture = texture_system_get_default_texture();
    state->default_pbr_material.maps[SAMP_NORMAL].texture = texture_system_get_default_normal_texture();
    state->default_pbr_material.maps[SAMP_METALLIC].texture = texture_system_get_default_metallic_texture();
    state->default_pbr_material.maps[SAMP_ROUGHNESS].texture = texture_system_get_default_roughness_texture();
    state->default_pbr_material.maps[SAMP_AO].texture = texture_system_get_default_ao_texture();
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        state->default_pbr_material.maps[SAMP_SHADOW_MAP + i].texture = texture_system_get_default_diffuse_texture();
    }
    state->default_pbr_material.maps[SAMP_IBL_CUBE].texture = texture_system_get_default_cube_texture();

    // Setup a configuration to get instance resources for this material.
    material* m = &state->default_pbr_material;
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
    instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Material textures
    shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
    mat_textures->uniform_location = state_ptr->pbr_locations.material_texures;
    mat_textures->texture_map_count = PBR_MATERIAL_TEXTURE_COUNT;
    mat_textures->texture_maps = kallocate(sizeof(texture_map*) * mat_textures->texture_map_count, MEMORY_TAG_ARRAY);
    mat_textures->texture_maps[SAMP_ALBEDO] = &m->maps[SAMP_ALBEDO];
    mat_textures->texture_maps[SAMP_NORMAL] = &m->maps[SAMP_NORMAL];
    mat_textures->texture_maps[SAMP_METALLIC] = &m->maps[SAMP_METALLIC];
    mat_textures->texture_maps[SAMP_ROUGHNESS] = &m->maps[SAMP_ROUGHNESS];
    mat_textures->texture_maps[SAMP_AO] = &m->maps[SAMP_AO];

    // Shadow textures
    shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
    shadow_textures->uniform_location = state_ptr->pbr_locations.shadow_textures;
    shadow_textures->texture_map_count = PBR_SHADOW_MAP_TEXTURE_COUNT;
    shadow_textures->texture_maps = kallocate(sizeof(texture_map*) * shadow_textures->texture_map_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < 4; ++i) {
        shadow_textures->texture_maps[i] = &m->maps[SAMP_SHADOW_MAP + i];
    }

    // IBL cube texture
    shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
    ibl_cube_texture->uniform_location = state_ptr->pbr_locations.ibl_cube_texture;
    ibl_cube_texture->texture_map_count = 1;
    ibl_cube_texture->texture_maps = kallocate(sizeof(texture_map*) * ibl_cube_texture->texture_map_count, MEMORY_TAG_ARRAY);
    ibl_cube_texture->texture_maps[0] = &m->maps[SAMP_IBL_CUBE];

    shader* s = shader_system_get_by_id(state_ptr->pbr_shader_id);
    if (!renderer_shader_instance_resources_acquire(s, &instance_resource_config, &state->default_pbr_material.internal_id)) {
        KFATAL("Failed to acquire renderer resources for default PBR material. Application cannot continue.");
        return false;
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg, sizeof(shader_instance_uniform_texture_config) * ucfg->texture_map_count, MEMORY_TAG_ARRAY);
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Make sure to assign the shader id.
    state->default_pbr_material.shader_id = s->id;

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
    state->default_terrain_material.maps = darray_reserve(texture_map, TERRAIN_SAMP_COUNT);
    darray_length_set(state->default_terrain_material.maps, TERRAIN_SAMP_COUNT);
    state->default_terrain_material.maps[SAMP_ALBEDO].texture = texture_system_get_default_texture();
    state->default_terrain_material.maps[SAMP_NORMAL].texture = texture_system_get_default_normal_texture();
    state->default_terrain_material.maps[SAMP_METALLIC].texture = texture_system_get_default_metallic_texture();
    state->default_terrain_material.maps[SAMP_ROUGHNESS].texture = texture_system_get_default_roughness_texture();
    state->default_terrain_material.maps[SAMP_AO].texture = texture_system_get_default_ao_texture();
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        state->default_terrain_material.maps[SAMP_TERRAIN_SHADOW_MAP + i].texture = texture_system_get_default_diffuse_texture();
    }

    // Change the clamp mode on the default shadow map to border.
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        texture_map* ssm = &state->default_terrain_material.maps[SAMP_TERRAIN_SHADOW_MAP + i];
        ssm->repeat_u = ssm->repeat_v = ssm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    }

    // NOTE: PBR materials are required for terrains.
    // NOTE: 4 materials * 5 maps per will still be loaded in order (albedo/norm/met/rough/ao per mat)
    // Next group will be shadow mappings
    // Last irradiance map

    // Setup a configuration to get instance resources for this material.
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
    instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Material textures
    material* m = &state_ptr->default_terrain_material;
    shader_instance_uniform_texture_config* mat_textures = &instance_resource_config.uniform_configs[0];
    mat_textures->uniform_location = state_ptr->terrain_locations.material_texures;
    mat_textures->texture_map_count = TERRAIN_PER_MATERIAL_SAMP_COUNT * TERRAIN_MAX_MATERIAL_COUNT;
    mat_textures->texture_maps = kallocate(sizeof(texture_map*) * mat_textures->texture_map_count, MEMORY_TAG_ARRAY);
    // Per material
    for (u32 i = 0; i < TERRAIN_MAX_MATERIAL_COUNT; ++i) {
        mat_textures->texture_maps[SAMP_ALBEDO + i] = &m->maps[SAMP_ALBEDO + i];
        mat_textures->texture_maps[SAMP_NORMAL + i] = &m->maps[SAMP_NORMAL + i];
        mat_textures->texture_maps[SAMP_METALLIC + i] = &m->maps[SAMP_METALLIC + i];
        mat_textures->texture_maps[SAMP_ROUGHNESS + i] = &m->maps[SAMP_ROUGHNESS + i];
        mat_textures->texture_maps[SAMP_AO + i] = &m->maps[SAMP_AO + i];
    }

    // Shadow textures
    shader_instance_uniform_texture_config* shadow_textures = &instance_resource_config.uniform_configs[1];
    shadow_textures->uniform_location = state_ptr->terrain_locations.shadow_textures;
    shadow_textures->texture_map_count = PBR_SHADOW_MAP_TEXTURE_COUNT;
    shadow_textures->texture_maps = kallocate(sizeof(texture_map*) * shadow_textures->texture_map_count, MEMORY_TAG_ARRAY);
    for (u32 i = 0; i < 4; ++i) {
        shadow_textures->texture_maps[i] = &m->maps[SAMP_TERRAIN_SHADOW_MAP + i];
    }

    // IBL cube texture
    shader_instance_uniform_texture_config* ibl_cube_texture = &instance_resource_config.uniform_configs[2];
    ibl_cube_texture->uniform_location = state_ptr->terrain_locations.ibl_cube_texture;
    ibl_cube_texture->texture_map_count = 1;
    ibl_cube_texture->texture_maps = kallocate(sizeof(texture_map*) * ibl_cube_texture->texture_map_count, MEMORY_TAG_ARRAY);
    ibl_cube_texture->texture_maps[0] = &m->maps[SAMP_TERRAIN_IRRADIANCE_MAP];
    // Map count for this type is known.
    instance_resource_config.uniform_config_count = 3;  // NOTE: This includes material maps, shadow maps and irradiance map.
    instance_resource_config.uniform_configs = kallocate(sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Acquire the resources
    shader* s = shader_system_get_by_id(state_ptr->terrain_shader_id);
    b8 result = renderer_shader_instance_resources_acquire(s, &instance_resource_config, &state->default_terrain_material.internal_id);
    if (!result) {
        KERROR("Failed to acquire renderer resources for default terrain material '%s'.");
    }

    // Clean up the uniform configs.
    for (u32 i = 0; i < instance_resource_config.uniform_config_count; ++i) {
        shader_instance_uniform_texture_config* ucfg = &instance_resource_config.uniform_configs[i];
        kfree(ucfg, sizeof(shader_instance_uniform_texture_config) * ucfg->texture_map_count, MEMORY_TAG_ARRAY);
    }
    kfree(instance_resource_config.uniform_configs, sizeof(shader_instance_uniform_texture_config) * instance_resource_config.uniform_config_count, MEMORY_TAG_ARRAY);

    // Make sure to assign the shader id.
    state->default_terrain_material.shader_id = s->id;

    return true;
}
