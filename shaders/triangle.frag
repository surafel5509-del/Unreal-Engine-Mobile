// Nova Engine — rotating hero triangle (fragment stage).
#version 450

layout(location = 0) in vec3 vColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 transform;
    vec4 colorA;
    vec4 colorB;
    vec4 accent;
    float time;
    float aspect;
    float pad0;
    float pad1;
} uFrame;

layout(location = 0) out vec4 outColor;

void main() {
    // Subtle breathing pulse so the 60 FPS pacing is visible on device.
    float pulse = 0.9 + 0.1 * sin(uFrame.time * 3.0);
    outColor = vec4(vColor * pulse, 1.0);
}
