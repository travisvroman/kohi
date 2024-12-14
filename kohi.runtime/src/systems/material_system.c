#include "material_system.h"

#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <core_render_types.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <serializers/kasset_material_serializer.h>
#include <serializers/kasset_shader_serializer.h>
#include <strings/kname.h>

#include "core/console.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kvar.h"
#include "kresources/kresource_types.h"
#include "renderer/renderer_frontend.h"
#include "runtime_defines.h"
#include "systems/kresource_system.h"
#include "systems/light_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

#define MATERIAL_SHADER_NAME_STANDARD "Shader.MaterialStandard"
#define MATERIAL_SHADER_NAME_WATER "Shader.MaterialWater"
#define MATERIAL_SHADER_NAME_BLENDED "Shader.MaterialBlended"

// Texture indices

// Standard material
const u32 MAT_STANDARD_IDX_BASE_COLOUR = 0;
const u32 MAT_STANDARD_IDX_NORMAL = 1;
const u32 MAT_STANDARD_IDX_METALLIC = 2;
const u32 MAT_STANDARD_IDX_ROUGHNESS = 3;
const u32 MAT_STANDARD_IDX_AO = 4;
const u32 MAT_STANDARD_IDX_MRA = 5;
const u32 MAT_STANDARD_IDX_EMISSIVE = 6;

#define MATERIAL_STANDARD_TEXTURE_COUNT 7
#define MATERIAL_STANDARD_SAMPLER_COUNT 7

// Water material
const u32 MAT_WATER_IDX_REFLECTION = 0;
const u32 MAT_WATER_IDX_REFRACTION = 1;
const u32 MAT_WATER_IDX_REFRACTION_DEPTH = 2;
const u32 MAT_WATER_IDX_DUDV = 3;
const u32 MAT_WATER_IDX_NORMAL = 4;

#define MATERIAL_WATER_TEXTURE_COUNT 5
#define MATERIAL_WATER_SAMPLER_COUNT 5

// TODO:
// - Water type material
// - Blended type material
// - Material models (unlit, PBR, Phong, etc.)
// - Shader interaction/binding/applying for material instances

// Represents the data for a single instance of a material.
// This can be thought of as "per-draw" data.
typedef struct material_instance_data {
    // A unique id used for handle validation.
    u64 unique_id;

    // A handle to the material to which this instance references.
    khandle material;

    // Multiplied by albedo/diffuse texture. Overrides the value set in the base material.
    vec4 base_colour;

    // Overrides the flags set in the base material.
    kmaterial_flags flags;

    // Added to UV coords of vertex data.
    vec3 uv_offset;
    // Multiplied against uv coords of vertex data.
    vec3 uv_scale;

    // Shader draw id for per-draw uniforms.
    u32 per_draw_id;

    // The generation of the material instance data. Incremented each time it is updated.
    // INVALID_ID_U16 means unloaded. Synced within the renderer backend as needed.
    // Can roll back around to 0.
    u16 generation;
} material_instance_data;

// Represents a base material.
// This can be thought of as "per-group" data.
typedef struct material_data {
    kname name;
    /** @brief The material type. Ultimately determines what shader the material is rendered with. */
    kmaterial_type type;
    /** @brief The material lighting model. */
    kmaterial_model model;
    // A unique id used for handle validation.
    u64 unique_id;

    vec4 base_colour;
    kresource_texture* base_colour_texture;

    vec3 normal;
    kresource_texture* normal_texture;

    f32 metallic;
    kresource_texture* metallic_texture;
    texture_channel metallic_texture_channel;

    f32 roughness;
    kresource_texture* roughness_texture;
    texture_channel roughness_texture_channel;

    f32 ao;
    kresource_texture* ao_texture;
    texture_channel ao_texture_channel;

    vec4 emissive;
    kresource_texture* emissive_texture;
    f32 emissive_texture_intensity;

    kresource_texture* refraction_texture;
    f32 refraction_scale;

    kresource_texture* reflection_texture;
    kresource_texture* reflection_depth_texture;
    kresource_texture* dudv_texture;
    kresource_texture* refraction_depth_texture;

    vec3 mra;
    /**
     * @brief This is a combined texture holding metallic/roughness/ambient occlusion all in one texture.
     * This is a more efficient replacement for using those textures individually. Metallic is sampled
     * from the Red channel, roughness from the Green channel, and ambient occlusion from the Blue channel.
     * Alpha is ignored.
     */
    kresource_texture* mra_texture;

    // Base set of flags for the material. Copied to the material instance when created.
    kmaterial_flags flags;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;

    // Affects the strength of waves for a water type material.
    f32 wave_strength;
    // Affects wave movement speed for a water material.
    f32 wave_speed;
    f32 tiling;

    // Shader group id for per-group uniforms.
    u32 group_id;

    // The generation of the material data. Incremented each time it is updated.
    // INVALID_ID_U16 means unloaded. Synced within the renderer backend as needed.
    // Can roll back around to 0.
    u16 generation;
} material_data;

// ======================================================
// Standard Material
// ======================================================

typedef enum material_standard_flag_bits {
    MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX = 0x0001,
    MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX = 0x0002,
    MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX = 0x0004,
    MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX = 0x0008,
    MATERIAL_STANDARD_FLAG_USE_AO_TEX = 0x0010,
    MATERIAL_STANDARD_FLAG_USE_MRA_TEX = 0x0020,
    MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX = 0x0040
} material_standard_flag_bits;

typedef u32 material_standard_flags;

typedef struct material_standard_shader_locations {
    // Per frame
    u16 material_frame_ubo;
    u16 shadow_texture;
    u16 irradiance_cube_textures;
    u16 shadow_sampler;
    u16 irradiance_sampler;

    // Per group
    u16 material_textures;
    u16 material_samplers;
    u16 material_group_ubo;

    // Per draw.
    u16 material_draw_ubo;
} material_standard_shader_locations;

// Standard Material Per-frame UBO data
typedef struct material_standard_frame_uniform_data {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 projection;
    mat4 views[MATERIAL_MAX_VIEWS];
    vec4 view_positions[MATERIAL_MAX_VIEWS];
    f32 cascade_splits[MATERIAL_MAX_SHADOW_CASCADES];
    f32 shadow_bias;
    u32 render_mode;
    u32 use_pcf;
    f32 delta_time;
    f32 game_time;
    vec3 padding;
} material_standard_frame_uniform_data;

