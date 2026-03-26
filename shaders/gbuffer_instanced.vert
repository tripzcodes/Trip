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

// per-vertex
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_tangent;

// per-instance
layout(location = 5) in mat4 in_model;    // locations 5-8
layout(location = 9) in vec4 in_albedo;
layout(location = 10) in vec4 in_material;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_world_pos;
layout(location = 4) out vec4 frag_material;
layout(location = 5) out vec4 frag_tangent;

void main() {
    vec4 world_pos = in_model * vec4(in_position, 1.0);
    gl_Position = ubo.projection * ubo.view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal = normalize(mat3(in_model) * in_normal);
    frag_color = in_color * in_albedo.rgb;
    frag_uv = in_uv;
    frag_material = in_material;
    frag_tangent = vec4(normalize(mat3(in_model) * in_tangent.xyz), in_tangent.w);
}
