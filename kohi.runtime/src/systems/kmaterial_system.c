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

#include "core/console.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kvar.h"
#include "kresources/kresource_types.h"
#include "renderer/renderer_frontend.h"
#include "runtime_defines.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kshader_system.h"
#include "systems/light_system.h"
#include "systems/texture_system.h"

#define MATERIAL_STANDARD_NAME_FRAG "Shader.MaterialStandard_frag"
#define MATERIAL_STANDARD_NAME_VERT "Shader.MaterialStandard_vert"
#define MATERIAL_WATER_NAME_FRAG "Shader.MaterialWater_frag"
#define MATERIAL_WATER_NAME_VERT "Shader.MaterialWater_vert"
#define MATERIAL_BLENDED_NAME_FRAG "Shader.MaterialBlended_frag"
#define MATERIAL_BLENDED_NAME_VERT "Shader.MaterialBlended_vert"

// Texture indices

// Standard material
const u32 MAT_STANDARD_IDX_BASE_COLOUR = 0;
const u32 MAT_STANDARD_IDX_NORMAL = 1;
const u32 MAT_STANDARD_IDX_METALLIC = 2;
const u32 MAT_STANDARD_IDX_ROUGHNESS = 3;
const u32 MAT_STANDARD_IDX_AO = 4;
const u32 MAT_STANDARD_IDX_MRA = 5;
const u32 MAT_STANDARD_IDX_EMISSIVE = 6;

// Option indices
const u32 MAT_OPTION_IDX_RENDER_MODE = 0;
const u32 MAT_OPTION_IDX_USE_PCF = 1;
const u32 MAT_OPTION_IDX_UNUSED_0 = 2;
const u32 MAT_OPTION_IDX_UNUSED_1 = 3;

// Param indices
const u32 MAT_PARAM_IDX_SHADOW_BIAS = 0;
const u32 MAT_PARAM_IDX_DELTA_TIME = 1;
const u32 MAT_PARAM_IDX_GAME_TIME = 2;
const u32 MAT_PARAM_IDX_UNUSED_0 = 3;

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
// - Blended type material
// - Material models (unlit, PBR, Phong, etc.)

typedef enum kmaterial_state {
    KMATERIAL_STATE_UNINITIALIZED = 0,
    KMATERIAL_STATE_LOADING,
    KMATERIAL_STATE_LOADED,
} kmaterial_state;

typedef enum kmaterial_instance_state {
    // Instance is available
    KMATERIAL_INSTANCE_STATE_UNINITIALIZED = 0,
    // Instance was issued while base material was loading, and needs initialization.
    KMATERIAL_INSTANCE_STATE_LOADING,
    // Instance is ready to be used.
    KMATERIAL_INSTANCE_STATE_LOADED,
} kmaterial_instance_state;

// Represents the data for a single instance of a material.
// This can be thought of as "per-draw" data.
typedef struct kmaterial_instance_data {
    kmaterial_instance_state state;

    // A handle to the material to which this instance references.
    kmaterial material;

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
} kmaterial_instance_data;

// Represents a base material.
// This can be thought of as "per-group" data.
typedef struct kmaterial_data {
    u16 index;

    kname name;
    // The state of the material (loaded vs not, etc.)
    kmaterial_state state;
    /** @brief The material type. Ultimately determines what shader the material is rendered with. */
    kmaterial_type type;
    /** @brief The material lighting model. */
    kmaterial_model model;

    vec4 base_colour;
    ktexture base_colour_texture;

    vec3 normal;
    ktexture normal_texture;

    f32 metallic;
    ktexture metallic_texture;
    texture_channel metallic_texture_channel;

    f32 roughness;
    ktexture roughness_texture;
    texture_channel roughness_texture_channel;

    f32 ao;
    ktexture ao_texture;
    texture_channel ao_texture_channel;

    vec4 emissive;
    ktexture emissive_texture;
    f32 emissive_texture_intensity;

    ktexture refraction_texture;
    f32 refraction_scale;

    ktexture reflection_texture;
    ktexture reflection_depth_texture;
    ktexture dudv_texture;
    ktexture refraction_depth_texture;

    vec3 mra;
    /**
     * @brief This is a combined texture holding metallic/roughness/ambient occlusion all in one texture.
     * This is a more efficient replacement for using those textures individually. Metallic is sampled
     * from the Red channel, roughness from the Green channel, and ambient occlusion from the Blue channel.
     * Alpha is ignored.
     */
    ktexture mra_texture;

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

} kmaterial_data;

