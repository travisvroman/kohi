#include "kmaterial_renderer.h"
#include "assets/kasset_types.h"
#include "core/engine.h"
#include "core/kvar.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_types.h"
#include "runtime_defines.h"
#include "serializers/kasset_shader_serializer.h"
#include "systems/kmaterial_system.h"
#include "systems/kshader_system.h"
#include "systems/light_system.h"
#include "systems/texture_system.h"

#define MATERIAL_STANDARD_NAME_FRAG "Shader.MaterialStandard_frag"
#define MATERIAL_STANDARD_NAME_VERT "Shader.MaterialStandard_vert"
#define MATERIAL_WATER_NAME_FRAG "Shader.MaterialWater_frag"
#define MATERIAL_WATER_NAME_VERT "Shader.MaterialWater_vert"
#define MATERIAL_BLENDED_NAME_FRAG "Shader.MaterialBlended_frag"
#define MATERIAL_BLENDED_NAME_VERT "Shader.MaterialBlended_vert"

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

#define MATERIAL_WATER_TEXTURE_COUNT 5
#define MATERIAL_WATER_SAMPLER_COUNT 5

// Standard material texture indices
const u32 MAT_STANDARD_IDX_BASE_COLOUR = 0;
const u32 MAT_STANDARD_IDX_NORMAL = 1;
const u32 MAT_STANDARD_IDX_METALLIC = 2;
const u32 MAT_STANDARD_IDX_ROUGHNESS = 3;
const u32 MAT_STANDARD_IDX_AO = 4;
const u32 MAT_STANDARD_IDX_MRA = 5;
const u32 MAT_STANDARD_IDX_EMISSIVE = 6;

// Water material texture indices
const u32 MAT_WATER_IDX_REFLECTION = 0;
const u32 MAT_WATER_IDX_REFRACTION = 1;
const u32 MAT_WATER_IDX_REFRACTION_DEPTH = 2;
const u32 MAT_WATER_IDX_DUDV = 3;
const u32 MAT_WATER_IDX_NORMAL = 4;

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