// Standard Material Per-group UBO
typedef struct material_standard_group_uniform_data {
    directional_light_data dir_light;                     // 48 bytes
    point_light_data p_lights[MATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
    u32 num_p_lights;
    /** @brief The material lighting model. */
    u32 lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    // Texture use flags
    u32 tex_flags;

    vec4 base_colour;
    vec4 emissive;
    vec3 normal;
    f32 metallic;
    vec3 mra;
    f32 roughness;

    // Added to UV coords of vertex data. Overridden by instance data.
    vec3 uv_offset;
    f32 ao;
    // Multiplied against uv coords of vertex data. Overridden by instance data.
    vec3 uv_scale;
    f32 emissive_texture_intensity;

    f32 refraction_scale;
    // Packed texture channels for various maps requiring it.
    u32 texture_channels; // [metallic, roughness, ao, unused]
    vec2 padding;
} material_standard_group_uniform_data;

// Standard Material Per-draw UBO
typedef struct material_standard_draw_uniform_data {
    mat4 model;
    vec4 clipping_plane;
    u32 view_index;
    u32 irradiance_cubemap_index;
    vec2 padding;
} material_standard_draw_uniform_data;

// ======================================================
// Water Material
// ======================================================

// Water Material Per-frame UBO data
typedef struct material_water_frame_uniform_data {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 projection;
    mat4 views[MATERIAL_MAX_VIEWS];
    f32 cascade_splits[MATERIAL_MAX_SHADOW_CASCADES];
    vec4 view_positions[MATERIAL_MAX_VIEWS];
    f32 shadow_bias;
    u32 render_mode;
    u32 use_pcf;
    f32 delta_time;
    f32 game_time;
    vec3 padding;
} material_water_frame_uniform_data;

// Water Material Per-group UBO
typedef struct material_water_group_uniform_data {
    directional_light_data dir_light;                     // 48 bytes
    point_light_data p_lights[MATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
    u32 num_p_lights;
    /** @brief The material lighting model. */
    u32 lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    f32 padding;
} material_water_group_uniform_data;

// Water Material Per-draw UBO
typedef struct material_water_draw_uniform_data {
    mat4 model;
    u32 irradiance_cubemap_index;
    u32 view_index;
    vec2 padding;
    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;
    f32 padding2;
} material_water_draw_uniform_data;

typedef struct material_water_shader_locations {
    // Per frame
    u16 material_frame_ubo;
    u16 shadow_texture;
    u16 irradiance_cube_textures;
    u16 shadow_sampler;
    u16 irradiance_sampler;

    // Per group
    u16 material_group_ubo;
    u16 material_textures;
    u16 material_samplers;

    // Per draw.
    u16 material_draw_ubo;
} material_water_shader_locations;

/**
 * The structure which holds state for the entire material system.
 */
typedef struct material_system_state {
    material_system_config config;

    // darray of materials, indexed by material khandle resource index.
    material_data* materials;
    // darray of material instances, indexed first by material khandle index, then by instance khandle index.
    material_instance_data** instances;

    // A default material for each type of material.
    khandle default_standard_material;
    khandle default_water_material;
    khandle default_blended_material;

    // Cached handles for various material types' shaders.
    khandle material_standard_shader;
    material_standard_shader_locations standard_material_locations;

    khandle material_water_shader;
    material_water_shader_locations water_material_locations;

    khandle material_blended_shader;

    // Keep a pointer to the renderer state for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct kresource_system_state* resource_system;

    // Runtime package name pre-hashed and kept here for convenience.
    kname runtime_package_name;
} material_system_state;

// Holds data for a material instance request.
typedef struct material_request_listener {
    khandle material_handle;
    khandle* instance_handle;
    material_system_state* state;
} material_request_listener;

static b8 create_default_standard_material(material_system_state* state);
static b8 create_default_water_material(material_system_state* state);
static b8 create_default_blended_material(material_system_state* state);
static void on_material_system_dump(console_command_context context);
static khandle get_shader_for_material_type(const material_system_state* state, kmaterial_type type);
static khandle material_handle_create(material_system_state* state, kname name);
static khandle material_instance_handle_create(material_system_state* state, khandle material_handle);
static b8 material_create(material_system_state* state, khandle material_handle, const kresource_material* typed_resource);
static void material_destroy(material_system_state* state, khandle* material_handle);
static b8 material_instance_create(material_system_state* state, khandle base_material, khandle* out_instance_handle);
static void material_instance_destroy(material_system_state* state, khandle base_material, khandle* instance_handle);
static void material_resource_loaded(kresource* resource, void* listener);
static material_instance default_material_instance_get(material_system_state* state, khandle base_material);
static material_instance_data* get_instance_data(material_system_state* state, material_instance instance);
static void increment_generation(u16* generation);
static b8 material_on_event(u16 code, void* sender, void* listener_inst, event_context data);

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

    // Just so it doesn't have to be rehashed all the time.
    state->runtime_package_name = kname_create(PACKAGE_NAME_RUNTIME);

    // Keep a pointer to the renderer system state for quick access.
    const engine_system_states* states = engine_systems_get();
    state->renderer = states->renderer_system;
    state->resource_system = states->kresource_state;
    state->texture_system = states->texture_system;

    state->config = *typed_config;

    state->materials = darray_create(material_data);
    // An array for each material will be created when a material is created.
    state->instances = darray_create(material_instance_data*);

    // Get default material shaders.

    // Standard material shader.
    {
        kname mat_std_shader_name = kname_create(MATERIAL_SHADER_NAME_STANDARD);
        kasset_shader mat_std_shader = {0};
        mat_std_shader.base.name = mat_std_shader_name;
        mat_std_shader.base.package_name = state->runtime_package_name;
        mat_std_shader.base.generation = INVALID_ID;
        mat_std_shader.base.type = KASSET_TYPE_SHADER;
        mat_std_shader.base.meta.version = 1;
        mat_std_shader.depth_test = true;
        mat_std_shader.depth_write = true;
        mat_std_shader.stencil_test = false;
        mat_std_shader.stencil_write = false;
        mat_std_shader.colour_write = true;
        mat_std_shader.colour_read = false;
        mat_std_shader.supports_wireframe = true;
        mat_std_shader.cull_mode = FACE_CULL_MODE_BACK;
        mat_std_shader.max_groups = state->config.max_material_count;
        mat_std_shader.max_draw_ids = state->config.max_instance_count;
        mat_std_shader.topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        mat_std_shader.stage_count = 2;
        mat_std_shader.stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, mat_std_shader.stage_count);
        mat_std_shader.stages[0].type = SHADER_STAGE_VERTEX;
        mat_std_shader.stages[0].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_shader.stages[0].source_asset_name = "MaterialStandard_vert";
        mat_std_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_std_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_shader.stages[1].source_asset_name = "MaterialStandard_frag";

        mat_std_shader.attribute_count = 5;
        mat_std_shader.attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, mat_std_shader.attribute_count);
        mat_std_shader.attributes[0].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_shader.attributes[0].name = "in_position";

        mat_std_shader.attributes[1].name = "in_normal";
        mat_std_shader.attributes[1].type = SHADER_ATTRIB_TYPE_FLOAT32_3;
        mat_std_shader.attributes[2].name = "in_texcoord";
        mat_std_shader.attributes[2].type = SHADER_ATTRIB_TYPE_FLOAT32_2;
        mat_std_shader.attributes[3].name = "in_colour";
        mat_std_shader.attributes[3].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_std_shader.attributes[4].name = "in_tangent";
        mat_std_shader.attributes[4].type = SHADER_ATTRIB_TYPE_FLOAT32_3;

        mat_std_shader.uniform_count = 9;
        mat_std_shader.uniforms = KALLOC_TYPE_CARRAY(kasset_shader_uniform, mat_std_shader.uniform_count);

        // per_frame
        mat_std_shader.uniforms[0].name = "material_frame_ubo";
        mat_std_shader.uniforms[0].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[0].size = sizeof(material_standard_frame_uniform_data);
        mat_std_shader.uniforms[0].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;

        mat_std_shader.uniforms[1].name = "shadow_texture";
        mat_std_shader.uniforms[1].type = SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY;
        mat_std_shader.uniforms[1].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;

        mat_std_shader.uniforms[2].name = "irradiance_cube_textures";
        mat_std_shader.uniforms[2].type = SHADER_UNIFORM_TYPE_TEXTURE_CUBE;
        mat_std_shader.uniforms[2].array_size = MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT;
        mat_std_shader.uniforms[2].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;

        mat_std_shader.uniforms[3].name = "shadow_sampler";
        mat_std_shader.uniforms[3].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[3].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;

        mat_std_shader.uniforms[4].name = "irradiance_sampler";
        mat_std_shader.uniforms[4].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[4].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        // per_group
        mat_std_shader.uniforms[5].name = "material_textures";
        mat_std_shader.uniforms[5].type = SHADER_UNIFORM_TYPE_TEXTURE_2D;
        mat_std_shader.uniforms[5].array_size = MATERIAL_STANDARD_TEXTURE_COUNT;
        mat_std_shader.uniforms[5].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;

        mat_std_shader.uniforms[6].name = "material_samplers";
        mat_std_shader.uniforms[6].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[6].array_size = MATERIAL_STANDARD_SAMPLER_COUNT;
        mat_std_shader.uniforms[6].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;

        mat_std_shader.uniforms[7].name = "material_group_ubo";
        mat_std_shader.uniforms[7].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[7].size = sizeof(material_standard_group_uniform_data);
        mat_std_shader.uniforms[7].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        // per_draw
        mat_std_shader.uniforms[8].name = "material_draw_ubo";
        mat_std_shader.uniforms[8].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[8].size = sizeof(material_standard_draw_uniform_data);
        mat_std_shader.uniforms[8].frequency = SHADER_UPDATE_FREQUENCY_PER_DRAW;

        // Serialize
        const char* config_source = kasset_shader_serialize((kasset*)&mat_std_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_std_shader.stages, kasset_shader_stage, mat_std_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_std_shader.attributes, kasset_shader_attribute, mat_std_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_std_shader.uniforms, kasset_shader_uniform, mat_std_shader.uniform_count);
        kzero_memory(&mat_std_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        state->material_standard_shader = shader_system_get_from_source(mat_std_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            state->standard_material_locations.material_frame_ubo = shader_system_uniform_location(state->material_standard_shader, kname_create("material_frame_ubo"));
            state->standard_material_locations.shadow_texture = shader_system_uniform_location(state->material_standard_shader, kname_create("shadow_texture"));
            state->standard_material_locations.irradiance_cube_textures = shader_system_uniform_location(state->material_standard_shader, kname_create("irradiance_cube_textures"));
            state->standard_material_locations.shadow_sampler = shader_system_uniform_location(state->material_standard_shader, kname_create("shadow_sampler"));
            state->standard_material_locations.irradiance_sampler = shader_system_uniform_location(state->material_standard_shader, kname_create("irradiance_sampler"));

            // Per group
            state->standard_material_locations.material_textures = shader_system_uniform_location(state->material_standard_shader, kname_create("material_textures"));
            state->standard_material_locations.material_samplers = shader_system_uniform_location(state->material_standard_shader, kname_create("material_samplers"));
            state->standard_material_locations.material_group_ubo = shader_system_uniform_location(state->material_standard_shader, kname_create("material_group_ubo"));

            // Per draw.
            state->standard_material_locations.material_draw_ubo = shader_system_uniform_location(state->material_standard_shader, kname_create("material_draw_ubo"));
        }
    }

    // Water material shader.
    {
        kname mat_water_shader_name = kname_create(MATERIAL_SHADER_NAME_WATER);
        kasset_shader mat_water_shader = {0};
        mat_water_shader.base.name = mat_water_shader_name;
        mat_water_shader.base.package_name = state->runtime_package_name;
        mat_water_shader.base.generation = INVALID_ID;
        mat_water_shader.base.type = KASSET_TYPE_SHADER;
        mat_water_shader.base.meta.version = 1;
        mat_water_shader.depth_test = true;
        mat_water_shader.depth_write = true;
        mat_water_shader.stencil_test = false;
        mat_water_shader.stencil_write = false;
        mat_water_shader.colour_write = true;
        mat_water_shader.colour_read = false;
        mat_water_shader.supports_wireframe = true;
        mat_water_shader.cull_mode = FACE_CULL_MODE_BACK;
        mat_water_shader.max_groups = state->config.max_material_count;
        mat_water_shader.max_draw_ids = state->config.max_instance_count;
        mat_water_shader.topology_types = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;

        mat_water_shader.stage_count = 2;
        mat_water_shader.stages = KALLOC_TYPE_CARRAY(kasset_shader_stage, mat_water_shader.stage_count);
        mat_water_shader.stages[0].type = SHADER_STAGE_VERTEX;
        mat_water_shader.stages[0].package_name = PACKAGE_NAME_RUNTIME;
        mat_water_shader.stages[0].source_asset_name = "MaterialWater_vert";
        mat_water_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_water_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_water_shader.stages[1].source_asset_name = "MaterialWater_frag";

        mat_water_shader.attribute_count = 1;
        mat_water_shader.attributes = KALLOC_TYPE_CARRAY(kasset_shader_attribute, mat_water_shader.attribute_count);
        mat_water_shader.attributes[0].type = SHADER_ATTRIB_TYPE_FLOAT32_4;
        mat_water_shader.attributes[0].name = "in_position";

        mat_water_shader.uniform_count = 9;
        mat_water_shader.uniforms = KALLOC_TYPE_CARRAY(kasset_shader_uniform, mat_water_shader.uniform_count);

        // per_frame
        u32 uidx = 0;
        mat_water_shader.uniforms[uidx].name = "material_frame_ubo";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_water_shader.uniforms[uidx].size = sizeof(material_water_frame_uniform_data);
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "shadow_texture";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "irradiance_cube_textures";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_CUBE;
        mat_water_shader.uniforms[uidx].array_size = MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "shadow_sampler";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "irradiance_sampler";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;
        // per_group
        mat_water_shader.uniforms[uidx].name = "material_group_ubo";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_water_shader.uniforms[uidx].size = sizeof(material_water_group_uniform_data);
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "material_textures";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_2D;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        mat_water_shader.uniforms[uidx].array_size = MATERIAL_WATER_TEXTURE_COUNT;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "material_samplers";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        mat_water_shader.uniforms[uidx].array_size = MATERIAL_WATER_SAMPLER_COUNT;
        uidx++;

        // per_draw
        mat_water_shader.uniforms[uidx].name = "material_draw_ubo";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_water_shader.uniforms[uidx].size = sizeof(material_water_draw_uniform_data);
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_DRAW;
        uidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize((kasset*)&mat_water_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_water_shader.stages, kasset_shader_stage, mat_water_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_water_shader.attributes, kasset_shader_attribute, mat_water_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_water_shader.uniforms, kasset_shader_uniform, mat_water_shader.uniform_count);
        kzero_memory(&mat_water_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        state->material_water_shader = shader_system_get_from_source(mat_water_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            state->water_material_locations.material_frame_ubo = shader_system_uniform_location(state->material_water_shader, kname_create("material_frame_ubo"));
            state->water_material_locations.shadow_texture = shader_system_uniform_location(state->material_water_shader, kname_create("shadow_texture"));
            state->water_material_locations.irradiance_cube_textures = shader_system_uniform_location(state->material_water_shader, kname_create("irradiance_cube_textures"));
            state->water_material_locations.shadow_sampler = shader_system_uniform_location(state->material_water_shader, kname_create("shadow_sampler"));
            state->water_material_locations.irradiance_sampler = shader_system_uniform_location(state->material_water_shader, kname_create("irradiance_sampler"));

            // Per group
            state->water_material_locations.material_textures = shader_system_uniform_location(state->material_water_shader, kname_create("material_textures"));
            state->water_material_locations.material_samplers = shader_system_uniform_location(state->material_water_shader, kname_create("material_samplers"));
            state->water_material_locations.material_group_ubo = shader_system_uniform_location(state->material_water_shader, kname_create("material_group_ubo"));

            // Per draw.
            state->water_material_locations.material_draw_ubo = shader_system_uniform_location(state->material_standard_shader, kname_create("material_draw_ubo"));
        }
    }

    // Blended material shader.
    {
        // TODO: blended materials.
        // state->material_blended_shader = shader_system_get(kname_create(MATERIAL_SHADER_NAME_BLENDED));
    }

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
    // if (!create_default_blended_material(state)) {
    //     KFATAL("Failed to create default blended material. Application cannot continue.");
    //     return false;
    // }

    // Register a console command to dump list of materials/references.
    console_command_register("material_system_dump", 0, on_material_system_dump);

    return true;
}

void material_system_shutdown(struct material_system_state* state) {
    if (state) {
        // Destroy default materials.
        material_destroy(state, &state->default_standard_material);
        material_destroy(state, &state->default_water_material);
        material_destroy(state, &state->default_blended_material);

        // Release shaders for the default materials.
        shader_system_destroy(&state->material_standard_shader);
        shader_system_destroy(&state->material_water_shader);
        shader_system_destroy(&state->material_blended_shader);
    }
}

kresource_texture* material_texture_get(struct material_system_state* state, khandle material, material_texture_input tex_input) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return false;
    }

    material_data* data = &state->materials[material.handle_index];

    switch (tex_input) {
    case MATERIAL_TEXTURE_INPUT_BASE_COLOUR:
        return data->base_colour_texture;
    case MATERIAL_TEXTURE_INPUT_NORMAL:
        return data->normal_texture;
    case MATERIAL_TEXTURE_INPUT_METALLIC:
        return data->metallic_texture;
    case MATERIAL_TEXTURE_INPUT_ROUGHNESS:
        return data->roughness_texture;
    case MATERIAL_TEXTURE_INPUT_AMBIENT_OCCLUSION:
        return data->ao_texture;
    case MATERIAL_TEXTURE_INPUT_EMISSIVE:
        return data->emissive_texture;
    case MATERIAL_TEXTURE_INPUT_REFLECTION:
        return data->reflection_texture;
    case MATERIAL_TEXTURE_INPUT_REFRACTION:
        return data->refraction_texture;
    case MATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH:
        return data->reflection_depth_texture;
    case MATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH:
        return data->refraction_depth_texture;
    case MATERIAL_TEXTURE_INPUT_DUDV:
        return data->dudv_texture;
    case MATERIAL_TEXTURE_INPUT_MRA:
        return data->mra_texture;
    case MATERIAL_TEXTURE_INPUT_COUNT:
    default:
        KERROR("Unknown material texture input.");
        return 0;
    }
}