// ======================================================
// Standard Material
// ======================================================

typedef enum kmaterial_standard_flag_bits {
    MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX = 0x0001,
    MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX = 0x0002,
    MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX = 0x0004,
    MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX = 0x0008,
    MATERIAL_STANDARD_FLAG_USE_AO_TEX = 0x0010,
    MATERIAL_STANDARD_FLAG_USE_MRA_TEX = 0x0020,
    MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX = 0x0040
} kmaterial_standard_flag_bits;

typedef u32 kmaterial_standard_flags;

typedef struct kmaterial_standard_shader_locations {
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
} kmaterial_standard_shader_locations;

// Standard Material Per-frame UBO data
typedef struct kmaterial_standard_frame_uniform_data {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[KMATERIAL_MAX_VIEWS];                              // 256 bytes
    mat4 projection;                                              // 64 bytes
    vec4 view_positions[KMATERIAL_MAX_VIEWS];                     // 64 bytes
    vec4 cascade_splits;                                          // 16 bytes TODO: support more splits? [MATERIAL_MAX_SHADOW_CASCADES];

    // [shadow_bias, delta_time, game_time, padding]
    vec4 params; // 16 bytes
    // [render_mode, use_pcf, padding, padding]
    uvec4 options; // 16 bytes
    vec4 padding;  // 16 bytes
} kmaterial_standard_frame_uniform_data;