b8 kmaterial_renderer_initialize(kmaterial_renderer* out_state, u32 max_material_count, u32 max_material_instance_count) {

    out_state->default_texture = texture_acquire_sync(kname_create(DEFAULT_TEXTURE_NAME));
    out_state->default_base_colour_texture = texture_acquire_sync(kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME));
    out_state->default_spec_texture = texture_acquire_sync(kname_create(DEFAULT_SPECULAR_TEXTURE_NAME));
    out_state->default_normal_texture = texture_acquire_sync(kname_create(DEFAULT_NORMAL_TEXTURE_NAME));
    out_state->default_mra_texture = texture_acquire_sync(kname_create(DEFAULT_MRA_TEXTURE_NAME));
    out_state->default_ibl_cubemap = texture_cubemap_acquire_sync(kname_create(DEFAULT_CUBE_TEXTURE_NAME));
    out_state->default_water_normal_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME));
    out_state->default_water_dudv_texture = texture_acquire_sync(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME));

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
        mat_std_shader.max_groups = max_material_count;
        mat_std_shader.max_draw_ids = max_material_instance_count;
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
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_global_uniform_data);
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
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_standard_base_uniform_data);
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
        mat_std_shader.uniforms[uidx].size = sizeof(kmaterial_standard_instance_uniform_data);
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
        out_state->material_standard_shader = kshader_system_get_from_source(mat_std_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            out_state->material_standard_locations.material_frame_ubo = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_frame_ubo"));
            out_state->material_standard_locations.shadow_texture = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("shadow_texture"));
            out_state->material_standard_locations.irradiance_cube_textures = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("irradiance_cube_textures"));
            out_state->material_standard_locations.shadow_sampler = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("shadow_sampler"));
            out_state->material_standard_locations.irradiance_sampler = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("irradiance_sampler"));

            // Per group
            out_state->material_standard_locations.material_textures = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_textures"));
            out_state->material_standard_locations.material_samplers = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_samplers"));
            out_state->material_standard_locations.material_group_ubo = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_group_ubo"));

            // Per draw.
            out_state->material_standard_locations.material_draw_ubo = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_draw_ubo"));
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
        mat_water_shader.max_groups = max_material_count;
        mat_water_shader.max_draw_ids = max_material_instance_count;
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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_global_uniform_data);
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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_water_base_uniform_data);
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
        mat_water_shader.uniforms[uidx].size = sizeof(kmaterial_water_instance_uniform_data);
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
        out_state->material_water_shader = kshader_system_get_from_source(mat_water_shader_name, config_source);

        // Save off the shader's uniform locations.
        {
            // Per frame
            out_state->material_water_locations.material_frame_ubo = kshader_system_uniform_location(out_state->material_water_shader, kname_create("material_frame_ubo"));
            out_state->material_water_locations.shadow_texture = kshader_system_uniform_location(out_state->material_water_shader, kname_create("shadow_texture"));
            out_state->material_water_locations.irradiance_cube_textures = kshader_system_uniform_location(out_state->material_water_shader, kname_create("irradiance_cube_textures"));
            out_state->material_water_locations.shadow_sampler = kshader_system_uniform_location(out_state->material_water_shader, kname_create("shadow_sampler"));
            out_state->material_water_locations.irradiance_sampler = kshader_system_uniform_location(out_state->material_water_shader, kname_create("irradiance_sampler"));

            // Per group
            out_state->material_water_locations.material_textures = kshader_system_uniform_location(out_state->material_water_shader, kname_create("material_textures"));
            out_state->material_water_locations.material_samplers = kshader_system_uniform_location(out_state->material_water_shader, kname_create("material_samplers"));
            out_state->material_water_locations.material_group_ubo = kshader_system_uniform_location(out_state->material_water_shader, kname_create("material_group_ubo"));

            // Per draw.
            out_state->material_water_locations.material_draw_ubo = kshader_system_uniform_location(out_state->material_standard_shader, kname_create("material_draw_ubo"));
        }
    }

    // Blended material shader.
    {
        // TODO: blended materials.
        // state->material_blended_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_MATERIAL_BLENDED));
    }
}

b8 kmaterial_renderer_shutdown(kmaterial_renderer* state) {
}

b8 kmaterial_renderer_update(kmaterial_renderer* state) {

    // Get "use pcf" option
    i32 iuse_pcf = 0;
    kvar_i32_get("use_pcf", &iuse_pcf);
    kmaterial_renderer_set_pcf_enabled(state, (b8)iuse_pcf);
}

void kmaterial_renderer_register_base(kmaterial_renderer* state, kmaterial base) {
}
void kmaterial_renderer_unregister_base(kmaterial_renderer* state, kmaterial base) {
}

void kmaterial_renderer_register_instance(kmaterial_renderer* state, kmaterial_instance instance) {
}
void kmaterial_renderer_unregister_instance(kmaterial_renderer* state, kmaterial_instance instance) {
}

void kmaterial_renderer_set_render_mode(kmaterial_renderer* state, renderer_debug_view_mode renderer_mode) {
    state->global_data.options.elements[MAT_OPTION_IDX_RENDER_MODE] = renderer_mode;
}

void kmaterial_renderer_set_pcf_enabled(kmaterial_renderer* state, b8 pcf_enabled) {
    state->global_data.options.elements[MAT_OPTION_IDX_USE_PCF] = pcf_enabled;
}

void kmaterial_renderer_set_shadow_bias(kmaterial_renderer* state, f32 shadow_bias) {
    state->global_data.params.elements[MAT_PARAM_IDX_SHADOW_BIAS] = shadow_bias;
}

void kmaterial_renderer_set_delta_game_times(kmaterial_renderer* state, f32 delta_time, f32 game_time) {
    state->global_data.params.elements[MAT_PARAM_IDX_DELTA_TIME] = delta_time;
    state->global_data.params.elements[MAT_PARAM_IDX_GAME_TIME] = game_time;
}

