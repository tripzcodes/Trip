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
    vec4 albedo;
    vec4 material;
    vec4 wind;        // x=amp, y=height_min, z=height_max, w=time*speed
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_tangent;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_world_pos;
layout(location = 4) out vec4 frag_tangent;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);

    // mask: 0 at height_min, 1 at height_max — only tops bend in the wind
    float h = (in_position.y - pc.wind.y) / max(0.001, pc.wind.z - pc.wind.y);
    float mask = clamp(h, 0.0, 1.0);
    mask = mask * mask;

    // world-space phase so neighboring blades stay in sync at any density
    float phase = world_pos.x * 0.35 + world_pos.z * 0.21 + pc.wind.w;
    float dx = sin(phase) * pc.wind.x * mask;
    float dz = cos(phase * 1.3) * pc.wind.x * 0.5 * mask;

    world_pos.xyz += vec3(dx, 0.0, dz);

    gl_Position = ubo.projection * ubo.view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal = normalize(mat3(pc.model) * in_normal);
    frag_color = in_color * pc.albedo.rgb;
    frag_uv = in_uv;
    frag_tangent = vec4(normalize(mat3(pc.model) * in_tangent.xyz), in_tangent.w);
}
