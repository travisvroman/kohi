#version 450

// TODO: All these types should be defined in some #include file when #includes are implemented.

const uint MATERIAL_MAX_POINT_LIGHTS = 10;
const uint MATERIAL_MAX_SHADOW_CASCADES = 4;
const uint MATERIAL_MAX_VIEWS = 4;
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
    vec4 colour;
    vec4 position;
    // Usually 1, make sure denominator never gets smaller than 1
    float constant_f;
    // Reduces light intensity linearly
    float linear;
    // Makes the light fall off slower at longer distances.
    float quadratic;
    float padding;
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

// per-frame
layout(std140, set = 0, binding = 0) uniform per_frame_ubo {
    // Light space for shadow mapping. Per cascade
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES]; // 256 bytes
    mat4 views[MATERIAL_MAX_VIEWS];
    mat4 projection;
    vec4 view_positions[MATERIAL_MAX_VIEWS];
    vec4 cascade_splits;// TODO: support for something other than 4[MATERIAL_MAX_SHADOW_CASCADES];

    // [shadow_bias, delta_time, game_time, padding]
    vec4 params;
    // [render_mode, use_pcf, padding, padding]
    uvec4 options;
    vec4 padding;  // 16 bytes
} material_frame_ubo;

// per-group
layout(set = 1, binding = 0) uniform per_group_ubo {
    directional_light dir_light;            // 48 bytes
    point_light p_lights[MATERIAL_MAX_POINT_LIGHTS]; // 48 bytes each
    uint num_p_lights;
    /** @brief The material lighting model. */
    uint lighting_model;
    // Base set of flags for the material. Copied to the material instance when created.
    uint flags;
    float padding;
} material_group_ubo;

// per-draw
layout(push_constant) uniform per_draw_ubo {
    mat4 model;
    uint irradiance_cubemap_index;
    uint view_index;
    vec2 padding;
    float tiling;
    float wave_strength;
    float wave_speed;
    float padding2;
} material_draw_ubo;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
    vec4 frag_position;
    vec4 light_space_frag_pos[MATERIAL_MAX_SHADOW_CASCADES];
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
    for(int i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (ndc_to_uvw * material_frame_ubo.directional_light_spaces[i]) * world_position;
    }
	
	out_dto.world_to_camera = material_frame_ubo.view_positions[material_draw_ubo.view_index].xyz - world_position.xyz;
}
