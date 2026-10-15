#pragma once
#include <string_view>
namespace nova::renderer::gles {
// GLSL ES 1.00 is accepted by both ES 2 and ES 3 contexts. No SPIR-V,
// uniform buffers, gl_VertexID, derivatives or optional extensions required.
inline constexpr std::string_view backgroundVertex = R"(
attribute vec2 aPosition;
varying mediump vec2 vUV;
void main() { vUV = aPosition * 0.5 + 0.5; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
inline constexpr std::string_view backgroundFragment = R"(
precision mediump float;
varying mediump vec2 vUV;
uniform vec4 uColorA;
uniform vec4 uColorB;
uniform float uAspect;
uniform float uGrid;
void main() {
    vec2 p = (vUV - 0.5) * vec2(uAspect, 1.0);
    vec2 cell = abs(fract(p * 12.0 + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.015, 0.035, min(cell.x, cell.y));
    vec3 color = mix(uColorB.rgb, uColorA.rgb, vUV.y);
    color += vec3(0.035) * grid * uGrid;
    float xAxis = 1.0 - smoothstep(0.001, 0.003, abs(p.y));
    float yAxis = 1.0 - smoothstep(0.001, 0.003, abs(p.x));
    color = mix(color, vec3(0.26, 0.10, 0.13), xAxis * 0.65 * uGrid);
    color = mix(color, vec3(0.10, 0.24, 0.19), yAxis * 0.65 * uGrid);
    gl_FragColor = vec4(color, 1.0);
}
)";
inline constexpr std::string_view meshVertex = R"(
attribute vec3 aPosition;
attribute vec3 aColor;
uniform mat4 uTransform;
varying mediump vec3 vColor;
void main() {
    vColor = aColor;
    vec4 clip = uTransform * vec4(aPosition, 1.0);
    // Scene/culling uses Vulkan clip space. Undo Y flip and [0,1] depth.
    clip.y = -clip.y;
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
}
)";
inline constexpr std::string_view meshFragment = R"(
precision mediump float;
varying mediump vec3 vColor;
uniform vec4 uAccent;
void main() { gl_FragColor = vec4(vColor * uAccent.rgb, 1.0); }
)";
}
