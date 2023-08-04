#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent; 
layout(location = 5) in vec4 in_mat_weights; // Supports 4 materials.

struct directional_light {
    vec4 colour;
    vec4 direction;
};

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
	vec4 ambient_colour;
	vec3 view_position;
	int mode;
    directional_light dir_light;
} global_ubo;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} u_push_constants;

layout(location = 0) out int out_mode;

// Data Transfer Object
layout(location = 1) out struct dto {
	vec4 ambient;
	vec2 tex_coord;
	vec3 normal;
	vec3 view_position;
	vec3 frag_position;
	vec4 colour;
	vec3 tangent;
    vec4 mat_weights;
} out_dto;

void main() {
	out_dto.tex_coord = in_texcoord;
	out_dto.colour = in_colour;
	// Fragment position in world space.
	out_dto.frag_position = vec3(u_push_constants.model * vec4(in_position, 1.0));
	// Copy the normal over.
	mat3 m3_model = mat3(u_push_constants.model);
	out_dto.normal = normalize(m3_model * in_normal);
	out_dto.tangent = normalize(m3_model * in_tangent.xyz);
	out_dto.ambient = global_ubo.ambient_colour;
	out_dto.view_position = global_ubo.view_position;
    out_dto.mat_weights = in_mat_weights;
    gl_Position = global_ubo.projection * global_ubo.view * u_push_constants.model * vec4(in_position, 1.0);

	out_mode = global_ubo.mode;
}
