// Nova Engine — gradient background (fragment stage).
// Draws a vertical gradient with a slow accent glow and a vignette; colors
// come from the shared FrameUBO (see engine/renderer/Renderer.h).
#version 450

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 transform;
    vec4 colorA;   // gradient top
    vec4 colorB;   // gradient bottom
    vec4 accent;   // engine accent (violet)
    float time;
    float aspect;
    float pad0;
    float pad1;
} uFrame;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
    vec3 color = mix(uFrame.colorA.rgb, uFrame.colorB.rgb, uv.y);

    // Soft accent glow sweeping across the sky.
    float sweep = 0.5 + 0.5 * sin(uFrame.time * 0.35);
    float glow = exp(-4.0 * distance(uv, vec2(sweep, 0.85)));
    color += uFrame.accent.rgb * glow * 0.08;

    // Vignette to focus the center.
    float vignette = smoothstep(1.25, 0.35, distance(uv, vec2(0.5)));
    color *= mix(0.75, 1.0, vignette);

    outColor = vec4(color, 1.0);
}
