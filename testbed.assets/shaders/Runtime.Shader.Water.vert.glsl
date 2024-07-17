#version 450

layout(location = 0) in vec4 in_position; // NOTE: w is ignored.

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

const int MAX_POINT_LIGHTS = 10;
const int MAX_SHADOW_CASCADES = 4;

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 projection;
	mat4 view;
    mat4 light_space[MAX_SHADOW_CASCADES];
    vec4 cascade_splits; // NOTE: 4 splits.
	vec3 view_position;
    int mode;
    int use_pcf;
    float bias;
	vec2 padding;
} global_ubo;

layout(set = 1, binding = 0) uniform instance_uniform_object {
    directional_light dir_light;
    point_light p_lights[MAX_POINT_LIGHTS];
    float tiling;
    float wave_strength;
    float move_factor;
	float padding;
} instance_ubo;

layout(push_constant) uniform push_constants {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
} local_ubo;

// Data transfer object to fragment shader.
layout(location = 0) out struct dto {
	vec4 clip_space;
	vec2 texcoord;
    vec2 padding;
    vec3 world_to_camera;
    float padding2;
    vec4 light_space_frag_pos[MAX_SHADOW_CASCADES];
	vec4 cascade_splits;
	vec3 frag_position;
} out_dto;

// Vulkan's Y axis is flipped and Z range is halved.
const mat4 bias = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

void main() {
	vec4 world_position = local_ubo.model * in_position;
	out_dto.frag_position = world_position.xyz;
	out_dto.clip_space = global_ubo.projection * global_ubo.view * world_position;
	gl_Position = out_dto.clip_space;
	out_dto.texcoord = vec2((in_position.x * 0.5) + 0.5, (in_position.z * 0.5) + 0.5) * instance_ubo.tiling;

	// Get a light-space-transformed fragment positions.
    for(int i = 0; i < MAX_SHADOW_CASCADES; ++i) {
	    out_dto.light_space_frag_pos[i] = (bias * global_ubo.light_space[i]) * vec4(out_dto.frag_position, 1.0);
    }
	
	vec3 view_position = global_ubo.view_position;
	out_dto.world_to_camera = view_position - world_position.xyz;
}