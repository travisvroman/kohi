#version 450

#define MAX_CASCADES 4

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
    mat4 view_projections[MAX_CASCADES];
} frame_ubo;

// per group NOTE: No per-group UBO for this shader

// per draw
layout(push_constant) uniform per_draw_ubo {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
    uint cascade_index;
} draw_ubo;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader
layout(location = 1) out struct dto {
	vec2 tex_coord;
} out_dto;

void main() {
    out_dto.tex_coord = in_texcoord;
    gl_Position = frame_ubo.view_projections[draw_ubo.cascade_index] * draw_ubo.model * vec4(in_position, 1.0);
}
