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
} pc;

layout(set = 2, binding = 0) readonly buffer BoneMatrices {
    mat4 bones[];
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_tangent;
layout(location = 5) in uvec4 in_bone_indices;
layout(location = 6) in vec4 in_bone_weights;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_world_pos;
layout(location = 4) out vec4 frag_tangent;

void main() {
    mat4 skin =
        in_bone_weights.x * bones[in_bone_indices.x] +
        in_bone_weights.y * bones[in_bone_indices.y] +
        in_bone_weights.z * bones[in_bone_indices.z] +
        in_bone_weights.w * bones[in_bone_indices.w];

    vec4 skinned_pos = skin * vec4(in_position, 1.0);
    vec3 skinned_normal = mat3(skin) * in_normal;
    vec3 skinned_tangent = mat3(skin) * in_tangent.xyz;

    vec4 world_pos = pc.model * skinned_pos;
    gl_Position = ubo.projection * ubo.view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal = normalize(mat3(pc.model) * skinned_normal);
    frag_color = in_color * pc.albedo.rgb;
    frag_uv = in_uv;
    frag_tangent = vec4(normalize(mat3(pc.model) * skinned_tangent), in_tangent.w);
}
