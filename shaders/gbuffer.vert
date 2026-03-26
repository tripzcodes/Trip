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

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;      // rgb = material tint
    vec4 material;    // x = metallic, y = roughness
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_world_pos;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = ubo.projection * ubo.view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal = normalize(mat3(pc.model) * in_normal);
    frag_color = in_color * pc.albedo.rgb;
    frag_uv = in_uv;
}