void material_texture_set(struct material_system_state* state, khandle material, material_texture_input tex_input, kresource_texture* texture) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return;
    }

    material_data* data = &state->materials[material.handle_index];

    switch (tex_input) {
    case MATERIAL_TEXTURE_INPUT_BASE_COLOUR:
        data->base_colour_texture = texture;
    case MATERIAL_TEXTURE_INPUT_NORMAL:
        data->normal_texture = texture;
    case MATERIAL_TEXTURE_INPUT_METALLIC:
        data->metallic_texture = texture;
    case MATERIAL_TEXTURE_INPUT_ROUGHNESS:
        data->roughness_texture = texture;
    case MATERIAL_TEXTURE_INPUT_AMBIENT_OCCLUSION:
        data->ao_texture = texture;
    case MATERIAL_TEXTURE_INPUT_EMISSIVE:
        data->emissive_texture = texture;
    case MATERIAL_TEXTURE_INPUT_REFLECTION:
        data->reflection_texture = texture;
    case MATERIAL_TEXTURE_INPUT_REFRACTION:
        data->refraction_texture = texture;
    case MATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH:
        data->reflection_depth_texture = texture;
    case MATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH:
        data->refraction_depth_texture = texture;
    case MATERIAL_TEXTURE_INPUT_DUDV:
        data->dudv_texture = texture;
    case MATERIAL_TEXTURE_INPUT_MRA:
        data->mra_texture = texture;
    case MATERIAL_TEXTURE_INPUT_COUNT:
    default:
        KERROR("Unknown material texture input.");
        return;
    }
}

