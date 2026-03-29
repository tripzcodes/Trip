#version 450

layout(binding = 0) uniform sampler2D hdr_input;
layout(binding = 1) uniform sampler2D gbuf_depth;
layout(binding = 2) uniform sampler2D gbuf_normal;
layout(binding = 3) uniform sampler2D gbuf_position;
layout(binding = 4) uniform sampler2D gbuf_albedo;

layout(push_constant) uniform Params {
    vec4 ssao_params;    // x=enabled, y=radius, z=intensity
    vec4 bloom_params;   // x=enabled, y=threshold, z=intensity
    vec4 tonemap_params; // x=mode, y=exposure, z=ssr_enabled
    mat4 view_proj;
    vec4 camera_pos;
} params;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

// ====== SSAO ======
float compute_ssao() {
    if (params.ssao_params.x < 0.5) { return 1.0; }

    float radius = params.ssao_params.y;
    float intensity = params.ssao_params.z;

    float center_depth = texture(gbuf_depth, frag_uv).r;
    if (center_depth >= 1.0) { return 1.0; }

    vec3 normal = texture(gbuf_normal, frag_uv).rgb * 2.0 - 1.0;
    if (length(normal) < 0.01) { return 1.0; }

    float ao = 0.0;
    vec2 texel = 1.0 / textureSize(gbuf_depth, 0);

    const int SAMPLES = 8;
    vec2 offsets[SAMPLES] = vec2[](
        vec2(-1, -1), vec2(1, -1), vec2(-1, 1), vec2(1, 1),
        vec2(0, -2), vec2(0, 2), vec2(-2, 0), vec2(2, 0)
    );

    for (int i = 0; i < SAMPLES; i++) {
        vec2 sample_uv = frag_uv + offsets[i] * texel * radius * 4.0;
        float sample_depth = texture(gbuf_depth, sample_uv).r;

        float diff = center_depth - sample_depth;
        if (diff > 0.0001 && diff < radius * 0.1) {
            ao += 1.0;
        }
    }

    ao = 1.0 - (ao / float(SAMPLES)) * intensity;
    return clamp(ao, 0.0, 1.0);
}

// ====== BLOOM (separable 5+5 = 10 taps) ======
vec3 compute_bloom(vec3 color) {
    if (params.bloom_params.x < 0.5) { return vec3(0.0); }

    float threshold = params.bloom_params.y;
    float bloom_intensity = params.bloom_params.z;

    vec2 texel = 1.0 / textureSize(hdr_input, 0);
    const float w[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);

    vec3 h_bloom = vec3(0.0);
    for (int x = -2; x <= 2; x++) {
        vec3 s = texture(hdr_input, frag_uv + vec2(x, 0) * texel * 3.0).rgb;
        float b = dot(s, vec3(0.2126, 0.7152, 0.0722));
        h_bloom += (b > threshold ? s : vec3(0.0)) * w[x + 2];
    }

    vec3 v_bloom = vec3(0.0);
    for (int y = -2; y <= 2; y++) {
        vec3 s = texture(hdr_input, frag_uv + vec2(0, y) * texel * 3.0).rgb;
        float b = dot(s, vec3(0.2126, 0.7152, 0.0722));
        v_bloom += (b > threshold ? s : vec3(0.0)) * w[y + 2];
    }

    return (h_bloom + v_bloom) * 0.5 * bloom_intensity;
}

// ====== SSR (true — samples lit HDR scene) ======
vec3 compute_ssr() {
    if (params.tonemap_params.z < 0.5) return vec3(0.0);

    vec3 world_pos = texture(gbuf_position, frag_uv).xyz;
    vec3 N = normalize(texture(gbuf_normal, frag_uv).rgb * 2.0 - 1.0);
    float metallic = texture(gbuf_albedo, frag_uv).a;
    float roughness = texture(gbuf_normal, frag_uv).a;

    // only reflect on smooth-ish surfaces
    if (roughness > 0.5) return vec3(0.0);

    vec3 V = normalize(params.camera_pos.xyz - world_pos);
    vec3 R = reflect(-V, N);

    // fresnel — stronger reflections at grazing angles
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = mix(metallic, 1.0, pow(1.0 - NdotV, 5.0));
    if (fresnel < 0.02) return vec3(0.0);

    // ray march in screen space
    const int MAX_STEPS = 48;
    const float THICKNESS = 0.3;

    vec3 ray_pos = world_pos + R * 0.1; // offset to avoid self-intersection
    float step_size = 0.3;

    for (int i = 0; i < MAX_STEPS; i++) {
        ray_pos += R * step_size;
        step_size *= 1.05; // accelerate

        // project to screen
        vec4 clip = params.view_proj * vec4(ray_pos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        float scene_depth = texture(gbuf_depth, uv).r;
        float ray_depth = ndc.z;

        float diff = ray_depth - scene_depth;
        if (diff > 0.0 && diff < THICKNESS) {
            // edge fade
            vec2 edge = smoothstep(vec2(0.0), vec2(0.1), uv) *
                        (1.0 - smoothstep(vec2(0.9), vec2(1.0), uv));
            float fade = edge.x * edge.y;

            // distance fade
            fade *= 1.0 - float(i) / float(MAX_STEPS);

            // roughness fade
            fade *= 1.0 - roughness / 0.5;

            // sample the ACTUAL lit scene
            vec3 reflected = texture(hdr_input, uv).rgb;
            return reflected * fresnel * fade;
        }
    }

    return vec3(0.0);
}

// ====== TONE MAPPING ======
vec3 aces_film(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 tone_map(vec3 color) {
    int mode = int(params.tonemap_params.x);
    float exposure = params.tonemap_params.y;

    color *= exposure;

    if (mode == 1) {
        color = color / (color + vec3(1.0));
    } else if (mode == 2) {
        color = aces_film(color);
    }

    color = pow(color, vec3(1.0 / 2.2));

    return color;
}

void main() {
    vec3 hdr = texture(hdr_input, frag_uv).rgb;

    float ao = compute_ssao();
    hdr *= ao;

    // SSR — add reflections to the lit scene before tone mapping
    vec3 ssr = compute_ssr();
    hdr += ssr;

    vec3 bloom = compute_bloom(hdr);
    hdr += bloom;

    vec3 ldr = tone_map(hdr);

    out_color = vec4(ldr, 1.0);
}