// Standard Material Per-group UBO
typedef struct kmaterial_standard_group_uniform_data {
    directional_light_data dir_light;                      // 48 bytes
    point_light_data p_lights[KMATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
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
    vec4 padding2;
    vec4 padding3;
    vec4 padding4;
} kmaterial_standard_group_uniform_data;

// Standard Material Per-draw UBO
typedef struct kmaterial_standard_draw_uniform_data {
    mat4 model;
    vec4 clipping_plane;
    u32 view_index;
    u32 irradiance_cubemap_index;
    vec2 padding;
} kmaterial_standard_draw_uniform_data;

// ======================================================
// Water Material
// ======================================================

// Water Material Per-frame UBO data
typedef struct kmaterial_water_frame_uniform_data {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[KMATERIAL_MAX_VIEWS];                              // 256 bytes
    mat4 projection;                                              // 64 bytes
    vec4 view_positions[KMATERIAL_MAX_VIEWS];                     // 64 bytes
    vec4 cascade_splits;                                          // 16 bytes TODO: support more splits? [MATERIAL_MAX_SHADOW_CASCADES];

    // [shadow_bias, delta_time, game_time, padding]
    vec4 params; // 16 bytes
    // [render_mode, use_pcf, padding, padding]
    uvec4 options; // 16 bytes
    vec4 padding;  // 16 bytes
} kmaterial_water_frame_uniform_data;

// Water Material Per-group UBO
typedef struct kmaterial_water_group_uniform_data {
    directional_light_data dir_light;                      // 48 bytes
    point_light_data p_lights[KMATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
    u32 num_p_lights;
    /** @brief The material lighting model. */
    u32 lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    u32 flags;
    f32 padding;
} kmaterial_water_group_uniform_data;

// Water Material Per-draw UBO
typedef struct kmaterial_water_draw_uniform_data {
    mat4 model;
    u32 irradiance_cubemap_index;
    u32 view_index;
    vec2 padding;
    f32 tiling;
    f32 wave_strength;
    f32 wave_speed;
    f32 padding2;
} kmaterial_water_draw_uniform_data;

typedef struct kmaterial_water_shader_locations {
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
} kmaterial_water_shader_locations;

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

    // Cached handles for various material types' shaders.
    kshader material_standard_shader;
    kmaterial_standard_shader_locations standard_material_locations;

    kshader material_water_shader;
    kmaterial_water_shader_locations water_material_locations;

    kshader material_blended_shader;

    // Pointer to use for material texture inputs _not_ using a texture map (because something has to be bound).
    ktexture default_texture;
    ktexture default_base_colour_texture;
    ktexture default_spec_texture;
    ktexture default_normal_texture;
    // Pointer to a default cubemap to fall back on if no IBL cubemaps are present.
    ktexture default_ibl_cubemap;
    ktexture default_mra_texture;
    ktexture default_water_normal_texture;
    ktexture default_water_dudv_texture;

    // Keep a pointer to the renderer state for quick access.
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct kresource_system_state* resource_system;

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
static kshader get_shader_for_material_type(const kmaterial_system_state* state, kmaterial_type type);
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
    state->resource_system = states->kresource_state;
    state->texture_system = states->texture_system;

    state->config = *typed_config;

    state->materials = darray_reserve(kmaterial_data, config->max_material_count);
    // An array for each material will be created when a material is created.
    state->instances = darray_reserve(kmaterial_instance_data*, config->max_material_count);

    state->default_texture = texture_acquire_sync(kname_create(DEFAULT_TEXTURE_NAME));
    state->default_base_colour_texture = texture_acquire_sync(kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME));
    state->default_spec_texture = texture_acquire_sync(kname_create(DEFAULT_SPECULAR_TEXTURE_NAME));
    state->default_normal_texture = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
    state->default_mra_texture = texture_acquire_sync(kname_create(DEFAULT_MRA_TEXTURE_NAME));
    state->default_ibl_cubemap = texture_cubemap_acquire_sync(kname_create(DEFAULT_CUBE_TEXTURE_NAME));
    state->default_water_normal_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME));
    state->default_water_dudv_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME));

    // Get default material shaders.

    // Standard material shader.
    {
        kname mat_std_shader_name = kname_create(SHADER_NAME_RUNTIME_MATERIAL_STANDARD);
        kasset_shader mat_std_shader = {0};
        mat_std_shader.name = mat_std_shader_name;
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
        mat_std_shader.stages[0].source_asset_name = MATERIAL_STANDARD_NAME_VERT;
        mat_std_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_std_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_std_shader.stages[1].source_asset_name = MATERIAL_STANDARD_NAME_FRAG;

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
        mat_std_shader.attributes[4].type = SHADER_ATTRIB_TYPE_FLOAT32_4;

        mat_std_shader.uniform_count = 9;
        mat_std_shader.uniforms = KALLOC_TYPE_CARRAY(kasset_shader_uniform, mat_std_shader.uniform_count);

        // per_frame
        u32 uidx = 0;
        mat_std_shader.uniforms[uidx].name = "material_frame_ubo";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_standard_frame_uniform_data);
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "shadow_texture";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "irradiance_cube_textures";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_CUBE;
        mat_std_shader.uniforms[uidx].array_size = KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "shadow_sampler";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "irradiance_sampler";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        // per_group
        mat_std_shader.uniforms[uidx].name = "material_group_ubo";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_standard_group_uniform_data);
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "material_textures";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_2D;
        mat_std_shader.uniforms[uidx].array_size = MATERIAL_STANDARD_TEXTURE_COUNT;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        uidx++;

        mat_std_shader.uniforms[uidx].name = "material_samplers";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_SAMPLER;
        mat_std_shader.uniforms[uidx].array_size = MATERIAL_STANDARD_SAMPLER_COUNT;
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_GROUP;
        uidx++;

        // per_draw
        mat_std_shader.uniforms[uidx].name = "material_draw_ubo";
        mat_std_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_STRUCT;
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_standard_draw_uniform_data);
        mat_std_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_DRAW;
        uidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize(&mat_std_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_std_shader.stages, kasset_shader_stage, mat_std_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_std_shader.attributes, kasset_shader_attribute, mat_std_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_std_shader.uniforms, kasset_shader_uniform, mat_std_shader.uniform_count);
        kzero_memory(&mat_std_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        state->material_standard_shader = kshader_system_get_from_source(mat_std_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            state->standard_material_locations.material_frame_ubo = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_frame_ubo"));
            state->standard_material_locations.shadow_texture = kshader_system_uniform_location(state->material_standard_shader, kname_create("shadow_texture"));
            state->standard_material_locations.irradiance_cube_textures = kshader_system_uniform_location(state->material_standard_shader, kname_create("irradiance_cube_textures"));
            state->standard_material_locations.shadow_sampler = kshader_system_uniform_location(state->material_standard_shader, kname_create("shadow_sampler"));
            state->standard_material_locations.irradiance_sampler = kshader_system_uniform_location(state->material_standard_shader, kname_create("irradiance_sampler"));

            // Per group
            state->standard_material_locations.material_textures = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_textures"));
            state->standard_material_locations.material_samplers = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_samplers"));
            state->standard_material_locations.material_group_ubo = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_group_ubo"));

            // Per draw.
            state->standard_material_locations.material_draw_ubo = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_draw_ubo"));
        }
    }

    // Water material shader.
    {
        kname mat_water_shader_name = kname_create(SHADER_NAME_RUNTIME_MATERIAL_WATER);
        kasset_shader mat_water_shader = {0};
        mat_water_shader.name = mat_water_shader_name;
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
        mat_water_shader.stages[0].source_asset_name = MATERIAL_WATER_NAME_VERT;
        mat_water_shader.stages[1].type = SHADER_STAGE_FRAGMENT;
        mat_water_shader.stages[1].package_name = PACKAGE_NAME_RUNTIME;
        mat_water_shader.stages[1].source_asset_name = MATERIAL_WATER_NAME_FRAG;

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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_water_frame_uniform_data);
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "shadow_texture";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY;
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_FRAME;
        uidx++;

        mat_water_shader.uniforms[uidx].name = "irradiance_cube_textures";
        mat_water_shader.uniforms[uidx].type = SHADER_UNIFORM_TYPE_TEXTURE_CUBE;
        mat_water_shader.uniforms[uidx].array_size = KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT;
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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_water_group_uniform_data);
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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_water_draw_uniform_data);
        mat_water_shader.uniforms[uidx].frequency = SHADER_UPDATE_FREQUENCY_PER_DRAW;
        uidx++;

        // Serialize
        const char* config_source = kasset_shader_serialize(&mat_water_shader);

        // Destroy the temp asset.
        KFREE_TYPE_CARRAY(mat_water_shader.stages, kasset_shader_stage, mat_water_shader.stage_count);
        KFREE_TYPE_CARRAY(mat_water_shader.attributes, kasset_shader_attribute, mat_water_shader.attribute_count);
        KFREE_TYPE_CARRAY(mat_water_shader.uniforms, kasset_shader_uniform, mat_water_shader.uniform_count);
        kzero_memory(&mat_water_shader, sizeof(kasset_shader));

        // Create/load the shader from the serialized source.
        state->material_water_shader = kshader_system_get_from_source(mat_water_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            state->water_material_locations.material_frame_ubo = kshader_system_uniform_location(state->material_water_shader, kname_create("material_frame_ubo"));
            state->water_material_locations.shadow_texture = kshader_system_uniform_location(state->material_water_shader, kname_create("shadow_texture"));
            state->water_material_locations.irradiance_cube_textures = kshader_system_uniform_location(state->material_water_shader, kname_create("irradiance_cube_textures"));
            state->water_material_locations.shadow_sampler = kshader_system_uniform_location(state->material_water_shader, kname_create("shadow_sampler"));
            state->water_material_locations.irradiance_sampler = kshader_system_uniform_location(state->material_water_shader, kname_create("irradiance_sampler"));

            // Per group
            state->water_material_locations.material_textures = kshader_system_uniform_location(state->material_water_shader, kname_create("material_textures"));
            state->water_material_locations.material_samplers = kshader_system_uniform_location(state->material_water_shader, kname_create("material_samplers"));
            state->water_material_locations.material_group_ubo = kshader_system_uniform_location(state->material_water_shader, kname_create("material_group_ubo"));

            // Per draw.
            state->water_material_locations.material_draw_ubo = kshader_system_uniform_location(state->material_standard_shader, kname_create("material_draw_ubo"));
        }
    }

    // Blended material shader.
    {
        // TODO: blended materials.
        // state->material_blended_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_MATERIAL_BLENDED));
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
    if (!create_default_blended_material(state)) {
        KFATAL("Failed to create default blended material. Application cannot continue.");
        return false;
    }

    // Register a console command to dump list of materials/references.
    console_command_register("material_system_dump", 0, state, on_material_system_dump);

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

b8 kmaterial_system_prepare_frame(kmaterial_system_state* state, kmaterial_frame_data mat_frame_data, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    // Standard shader type
    {
        kshader shader = state->material_standard_shader;
        kshader_system_use(shader);

        // Ensure wireframe mode is (un)set.
        b8 is_wireframe = (mat_frame_data.render_mode == RENDERER_VIEW_MODE_WIREFRAME);
        kshader_system_set_wireframe(shader, is_wireframe);

        // Setup frame data UBO structure to send over.
        kmaterial_standard_frame_uniform_data frame_ubo = {0};
        frame_ubo.projection = mat_frame_data.projection;
        for (u32 i = 0; i < KMATERIAL_MAX_VIEWS; ++i) {
            frame_ubo.views[i] = mat_frame_data.views[i];
            frame_ubo.view_positions[i] = mat_frame_data.view_positions[i];
        }
        for (u8 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
            frame_ubo.cascade_splits.elements[i] = mat_frame_data.cascade_splits[i];
            frame_ubo.directional_light_spaces[i] = mat_frame_data.directional_light_spaces[i];
        }

        // Set options
        {
            // Get "use pcf" option
            i32 iuse_pcf = 0;
            kvar_i32_get("use_pcf", &iuse_pcf);
            frame_ubo.options.elements[MAT_OPTION_IDX_USE_PCF] = (u32)iuse_pcf;

            frame_ubo.options.elements[MAT_OPTION_IDX_RENDER_MODE] = mat_frame_data.render_mode;
        }

        // Set params
        {
            frame_ubo.params.elements[MAT_PARAM_IDX_DELTA_TIME] = mat_frame_data.delta_time;
            frame_ubo.params.elements[MAT_PARAM_IDX_GAME_TIME] = mat_frame_data.game_time;

            // TODO: These params below should be pulled in from global settings somewhere instead of this way.
            frame_ubo.params.elements[MAT_PARAM_IDX_SHADOW_BIAS] = mat_frame_data.shadow_bias;
        }

        if (!kshader_system_bind_frame(shader)) {
            KERROR("Failed to bind frame frequency for standard material shader.");
            return false;
        }

        // Set the whole UBO at once.
        kshader_system_uniform_set_by_location(shader, state->standard_material_locations.material_frame_ubo, &frame_ubo);

        // Texture maps
        // Shadow map - arrayed texture.
        if (mat_frame_data.shadow_map_texture) {
            kshader_system_texture_set_by_location(shader, state->standard_material_locations.shadow_texture, mat_frame_data.shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = mat_frame_data.irradiance_cubemap_textures[i] ? mat_frame_data.irradiance_cubemap_textures[i] : state->default_ibl_cubemap;
            // FIXME: Check if the texture is loaded.
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            if (!kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.irradiance_cube_textures, i, t)) {
                KERROR("Failed to set ibl cubemap at index %i", i);
            }
        }

        // Apply/upload everything to the GPU
        if (!kshader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }
    }

    // Water shader type
    {
        kshader shader = state->material_water_shader;
        kshader_system_use(shader);

        // Ensure wireframe mode is (un)set.
        b8 is_wireframe = (mat_frame_data.render_mode == RENDERER_VIEW_MODE_WIREFRAME);
        kshader_system_set_wireframe(shader, is_wireframe);

        // Setup frame data UBO structure to send over.
        kmaterial_water_frame_uniform_data frame_ubo = {0};
        frame_ubo.projection = mat_frame_data.projection;
        for (u32 i = 0; i < KMATERIAL_MAX_VIEWS; ++i) {
            frame_ubo.views[i] = mat_frame_data.views[i];
            frame_ubo.view_positions[i] = mat_frame_data.view_positions[i];
        }
        for (u8 i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
            frame_ubo.cascade_splits.elements[i] = mat_frame_data.cascade_splits[i];
            frame_ubo.directional_light_spaces[i] = mat_frame_data.directional_light_spaces[i];
        }

        // Set options
        {
            // Get "use pcf" option
            i32 iuse_pcf = 0;
            kvar_i32_get("use_pcf", &iuse_pcf);
            frame_ubo.options.elements[MAT_OPTION_IDX_USE_PCF] = (u32)iuse_pcf;
            frame_ubo.options.elements[MAT_OPTION_IDX_RENDER_MODE] = mat_frame_data.render_mode;
        }

        // Set params
        {
            frame_ubo.params.elements[MAT_PARAM_IDX_DELTA_TIME] = mat_frame_data.delta_time;
            frame_ubo.params.elements[MAT_PARAM_IDX_GAME_TIME] = mat_frame_data.game_time;

            // TODO: These properties below should be pulled in from global settings somewhere instead of this way.
            frame_ubo.params.elements[MAT_PARAM_IDX_SHADOW_BIAS] = mat_frame_data.shadow_bias;
        }

        if (!kshader_system_bind_frame(shader)) {
            KERROR("Failed to bind frame frequency for water material shader.");
            return false;
        }

        // Set the whole UBO at once.
        kshader_system_uniform_set_by_location(shader, state->water_material_locations.material_frame_ubo, &frame_ubo);

        // Texture maps
        // Shadow map - arrayed texture.
        if (mat_frame_data.shadow_map_texture) {
            kshader_system_texture_set_by_location(shader, state->water_material_locations.shadow_texture, mat_frame_data.shadow_map_texture);
        }

        // Irradiance textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = mat_frame_data.irradiance_cubemap_textures[i] ? mat_frame_data.irradiance_cubemap_textures[i] : state->default_ibl_cubemap;
            // FIXME: Check if the texture is loaded.
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.irradiance_cube_textures, i, t);
        }

        // Apply/upload everything to the GPU
        if (!kshader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }
    }

    // TODO: Blended

    return true;
}