b8 material_has_transparency_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT);
}
void material_has_transparency_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT, value);
}

b8 material_double_sided_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_DOUBLE_SIDED_BIT);
}
void material_double_sided_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_DOUBLE_SIDED_BIT, value);
}

b8 material_recieves_shadow_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT);
}
void material_recieves_shadow_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT, value);
}

b8 material_casts_shadow_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_CASTS_SHADOW_BIT);
}
void material_casts_shadow_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_CASTS_SHADOW_BIT, value);
}

b8 material_normal_enabled_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_NORMAL_ENABLED_BIT);
}
void material_normal_enabled_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, value);
}

b8 material_ao_enabled_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_AO_ENABLED_BIT);
}
void material_ao_enabled_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_AO_ENABLED_BIT, value);
}

b8 material_emissive_enabled_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT);
}
void material_emissive_enabled_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT, value);
}

b8 material_refraction_enabled_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT);
}
void material_refraction_enabled_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT, value);
}

f32 material_refraction_scale_get(struct material_system_state* state, khandle material) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return 0;
    }

    material_data* data = &state->materials[material.handle_index];
    return data->refraction_scale;
}
void material_refraction_scale_set(struct material_system_state* state, khandle material, f32 value) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return;
    }

    material_data* data = &state->materials[material.handle_index];
    data->refraction_scale = value;
}

b8 material_use_vertex_colour_as_base_colour_get(struct material_system_state* state, khandle material) {
    return material_flag_get(state, material, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT);
}
void material_use_vertex_colour_as_base_colour_set(struct material_system_state* state, khandle material, b8 value) {
    material_flag_set(state, material, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT, value);
}

b8 material_flag_set(struct material_system_state* state, khandle material, kmaterial_flag_bits flag, b8 value) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return false;
    }

    material_data* data = &state->materials[material.handle_index];

    FLAG_SET(data->flags, flag, value);
    return true;
}

b8 material_flag_get(struct material_system_state* state, khandle material, kmaterial_flag_bits flag) {
    if (!state || khandle_is_invalid(material) || khandle_is_stale(material, state->materials[material.handle_index].unique_id)) {
        return false;
    }

    material_data* data = &state->materials[material.handle_index];

    return FLAG_GET(data->flags, flag);
}

b8 material_system_acquire(material_system_state* state, kname name, material_instance* out_instance) {
    KASSERT_MSG(out_instance, "out_instance is required.");

    u32 material_count = darray_length(state->materials);
    for (u32 i = 0; i < material_count; ++i) {
        material_data* material = &state->materials[i];
        if (material->name == name) {
            // Material exists, create an instance and boot.
            out_instance->material = khandle_create_with_u64_identifier(i, material->unique_id);

            // Request instance and set handle.
            b8 instance_result = material_instance_create(state, out_instance->material, &out_instance->instance);
            if (!instance_result) {
                KERROR("Failed to create material instance during new material creation.");
            }
            return instance_result;
        }
    }

    // Material is not yet loaded, request it.
    // Setup a listener.
    material_request_listener* listener = KALLOC_TYPE(material_request_listener, MEMORY_TAG_MATERIAL_INSTANCE);
    listener->state = state;
    listener->material_handle = material_handle_create(state, name);
    listener->instance_handle = &out_instance->instance;

    // Request the resource.
    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.base.user_callback = material_resource_loaded;
    request.base.listener_inst = listener;
    kresource* r = kresource_system_request(state->resource_system, name, (kresource_request_info*)&request);
    return r != 0;
}

void material_system_release(material_system_state* state, material_instance* instance) {
    if (!state) {
        return;
    }

    // Getting the material instance data successfully performs all handle checks for
    // the material and instance. This means it's safe to destroy.
    if (get_instance_data(state, *instance)) {

        material_instance_destroy(state, instance->material, &instance->instance);
        // Invalidate the material handle in the instance pointer as well.
        khandle_invalidate(&instance->material);
    }
}

b8 material_system_prepare_frame(material_system_state* state, material_frame_data mat_frame_data, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    // Standard shader type
    {
        khandle shader = state->material_standard_shader;
        shader_system_use(shader);

        // Ensure wireframe mode is (un)set.
        b8 is_wireframe = (mat_frame_data.render_mode == RENDERER_VIEW_MODE_WIREFRAME);
        shader_system_set_wireframe(shader, is_wireframe);

        // Setup frame data UBO structure to send over.
        material_standard_frame_uniform_data frame_ubo = {0};
        frame_ubo.projection = mat_frame_data.projection;
        for (u32 i = 0; i < MATERIAL_MAX_VIEWS; ++i) {
            frame_ubo.views[i] = mat_frame_data.views[i];
            frame_ubo.view_positions[i] = mat_frame_data.view_positions[i];
        }
        for (u8 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
            frame_ubo.cascade_splits[i] = mat_frame_data.cascade_splits[i];
            frame_ubo.directional_light_spaces[i] = mat_frame_data.directional_light_spaces[i];
        }

        // Get "use pcf" option
        i32 iuse_pcf = 0;
        kvar_i32_get("use_pcf", &iuse_pcf);
        frame_ubo.use_pcf = (u32)iuse_pcf;

        frame_ubo.delta_time = mat_frame_data.delta_time;
        frame_ubo.game_time = mat_frame_data.game_time;

        // TODO: These properties below should be pulled in from global settings somewhere instead of this way.
        frame_ubo.shadow_bias = mat_frame_data.shadow_bias;
        frame_ubo.render_mode = mat_frame_data.render_mode;

        if (!shader_system_bind_frame(shader)) {
            KERROR("Failed to bind frame frequency for standard material shader.");
            return false;
        }

        // Set the whole UNO at once.
        shader_system_uniform_set_by_location(shader, state->standard_material_locations.material_frame_ubo, &frame_ubo);

        // Texture maps
        // Shadow map - arrayed texture.
        if (mat_frame_data.shadow_map_texture) {
            shader_system_texture_set_by_location(shader, state->standard_material_locations.shadow_texture, mat_frame_data.shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            if (mat_frame_data.irradiance_cubemap_textures[i]) {
                shader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.shadow_texture, i, mat_frame_data.irradiance_cubemap_textures[i]);
            }
        }

        // Apply/upload everything to the GPU
        if (!shader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }
    }

    // Water shader type
    {
        khandle shader = state->material_water_shader;
        shader_system_use(shader);

        // Ensure wireframe mode is (un)set.
        b8 is_wireframe = (mat_frame_data.render_mode == RENDERER_VIEW_MODE_WIREFRAME);
        shader_system_set_wireframe(shader, is_wireframe);

        // Setup frame data UBO structure to send over.
        material_water_frame_uniform_data frame_ubo = {0};
        frame_ubo.projection = mat_frame_data.projection;
        for (u32 i = 0; i < MATERIAL_MAX_VIEWS; ++i) {
            frame_ubo.views[i] = mat_frame_data.views[i];
            frame_ubo.view_positions[i] = mat_frame_data.view_positions[i];
        }
        for (u8 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
            frame_ubo.cascade_splits[i] = mat_frame_data.cascade_splits[i];
            frame_ubo.directional_light_spaces[i] = mat_frame_data.directional_light_spaces[i];
        }

        // Get "use pcf" option
        i32 iuse_pcf = 0;
        kvar_i32_get("use_pcf", &iuse_pcf);
        frame_ubo.use_pcf = (u32)iuse_pcf;

        frame_ubo.delta_time = mat_frame_data.delta_time;
        frame_ubo.game_time = mat_frame_data.game_time;

        // TODO: These properties below should be pulled in from global settings somewhere instead of this way.
        frame_ubo.shadow_bias = mat_frame_data.shadow_bias;
        frame_ubo.render_mode = mat_frame_data.render_mode;

        if (!shader_system_bind_frame(shader)) {
            KERROR("Failed to bind frame frequency for water material shader.");
            return false;
        }

        // Set the whole UNO at once.
        shader_system_uniform_set_by_location(shader, state->water_material_locations.material_frame_ubo, &frame_ubo);

        // Texture maps
        // Shadow map - arrayed texture.
        if (mat_frame_data.shadow_map_texture) {
            shader_system_texture_set_by_location(shader, state->water_material_locations.shadow_texture, mat_frame_data.shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            if (mat_frame_data.irradiance_cubemap_textures[i]) {
                shader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.shadow_texture, i, mat_frame_data.irradiance_cubemap_textures[i]);
            }
        }

        // Apply/upload everything to the GPU
        if (!shader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }
    }

    // TODO: Blended

    return true;
}

