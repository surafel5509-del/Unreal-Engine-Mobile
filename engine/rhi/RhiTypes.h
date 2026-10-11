#pragma once

// Nova Engine — RHI (Render Hardware Interface) shared types.
//
// The RHI is deliberately small and mobile-first: it models exactly what the
// renderer needs (Vulkan 1.3 dynamic rendering on Android, GL ES 3.2 later),
// so backends stay thin and no backend enum leaks into renderer code.
//
// Conventions:
//  * All GPU handles are owned through nova::Unique<> RAII types.
//  * Extents are in physical pixels.
//  * Colors are linear-space floats ([0,1]); gamma handling lives in shaders.

#include "core/Types.h"

#include <array>
#include <span>

namespace nova::rhi {

using nova::Unique;
using nova::core::f32;
using nova::core::u32;
using nova::core::u64;
using nova::core::u8;
using nova::core::usize;

inline constexpr u32 kMaxColorAttachments = 4;

enum class Format : nova::core::u8 {
    Unknown = 0,
    RGBA8_UNorm,
    RGBA8_SRGB,
    BGRA8_UNorm,
    BGRA8_SRGB,
    RGBA16_SFloat,
    R32_SFloat,
    RG32_SFloat,
    RGB32_SFloat,
    RGBA32_SFloat,
    D32_SFloat,
};

/// Bytes per texel for the formats the M1 backend exposes.
usize formatTexelSize(Format format);

/// Human-readable name (for logs/console).
const char* formatName(Format format);

enum class BufferUsage : nova::core::u8 { Vertex, Index, Uniform, Storage };

/// DeviceLocal: GPU-only, best for static geometry.
/// HostVisible: persistently mapped, written by the CPU every frame (UBOs).
enum class BufferDomain : nova::core::u8 { DeviceLocal, HostVisible };

struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    BufferDomain domain = BufferDomain::DeviceLocal;
    usize size = 0;
    const char* debugName = nullptr;
};

enum class ShaderStage : nova::core::u8 { Vertex, Fragment, Compute };

struct ShaderModuleDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::span<const nova::core::u32> spirv;
    const char* debugName = nullptr;
};

enum class PrimitiveTopology : nova::core::u8 { TriangleList, LineList, PointList };
enum class CullMode : nova::core::u8 { None, Back, Front };
enum class CompareOp : nova::core::u8 { Never, Less, LessEqual, Greater, GreaterEqual, Equal, Always };
enum class BlendMode : nova::core::u8 { Opaque, AlphaBlend };
enum class LoadOp : nova::core::u8 { Load, Clear, DontCare };
enum class StoreOp : nova::core::u8 { Store, DontCare };
enum class VertexRate : nova::core::u8 { PerVertex, PerInstance };

/// Swapchain pacing. FIFO is the only universally supported mode (60 Hz cap).
enum class PresentMode : nova::core::u8 { Fifo, Mailbox, Immediate };

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct VertexAttribute {
    u32 location = 0;
    Format format = Format::Unknown;
    u32 offset = 0;
};

/// One interleaved vertex stream (M1 uses a single binding; multi-stream
/// layouts are added with the glTF milestone).
struct VertexBinding {
    u32 binding = 0;
    u32 stride = 0;
    VertexRate rate = VertexRate::PerVertex;
    std::array<VertexAttribute, 8> attributes{};
    u32 attributeCount = 0;
};

/// Everything needed to create a graphics pipeline. Color/depth formats come
/// from the target pass (swapchain format is known at pipeline creation).
struct PipelineDesc {
    const class IShaderModule* vertexShader = nullptr;
    const class IShaderModule* fragmentShader = nullptr;
    VertexBinding vertexBinding{};  // attributeCount == 0 => vertex-less pass
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    CompareOp depthCompare = CompareOp::Less;
    bool depthTest = false;
    bool depthWrite = false;
    std::array<Format, kMaxColorAttachments> colorFormats{};
    u32 colorAttachmentCount = 1;
    Format depthFormat = Format::Unknown;
    const char* debugName = nullptr;
};

/// A color attachment binding for one dynamic-rendering scope.
struct ColorAttachment {
    class ITexture* texture = nullptr;
    LoadOp loadOp = LoadOp::DontCare;
    StoreOp storeOp = StoreOp::Store;
    ClearColor clearColor{};
};

/// Counters exposed to the stats HUD / console `stats`.
struct DeviceStats {
    u32 width = 0;
    u32 height = 0;
    u32 imageCount = 0;
    u32 framesInFlight = 0;
    u64 frameIndex = 0;
    u32 pipelinesCreated = 0;
    u32 buffersCreated = 0;
    bool validationEnabled = false;
    PresentMode presentMode = PresentMode::Fifo;
};

}  // namespace nova::rhi
