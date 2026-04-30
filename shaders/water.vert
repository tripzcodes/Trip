#version 450

layout(set = 0, binding = 0) uniform UniformData {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient_color;
    vec4 material;
} ubo;

layout(push_constant) uniform Push {
    mat4 model;       // water entity world transform
    vec4 color_time;  // rgb = water tint, a = time (seconds)
    vec4 wave_a;      // x=amplitude, y=frequency, z=speed, w=dir.x
    vec4 wave_b;      // similar but second wave; .w = dir.z for both?
} push;

layout(location = 0) in vec3 in_position; // unit grid in XZ, y=0
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec4 in_tangent;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;

// Gerstner-style wave: returns displaced position and contributes to a tangent
// frame so the vertex shader can hand the fragment a hand-derived normal.
vec3 gerstner(vec2 xz, vec2 dir, float steepness, float wavelength,
              float speed, float t, inout vec3 tangent, inout vec3 binormal) {
    float k = 6.2831853 / wavelength;          // 2*pi/wavelength
    float c = sqrt(9.81 / k);                   // phase speed (deep water)
    vec2  d = normalize(dir);
    float f = k * (dot(d, xz) - c * speed * t);
    float a = steepness / k;

    float cos_f = cos(f);
    float sin_f = sin(f);

    tangent  += vec3(-d.x * d.x * (steepness * sin_f),
                      d.x * (steepness * cos_f),
                     -d.x * d.y * (steepness * sin_f));
    binormal += vec3(-d.x * d.y * (steepness * sin_f),
                      d.y * (steepness * cos_f),
                     -d.y * d.y * (steepness * sin_f));

    return vec3(d.x * (a * cos_f), a * sin_f, d.y * (a * cos_f));
}

void main() {
    vec4 world4 = push.model * vec4(in_position, 1.0);
    vec3 world = world4.xyz;

    float t = push.color_time.a;

    vec3 tangent  = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);

    vec3 disp = vec3(0.0);
    disp += gerstner(world.xz, vec2(push.wave_a.w, 0.7),
                     push.wave_a.x, push.wave_a.y, push.wave_a.z, t,
                     tangent, binormal);
    disp += gerstner(world.xz, vec2(0.4, push.wave_b.w),
                     push.wave_b.x * 0.6, push.wave_b.y * 0.7, push.wave_b.z, t,
                     tangent, binormal);

    world += disp;

    vec3 normal = normalize(cross(binormal, tangent));

    v_world_pos = world;
    v_world_normal = normal;
    v_uv = in_uv;

    gl_Position = ubo.projection * ubo.view * vec4(world, 1.0);
}
