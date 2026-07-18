#version 450

#define KMATERIAL_MAX_WATER_PLANES 4

// =========================================================
// Inputs
// =========================================================

layout(set = 0, binding = 0) uniform global_ubo_data {
	vec4 fog_colour;
	
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
} global_ubo;

// All matrices
layout(std430, set = 0, binding = 1) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

layout(set = 0, binding = 2) uniform textureCube cube_texture;
layout(set = 0, binding = 3) uniform sampler cube_sampler;

layout(push_constant) uniform immediate_data {
	uint view_index;
	uint projection_index;
} immediate;

// Data Transfer Object from vertex shader.
layout(location = 0) in dto {
	vec4 frag_pos;
	vec3 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

void main() {
	out_colour = texture(samplerCube(cube_texture, cube_sampler), in_dto.tex_coord);

	float min_fog_y = 0.0;
	float max_fog_y = 0.02;
	float fog_factor = smoothstep(max_fog_y, min_fog_y, in_dto.frag_pos.y);

	vec3 final_colour = mix(out_colour.rgb, global_ubo.fog_colour.rgb, fog_factor * global_ubo.fog_colour.a);

	out_colour = vec4(final_colour, 1.0);
} 
