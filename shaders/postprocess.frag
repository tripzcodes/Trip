#version 450

layout(binding = 0) uniform sampler2D hdr_input;
layout(binding = 1) uniform sampler2D gbuf_depth;
layout(binding = 2) uniform sampler2D gbuf_normal;

layout(push_constant) uniform Params {
    vec4 ssao_params;    // x=enabled, y=radius, z=intensity
    vec4 bloom_params;   // x=enabled, y=threshold, z=intensity
    vec4 tonemap_params; // x=mode, y=exposure
} params;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

// ====== SSAO ======
// simplified screen-space AO using depth differences
float compute_ssao() {
    if (params.ssao_params.x < 0.5) { return 1.0; }

    float radius = params.ssao_params.y;
    float intensity = params.ssao_params.z;

    float center_depth = texture(gbuf_depth, frag_uv).r;
    if (center_depth >= 1.0) { return 1.0; } // sky

    vec3 normal = texture(gbuf_normal, frag_uv).rgb * 2.0 - 1.0;
    if (length(normal) < 0.01) { return 1.0; }

    // sample in a small kernel around the pixel
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

// ====== BLOOM ======
vec3 compute_bloom(vec3 color) {
    if (params.bloom_params.x < 0.5) { return vec3(0.0); }

    float threshold = params.bloom_params.y;
    float bloom_intensity = params.bloom_params.z;

    // simple blur of bright areas
    vec2 texel = 1.0 / textureSize(hdr_input, 0);
    vec3 bloom = vec3(0.0);

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec3 s = texture(hdr_input, frag_uv + vec2(x, y) * texel * 3.0).rgb;
            float brightness = dot(s, vec3(0.2126, 0.7152, 0.0722));
            if (brightness > threshold) {
                bloom += s;
            }
        }
    }
    bloom /= 25.0;

    return bloom * bloom_intensity;
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
        // Reinhard
        color = color / (color + vec3(1.0));
    } else if (mode == 2) {
        // ACES
        color = aces_film(color);
    }
    // mode 0 = no tone mapping

    // gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    return color;
}

void main() {
    vec3 hdr = texture(hdr_input, frag_uv).rgb;

    float ao = compute_ssao();
    hdr *= ao;

    vec3 bloom = compute_bloom(hdr);
    hdr += bloom;

    vec3 ldr = tone_map(hdr);

    out_color = vec4(ldr, 1.0);
}