void kmaterial_renderer_set_directional_light(kmaterial_renderer* state, const directional_light* dir_light) {
    KASSERT_DEBUG(state);
    KASSERT_DEBUG(dir_light);
    state->global_data.dir_light.colour = dir_light->data.colour;
    state->global_data.dir_light.direction = dir_light->data.direction;
    state->global_data.dir_light.shadow_distance = dir_light->data.shadow_distance;
    state->global_data.dir_light.shadow_fade_distance = dir_light->data.shadow_fade_distance;
    state->global_data.dir_light.shadow_split_mult = dir_light->data.shadow_split_mult;
}

// Sets global point light data for the entire scene.
// NOTE: count exceeding KMATERIAL_MAX_GLOBAL_POINT_LIGHTS will be ignored
void kmaterial_renderer_set_point_lights(kmaterial_renderer* state, u8 point_light_count, point_light* point_lights) {
    KASSERT_DEBUG(state);

    u8 count = KMIN(KMATERIAL_MAX_GLOBAL_POINT_LIGHTS, point_light_count);
    for (u8 i = 0; i < count; ++i) {
        point_light* p = &point_lights[i];
        kpoint_light_uniform_data* gpl = &state->global_data.global_point_lights[i];

        gpl->colour = p->data.colour;
        // Linear stored in colour.a
        gpl->colour.a = p->data.linear;
        gpl->position = p->data.position;
        // Quadratic stored in position.w
        gpl->position.w = p->data.quadratic;
    }
}

void kmaterial_renderer_set_matrices(kmaterial_renderer* state, mat4 projection, mat4 view) {
    state->global_data.projection = projection;
    state->global_data.view = view;
    state->global_data.view_position = vec3_to_vec4(mat4_position(view), 1.0f);
}

void kmaterial_renderer_set_shadow_map_texture(kmaterial_renderer* state, ktexture shadow_map_texture) {
    state->shadow_map_texture = shadow_map_texture;
}

void kmaterial_renderer_set_ibl_cubemap_textures(kmaterial_renderer* state, u32 count, ktexture* ibl_cubemap_textures) {
    if (state->ibl_cubemap_texture_count != count) {
        if (state->ibl_cubemap_textures) {
            kfree(state->ibl_cubemap_textures, sizeof(ktexture) * state->ibl_cubemap_texture_count, MEMORY_TAG_ARRAY);
            state->ibl_cubemap_textures = KALLOC_TYPE_CARRAY(ktexture, count);
            state->ibl_cubemap_texture_count = count;
        }
    }
    KCOPY_TYPE_CARRAY(state->ibl_cubemap_textures, ibl_cubemap_textures, ktexture, count);
}

void kmaterial_renderer_apply_globals(kmaterial_renderer* state) {

    b8 is_wireframe = (state->global_data.options.elements[MAT_OPTION_IDX_RENDER_MODE] == RENDERER_VIEW_MODE_WIREFRAME);

    // TODO: Set standard shader globals
    {
        kshader shader = state->material_standard_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // Ensure wireframe mode is (un)set.
        KASSERT_DEBUG(kshader_system_set_wireframe(shader, is_wireframe));

        KASSERT_DEBUG(kshader_system_bind_frame(shader));

        // Set UBO
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_standard_locations.material_frame_ubo, &state->global_data));

        // Set texture maps.
        if (state->shadow_map_texture != INVALID_KTEXTURE) {
            kshader_system_texture_set_by_location(shader, state->material_standard_locations.shadow_texture, state->shadow_map_texture);
        }

        // Irradience textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = state->ibl_cubemap_textures[i] ? state->ibl_cubemap_textures[i] : state->default_ibl_cubemap;
            // FIXME: Check if the texture is loaded.
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            if (!kshader_system_texture_set_by_location_arrayed(shader, state->material_standard_locations.irradiance_cube_textures, i, t)) {
                KERROR("Failed to set ibl cubemap at index %i", i);
            }
        }

        // Apply/upload everything to the GPU
        KASSERT_DEBUG(kshader_system_apply_per_frame(shader));
    }
    // Set water shader globals
    {
        kshader shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // Ensure wireframe mode is (un)set.
        KASSERT_DEBUG(kshader_system_set_wireframe(shader, is_wireframe));

        KASSERT_DEBUG(kshader_system_bind_frame(shader));

        // Set the whole UBO at once.
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_water_locations.material_frame_ubo, &state->global_data));

        // Texture maps
        // Shadow map - arrayed texture.
        if (state->shadow_map_texture) {
            kshader_system_texture_set_by_location(shader, state->material_water_locations.shadow_texture, state->shadow_map_texture);
        }

        // Irradiance textures provided by probes around in the world.
        for (u32 i = 0; i < KMATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT; ++i) {
            ktexture t = state->ibl_cubemap_textures[i] ? state->ibl_cubemap_textures[i] : state->default_ibl_cubemap;
            // FIXME: Check if the texture is loaded.
            if (!texture_is_loaded(t)) {
                t = state->default_ibl_cubemap;
            }
            kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.irradiance_cube_textures, i, t);
        }

        // Apply/upload everything to the GPU
        KASSERT_DEBUG(kshader_system_apply_per_frame(shader));
    }

    // TODO: Set blended shader globals
}

