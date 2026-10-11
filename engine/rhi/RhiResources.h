#pragma once

// Nova Engine — RHI resource and command interfaces.
//
// All resources are RAII (nova::Unique<>) and backend-neutral. Virtual
// dispatch exists on resource objects and command recording only — never in
// inner per-vertex loops (coding standards).

#include "core/Types.h"
#include "rhi/RhiTypes.h"

namespace nova::rhi {

/// A GPU buffer. HostVisible buffers are persistently mapped; mappedData()
/// returns nullptr for DeviceLocal buffers.
class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual const BufferDesc& desc() const = 0;
    virtual void* mappedData() const = 0;
};

/// A GPU texture. Swapchain backbuffers are exposed as ITexture so passes
/// reference attachments uniformly.
class ITexture {
public:
    virtual ~ITexture() = default;
    virtual Format format() const = 0;
    virtual u32 width() const = 0;
    virtual u32 height() const = 0;
};

/// A compiled SPIR-V shader module.
class IShaderModule {
public:
    virtual ~IShaderModule() = default;
    virtual ShaderStage stage() const = 0;
};

/// A graphics pipeline (PSO): shaders + vertex layout + render-state,
/// validated against the attachment formats it is used with.
class IPipeline {
public:
    virtual ~IPipeline() = default;
    virtual const PipelineDesc& desc() const = 0;
};

/// Command recording for the frame currently open on IDevice::beginFrame().
/// Valid only between beginFrame() and endFrame().
class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    /// Open a dynamic-rendering scope with the given color attachments.
    virtual void beginRendering(u32 width, u32 height,
                                std::span<const ColorAttachment> colorAttachments) = 0;
    virtual void endRendering() = 0;

    /// Full-surface viewport + scissor (M1 always renders viewport-sized
    /// passes; per-pass viewports arrive with the editor milestone).
    virtual void setViewportScissor(u32 width, u32 height) = 0;

    virtual void bindPipeline(const IPipeline* pipeline) = 0;

    /// Bind the vertex buffer for subsequent draws (no-op for vertex-less
    /// pipelines, but must not be called with null on pipelines with layout).
    virtual void bindVertexBuffer(const IBuffer* buffer) = 0;

    /// Bind a uniform buffer at descriptor slot `slot` for subsequent draws
    /// (slot 0 is the frame UBO; layout is shared by all M1 pipelines).
    virtual void bindUniformBuffer(u32 slot, const IBuffer* buffer, usize offset,
                                   usize range) = 0;

    virtual void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0,
                      u32 firstInstance = 0) = 0;
};

}  // namespace nova::rhi
