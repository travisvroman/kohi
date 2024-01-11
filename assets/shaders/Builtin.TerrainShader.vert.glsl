#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent; 
layout(location = 5) in vec4 in_mat_weights; // Supports 4 materials.

const int MAX_SHADOW_CASCADES = 4;

struct directional_light {
    vec4 colour;
    vec4 direction;
};

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
	mat4 light_space[MAX_SHADOW_CASCADES];
	vec4 cascade_splits;
	directional_light dir_light;
	vec3 view_position;
	int mode;
    int use_pcf;
    float bias;
    vec2 padding;
} global_ubo;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} u_push_constants;

layout(location = 0) out int out_mode;
layout(location = 1) out int use_pcf;

// Data Transfer Object
layout(location = 2) out struct dto {
	vec4 light_space_frag_pos[MAX_SHADOW_CASCADES];
	vec4 cascade_splits;
	vec2 tex_coord;
	vec3 normal;
	vec3 view_position;
	vec3 frag_position;
	vec4 colour;
	vec3 tangent;
    vec4 mat_weights;
    float bias;
    vec3 padding;
} out_dto;

// Vulkan's Y axis is flipped and Z range is halved.
const mat4 bias = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

void main() {
	out_dto.tex_coord = in_texcoord;
	out_dto.colour = in_colour;
	// Fragment position in world space.
	out_dto.frag_position = vec3(u_push_constants.model * vec4(in_position, 1.0));
	// Copy the normal over.
	mat3 m3_model = mat3(u_push_constants.model);
	out_dto.normal = normalize(m3_model * in_normal);
	out_dto.tangent = normalize(m3_model * in_tangent.xyz);
	out_dto.cascade_splits = global_ubo.cascade_splits;
	out_dto.view_position = global_ubo.view_position;
    out_dto.mat_weights = in_mat_weights;
    gl_Position = global_ubo.projection * global_ubo.view * u_push_constants.model * vec4(in_position, 1.0);

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (bias * global_ubo.light_space[i]) * vec4(out_dto.frag_position, 1.0);
    }

	out_mode = global_ubo.mode;
    use_pcf = global_ubo.use_pcf;
    out_dto.bias = global_ubo.bias;
}