b8 kmaterial_system_apply(kmaterial_system_state* state, kmaterial material, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    kmaterial_data* base_material = &state->materials[material];

    kshader shader;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        return false;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        kshader_system_use(shader);

        if (!kshader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }

        // per-group - ensure this is done once per frame per material

        // bind per-group
        if (!kshader_system_bind_group(shader, base_material->group_id)) {
            KERROR("Failed to bind material shader group.");
            return false;
        }

        // Setup frame data UBO structure to send over.
        kmaterial_standard_group_uniform_data group_ubo = {0};
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
        group_ubo.num_p_lights = KMIN(light_system_point_light_count(), KMATERIAL_MAX_POINT_LIGHTS);
        if (group_ubo.num_p_lights) {
            point_light p_lights[KMATERIAL_MAX_POINT_LIGHTS];
            kzero_memory(p_lights, sizeof(point_light) * KMATERIAL_MAX_POINT_LIGHTS);

            light_system_point_lights_get(p_lights);

            for (u32 i = 0; i < group_ubo.num_p_lights; ++i) {
                group_ubo.p_lights[i] = p_lights[i].data;
            }
        }

        // Inputs - Bind the texture if used.

        // Base colour
        if (texture_is_loaded(base_material->base_colour_texture)) {
            group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX, true);
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_BASE_COLOUR, base_material->base_colour_texture);
        } else {
            group_ubo.base_colour = base_material->base_colour;
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_BASE_COLOUR, state->default_texture);
        }

        // Normal
        if (FLAG_GET(base_material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)) {
            if (texture_is_loaded(base_material->normal_texture)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX, true);
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_NORMAL, base_material->normal_texture);
            } else {
                group_ubo.normal = base_material->normal;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_NORMAL, state->default_normal_texture);
            }
        } else {
            group_ubo.normal = vec3_up();
            // Still need this set.
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_NORMAL, state->default_normal_texture);
        }

        // MRA
        b8 mra_enabled = FLAG_GET(base_material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT);
        if (mra_enabled) {
            if (texture_is_loaded(base_material->mra_texture)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX, true);
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_MRA, base_material->mra_texture);
            } else {
                group_ubo.mra = base_material->mra;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_MRA, state->default_mra_texture);
            }

            // Even though MRA is being used, still need to bind something for these.
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_METALLIC, state->default_texture);
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_ROUGHNESS, state->default_texture);
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_AO, state->default_texture);
        } else {

            // Still have to bind something to MRA.
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_MRA, state->default_mra_texture);

            // If not using MRA, then do these:

            // Metallic
            if (texture_is_loaded(base_material->metallic_texture)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX, true);
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_METALLIC, base_material->metallic_texture);
            } else {
                group_ubo.metallic = base_material->metallic;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_METALLIC, state->default_texture);
            }

            // Roughness
            if (texture_is_loaded(base_material->roughness_texture)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX, true);
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_ROUGHNESS, base_material->roughness_texture);
            } else {
                group_ubo.roughness = base_material->roughness;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_ROUGHNESS, state->default_texture);
            }

            // AO
            if (FLAG_GET(base_material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) {
                if (texture_is_loaded(base_material->ao_texture)) {
                    group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX, true);
                    kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_AO, base_material->ao_texture);
                } else {
                    group_ubo.ao = base_material->ao;
                    kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_AO, state->default_texture);
                }
            } else {
                group_ubo.ao = 1.0f;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_AO, state->default_texture);
            }

            // Pack source channels. [Metallic, roughness, ao, unused].
            group_ubo.texture_channels = pack_u8_into_u32(base_material->metallic_texture_channel, base_material->roughness_texture_channel, base_material->ao_texture_channel, 0);
        }

        // Emissive
        if (FLAG_GET(base_material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) {
            if (texture_is_loaded(base_material->emissive_texture)) {
                group_ubo.tex_flags = FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX, true);
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_EMISSIVE, base_material->emissive_texture);
            } else {
                group_ubo.emissive = base_material->emissive;
                kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_EMISSIVE, state->default_texture);
            }
        } else {
            group_ubo.emissive = vec4_zero();
            kshader_system_texture_set_by_location_arrayed(shader, state->standard_material_locations.material_textures, MAT_STANDARD_IDX_EMISSIVE, state->default_texture);
        }

        // Set the whole thing at once.
        kshader_system_uniform_set_by_location(shader, state->standard_material_locations.material_group_ubo, &group_ubo);

        // Apply/upload them to the GPU
        return kshader_system_apply_per_group(shader);
    }
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;
        kshader_system_use(shader);

        // Need to reapply per-frame so descriptors are bound, etc.
        if (!kshader_system_apply_per_frame(shader)) {
            KERROR("Failed to apply per-frame uniforms.");
            return false;
        }

        // per-group - ensure this is done once per frame per material

        // bind per-group
        if (!kshader_system_bind_group(shader, base_material->group_id)) {
            KERROR("Failed to bind water material shader group.");
            return false;
        }

        // Setup frame data UBO structure to send over.
        kmaterial_water_group_uniform_data group_ubo = {0};
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
        group_ubo.num_p_lights = KMIN(light_system_point_light_count(), KMATERIAL_MAX_POINT_LIGHTS);
        if (group_ubo.num_p_lights) {
            point_light p_lights[KMATERIAL_MAX_POINT_LIGHTS];
            kzero_memory(p_lights, sizeof(point_light) * KMATERIAL_MAX_POINT_LIGHTS);

            light_system_point_lights_get(p_lights);

            for (u32 i = 0; i < group_ubo.num_p_lights; ++i) {
                group_ubo.p_lights[i] = p_lights[i].data;
            }
        }

        // Reflection texture.
        {
            ktexture t = base_material->reflection_texture;
            if (!texture_is_loaded(t)) {
                t = state->default_texture;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFLECTION, t);
        }

        // Refraction texture.
        {
            ktexture t = base_material->refraction_texture;
            if (!texture_is_loaded(t)) {
                t = state->default_texture;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFRACTION, t);
        }

        // Refraction depth texture.
        {
            ktexture t = base_material->refraction_depth_texture;
            if (texture_is_loaded(t)) {
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_REFRACTION_DEPTH, t);
        }

        // DUDV texture.
        {
            ktexture t = base_material->dudv_texture;
            if (!texture_is_loaded(t)) {
                t = state->default_normal_texture; // default_water_dudv_texture;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_DUDV, t);
        }

        // Water Normal texture.
        {
            ktexture t = base_material->normal_texture;
            if (!texture_is_loaded(t)) {
                t = state->default_normal_texture; // default_water_normal_texture;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->water_material_locations.material_textures, MAT_WATER_IDX_NORMAL, t);
        }

        // Set the whole thing at once.
        kshader_system_uniform_set_by_location(shader, state->water_material_locations.material_group_ubo, &group_ubo);

        // Apply/upload them to the GPU
        return kshader_system_apply_per_group(shader);
    }
    case KMATERIAL_TYPE_BLENDED:
        shader = state->material_blended_shader;
        return false;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Not yet implemented!");
        return false;
    }
}

