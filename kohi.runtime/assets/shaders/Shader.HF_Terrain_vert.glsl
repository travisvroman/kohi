#version 450

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_normal;
layout(location = 2) in vec4 in_tangent; 


// =========================================================
// Inputs
// =========================================================

const uint HF_TERRAIN_MAX_MATERIAL_COUNT = 5;
const uint KMATERIAL_UBO_MAX_SHADOW_CASCADES = 4;

struct light_data {
    // Directional light: .rgb = colour, .w = ignored - Point lights: .rgb = colour, .a = linear 
    vec4 colour;
    // Directional Light: .xyz = direction, .w = ignored - Point lights: .xyz = position, .w = quadratic
    vec4 position;
};

// Binding Set 0 - global-level stuff

// Global settings for the scene.
layout(std140, set = 0, binding = 0) uniform terrain_settings_ubo {
    mat4 directional_light_spaces[KMATERIAL_UBO_MAX_SHADOW_CASCADES]; // 256 bytes
    vec4 cascade_splits;                                         // 16 bytes

    float delta_time;
    float game_time;
    uint render_mode;
    uint use_pcf;

    // Shadow settings
    float shadow_bias;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_split_mult;

    vec4 fog_colour;
    float fog_start;
    float fog_end;
    float near_clip;

    float far_clip;
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
	uint padding;
} global_settings;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

// All lighting
layout(std430, set = 0, binding = 2) readonly buffer global_lighting_ssbo {
    light_data lights[]; // indexed by immediate.packed_point_light_indices (needs unpacking to 16x u8s)
} global_lighting;


// Immediate data
layout(push_constant) uniform immediate_data {
    // bytes 0-15
    uint view_index;
    uint projection_index;
    uint dir_light_index;
    uint base_material_index;

    // bytes 16-31
    // Index into the global point lights array. Up to 16 indices as u8s packed into 2 uints.
    uvec2 packed_point_light_indices; // 8 bytes
    uint num_p_lights;
    // Index into global irradiance cubemap texture array
    uint irradiance_cubemap_index;

    // bytes 32-47
	vec4 clipping_plane;

	// bytes 48-63
	uvec4 material_indices;
} immediate;

// Data Transfer Object
layout(location = 0) out struct dto {
	vec4 light_space_frag_pos[KMATERIAL_UBO_MAX_SHADOW_CASCADES];
	vec4 frag_position;
	vec4 tangent;
	vec3 normal;
    float view_depth;
    vec3 world_to_camera;
	float padding2;
	vec2 tex_coord;
} out_dto;

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

void main() {
	mat4 view = global_matrices.matrices[global_settings.mat_ssbo_view_offset + immediate.view_index];
	mat4 projection = global_matrices.matrices[global_settings.mat_ssbo_proj_offset + immediate.projection_index];
	out_dto.tex_coord = vec2(in_position.w, in_normal.w);

	mat4 inv_view = inverse(view);
	vec3 view_position = vec3(inv_view[3]);

	// Fragment position in world space.
	// Copy the normal over.
	out_dto.normal = normalize(in_normal.xyz);
	out_dto.tangent = normalize(in_tangent);
	out_dto.frag_position = vec4(in_position.xyz, 1.0);
    vec4 view_fragpos = view * out_dto.frag_position;
    out_dto.view_depth = -view_fragpos.z;
    gl_Position = projection * view * vec4(in_position.xyz, 1.0);

	// Apply clipping plane
	gl_ClipDistance[0] = dot(out_dto.frag_position, immediate.clipping_plane);

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < KMATERIAL_UBO_MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (ndc_to_uvw * global_settings.directional_light_spaces[i]) * out_dto.frag_position;
    }

	out_dto.world_to_camera = view_position.xyz - out_dto.frag_position.xyz;
}