b8 material_system_apply(material_system_state* state, khandle material, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    material_data* base_material = &state->materials[material.handle_index];

    khandle shader;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        return false;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        shader_system_use(shader);

        // per-group - ensure this is done once per frame per material

        // bind per-group
        if (!shader_system_bind_group(material, base_material->group_id)) {
            KERROR("Failed to bind material shader group.");
            return false;
        }

        // Setup frame data UBO structure to send over.
        material_standard_group_uniform_data group_ubo = {0};
        group_ubo.flags = base_material->flags;

        group_ubo.lighting_model = (u32)base_material->model;
        group_ubo.uv_offset = base_material->uv_offset;
        group_ubo.uv_scale = base_material->uv_scale;
        // LEFTOFF: Move this to the frame UBO - don't forget shaders!!!
        group_ubo.refraction_scale = 0;           // TODO: Implement this once refraction is supported in standard materials.
        group_ubo.emissive_texture_intensity = 0; // TODO: emissive intensity.

        // FIXME: Light data should be per-frame, and for the entire scene, then indexed at the per-draw level. Light count
        // and list of indices into the light array would be per-draw.
        // TODO: These should be stored in a SSBO

        // Directional light.
        directional_light* dir_light = light_system_directional_light_get();
        if (dir_light) {
            group_ubo.dir_light = dir_light->data;
        } else {
            KERROR("Failed to bind standard material shader group.");
            return false;
            kzero_memory(&group_ubo.dir_light, sizeof(directional_light_data));
        }
        // Point lights.
        group_ubo.num_p_lights = KMIN(light_system_point_light_count(), MATERIAL_MAX_POINT_LIGHTS);
        if (group_ubo.num_p_lights) {
            point_light p_lights[MATERIAL_MAX_POINT_LIGHTS];
            kzero_memory(p_lights, sizeof(point_light) * MATERIAL_MAX_POINT_LIGHTS);

            light_system_point_lights_get(p_lights);

            for (u32 i = 0; i < group_ubo.num_p_lights; ++i) {
                group_ubo.p_lights[i] = p_lights[i].data;
            }
        }

        // Inputs - Bind the texture if used.

        // Base colour
        if (base_material->base_colour_texture) {
            group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX, true);
            shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_BASE_COLOUR, &base_material->base_colour_texture);
        } else {
            group_ubo.base_colour = base_material->base_colour;
        }

        // Normal
        if (FLAG_GET(base_material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)) {
            if (base_material->normal_texture) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX, true);
                shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_NORMAL, &base_material->normal_texture);
            } else {
                group_ubo.normal = base_material->normal;
            }
        }

        // MRA
        b8 mra_enabled = FLAG_GET(base_material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT);
        if (mra_enabled) {
            if (base_material->mra_texture) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX, true);
                shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_MRA, &base_material->mra_texture);
            } else {
                group_ubo.mra = base_material->mra;
            }
        } else {
            // If not using MRA, then do these:

            // Metallic
            if (base_material->metallic_texture) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX, true);
                shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_METALLIC, &base_material->metallic_texture);
            } else {
                group_ubo.metallic = base_material->metallic;
            }

            // Roughness
            if (base_material->roughness_texture) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX, true);
                shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_ROUGHNESS, &base_material->roughness_texture);
            } else {
                group_ubo.roughness = base_material->roughness;
            }

            // AO
            if (base_material->ao_texture && FLAG_GET(base_material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX, true);
                shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_NORMAL, &base_material->ao_texture);
            } else {
                group_ubo.ao = base_material->ao;
            }

            // Pack source channels. [Metallic, roughness, ao, unused].
            group_ubo.texture_channels = pack_u8_into_u32(base_material->metallic_texture_channel, base_material->roughness_texture_channel, base_material->ao_texture_channel, 0);
        }

        // Emissive
        if (base_material->emissive_texture && FLAG_GET(base_material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) {
            group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX, true);
            shader_system_uniform_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_EMISSIVE, &base_material->emissive_texture);
        } else {
            group_ubo.emissive = base_material->emissive;
        }

        // Set the whole thing at once.
        shader_system_uniform_set_by_location(shader, state->standard_material_locations.material_group_ubo, &group_ubo);

        // Apply/upload them to the GPU
        shader_system_apply_per_group(shader, base_material->generation);
    }
        return true;
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;
        shader_system_use(shader);

        // per-group - ensure this is done once per frame per material

        // bind per-group
        if (!shader_system_bind_group(material, base_material->group_id)) {
            KERROR("Failed to bind water material shader group.");
            return false;
        }

        // Setup frame data UBO structure to send over.
        material_water_group_uniform_data group_ubo = {0};
        group_ubo.flags = base_material->flags;

        group_ubo.lighting_model = (u32)base_material->model;

        // FIXME: Light data should be per-frame, and for the entire scene, then indexed at the per-draw level. Light count
        // and list of indices into the light array would be per-draw.
        // TODO: These should be stored in a SSBO

        // Directional light.
        directional_light* dir_light = light_system_directional_light_get();
        if (dir_light) {
            group_ubo.dir_light = dir_light->data;
        } else {
            KERROR("Failed to bind material shader group.");
            return false;
            kzero_memory(&group_ubo.dir_light, sizeof(directional_light_data));
        }
        // Point lights.
        group_ubo.num_p_lights = KMIN(light_system_point_light_count(), MATERIAL_MAX_POINT_LIGHTS);
        if (group_ubo.num_p_lights) {
            point_light p_lights[MATERIAL_MAX_POINT_LIGHTS];
            kzero_memory(p_lights, sizeof(point_light) * MATERIAL_MAX_POINT_LIGHTS);

            light_system_point_lights_get(p_lights);

            for (u32 i = 0; i < group_ubo.num_p_lights; ++i) {
                group_ubo.p_lights[i] = p_lights[i].data;
            }
        }

        // Reflection texture.
        if (base_material->reflection_texture) {
            shader_system_uniform_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFLECTION, &base_material->reflection_texture);
        } else {
            KFATAL("Water material shader requires a reflection texture.");
        }

        // Refraction texture.
        if (base_material->refraction_texture) {
            shader_system_uniform_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFRACTION, &base_material->refraction_texture);
        } else {
            KFATAL("Water material shader requires a refraction texture.");
        }

        // Refraction depth texture.
        if (base_material->refraction_depth_texture) {
            shader_system_uniform_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFRACTION_DEPTH, &base_material->refraction_depth_texture);
        } else {
            KFATAL("Water material shader requires a refraction depth texture.");
        }

        // DUDV texture.
        if (base_material->dudv_texture) {
            shader_system_uniform_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_DUDV, &base_material->dudv_texture);
        } else {
            KFATAL("Water material shader requires a dudv texture.");
        }

        // Normal texture.
        if (base_material->normal_texture) {
            shader_system_uniform_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_NORMAL, &base_material->normal_texture);
        } else {
            KFATAL("Water material shader requires a normal texture.");
        }

        // Set the whole thing at once.
        shader_system_uniform_set_by_location(shader, state->water_material_locations.material_group_ubo, &group_ubo);

        // Apply/upload them to the GPU
        shader_system_apply_per_group(shader, base_material->generation);
    }
        return false;
    case KMATERIAL_TYPE_BLENDED:
        shader = state->material_blended_shader;
        return false;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Not yet implemented!");
        return false;
    }
}

