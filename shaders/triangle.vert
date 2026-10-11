// Nova Engine — rotating hero triangle (vertex stage).
// Applies the FrameUBO model-view-projection (built each frame on the CPU).
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

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

layout(location = 0) out vec3 vColor;

void main() {
    vColor = inColor;
    gl_Position = uFrame.transform * vec4(inPos, 1.0);
}
