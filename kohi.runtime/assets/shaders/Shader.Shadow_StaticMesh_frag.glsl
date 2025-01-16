#version 450

#define MAX_CASCADES 4

// =========================================================
// Inputs
// =========================================================

// per frame
layout(set = 0, binding = 0) uniform per_frame_ubo {
    mat4 projections[MAX_CASCADES];
	mat4 views[MAX_CASCADES];
} frame_ubo;

// per group NOTE: No per-group UBO for this shader
layout (set = 1, binding = 0) uniform texture2D base_colour_texture;
layout (set = 1, binding = 1) uniform sampler base_colour_sampler;

// per draw
layout(push_constant) uniform per_draw_ubo {
	
	// Only guaranteed a total of 128 bytes.
	mat4 model; // 64 bytes
    uint cascade_index;
} draw_ubo;

// Data Transfer Object from vertex shader
layout(location = 1) in struct dto {
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================

layout(location = 0) out vec4 out_colour;

void main() {
    float alpha = texture(sampler2D(base_colour_texture, base_colour_sampler), in_dto.tex_coord).a;
    out_colour = vec4(1.0, 1.0, 1.0, alpha);
    if(alpha < 0.5) { // TODO: This should probably be configurable.
        discard;
    }
}