b8 material_system_apply_instance(material_system_state* state, const material_instance* instance, struct material_instance_draw_data draw_data, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    material_instance_data* mat_inst_data = get_instance_data(state, *instance);
    if (!mat_inst_data) {
        return false;
    }
    material_data* base_material = &state->materials[instance->material.handle_index];

    khandle shader;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        return false;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;

        // per-draw - this gets run every time apply is called
        // bind per-draw
        if (!shader_system_bind_draw_id(shader, mat_inst_data->per_draw_id)) {
            KERROR("Failed to bind standard material shader draw id.");
            return false;
        }

        // Update uniform data
        material_standard_draw_uniform_data draw_ubo = {0};
        draw_ubo.clipping_plane = draw_data.clipping_plane;
        draw_ubo.model = draw_data.model;
        draw_ubo.irradiance_cubemap_index = draw_data.irradiance_cubemap_index;
        draw_ubo.view_index = draw_data.view_index;

        // Set the whole thing at once.
        shader_system_uniform_set_by_location(shader, state->standard_material_locations.material_draw_ubo, &draw_ubo);

        // apply per-draw
        shader_system_apply_per_draw(shader, mat_inst_data->generation);
    }
        return true;
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;

        // per-draw - this gets run every time apply is called
        // bind per-draw
        if (!shader_system_bind_draw_id(shader, mat_inst_data->per_draw_id)) {
            KERROR("Failed to bind water material shader draw id.");
            return false;
        }

        // Update uniform data
        material_water_draw_uniform_data draw_ubo = {0};
        draw_ubo.model = draw_data.model;
        draw_ubo.irradiance_cubemap_index = draw_data.irradiance_cubemap_index;
        draw_ubo.view_index = draw_data.view_index;
        // TODO: Pull in instance-specific overrides for these, if set.
        draw_ubo.tiling = base_material->tiling;
        draw_ubo.wave_speed = base_material->wave_speed;
        draw_ubo.wave_strength = base_material->wave_strength;

        // Set the whole thing at once.
        shader_system_uniform_set_by_location(shader, state->water_material_locations.material_draw_ubo, &draw_ubo);

        // apply per-draw
        shader_system_apply_per_draw(shader, mat_inst_data->generation);
    }
        return false;
    case KMATERIAL_TYPE_BLENDED:
        shader = state->material_blended_shader;
        return false;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Not yet implemented!");
        return false;
    }
}

b8 material_instance_flag_set(struct material_system_state* state, material_instance instance, kmaterial_flag_bits flag, b8 value) {
    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->flags = FLAG_SET(data->flags, flag, value);

    return true;
}

b8 material_instance_flag_get(struct material_system_state* state, material_instance instance, kmaterial_flag_bits flag) {
    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    return FLAG_GET(data->flags, flag);
}

b8 material_instance_base_colour_get(struct material_system_state* state, material_instance instance, vec4* out_value) {
    if (!out_value) {
        return false;
    }

    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->base_colour;
    return true;
}
b8 material_instance_base_colour_set(struct material_system_state* state, material_instance instance, vec4 value) {
    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->base_colour = value;
    increment_generation(&data->generation);
    return true;
}

b8 material_instance_uv_offset_get(struct material_system_state* state, material_instance instance, vec3* out_value) {
    if (!out_value) {
        return false;
    }

    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->uv_offset;
    return true;
}
b8 material_instance_uv_offset_set(struct material_system_state* state, material_instance instance, vec3 value) {
    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->uv_offset = value;
    increment_generation(&data->generation);
    return true;
}

b8 material_instance_uv_scale_get(struct material_system_state* state, material_instance instance, vec3* out_value) {
    if (!out_value) {
        return false;
    }

    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    *out_value = data->uv_scale;
    return true;
}

b8 material_instance_uv_scale_set(struct material_system_state* state, material_instance instance, vec3 value) {
    material_instance_data* data = get_instance_data(state, instance);
    if (!data) {
        return false;
    }

    data->uv_offset = value;
    increment_generation(&data->generation);
    return true;
}

material_instance material_system_get_default_standard(material_system_state* state) {
    return default_material_instance_get(state, state->default_standard_material);
}

material_instance material_system_get_default_water(material_system_state* state) {
    return default_material_instance_get(state, state->default_water_material);
}

material_instance material_system_get_default_blended(material_system_state* state) {
    return default_material_instance_get(state, state->default_blended_material);
}

void material_system_dump(material_system_state* state) {
    u32 material_count = darray_length(state->materials);
    for (u32 i = 0; i < material_count; ++i) {
        material_data* m = &state->materials[i];
        // Skip "free" slots.
        if (m->unique_id == INVALID_ID_U64) {
            continue;
        }

        material_instance_data* instance_array = state->instances[i];
        // Get a count of active instances.
        u32 instance_count = darray_length(instance_array);
        u32 active_instance_count = 0;
        for (u32 j = 0; j < instance_count; ++j) {
            if (instance_array[j].unique_id != INVALID_ID_U64) {
                active_instance_count++;
            }
        }

        KTRACE("Material name: '%s', active instance count = %u", kname_string_get(m->name), active_instance_count);
    }
}

static b8 create_default_standard_material(material_system_state* state) {
    KTRACE("Creating default standard material...");
    kname material_name = kname_create(MATERIAL_DEFAULT_NAME_STANDARD);

    // Create a fake material "asset" that can be serialized into a string.
    kasset_material asset = {0};
    asset.base.name = material_name;
    asset.base.type = KASSET_TYPE_MATERIAL;
    asset.type = KMATERIAL_TYPE_STANDARD;
    asset.model = KMATERIAL_MODEL_PBR;
    asset.has_transparency = MATERIAL_DEFAULT_HAS_TRANSPARENCY;
    asset.double_sided = MATERIAL_DEFAULT_DOUBLE_SIDED;
    asset.recieves_shadow = MATERIAL_DEFAULT_RECIEVES_SHADOW;
    asset.casts_shadow = MATERIAL_DEFAULT_CASTS_SHADOW;
    asset.use_vertex_colour_as_base_colour = MATERIAL_DEFAULT_USE_VERTEX_COLOUR_AS_BASE_COLOUR;
    asset.base_colour = MATERIAL_DEFAULT_BASE_COLOUR_VALUE; // white
    asset.normal = MATERIAL_DEFAULT_NORMAL_VALUE;
    asset.normal_enabled = MATERIAL_DEFAULT_NORMAL_ENABLED;
    asset.ambient_occlusion_enabled = MATERIAL_DEFAULT_AO_ENABLED;
    asset.mra = MATERIAL_DEFAULT_MRA_VALUE;
    asset.use_mra = MATERIAL_DEFAULT_MRA_ENABLED;
    asset.custom_shader_name = 0;

    // Setup a listener.
    material_request_listener* listener = KALLOC_TYPE(material_request_listener, MEMORY_TAG_MATERIAL_INSTANCE);
    listener->state = state;
    listener->material_handle = material_handle_create(state, material_name);
    listener->instance_handle = 0; // NOTE: creation of default materials does not immediately need an instance.

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.base.listener_inst = listener;
    request.base.user_callback = material_resource_loaded;
    // The material source is serialized into a string.
    request.material_source_text = kasset_material_serialize((kasset*)&asset);

    if (!kresource_system_request(state->resource_system, material_name, (kresource_request_info*)&request)) {
        KERROR("Resource request for default standard material failed. See logs for details.");
        return false;
    }

    KTRACE("Done.");
    return true;
}

static b8 create_default_water_material(material_system_state* state) {
    KTRACE("Creating default water material...");
    kname material_name = kname_create(MATERIAL_DEFAULT_NAME_WATER);

    // Create a fake material "asset" that can be serialized into a string.
    kasset_material asset = {0};
    asset.base.name = material_name;
    asset.base.type = KASSET_TYPE_MATERIAL;
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

    // Setup a listener.
    material_request_listener* listener = KALLOC_TYPE(material_request_listener, MEMORY_TAG_MATERIAL_INSTANCE);
    listener->state = state;
    listener->material_handle = material_handle_create(state, material_name);
    listener->instance_handle = 0; // NOTE: creation of default materials does not immediately need an instance.

    kresource_material_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_MATERIAL;
    request.base.listener_inst = listener;
    request.base.user_callback = material_resource_loaded;
    // The material source is serialized into a string.
    request.material_source_text = kasset_material_serialize((kasset*)&asset);

    // NOTE: This material also owns (and requests) the reflect/refract (and depth
    // textures for each) as opposed to the typical route of requesting via config.
    //
    // Request the resource.
    if (!kresource_system_request(state->resource_system, material_name, (kresource_request_info*)&request)) {
        KERROR("Resource request for default water material failed. See logs for details.");
        return false;
    }

    KTRACE("Done.");
    return true;
}

static b8 create_default_blended_material(material_system_state* state) {

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

    return true;
}

static void on_material_system_dump(console_command_context context) {
    material_system_dump(engine_systems_get()->material_system);
}

static khandle get_shader_for_material_type(const material_system_state* state, kmaterial_type type) {
    switch (type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KERROR("Cannot create a material using an 'unknown' material type.");
        return khandle_invalid();
    case KMATERIAL_TYPE_STANDARD:
        return state->material_standard_shader;
        break;
    case KMATERIAL_TYPE_WATER:
        return state->material_water_shader;
        break;
    case KMATERIAL_TYPE_BLENDED:
        return state->material_blended_shader;
        break;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Not yet implemented!");
        return khandle_invalid();
    }
}