b8 kmaterial_system_apply_instance(kmaterial_system_state* state, const kmaterial_instance* instance, struct kmaterial_instance_draw_data draw_data, frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    kmaterial_instance_data* mat_inst_data = get_kmaterial_instance_data(state, *instance);
    if (!mat_inst_data) {
        return false;
    }
    kmaterial_data* base_material = &state->materials[instance->base_material];

    kshader shader;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        return false;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        kshader_system_use(shader);

        // per-draw - this gets run every time apply is called
        // bind per-draw
        if (!kshader_system_bind_draw_id(shader, mat_inst_data->per_draw_id)) {
            KERROR("Failed to bind standard material shader draw id.");
            return false;
        }

        // Update uniform data
        kmaterial_standard_draw_uniform_data draw_ubo = {0};
        draw_ubo.clipping_plane = draw_data.clipping_plane;
        draw_ubo.model = draw_data.model;
        draw_ubo.irradiance_cubemap_index = draw_data.irradiance_cubemap_index;
        draw_ubo.view_index = draw_data.view_index;

        // Set the whole thing at once.
        kshader_system_uniform_set_by_location(shader, state->standard_material_locations.material_draw_ubo, &draw_ubo);

        // apply per-draw
        return kshader_system_apply_per_draw(shader);
    }
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;
        kshader_system_use(shader);

        // per-draw - this gets run every time apply is called
        // bind per-draw
        if (!kshader_system_bind_draw_id(shader, mat_inst_data->per_draw_id)) {
            KERROR("Failed to bind water material shader draw id.");
            return false;
        }

        // Update uniform data
        kmaterial_water_draw_uniform_data draw_ubo = {0};
        draw_ubo.model = draw_data.model;
        draw_ubo.irradiance_cubemap_index = draw_data.irradiance_cubemap_index;
        draw_ubo.view_index = draw_data.view_index;
        // TODO: Pull in instance-specific overrides for these, if set.
        draw_ubo.tiling = base_material->tiling;
        draw_ubo.wave_speed = base_material->wave_speed;
        draw_ubo.wave_strength = base_material->wave_strength;

        // Set the whole thing at once.
        kshader_system_uniform_set_by_location(shader, state->water_material_locations.material_draw_ubo, &draw_ubo);

        // apply per-draw
        return kshader_system_apply_per_draw(shader);
    }
    case KMATERIAL_TYPE_BLENDED:
        shader = state->material_blended_shader;
        return false;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Not yet implemented!");
        return false;
    }
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
    kname material_name = kname_create(KMATERIAL_DEFAULT_NAME_STANDARD);

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
    kname material_name = kname_create(KMATERIAL_DEFAULT_NAME_WATER);

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

