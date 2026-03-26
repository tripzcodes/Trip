#version 450

const float PI = 3.14159265359;
const int CASCADE_COUNT = 3;

layout(binding = 0) uniform LightData {
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient_color;
    vec4 camera_pos;
    vec4 clear_color;
    mat4 cascade_vp[CASCADE_COUNT];
    vec4 cascade_splits;
    vec4 debug_flags;    // x = show_cascade_debug
    vec4 camera_forward; // xyz = camera forward direction
} light;

layout(binding = 1) uniform sampler2D gbuf_albedo;
layout(binding = 2) uniform sampler2D gbuf_normal;
layout(binding = 3) uniform sampler2D gbuf_depth;
layout(binding = 4) uniform sampler2DArray shadow_map;
layout(binding = 5) uniform sampler2D gbuf_position;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// PCSS: estimate penumbra width from blocker distance
const float PCSS_LIGHT_SIZE = 0.04;  // world-space light angular size
const int   PCSS_BLOCKER_SAMPLES = 16;
const int   PCSS_PCF_SAMPLES = 25;

// Poisson disk for sampling
const vec2 poisson_disk[25] = vec2[](
    vec2(-0.9409, -0.2449), vec2(-0.3531,  0.8864), vec2( 0.7660, -0.0688),
    vec2(-0.6999, -0.6890), vec2( 0.1398,  0.3559), vec2(-0.1622, -0.7611),
    vec2( 0.8033,  0.5570), vec2(-0.4897,  0.1868), vec2( 0.4323, -0.5397),
    vec2(-0.8649,  0.3542), vec2( 0.2119, -0.9692), vec2( 0.5897,  0.8588),
    vec2(-0.2418, -0.3692), vec2( 0.9593, -0.2852), vec2(-0.5817,  0.6501),
    vec2( 0.0285,  0.9294), vec2( 0.3618, -0.2019), vec2(-0.9207,  0.0783),
    vec2( 0.6528,  0.2258), vec2(-0.3207, -0.9484), vec2( 0.4709,  0.4685),
    vec2(-0.7571, -0.3594), vec2( 0.1022,  0.6898), vec2( 0.8560, -0.5852),
    vec2(-0.4520,  0.9012)
);

float search_blockers(vec2 uv, float receiver_depth, float search_radius, int cascade) {
    float blocker_sum = 0.0;
    int blocker_count = 0;

    for (int i = 0; i < PCSS_BLOCKER_SAMPLES; i++) {
        vec2 offset = poisson_disk[i] * search_radius;
        float depth = texture(shadow_map, vec3(uv + offset, float(cascade))).r;
        if (depth < receiver_depth) {
            blocker_sum += depth;
            blocker_count++;
        }
    }

    return blocker_count > 0 ? blocker_sum / float(blocker_count) : -1.0;
}

float pcf_filter(vec2 uv, float receiver_depth, float filter_radius, float bias, int cascade) {
    float shadow = 0.0;
    for (int i = 0; i < PCSS_PCF_SAMPLES; i++) {
        vec2 offset = poisson_disk[i] * filter_radius;
        float depth = texture(shadow_map, vec3(uv + offset, float(cascade))).r;
        shadow += (receiver_depth - bias > depth) ? 0.0 : 1.0;
    }
    return shadow / float(PCSS_PCF_SAMPLES);
}

float shadow_calc(vec3 world_pos) {
    // select cascade by view-space depth
    float view_depth = dot(world_pos - light.camera_pos.xyz, light.camera_forward.xyz);

    int cascade = CASCADE_COUNT - 1;
    for (int i = 0; i < CASCADE_COUNT; i++) {
        if (view_depth < light.cascade_splits[i]) {
            cascade = i;
            break;
        }
    }

    // project into light space
    vec4 shadow_coord = light.cascade_vp[cascade] * vec4(world_pos, 1.0);
    shadow_coord /= shadow_coord.w;
    shadow_coord.xy = shadow_coord.xy * 0.5 + 0.5;

    // outside shadow map
    if (shadow_coord.x < 0.0 || shadow_coord.x > 1.0 ||
        shadow_coord.y < 0.0 || shadow_coord.y > 1.0 ||
        shadow_coord.z < 0.0 || shadow_coord.z > 1.0) {
        return 1.0;
    }

    vec2 texel_size = vec2(1.0 / textureSize(shadow_map, 0).xy);
    float bias = 0.001;
    float receiver = shadow_coord.z;

    // PCSS step 1: blocker search
    float search_radius = PCSS_LIGHT_SIZE * receiver * 8.0;
    search_radius = max(search_radius, texel_size.x * 2.0);
    float avg_blocker = search_blockers(shadow_coord.xy, receiver - bias, search_radius, cascade);

    // no blockers = fully lit
    if (avg_blocker < 0.0) return 1.0;

    // PCSS step 2: penumbra estimation
    float penumbra = (receiver - avg_blocker) * PCSS_LIGHT_SIZE / avg_blocker;
    float filter_radius = max(penumbra * 4.0, texel_size.x * 1.5);

    // PCSS step 3: filtered PCF
    return pcf_filter(shadow_coord.xy, receiver, filter_radius, bias, cascade);
}

void main() {
    vec4 albedo_sample = texture(gbuf_albedo, frag_uv);
    vec4 normal_sample = texture(gbuf_normal, frag_uv);

    if (length(normal_sample.rgb) < 0.01) {
        out_color = vec4(light.clear_color.rgb, 1.0);
        return;
    }

    vec3 albedo = albedo_sample.rgb;
    float metallic = albedo_sample.a;
    float roughness = max(normal_sample.a, 0.04);

    vec3 world_pos = texture(gbuf_position, frag_uv).xyz;
    vec3 N = normalize(normal_sample.rgb * 2.0 - 1.0);
    vec3 V = normalize(light.camera_pos.xyz - world_pos);
    vec3 L = normalize(-light.light_dir.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = distribution_ggx(N, H, roughness);
    float G = geometry_smith(N, V, L, roughness);
    vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);

    float shadow = shadow_calc(world_pos);

    vec3 radiance = light.light_color.rgb * light.light_color.a;
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL * shadow;

    vec3 ambient = light.ambient_color.rgb * light.ambient_color.a * albedo;

    vec3 color = ambient + Lo;

    // output raw HDR — tone mapping handled in post-process

    // cascade debug visualization
    if (light.debug_flags.x > 0.5) {
        float dist = dot(world_pos - light.camera_pos.xyz, light.camera_forward.xyz);
        vec3 cascade_color;
        if (dist < light.cascade_splits.x) {
            cascade_color = vec3(1.0, 0.2, 0.2); // red = near
        } else if (dist < light.cascade_splits.y) {
            cascade_color = vec3(0.2, 1.0, 0.2); // green = mid
        } else {
            cascade_color = vec3(0.2, 0.2, 1.0); // blue = far
        }
        color = mix(color, cascade_color, 0.4);
    }

    out_color = vec4(color, 1.0);
}