// Updates and binds base material.
void kmaterial_renderer_bind_base(kmaterial_renderer* state, kmaterial base_material) {
    KASSERT_DEBUG(state);

    const kmaterial_data* material = kmaterial_get_base_material_data(engine_systems_get()->material_system, base_material);
    KASSERT_DEBUG(material);

    kshader shader = KSHADER_INVALID;

    switch (material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown shader type cannot be applied.");
        break;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        kshader_system_use(shader);

        // bind per-group (i.e. base material)
        KASSERT_DEBUG(kshader_system_bind_group(shader, material->group_id));
        kmaterial_standard_base_uniform_data group_ubo = {
            .flags = material->flags,
            .lighting_model = material->model,
            .uv_offset = material->uv_offset,
            .uv_scale = material->uv_scale,
            .refraction_scale = 0,           // TODO: implement this once refraction is supported to standard materials.
            .emissive_texture_intensity = 0, // TODO: emissive support
        };

        // --------------------------------------------
        // Texture inputs - bind each texture if used.
        // --------------------------------------------

        // Base colour
        group_ubo.base_colour = material->base_colour;
        ktexture base_colour_tex = state->default_base_colour_texture;
        if (texture_is_loaded(material->base_colour_texture)) {
            FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_BASE_COLOUR_TEX, true);
            base_colour_tex = material->base_colour_texture;
        }
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(
            shader,
            state->material_standard_locations.material_textures,
            MAT_STANDARD_IDX_BASE_COLOUR,
            base_colour_tex));

        // Normal, if used
        group_ubo.normal = vec3_up();
        ktexture normal_tex = state->default_normal_texture;
        if (FLAG_GET(material->flags, KMATERIAL_FLAG_NORMAL_ENABLED_BIT)) {
            group_ubo.normal = material->normal;
            if (texture_is_loaded(material->normal_texture)) {
                FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_NORMAL_TEX, true);
                normal_tex = material->normal_texture;
            }
        }
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(
            shader,
            state->material_standard_locations.material_textures,
            MAT_STANDARD_IDX_NORMAL,
            normal_tex));

        // MRA (Metallic/Roughness/AO)
        b8 mra_enabled = FLAG_GET(material->flags, KMATERIAL_FLAG_MRA_ENABLED_BIT);
        ktexture mra_texture = state->default_mra_texture;
        ktexture metallic_texture = state->default_base_colour_texture;
        ktexture roughness_texture = state->default_base_colour_texture;
        ktexture ao_texture = state->default_base_colour_texture;
        if (mra_enabled) {
            // Use the MRA texture or fallback to the MRA value on the material.
            if (texture_is_loaded(material->mra_texture)) {
                FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_MRA_TEX, true);
                mra_texture = material->mra_texture;
            } else {
                group_ubo.mra = material->mra;
            }
        } else {
            // If not using MRA, then do these:

            // Metallic texture or value
            if (texture_is_loaded(material->metallic_texture)) {
                FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_METALLIC_TEX, true);
                metallic_texture = material->metallic_texture;
            } else {
                group_ubo.metallic = material->metallic;
            }

            // Roughness texture or value
            if (texture_is_loaded(material->roughness_texture)) {
                FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_ROUGHNESS_TEX, true);
                roughness_texture = material->roughness_texture;
            } else {
                group_ubo.roughness = material->roughness;
            }

            // AO texture or value (if enabled)
            if (FLAG_GET(material->flags, KMATERIAL_FLAG_AO_ENABLED_BIT)) {
                if (texture_is_loaded(material->ao_texture)) {
                    FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_AO_TEX, true);
                    ao_texture = material->ao_texture;
                } else {
                    group_ubo.ao = material->ao;
                }
            } else {
                group_ubo.ao = 1.0f;
            }

            // Pack source channels. [Metallic, roughness, ao, unused].
            group_ubo.texture_channels = pack_u8_into_u32(material->metallic_texture_channel, material->roughness_texture_channel, material->ao_texture_channel, 0);
        }

        // Apply textures
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_standard_locations.material_textures, MAT_STANDARD_IDX_MRA, mra_texture));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_standard_locations.material_textures, MAT_STANDARD_IDX_METALLIC, metallic_texture));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_standard_locations.material_textures, MAT_STANDARD_IDX_ROUGHNESS, roughness_texture));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_standard_locations.material_textures, MAT_STANDARD_IDX_AO, ao_texture));

        // Emissive
        ktexture emissive_texture = state->default_base_colour_texture;
        if (FLAG_GET(material->flags, KMATERIAL_FLAG_EMISSIVE_ENABLED_BIT)) {
            if (texture_is_loaded(material->emissive_texture)) {
                FLAG_SET(group_ubo.tex_flags, MATERIAL_STANDARD_FLAG_USE_EMISSIVE_TEX, true);
                emissive_texture = material->emissive_texture;
            } else {
                group_ubo.emissive = material->emissive;
            }
        } else {
            group_ubo.emissive = vec4_zero();
        }

        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(
            shader,
            state->material_standard_locations.material_textures,
            MAT_STANDARD_IDX_EMISSIVE,
            emissive_texture));

        // Set the group/base material UBO.
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_standard_locations.material_group_ubo, &group_ubo));

        // Apply/upload uniforms to the GPU
        KASSERT_DEBUG(kshader_system_apply_per_group(shader));
    } break;
    case KMATERIAL_TYPE_WATER: {

        shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // bind per-group (i.e. base material)
        KASSERT_DEBUG(kshader_system_bind_group(shader, material->group_id));
        kmaterial_water_base_uniform_data group_ubo = {
            .flags = material->flags,
            .lighting_model = material->model,
        };

        ktexture reflection_colour_tex = texture_is_loaded(material->reflection_texture) ? material->reflection_texture : state->default_texture;
        ktexture refraction_colour_tex = texture_is_loaded(material->refraction_texture) ? material->refraction_texture : state->default_texture;
        ktexture reflection_depth_tex = texture_is_loaded(material->reflection_depth_texture) ? material->reflection_depth_texture : state->default_texture;
        ktexture dudv_texture = texture_is_loaded(material->dudv_texture) ? material->dudv_texture : state->default_texture;
        ktexture normal_texture = texture_is_loaded(material->normal_texture) ? material->normal_texture : state->default_normal_texture;

        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.material_textures, MAT_WATER_IDX_REFLECTION, reflection_colour_tex));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.material_textures, MAT_WATER_IDX_REFRACTION, refraction_colour_tex));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.material_textures, MAT_WATER_IDX_REFRACTION_DEPTH, reflection_depth_tex));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.material_textures, MAT_WATER_IDX_DUDV, dudv_texture));
        KASSERT_DEBUG(kshader_system_texture_set_by_location_arrayed(shader, state->material_water_locations.material_textures, MAT_WATER_IDX_NORMAL, normal_texture));

        // Set the group/base material UBO.
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_water_locations.material_group_ubo, &group_ubo));

        // Apply/upload uniforms to the GPU
        KASSERT_DEBUG(kshader_system_apply_per_group(shader));
    } break;
    case KMATERIAL_TYPE_BLENDED: {
        shader = state->material_blended_shader;
        KASSERT_MSG(false, "Blended materials not yet supported.");
    } break;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Custom materials not yet supported.");
        break;
    }
}

