#include "renderer/Renderer.h"

#include "core/Assert.h"
#include "renderer/GlesShaders.h"
#include "core/Log.h"

#include <cstring>

namespace nova::renderer {
namespace {

using nova::core::u32;
using nova::core::usize;

// Interleaved triangle: position (vec3) + color (vec3), Y-up. Colors follow
// the engine palette: violet, cyan, magenta.
constexpr std::array<float, 18> kTriangleVertices = {
    0.0f,  0.75f, 0.0f, 0.486f, 0.302f, 1.0f,   // top     — #7C4DFF
    -0.75f, -0.65f, 0.0f, 0.0f,  0.898f, 1.0f,  // bottomL — #00E5FF
    0.75f, -0.65f, 0.0f, 1.0f,  0.302f, 0.553f, // bottomR — #FF4D8D
};

constexpr std::array<float, 216> kCubeVertices = {
    -0.500f, -0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    0.500f, -0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    0.500f, 0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    -0.500f, -0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    0.500f, 0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    -0.500f, 0.500f, 0.500f, 0.620f, 0.700f, 1.000f,
    0.500f, -0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    -0.500f, -0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    -0.500f, 0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    0.500f, -0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    -0.500f, 0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    0.500f, 0.500f, -0.500f, 0.350f, 0.450f, 0.750f,
    -0.500f, -0.500f, -0.500f, 0.450f, 0.550f, 0.880f,
    -0.500f, -0.500f, 0.500f, 0.450f, 0.550f, 0.880f,
    -0.500f, 0.500f, 0.500f, 0.450f, 0.550f, 0.880f,
    -0.500f, -0.500f, -0.500f, 0.450f, 0.550f, 0.880f,
    -0.500f, 0.500f, 0.500f, 0.450f, 0.550f, 0.880f,
    -0.500f, 0.500f, -0.500f, 0.450f, 0.550f, 0.880f,
    0.500f, -0.500f, 0.500f, 0.500f, 0.620f, 0.950f,
    0.500f, -0.500f, -0.500f, 0.500f, 0.620f, 0.950f,
    0.500f, 0.500f, -0.500f, 0.500f, 0.620f, 0.950f,
    0.500f, -0.500f, 0.500f, 0.500f, 0.620f, 0.950f,
    0.500f, 0.500f, -0.500f, 0.500f, 0.620f, 0.950f,
    0.500f, 0.500f, 0.500f, 0.500f, 0.620f, 0.950f,
    -0.500f, 0.500f, 0.500f, 0.800f, 0.850f, 1.000f,
    0.500f, 0.500f, 0.500f, 0.800f, 0.850f, 1.000f,
    0.500f, 0.500f, -0.500f, 0.800f, 0.850f, 1.000f,
    -0.500f, 0.500f, 0.500f, 0.800f, 0.850f, 1.000f,
    0.500f, 0.500f, -0.500f, 0.800f, 0.850f, 1.000f,
    -0.500f, 0.500f, -0.500f, 0.800f, 0.850f, 1.000f,
    -0.500f, -0.500f, -0.500f, 0.300f, 0.380f, 0.600f,
    0.500f, -0.500f, -0.500f, 0.300f, 0.380f, 0.600f,
    0.500f, -0.500f, 0.500f, 0.300f, 0.380f, 0.600f,
    -0.500f, -0.500f, -0.500f, 0.300f, 0.380f, 0.600f,
    0.500f, -0.500f, 0.500f, 0.300f, 0.380f, 0.600f,
    -0.500f, -0.500f, 0.500f, 0.300f, 0.380f, 0.600f,
};

}  // namespace

const char* initErrorMessage(InitError error) {
    switch (error) {
        case InitError::MissingShader: return "shader blob missing or unreadable";
        case InitError::ShaderModuleFailed: return "shader module creation failed";
        case InitError::BufferFailed: return "buffer allocation failed";
        case InitError::PipelineFailed: return "graphics pipeline creation failed";
    }
    return "unknown renderer error";
}

math::Mat4 Renderer::viewProjection(float aspect) {
    const math::Mat4 projection = math::perspectiveVk(math::toRadians(60.0f), aspect, 0.1f,
                                                      100.0f);
    const math::Mat4 view = math::lookAt(math::Vec3(0.0f, 0.0f, 3.0f),
                                         math::Vec3(0.0f, 0.0f, 0.0f),
                                         math::Vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

nova::Result<void, InitError> Renderer::init(
    rhi::IDevice& device, std::span<const u32> backgroundVert,
    std::span<const u32> backgroundFrag, std::span<const u32> triangleVert,
    std::span<const u32> triangleFrag) {
    NOVA_ENSURE(device.usesGlsl() || (backgroundVert.size() > 0 && backgroundFrag.size() > 0 &&
                    triangleVert.size() > 0 && triangleFrag.size() > 0),
                "renderer init requires four SPIR-V blobs");

    shutdown();
    m_device = &device;

    m_backgroundVert.assign(backgroundVert.begin(), backgroundVert.end());
    m_backgroundFrag.assign(backgroundFrag.begin(), backgroundFrag.end());
    m_triangleVert.assign(triangleVert.begin(), triangleVert.end());
    m_triangleFrag.assign(triangleFrag.begin(), triangleFrag.end());

    auto createModule = [&](std::vector<u32>& blob, rhi::ShaderStage stage,
                            const char* name) -> nova::rhi::Unique<rhi::IShaderModule> {
        rhi::ShaderModuleDesc desc{};
        desc.stage = stage;
        desc.spirv = blob;
        desc.debugName = name;
        if (std::strcmp(name, "background.vert") == 0) desc.glsl = gles::backgroundVertex;
        else if (std::strcmp(name, "background.frag") == 0) desc.glsl = gles::backgroundFragment;
        else if (stage == rhi::ShaderStage::Vertex) desc.glsl = gles::meshVertex;
        else desc.glsl = gles::meshFragment;
        auto module = device.createShaderModule(desc);
        if (module == nullptr) {
            NOVA_LOG_ERROR("renderer: failed to create shader module '%s'", name);
        }
        return module;
    };

    m_backgroundVs = createModule(m_backgroundVert, rhi::ShaderStage::Vertex,
                                  "background.vert");
    m_backgroundFs = createModule(m_backgroundFrag, rhi::ShaderStage::Fragment,
                                  "background.frag");
    m_triangleVs = createModule(m_triangleVert, rhi::ShaderStage::Vertex, "triangle.vert");
    m_triangleFs = createModule(m_triangleFrag, rhi::ShaderStage::Fragment, "triangle.frag");
    if (m_backgroundVs == nullptr || m_backgroundFs == nullptr || m_triangleVs == nullptr ||
        m_triangleFs == nullptr) {
        shutdown();
        return nova::Result<void, InitError>::failure(InitError::ShaderModuleFailed);
    }

    // Triangle geometry. HostVisible for M1 (static GPU-upload path with
    // staging lands with the glTF milestone); 36 bytes is irrelevant to VRAM.
    rhi::BufferDesc vertexDesc{};
    vertexDesc.usage = rhi::BufferUsage::Vertex;
    vertexDesc.domain = rhi::BufferDomain::HostVisible;
    vertexDesc.size = sizeof(kTriangleVertices) + sizeof(kCubeVertices);
    vertexDesc.debugName = "nova.triangle.vertices";
    m_vertexBuffer = device.createBuffer(vertexDesc);
    if (m_vertexBuffer == nullptr) {
        shutdown();
        return nova::Result<void, InitError>::failure(InitError::BufferFailed);
    }
    std::memcpy(m_vertexBuffer->mappedData(), kTriangleVertices.data(),
                sizeof(kTriangleVertices));
    std::memcpy(static_cast<u8*>(m_vertexBuffer->mappedData()) + sizeof(kTriangleVertices),
                kCubeVertices.data(), sizeof(kCubeVertices));

    // One persistently mapped UBO per frame slot (matches frames-in-flight),
    // sliced per object: slice 0 is the camera/background block, slices 1..N
    // hold one FrameUniforms each for the frame's draw commands.
    constexpr nova::core::usize kUniformBufferBytes =
        (static_cast<nova::core::usize>(kMaxDrawCommandsPerFrame) + 1) * kUniformSliceBytes;
    static_assert(kUniformSliceBytes >= sizeof(FrameUniforms), "UBO slice too small");

    for (u32 slot = 0; slot < kFrameSlots; ++slot) {
        rhi::BufferDesc uniformDesc{};
        uniformDesc.usage = rhi::BufferUsage::Uniform;
        uniformDesc.domain = rhi::BufferDomain::HostVisible;
        uniformDesc.size = kUniformBufferBytes;
        uniformDesc.debugName = "nova.frame.ubo";
        m_uniforms[slot] = device.createBuffer(uniformDesc);
        if (m_uniforms[slot] == nullptr) {
            shutdown();
            return nova::Result<void, InitError>::failure(InitError::BufferFailed);
        }
        // Zero-initialize so the first frame is defined even if render()
        // is interrupted.
        std::memset(m_uniforms[slot]->mappedData(), 0, kUniformBufferBytes);
    }

    m_stats = RenderStats{};
    NOVA_LOG_INFO("renderer: initialized (gradient background + scene draw list)");
    return nova::Result<void, InitError>::success();
}

void Renderer::shutdown() {
    if (m_device == nullptr) return;
    // Pipelines first (they reference modules), then buffers, then modules.
    m_backgroundPipeline.reset();
    m_trianglePipeline.reset();
    m_pipelineFormat = rhi::Format::Unknown;
    m_uniforms[0].reset();
    m_uniforms[1].reset();
    m_vertexBuffer.reset();
    m_backgroundVs.reset();
    m_backgroundFs.reset();
    m_triangleVs.reset();
    m_triangleFs.reset();
    m_backgroundVert.clear();
    m_backgroundFrag.clear();
    m_triangleVert.clear();
    m_triangleFrag.clear();
    m_device = nullptr;
    m_stats = RenderStats{};
}

bool Renderer::ensurePipelines(rhi::Format colorFormat) {
    NOVA_ENSURE(m_device != nullptr, "ensurePipelines before init");
    if (m_backgroundPipeline != nullptr && m_trianglePipeline != nullptr &&
        m_pipelineFormat == colorFormat) {
        return true;
    }

    m_backgroundPipeline.reset();
    m_trianglePipeline.reset();
    m_pipelineFormat = colorFormat;

    // Fullscreen gradient: position comes from gl_VertexIndex (no VBO).
    rhi::PipelineDesc background{};
    background.vertexShader = m_backgroundVs.get();
    background.fragmentShader = m_backgroundFs.get();
    background.vertexBinding.attributeCount = 0;
    background.cull = rhi::CullMode::None;
    background.blend = rhi::BlendMode::Opaque;
    background.colorFormats[0] = colorFormat;
    background.colorAttachmentCount = 1;
    background.debugName = "nova.background";
    m_backgroundPipeline = m_device->createPipeline(background);
    if (m_backgroundPipeline == nullptr) {
        NOVA_LOG_ERROR("renderer: background pipeline failed for format %s",
                       rhi::formatName(colorFormat));
        return false;
    }

    // Hero triangle: interleaved pos+color, back-face culled.
    rhi::PipelineDesc triangle{};
    triangle.vertexShader = m_triangleVs.get();
    triangle.fragmentShader = m_triangleFs.get();
    triangle.vertexBinding.binding = 0;
    triangle.vertexBinding.stride = 6 * sizeof(float);
    triangle.vertexBinding.rate = rhi::VertexRate::PerVertex;
    triangle.vertexBinding.attributeCount = 2;
    triangle.vertexBinding.attributes[0] = {0, rhi::Format::RGB32_SFloat, 0};
    triangle.vertexBinding.attributes[1] = {1, rhi::Format::RGB32_SFloat, 12};
    triangle.cull = rhi::CullMode::None;
    triangle.depthTest = m_device->usesGlsl();
    triangle.depthWrite = m_device->usesGlsl();
    triangle.blend = rhi::BlendMode::Opaque;
    triangle.colorFormats[0] = colorFormat;
    triangle.colorAttachmentCount = 1;
    triangle.debugName = "nova.triangle";
    m_trianglePipeline = m_device->createPipeline(triangle);
    if (m_trianglePipeline == nullptr) {
        NOVA_LOG_ERROR("renderer: triangle pipeline failed for format %s",
                       rhi::formatName(colorFormat));
        m_backgroundPipeline.reset();
        return false;
    }

    m_stats.pipelines = 2;
    NOVA_LOG_INFO("renderer: pipelines created for format %s",
                  rhi::formatName(colorFormat));
    return true;
}

rhi::IPipeline* Renderer::pipelineFor(BuiltinMesh mesh) const {
    switch (mesh) {
        case BuiltinMesh::kTriangle:
        case BuiltinMesh::kCube: return m_trianglePipeline.get();
        case BuiltinMesh::kNone: break;
    }
    return nullptr;
}

u32 Renderer::vertexCountFor(BuiltinMesh mesh) {
    switch (mesh) {
        case BuiltinMesh::kTriangle: return 3;
        case BuiltinMesh::kCube: return 36;
        case BuiltinMesh::kNone: break;
    }
    return 0;
}

bool Renderer::render(rhi::FrameInfo& frame, const SceneSnapshot& snapshot) {
    NOVA_ENSURE(m_device != nullptr, "render before init");
    NOVA_ENSURE(frame.commands != nullptr && frame.backbuffer != nullptr,
                "render without a valid frame");
    if (!ensurePipelines(frame.colorFormat)) {
        return false;
    }

    const u32 slot = frame.ringIndex < kFrameSlots ? frame.ringIndex : 0;
    NOVA_ENSURE(m_uniforms[slot] != nullptr, "missing frame UBO slot");

    const float aspect = snapshot.aspect > 0.0f
                             ? snapshot.aspect
                             : (frame.height > 0
                                    ? static_cast<float>(frame.width) / static_cast<float>(frame.height)
                                    : 1.0f);

    // Slice 0: camera + palette constants shared by every pass this frame.
    FrameUniforms base{};
    base.transform = snapshot.viewProjection;
    base.colorA = math::Vec4(0.055f, 0.063f, 0.145f, 1.0f);  // deep indigo (top)
    base.colorB = math::Vec4(0.008f, 0.012f, 0.031f, 1.0f);  // near black (bottom)
    base.accent = math::Vec4(0.486f, 0.302f, 1.0f, 1.0f);    // #7C4DFF
    base.time = snapshot.timeSeconds;
    base.aspect = aspect;

    auto* uniformBytes = static_cast<nova::core::u8*>(m_uniforms[slot]->mappedData());
    std::memcpy(uniformBytes, &base, sizeof(base));

    rhi::ICommandBuffer* commands = frame.commands;
    rhi::ColorAttachment color{};
    color.texture = frame.backbuffer;
    color.loadOp = rhi::LoadOp::DontCare;
    color.storeOp = rhi::StoreOp::Store;
    color.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    commands->beginRendering(frame.width, frame.height, std::span<const rhi::ColorAttachment>(&color, 1));

    // 1) Gradient background (fullscreen triangle, covers every pixel).
    commands->bindPipeline(m_backgroundPipeline.get());
    commands->bindUniformBuffer(0, m_uniforms[slot].get(), 0, sizeof(FrameUniforms));
    commands->draw(3);

    // 2) One pass per draw command, each with its own UBO slice (and therefore
    //    its own descriptor set — the device allocates one per bind).
    u32 draws = 1;
    u32 dropped = 0;
    const nova::core::usize commandCount = snapshot.commands.size();
    const nova::core::usize budget =
        commandCount < kMaxDrawCommandsPerFrame ? commandCount : kMaxDrawCommandsPerFrame;

    for (nova::core::usize index = 0; index < commandCount; ++index) {
        const DrawCommand& command = snapshot.commands[index];
        rhi::IPipeline* pipeline = pipelineFor(command.mesh);
        const u32 vertexCount = vertexCountFor(command.mesh);
        if (pipeline == nullptr || vertexCount == 0) {
            ++dropped;  // unknown mesh: nothing to draw, not a budget problem
            continue;
        }
        if (index >= budget) {
            ++dropped;
            continue;
        }

        FrameUniforms slice = base;
        slice.transform = snapshot.viewProjection * command.world;
        slice.accent = command.tint;
        const nova::core::usize offset = (index + 1) * kUniformSliceBytes;
        std::memcpy(uniformBytes + offset, &slice, sizeof(slice));

        commands->bindPipeline(pipeline);
        commands->bindVertexBuffer(m_vertexBuffer.get());
        commands->bindUniformBuffer(0, m_uniforms[slot].get(), offset, sizeof(FrameUniforms));
        commands->draw(vertexCount, 1, command.mesh == BuiltinMesh::kCube ? 3 : 0);
        ++draws;
    }

    if (dropped > 0) {
        NOVA_LOG_WARN("renderer: dropped %u draw command(s) (budget %u per frame)", dropped,
                         kMaxDrawCommandsPerFrame);
    }

    commands->endRendering();

    m_stats.drawCalls = draws;
    m_stats.droppedCommands = dropped;
    m_stats.frameSlot = slot;
    return true;
}

}  // namespace nova::renderer
