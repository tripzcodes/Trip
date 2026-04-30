#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color_time;  // rgb = water tint
    vec4 wave_a;
    vec4 wave_b;
} push;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;

// G-Buffer outputs match the standard geometry pass:
//   0 = albedo.rgb + metallic
//   1 = normal.rgb + roughness
//   2 = world position
layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_position;

void main() {
    vec3 N = normalize(v_world_normal);

    // mostly metal-like surface so the existing PBR + SSR path renders sky
    // and scene reflections. Low roughness = sharp specular.
    float metallic = 1.0;
    float roughness = 0.05;

    out_albedo = vec4(push.color_time.rgb, metallic);
    out_normal = vec4(N * 0.5 + 0.5, roughness);
    out_position = vec4(v_world_pos, 1.0);
}
