#version 450

layout(push_constant) uniform PushConstants {
    mat4 light_view_proj;
} pc;

// per-vertex
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_tangent;

// per-instance (same layout as gbuffer_instanced — InstanceData at binding 1)
layout(location = 5) in mat4 in_model;     // locations 5-8
layout(location = 9) in vec4 in_albedo;    // unused but must match layout
layout(location = 10) in vec4 in_material; // unused but must match layout

void main() {
    gl_Position = pc.light_view_proj * in_model * vec4(in_position, 1.0);
}
