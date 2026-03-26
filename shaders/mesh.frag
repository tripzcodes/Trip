#version 450

layout(binding = 0) uniform UniformData {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient_color;
    vec4 material;
} ubo;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light = normalize(-ubo.light_dir.xyz);

    float diff = max(dot(normal, light), 0.0);

    vec3 ambient = ubo.ambient_color.rgb * ubo.ambient_color.a;
    vec3 diffuse = ubo.light_color.rgb * ubo.light_color.a * diff;

    vec3 result = (ambient + diffuse) * frag_color;
    out_color = vec4(result, 1.0);
}
