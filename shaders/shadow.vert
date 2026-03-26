#version 450

layout(push_constant) uniform PushConstants {
    mat4 light_view_proj;
    mat4 model;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;

void main() {
    gl_Position = pc.light_view_proj * pc.model * vec4(in_position, 1.0);
}
