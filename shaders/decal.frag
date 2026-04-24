#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view_proj;
    vec4 screen_size;  // xy = 1/width, 1/height
} frame;

layout(set = 0, binding = 1) uniform sampler2D gbuf_position;
layout(set = 1, binding = 0) uniform sampler2D decal_tex;

layout(push_constant) uniform Push {
    mat4 world;
    mat4 inv_world;
    vec4 tint;
} decal;

layout(location = 0) out vec4 out_color;

void main() {
    // reconstruct world position from the G-Buffer at this screen location
    vec2 uv = gl_FragCoord.xy * frame.screen_size.xy;
    vec3 world_pos = texture(gbuf_position, uv).xyz;

    // kill sky pixels (position.w == 0 was not written — position is cleared to 0)
    if (dot(world_pos, world_pos) < 0.0001) discard;

    // transform into decal-local space — unit box is [-0.5, 0.5]
    vec3 local = (decal.inv_world * vec4(world_pos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    // project down the local -Y axis (top-down decal) — sample xz
    vec2 dec_uv = local.xz + 0.5;
    vec4 sample_color = texture(decal_tex, dec_uv) * decal.tint;

    if (sample_color.a < 0.01) discard;

    // premultiplied alpha — blender does (ONE, ONE_MINUS_SRC_ALPHA)
    out_color = vec4(sample_color.rgb * sample_color.a, sample_color.a);
}