static khandle material_handle_create(material_system_state* state, kname name) {
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
        new_inst_array->unique_id = INVALID_ID_U64;
        darray_push(state->instances, new_inst_array);
    }

    material_data* material = &state->materials[resource_index];

    // Setup a handle first.
    khandle handle = khandle_create(resource_index);
    material->unique_id = handle.unique_id.uniqueid;
    material->name = name;

    return handle;
}

static khandle material_instance_handle_create(material_system_state* state, khandle material_handle) {
    u32 instance_index = INVALID_ID;

    // Attempt to find a free "slot", or create a new entry if there isn't one.
    u32 instance_count = darray_length(state->instances[material_handle.handle_index]);
    for (u32 i = 0; i < instance_count; ++i) {
        if (state->instances[material_handle.handle_index][i].unique_id == INVALID_ID_U64) {
            // free slot. An array should already exists for instances here.
            instance_index = i;
            break;
        }
    }
    if (instance_index == INVALID_ID) {
        instance_index = instance_count;
        darray_push(state->instances[material_handle.handle_index], (material_instance_data){0});
    }

    material_instance_data* inst = &state->instances[material_handle.handle_index][instance_index];

    // Setup a handle first.
    khandle handle = khandle_create(instance_index);
    inst->unique_id = handle.unique_id.uniqueid;
    inst->material = material_handle;

    return handle;
}

static b8 material_create(material_system_state* state, khandle material_handle, const kresource_material* typed_resource) {
    material_data* material = &state->materials[material_handle.handle_index];

    // Validate the material type and model.
    material->type = typed_resource->type;
    material->model = typed_resource->model;

    // Select shader.
    khandle material_shader = get_shader_for_material_type(state, material->type);
    if (khandle_is_invalid(material_shader)) {
        // TODO: invalidate handle/entry?
        return false;
    }

    // Base colour map or value - used by all material types.
    if (typed_resource->base_colour_map.resource_name) {
        material->base_colour_texture = texture_system_request(typed_resource->base_colour_map.resource_name, typed_resource->base_colour_map.package_name, 0, 0);
    } else {
        material->base_colour = typed_resource->base_colour;
    }

    // Normal map - used by all material types.
    if (typed_resource->normal_map.resource_name) {
        material->normal_texture = texture_system_request(typed_resource->normal_map.resource_name, typed_resource->normal_map.package_name, 0, 0);
    }
    FLAG_SET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, typed_resource->normal_enabled);

    // Water textures require normals to be enabled and a texture to exist.
    if (material->type == KMATERIAL_TYPE_WATER) {
        FLAG_SET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT, true);

        // A special normal texture is also required, if not set.
        if (!material->normal_texture) {
            material->dudv_texture = texture_system_request(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME), state->runtime_package_name, 0, 0);
        }
    }

    // Inputs only used by standard materials.
    if (material->type == KMATERIAL_TYPE_STANDARD) {
        // Metallic map or value
        if (typed_resource->metallic_map.resource_name) {
            material->metallic_texture = texture_system_request(typed_resource->metallic_map.resource_name, typed_resource->metallic_map.package_name, 0, 0);
            material->metallic_texture_channel = typed_resource->metallic_map.channel;
        } else {
            material->metallic = typed_resource->metallic;
        }
        // Roughness map or value
        if (typed_resource->roughness_map.resource_name) {
            material->roughness_texture = texture_system_request(typed_resource->roughness_map.resource_name, typed_resource->roughness_map.package_name, 0, 0);
            material->roughness_texture_channel = typed_resource->roughness_map.channel;
        } else {
            material->roughness = typed_resource->roughness;
        }
        // Ambient occlusion map or value
        if (typed_resource->ambient_occlusion_map.resource_name) {
            material->ao_texture = texture_system_request(typed_resource->ambient_occlusion_map.resource_name, typed_resource->ambient_occlusion_map.package_name, 0, 0);
            material->ao_texture_channel = typed_resource->ambient_occlusion_map.channel;
        } else {
            material->ao = typed_resource->ambient_occlusion;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT, typed_resource->ambient_occlusion_enabled);

        // MRA (combined metallic/roughness/ao) map or value
        if (typed_resource->mra_map.resource_name) {
            material->mra_texture = texture_system_request(typed_resource->mra_map.resource_name, typed_resource->mra_map.package_name, 0, 0);
        } else {
            material->mra = typed_resource->mra;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT, typed_resource->use_mra);

        // Emissive map or value
        if (typed_resource->emissive_map.resource_name) {
            material->emissive_texture = texture_system_request(typed_resource->emissive_map.resource_name, typed_resource->emissive_map.package_name, 0, 0);
        } else {
            material->emissive = typed_resource->emissive;
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT, typed_resource->emissive_enabled);

        // Refraction
        // TODO: implement refraction. Any materials implementing this would obviously need to be drawn _after_ everything else in the
        // scene (opaque, then transparent front-to-back, THEN refractive materials), and likely sample the colour buffer behind it
        // when applying the effect.
        /* if (typed_resource->refraction_map.resource_name) {
            material->refraction_texture = texture_system_request(typed_resource->refraction_map.resource_name, typed_resource->refraction_map.package_name, 0, 0);
        }
        FLAG_SET(material->flags, KMATERIAL_FLAG_REFRACTION_ENABLED_BIT, typed_resource->refraction_enabled); */
    } else if (material->type == KMATERIAL_TYPE_WATER) {
        // Inputs only used by water materials.

        // Derivative (dudv) map.
        if (typed_resource->dudv_map.resource_name) {
            material->dudv_texture = texture_system_request(typed_resource->dudv_map.resource_name, typed_resource->dudv_map.package_name, 0, 0);
        } else {
            material->dudv_texture = texture_system_request(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME), state->runtime_package_name, 0, 0);
        }

        // NOTE: This material also owns (and requests) the reflect/refract (and depth
        // textures for each) as opposed to the typical route of requesting via config.

        // Get the current window size as the dimensions of these textures will be based on this.
        kwindow* window = engine_active_window_get();
        // TODO: should probably cut this in half.
        u32 tex_width = window->width;
        u32 tex_height = window->height;

        // Create reflection textures.
        material->reflection_texture = texture_system_request_writeable(kname_create("__waterplane_reflection_colour__"), tex_width, tex_height, KRESOURCE_TEXTURE_FORMAT_RGBA8, false, true);
        if (!material->reflection_texture) {
            return false;
        }
        material->reflection_depth_texture = texture_system_request_depth(kname_create("__waterplane_reflection_depth__"), tex_width, tex_height, true);
        if (!material->reflection_depth_texture) {
            return false;
        }

        // Create refraction textures.
        material->refraction_texture = texture_system_request_writeable(kname_create("__waterplane_refraction_colour__"), tex_width, tex_height, KRESOURCE_TEXTURE_FORMAT_RGBA8, false, true);
        if (!material->refraction_texture) {
            return false;
        }
        material->refraction_depth_texture = texture_system_request_depth(kname_create("__waterplane_refraction_depth__"), tex_width, tex_height, true);
        if (!material->reflection_depth_texture) {
            return false;
        }

        // Listen for window resizes, as these must trigger a resize of our reflect/refract
        // texture render targets. This should only be active while the material is loaded.
        if (!event_register(EVENT_CODE_WINDOW_RESIZED, material, material_on_event)) {
            KERROR("Unable to register material for window resize event. See logs for details.");
            return false;
        }
    }

    // Set remaining flags
    FLAG_SET(material->flags, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT, typed_resource->has_transparency);
    FLAG_SET(material->flags, KMATERIAL_FLAG_DOUBLE_SIDED_BIT, typed_resource->double_sided);
    FLAG_SET(material->flags, KMATERIAL_FLAG_RECIEVES_SHADOW_BIT, typed_resource->recieves_shadow);
    FLAG_SET(material->flags, KMATERIAL_FLAG_CASTS_SHADOW_BIT, typed_resource->casts_shadow);
    FLAG_SET(material->flags, KMATERIAL_FLAG_USE_VERTEX_COLOUR_AS_BASE_COLOUR_BIT, typed_resource->use_vertex_colour_as_base_colour);

    // Create a group for the material.
    if (!shader_system_shader_group_acquire(material_shader, &material->group_id)) {
        KERROR("Failed to acquire shader group while creating material. See logs for details.");
        // TODO: destroy/release
        return false;
    }

    // TODO: Custom samplers.

    return true;
}

