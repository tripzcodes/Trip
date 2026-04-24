#version 450

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 right;   // camera right (world space), w=aspect unused
    vec4 up;      // camera up (world space)
} push;

layout(location = 0) in vec4 inst_pos_size; // xyz=world pos, w=size
layout(location = 1) in vec4 inst_color;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

// 6-vertex billboard quad without index buffer
const vec2 quad[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

void main() {
    vec2 p = quad[gl_VertexIndex];
    vec3 world = inst_pos_size.xyz + (push.right.xyz * p.x + push.up.xyz * p.y) * inst_pos_size.w;
    gl_Position = push.view_proj * vec4(world, 1.0);
    v_color = inst_color;
    v_uv = p;
}
