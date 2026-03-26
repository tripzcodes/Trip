#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedo;
    vec4 material;    // x = metallic, y = roughness
} pc;

layout(set = 1, binding = 0) uniform sampler2D albedo_tex;
layout(set = 1, binding = 1) uniform sampler2D normal_tex;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_world_pos;
layout(location = 4) in vec4 frag_tangent;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_position;

void main() {
    vec3 tex_color = texture(albedo_tex, frag_uv).rgb;

    // build TBN matrix
    vec3 N = normalize(frag_normal);
    vec3 T = normalize(frag_tangent.xyz);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt re-orthogonalize
    vec3 B = cross(N, T) * frag_tangent.w;
    mat3 TBN = mat3(T, B, N);

    // sample normal map and transform to world space
    vec3 tangent_normal = texture(normal_tex, frag_uv).rgb * 2.0 - 1.0;
    vec3 world_normal = normalize(TBN * tangent_normal);

    out_albedo = vec4(frag_color * tex_color, pc.material.x);
    out_normal = vec4(world_normal * 0.5 + 0.5, pc.material.y);
    out_position = vec4(frag_world_pos, 1.0);
}
