// Nova Engine — fullscreen gradient background (vertex stage).
// The triangle is generated from gl_VertexIndex; no vertex buffer needed.
#version 450

layout(location = 0) out vec2 vUV;

void main() {
    // Vertices (0,0) (2,0) (0,2) -> covers clip space (-1..3).
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
