#version 450

layout(location = 0) out vec4 out_colour;

// Data Transfer Object
layout(location = 1) in struct dto {
	vec4 colour;
} in_dto;

void main() {
    out_colour = in_dto.colour;
}