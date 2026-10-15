#pragma once

// Nova Engine — renderer: gradient background pass + scene-driven draw list.
//
// Responsibilities:
//  * own the frame UBO ring (one persistently mapped buffer per frame slot),
//  * own the triangle vertex buffer and the two pipelines (background is a
//    vertex-less fullscreen triangle; the hero triangle has a real layout),
//  * lazily (re)create pipelines when the swapchain format is first known or
//    changes (dynamic rendering bakes formats into the PSO),
//  * record the frame from a SceneSnapshot: background pass, then one pass per
//    draw command, all inside a single dynamic-rendering scope.
//
// The renderer owns no scene state. Per-object data lives in a 256-byte-aligned
// slice of the frame UBO (slice 0 = camera/background constants, slice i + 1 =
// draw command i), so each object gets its own descriptor set — no shader or
// SPIR-V changes were needed to move from a single hard-coded triangle to an
// arbitrary draw list.
//
// Threading: render() runs on the render thread under nova::Engine's frame
// mutex; init()/shutdown() run during engine init/teardown (no overlap).

#include "core/Math.h"
#include "core/Result.h"
#include "core/Types.h"
#include "renderer/DrawList.h"
#include "renderer/BuiltinGeometry.h"
#include "rhi/IDevice.h"

#include <array>
#include <vector>

namespace nova::renderer {

using nova::core::u32;

enum class InitError {
    MissingShader,
    ShaderModuleFailed,
    BufferFailed,
    PipelineFailed,
};

const char* initErrorMessage(InitError error);

/// Frame uniform block (std140). MUST match the `FrameUBO` block declared in
/// shaders/background.* and shaders/triangle.*.
struct alignas(16) FrameUniforms {
    math::Mat4 transform;  //   0: clip = transform * position
    math::Vec4 colorA;     //  64: gradient top color
    math::Vec4 colorB;     //  80: gradient bottom color
    math::Vec4 accent;     //  96: engine accent (used for future UI tints)
    float time = 0.0f;     // 112: seconds since start
    float aspect = 1.0f;   // 116: viewport width / height
    float pad0 = 0.0f;     // 120
    float pad1 = 0.0f;     // 124
};
static_assert(sizeof(FrameUniforms) == 128, "FrameUBO std140 layout drift");
static_assert(offsetof(FrameUniforms, time) == 112, "FrameUBO layout drift");

struct RenderStats {
    u32 drawCalls = 0;
    u32 pipelines = 0;
    u32 frameSlot = 0;
    /// Draw commands dropped this frame because the UBO slice budget ran out.
    u32 droppedCommands = 0;
};

class Renderer : public nova::core::NonCopyable {
public:
    Renderer() = default;

    /// Create GPU resources from SPIR-V blobs (the engine loads them through
    /// the VFS). Safe to call again after shutdown().
    nova::Result<void, InitError> init(rhi::IDevice& device,
                                       std::span<const nova::core::u32> backgroundVert,
                                       std::span<const nova::core::u32> backgroundFrag,
                                       std::span<const nova::core::u32> triangleVert,
                                       std::span<const nova::core::u32> triangleFrag);

    /// Release all device resources. Must run before the device is destroyed.
    void shutdown();

    /// Record the frame described by `snapshot` into `frame`'s command buffer.
    /// Returns false when pipelines could not be created for this format.
    bool render(rhi::FrameInfo& frame, const SceneSnapshot& snapshot);

    float pick(BuiltinMesh mesh, math::Vec3 origin, math::Vec3 direction) const { return hitMesh(m_geometry, mesh, origin, direction); }
    void setTerrain(const std::vector<float>& heights);
    void setGrid(bool visible) { m_grid = visible; }

    bool isInitialized() const { return m_device != nullptr; }
    const RenderStats& stats() const { return m_stats; }

    /// Bytes per object slice in the frame UBO. 256 covers every Vulkan
    /// minUniformBufferOffsetAlignment (16/64/256) and gives each draw command
    /// its own descriptor set without dynamic offsets.
    static constexpr nova::core::usize kUniformSliceBytes = 256;

    /// Draw commands per frame beyond which the frame budget is exceeded and
    /// the overflow is dropped (counted in RenderStats::droppedCommands).
    /// Slice 0 is the camera/background block, hence the + 1.
    static constexpr u32 kMaxDrawCommandsPerFrame = 32;

    /// Initial view-projection (exposed for tests and as the reference camera):
    /// Vulkan clip space, 60° vertical FOV, camera at +Z looking at the origin.
    static math::Mat4 viewProjection(float aspect);

private:
    bool ensurePipelines(rhi::Format colorFormat);

    /// GPU resources for a builtin mesh, or nullptr when unsupported.
    rhi::IPipeline* pipelineFor(BuiltinMesh mesh) const;
    u32 vertexCountFor(BuiltinMesh mesh) const;
    GeometryAtlas m_geometry;
    std::vector<float> m_heights;
    bool m_grid = true;

    rhi::IDevice* m_device = nullptr;

    // SPIR-V blobs stay alive for pipeline recreation on format changes.
    std::vector<nova::core::u32> m_backgroundVert;
    std::vector<nova::core::u32> m_backgroundFrag;
    std::vector<nova::core::u32> m_triangleVert;
    std::vector<nova::core::u32> m_triangleFrag;

    nova::rhi::Unique<nova::rhi::IShaderModule> m_backgroundVs;
    nova::rhi::Unique<nova::rhi::IShaderModule> m_backgroundFs;
    nova::rhi::Unique<nova::rhi::IShaderModule> m_triangleVs;
    nova::rhi::Unique<nova::rhi::IShaderModule> m_triangleFs;

    nova::rhi::Unique<nova::rhi::IBuffer> m_vertexBuffer;
    static constexpr nova::core::u32 kFrameSlots = 2;
    std::array<nova::rhi::Unique<nova::rhi::IBuffer>, kFrameSlots> m_uniforms{};

    nova::rhi::Unique<nova::rhi::IPipeline> m_backgroundPipeline;
    nova::rhi::Unique<nova::rhi::IPipeline> m_trianglePipeline;
    rhi::Format m_pipelineFormat = rhi::Format::Unknown;

    RenderStats m_stats{};
};

}  // namespace nova::renderer
