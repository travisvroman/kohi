#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint KMATERIAL_MAX_GLOBAL_POINT_LIGHTS = 64;
const uint KMATERIAL_MAX_SHADOW_CASCADES = 4;
const uint KMATERIAL_MAX_WATER_PLANES = 4;
// One view for regular camera, plus one reflection view per water plane.
const uint KMATERIAL_MAX_VIEWS = KMATERIAL_MAX_WATER_PLANES + 1;
const uint MATERIAL_WATER_TEXTURE_COUNT = 5;
const uint MATERIAL_WATER_SAMPLER_COUNT = 5;
const uint MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT = 4;

struct directional_light {
    vec4 colour;
    vec4 direction;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;
    float padding;
};

struct point_light {
    // .rgb = colour, .a = linear 
    vec4 colour;
    // .xyz = position, .w = quadratic
    vec4 position;
};

/** 
 * Used to convert from NDC -> UVW by taking the x/y components and transforming them:
 * 
 *   xy *= 0.5 + 0.5
 */
const mat4 ndc_to_uvw = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

// =========================================================
// Inputs
// =========================================================

// Vertex inputs
layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

// per-frame, "global" data
layout(std140, set = 0, binding = 0) uniform kmaterial_global_uniform_data {
    point_light p_lights[KMATERIAL_MAX_GLOBAL_POINT_LIGHTS]; // 2048 bytes @ 32 bytes each
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[KMATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[KMATERIAL_MAX_VIEWS];                             // 320 bytes
    vec4 view_positions[KMATERIAL_MAX_VIEWS];                     // 80 bytes
    mat4 projection;                                             // 64 bytes
    directional_light dir_light;                                 // 48 bytes
    vec4 cascade_splits;                                         // 16 bytes
    // [shadow_bias, delta_time, game_time, padding]
    vec4 params;
    // [render_mode, use_pcf, padding, padding]
    uvec4 options;
    vec4 padding;  // 16 bytes
} material_frame_ubo;

// per-group
layout(set = 1, binding = 0) uniform per_group_ubo {
    /** @brief The material lighting model. */
    uint lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    uint flags;
    vec2 padding;
} material_group_ubo;

// per-draw
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 u32s.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    uint irradiance_cubemap_index;
    uint view_index;
    float tiling;
    float wave_strength;
    float wave_speed;
} material_draw_ubo;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
    vec4 frag_position;
    vec4 light_space_frag_pos[KMATERIAL_MAX_SHADOW_CASCADES];
	vec4 clip_space;
    vec3 world_to_camera;
    float padding;
	vec2 texcoord;
    vec2 padding2;
} out_dto;

void main() {
	vec4 world_position = material_draw_ubo.model * in_position;
	out_dto.frag_position = world_position;
	out_dto.clip_space = material_frame_ubo.projection * material_frame_ubo.views[material_draw_ubo.view_index] * world_position;
	gl_Position = out_dto.clip_space;
	out_dto.texcoord = vec2((in_position.x * 0.5) + 0.5, (in_position.z * 0.5) + 0.5) * material_draw_ubo.tiling;

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < KMATERIAL_MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (ndc_to_uvw * material_frame_ubo.directional_light_spaces[i]) * world_position;
    }
	
	out_dto.world_to_camera = material_frame_ubo.view_positions[material_draw_ubo.view_index].xyz - world_position.xyz;
}
