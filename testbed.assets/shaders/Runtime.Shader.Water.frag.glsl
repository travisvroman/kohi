#version 450

layout(location = 0) in vec4 dummy;

layout(location = 0) out vec4 out_colour;

void main() {
    out_colour = vec4(0.0, 0.0, dummy.z, 1.0);
}