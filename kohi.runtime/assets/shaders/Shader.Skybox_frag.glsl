#version 450

// =========================================================
// Inputs
// =========================================================

// All matrices
layout(std430, set = 0, binding = 0) readonly buffer global_matrix_ssbo {
	mat4 matrices[]; // indexed by immediate.transform_index
} global_matrices;

layout(set = 0, binding = 1) uniform textureCube cube_texture;
layout(set = 0, binding = 2) uniform sampler cube_sampler;

layout(push_constant) uniform immediate_data {
	vec4 fog_colour;
	// Offsets into the matrix SSBO per type.
	uint mat_ssbo_view_offset;
	uint mat_ssbo_proj_offset;
	// Indices into the matrix SSBO, offset by above types.
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

	vec3 final_colour = mix(out_colour.rgb, immediate.fog_colour.rgb, fog_factor * immediate.fog_colour.a);

	out_colour = vec4(final_colour, 1.0);
} 
