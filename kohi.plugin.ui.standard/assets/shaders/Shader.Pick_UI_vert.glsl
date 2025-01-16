#version 450

// =========================================================
// Inputs
// =========================================================

// Vertex inputs
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texcoord;

layout(set = 0, binding = 0) uniform per_frame_ubo {
    mat4 projection;
	mat4 view;
} sui_pick_frame_ubo;

layout(set = 1, binding = 0) uniform per_group_ubo {
    vec3 id_colour;
} sui_pick_group_ubo;

layout(push_constant) uniform per_draw_ubo {
	mat4 model; // 64 bytes
} sui_pick_draw_ubo;

// =========================================================
// Outputs
// =========================================================

void main() {
	gl_Position = sui_pick_frame_ubo.projection * sui_pick_frame_ubo.view * sui_pick_draw_ubo.model * vec4(in_position, 0.0, 1.0);
}
