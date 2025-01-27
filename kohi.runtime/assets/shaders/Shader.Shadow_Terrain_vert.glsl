#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent; 
layout(location = 5) in vec4 in_mat_weights; // Supports 4 materials.

#define MAX_CASCADES 4

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 view_projections[MAX_CASCADES];
} global_ubo;

layout(push_constant) uniform push_constants {
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
    uint cascade_index;
} local_ubo;

void main() {
    gl_Position = global_ubo.view_projections[local_ubo.cascade_index] * local_ubo.model * vec4(in_position, 1.0);
}
