#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

void main() {
    // soft circular falloff in [-1,1]^2 uv space
    float d = dot(v_uv, v_uv);
    if (d > 1.0) discard;

    float a = (1.0 - d);
    a *= a;

    // additive: premultiplied rgb, alpha unused since blend is (ONE, ONE)
    out_color = vec4(v_color.rgb * v_color.a * a, 0.0);
}