// Updates and binds material instance using the provided lighting information.
void kmaterial_renderer_bind_instance(kmaterial_renderer* state, kmaterial_instance instance, mat4 model, vec4 clipping_plane, u8 point_light_count, const u8* point_light_indices) {
    KASSERT_DEBUG(state);

    const kmaterial_instance_data* instance_data = kmaterial_get_material_instance_data(engine_systems_get()->material_system, instance);
    KASSERT_DEBUG(instance_data);

    const kmaterial_data* base_material = kmaterial_get_base_material_data(engine_systems_get()->material_system, instance.base_material);
    KASSERT_DEBUG(base_material);

    // Pack point light indices
    uvec2 packed_point_light_indices = {0};
    u8 written = 0;
    for (u8 i = 0; i < 2 && written < point_light_count; ++i) {
        u32 vi = 0;

        for (u8 p = 0; p < 4 && written < point_light_count; ++p) {

            // Pack the u8 into the given u32
            vi |= ((u32)point_light_indices[written] << ((3 - p) * 8));
            ++written;
        }

        // Store the packed u32
        packed_point_light_indices.elements[i] = vi;
    }

    kshader shader = KSHADER_INVALID;

    switch (base_material->type) {
    default:
    case KMATERIAL_TYPE_UNKNOWN:
        KASSERT_MSG(false, "Unknown material type cannot be applied.");
        break;
    case KMATERIAL_TYPE_STANDARD: {
        shader = state->material_standard_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // bind per-draw/material instance
        KASSERT_DEBUG(kshader_system_bind_draw_id(shader, instance_data->per_draw_id));

        // Setup the UBO
        kmaterial_standard_instance_uniform_data inst_ubo_data = {
            .num_p_lights = point_light_count,
            .irradiance_cubemap_index = 0, // TODO: Multiple IBL cubemap support.
            .packed_point_light_indices = packed_point_light_indices,
            .model = model,
            .clipping_plane = clipping_plane};

        // Upload the data
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_standard_locations.material_draw_ubo, &inst_ubo_data));

        KASSERT_DEBUG(kshader_system_apply_per_draw(shader));
    } break;
    case KMATERIAL_TYPE_WATER: {
        shader = state->material_water_shader;
        KASSERT_DEBUG(kshader_system_use(shader));

        // bind per-draw/material instance
        KASSERT_DEBUG(kshader_system_bind_draw_id(shader, instance_data->per_draw_id));

        // Setup the UBO
        kmaterial_water_instance_uniform_data inst_ubo_data = {
            .num_p_lights = point_light_count,
            .irradiance_cubemap_index = 0, // TODO: Multiple IBL cubemap support.
            .packed_point_light_indices = packed_point_light_indices,
            .model = model,
            .tiling = base_material->tiling,
            .wave_strength = base_material->wave_strength,
            .wave_speed = base_material->wave_speed};

        // Upload the data
        KASSERT_DEBUG(kshader_system_uniform_set_by_location(shader, state->material_water_locations.material_draw_ubo, &inst_ubo_data));

        KASSERT_DEBUG(kshader_system_apply_per_draw(shader));
    } break;
    case KMATERIAL_TYPE_BLENDED: {
        shader = state->material_blended_shader;
        KASSERT_MSG(false, "Blended materials not yet supported.");
    } break;
    case KMATERIAL_TYPE_CUSTOM:
        KASSERT_MSG(false, "Custom materials not yet supported.");
        break;
    }
}