static void material_destroy(material_system_state* state, khandle* material_handle) {
    if (khandle_is_invalid(*material_handle) || khandle_is_stale(*material_handle, state->materials[material_handle->handle_index].unique_id)) {
        KWARN("Attempting to release material that has an invalid or stale handle.");
        return;
    }

    material_data* material = &state->materials[material_handle->handle_index];

    // Select shader.
    khandle material_shader = get_shader_for_material_type(state, material->type);
    if (khandle_is_invalid(material_shader)) {
        KWARN("Attempting to release material that had an invalid shader.");
        return;
    }

    // Release texture resources/references
    if (material->base_colour_texture) {
        texture_system_release_resource(material->base_colour_texture);
    }
    if (material->normal_texture) {
        texture_system_release_resource(material->normal_texture);
    }
    if (material->metallic_texture) {
        texture_system_release_resource(material->metallic_texture);
    }
    if (material->roughness_texture) {
        texture_system_release_resource(material->roughness_texture);
    }
    if (material->ao_texture) {
        texture_system_release_resource(material->ao_texture);
    }
    if (material->mra_texture) {
        texture_system_release_resource(material->mra_texture);
    }
    if (material->emissive_texture) {
        texture_system_release_resource(material->emissive_texture);
    }
    if (material->dudv_texture) {
        texture_system_release_resource(material->dudv_texture);
    }
    if (material->reflection_texture) {
        texture_system_release_resource(material->reflection_texture);
    }
    if (material->reflection_depth_texture) {
        texture_system_release_resource(material->reflection_depth_texture);
    }
    if (material->refraction_texture) {
        texture_system_release_resource(material->refraction_texture);
    }
    if (material->refraction_depth_texture) {
        texture_system_release_resource(material->refraction_depth_texture);
    }

    if (material->type == KMATERIAL_TYPE_WATER) {
        // Immediately stop listening for resize events.
        if (!event_unregister(EVENT_CODE_WINDOW_RESIZED, material, material_on_event)) {
            // Nothing to really do about it, but warn the user.
            KWARN("Unable to unregister material for resize event. See logs for details.");
        }
    }

    // Release the group for the material.
    if (!shader_system_shader_group_release(material_shader, material->group_id)) {
        KWARN("Failed to release shader group while creating material. See logs for details.");
    }

    // TODO: Custom samplers.

    // Destroy instances.
    u32 instance_count = darray_length(state->instances[material_handle->handle_index]);
    for (u32 i = 0; i < instance_count; ++i) {
        material_instance_data* inst = &state->instances[material_handle->handle_index][i];
        if (inst->unique_id != INVALID_ID_U64) {
            khandle temp_handle = khandle_create_with_u64_identifier(i, inst->unique_id);
            material_instance_destroy(state, *material_handle, &temp_handle);
        }
    }

    kzero_memory(material, sizeof(material_data));

    // Mark the material slot as free for another material to be loaded.
    material->unique_id = INVALID_ID_U64;
    material->group_id = INVALID_ID;

    khandle_invalidate(material_handle);
}

static b8 material_instance_create(material_system_state* state, khandle base_material, khandle* out_instance_handle) {

    *out_instance_handle = material_instance_handle_create(state, base_material);
    if (khandle_is_invalid(*out_instance_handle)) {
        KERROR("Failed to create material instance handle. Instance will not be created.");
        return false;
    }

    material_data* material = &state->materials[base_material.handle_index];
    material_instance_data* inst = &state->instances[base_material.handle_index][out_instance_handle->handle_index];

    // Get per-draw resources for the instance.
    if (!renderer_shader_per_draw_resources_acquire(state->renderer, get_shader_for_material_type(state, material->type), &inst->per_draw_id)) {
        KERROR("Failed to create per-draw resources for a material instance. Instance creation failed.");
        return false;
    }

    // Take a copy of the base material properties.
    inst->flags = material->flags;
    inst->uv_scale = material->uv_scale;
    inst->uv_offset = material->uv_offset;
    inst->base_colour = material->base_colour;

    // New instances are always dirty.
    increment_generation(&inst->generation);

    return true;
}

static void material_instance_destroy(material_system_state* state, khandle base_material, khandle* instance_handle) {
    material_data* material = &state->materials[base_material.handle_index];
    material_instance_data* inst = &state->instances[base_material.handle_index][instance_handle->handle_index];
    if (khandle_is_invalid(*instance_handle) || khandle_is_stale(*instance_handle, state->instances[base_material.handle_index][instance_handle->handle_index].unique_id)) {
        KWARN("Tried to destroy a material instance whose handle is either invalid or stale. Nothing will be done.");
        return;
    }

    // Release per-draw resources for the instance.
    renderer_shader_per_draw_resources_release(state->renderer, get_shader_for_material_type(state, material->type), inst->per_draw_id);

    kzero_memory(inst, sizeof(material_instance_data));

    // Make sure to invalidate the entry.
    inst->unique_id = INVALID_ID_U64;
    inst->per_draw_id = INVALID_ID;

    // Invalidate the handle too.
    khandle_invalidate(instance_handle);
}

static void material_resource_loaded(kresource* resource, void* listener) {
    kresource_material* typed_resource = (kresource_material*)resource;
    material_request_listener* listener_inst = (material_request_listener*)listener;
    material_system_state* state = listener_inst->state;

    // Create the base material.
    if (!material_create(state, listener_inst->material_handle, typed_resource)) {
        KERROR("Failed to create material. See logs for details.");
        return;
    }

    // Create an instance of it if one is required.
    if (listener_inst->instance_handle) {
        if (!material_instance_create(state, listener_inst->material_handle, listener_inst->instance_handle)) {
            KERROR("Failed to create material instance during new material creation.");
        }
    }
}

static material_instance default_material_instance_get(material_system_state* state, khandle base_material) {
    material_instance instance = {0};
    instance.material = base_material;

    material_data* base = &state->materials[base_material.handle_index];

    // Get an instance of it.
    if (!material_instance_create(state, instance.material, &instance.instance)) {
        // Fatal here because if this happens on a default material, something is seriously borked.
        KFATAL("Failed to obtain an instance of the default '%s' material.", kname_string_get(base->name));

        // Invalidate the handles.
        khandle_invalidate(&instance.material);
        khandle_invalidate(&instance.instance);
    }

    return instance;
}

static material_instance_data* get_instance_data(material_system_state* state, material_instance instance) {
    if (!state) {
        return 0;
    }

    // Verify handles first.
    if (khandle_is_invalid(instance.material) || khandle_is_invalid(instance.instance)) {
        KWARN("Attempted to get material instance with an invalid base material or instance handle. Nothing to do.");
        return 0;
    }

    if (khandle_is_stale(instance.material, state->materials[instance.material.handle_index].unique_id)) {
        KWARN("Attempted to get material instance using a stale material handle. Nothing will be done.");
        return 0;
    }

    if (khandle_is_stale(instance.material, state->instances[instance.material.handle_index][instance.instance.handle_index].unique_id)) {
        KWARN("Attempted to get material instance using a stale material instance handle. Nothing will be done.");
        return 0;
    }

    return &state->instances[instance.material.handle_index][instance.instance.handle_index];
}

static void increment_generation(u16* generation) {
    (*generation)++;
    // Roll over to ensure a valid generation.
    if ((*generation) == INVALID_ID_U16) {
        (*generation) = 0;
    }
}

static b8 material_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_WINDOW_RESIZED) {
        // Resize textures to match new frame buffer.
        u16 width = context.data.u16[0] / 8;
        u16 height = context.data.u16[1] / 8;

        // const kwindow* window = sender;
        material_data* material = listener_inst;

        if (material->reflection_texture->base.generation != INVALID_ID_U8) {
            if (!texture_system_resize(material->reflection_texture, width, height, true)) {
                KERROR("Failed to resize reflection colour texture for material.");
            }
        }
        if (material->reflection_depth_texture->base.generation != INVALID_ID_U8) {
            if (!texture_system_resize(material->reflection_depth_texture, width, height, true)) {
                KERROR("Failed to resize reflection depth texture for material.");
            }
        }

        if (material->refraction_texture->base.generation != INVALID_ID_U8) {
            if (!texture_system_resize(material->refraction_texture, width, height, true)) {
                KERROR("Failed to resize refraction colour texture for material.");
            }
        }
        if (material->reflection_depth_texture->base.generation != INVALID_ID_U8) {
            if (!texture_system_resize(material->reflection_depth_texture, width, height, true)) {
                KERROR("Failed to resize refraction depth texture for material.");
            }
        }
    }

    // Allow other systems to pick up event.
    return false;
}
