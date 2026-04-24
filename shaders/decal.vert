#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view_proj;
    vec4 screen_size;  // xy = 1/width, 1/height
} frame;

layout(push_constant) uniform Push {
    mat4 world;
    mat4 inv_world;
    vec4 tint;
} decal;

// unit cube, 36 verts — 12 triangles, positions in [-0.5, 0.5]^3
const vec3 cube_verts[8] = vec3[](
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5)
);
const uint cube_indices[36] = uint[](
    0, 2, 1,  2, 3, 1,   // -Z
    4, 5, 6,  6, 5, 7,   // +Z
    0, 4, 2,  2, 4, 6,   // -X
    1, 3, 5,  5, 3, 7,   // +X
    2, 6, 3,  3, 6, 7,   // +Y
    0, 1, 4,  4, 1, 5    // -Y
);

void main() {
    vec3 local = cube_verts[cube_indices[gl_VertexIndex]];
    vec4 world_pos = decal.world * vec4(local, 1.0);
    gl_Position = frame.view_proj * world_pos;
}
