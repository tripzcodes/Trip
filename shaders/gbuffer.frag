#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    vec4 material;    // x = metallic, y = roughness
} pc;

layout(set = 1, binding = 0) uniform sampler2D albedo_tex;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_position;

void main() {
    vec3 tex_color = texture(albedo_tex, frag_uv).rgb;
    out_albedo = vec4(frag_color * tex_color, pc.material.x);
    out_normal = vec4(normalize(frag_normal) * 0.5 + 0.5, pc.material.y);
    out_position = vec4(frag_world_pos, 1.0);
}