static kshader get_shader_for_material_type(const kmaterial_system_state* state, kmaterial_type type) {
    switch (type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KERROR("Cannot get shader for a material using an 'unknown' material type.");
        return KSHADER_INVALID;
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
        return KSHADER_INVALID;
    }
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

    // Select shader.
    kshader material_shader = get_shader_for_material_type(state, material->type);
    if (material_shader == KSHADER_INVALID) {
        // TODO: invalidate handle/entry?
        return false;
    }

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

    // Create a group for the material.
    if (!kshader_system_shader_group_acquire(material_shader, &material->group_id)) {
        KERROR("Failed to acquire shader group while creating material. See logs for details.");
        // TODO: destroy/release
        return false;
    }

    // TODO: Custom samplers.

    material->state = KMATERIAL_STATE_LOADED;

    return true;
}

static void material_destroy(kmaterial_system_state* state, kmaterial_data* material, u32 material_index) {
    KASSERT_MSG(material, "Tried to destroy null material.");

    // Immediately mark it as unavailable for use.
    material->state = KMATERIAL_STATE_UNINITIALIZED;

    // Select shader.
    kshader material_shader = get_shader_for_material_type(state, material->type);
    if (material_shader == KSHADER_INVALID) {
        KWARN("Attempting to release material that had an invalid shader.");
        return;
    }

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

    // Release the group for the material.
    if (!kshader_system_shader_group_release(material_shader, material->group_id)) {
        KWARN("Failed to release shader group while creating material. See logs for details.");
    }

    // TODO: Custom samplers.

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

        // Get per-draw resources for the instance.
        if (!renderer_shader_per_draw_resources_acquire(state->renderer, get_shader_for_material_type(state, material->type), &inst->per_draw_id)) {
            KERROR("Failed to create per-draw resources for a material instance. Instance creation failed.");
            inst->state = KMATERIAL_INSTANCE_STATE_UNINITIALIZED;
            return false;
        }

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

        // Release per-draw resources for the instance.
        renderer_shader_per_draw_resources_release(state->renderer, get_shader_for_material_type(state, base_material->type), inst->per_draw_id);

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
            // Get per-draw resources for the instance.
            if (!renderer_shader_per_draw_resources_acquire(state->renderer, get_shader_for_material_type(state, material->type), &inst->per_draw_id)) {
                KERROR("Failed to create per-draw resources for a material instance. Instance creation failed.");
                inst->state = KMATERIAL_INSTANCE_STATE_UNINITIALIZED;
                continue;
            }

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
