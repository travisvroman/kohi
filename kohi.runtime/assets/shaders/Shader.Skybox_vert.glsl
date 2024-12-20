#version 450

// =========================================================
// Inputs
// =========================================================

// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

// per frame
layout(set = 0, binding = 0) uniform per_frame_ubo {
    mat4 projection;
	mat4 view;
} frame_ubo;

// per group NOTE: No per-group UBO for this shader

// per draw NOTE: No per-draw UBO for this shader

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec3 tex_coord;
} out_dto;

void main() {
	out_dto.tex_coord = in_position;
	gl_Position = frame_ubo.projection * frame_ubo.view * vec4(in_position, 1.0);
} 