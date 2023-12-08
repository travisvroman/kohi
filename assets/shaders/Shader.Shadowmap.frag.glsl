#version 450

layout (set = 1, binding = 1) uniform sampler2D colour_map;

// Data Transfer Object
layout(location = 1) in struct dto {
	vec2 tex_coord;
} in_dto;

layout(location = 0) out vec4 out_colour;

void main() {
    out_colour = vec4(1.0);
    // float alpha = texture(colour_map, in_dto.tex_coord).a;
    // if(alpha < 0.5) {
    //     discard;
    // }
}
