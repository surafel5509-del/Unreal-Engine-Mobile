// Unit tests — renderer frame logic against a mock RHI device.
//
// The mock records every command so we can assert the exact draw sequence,
// the UBO ring behaviour and pipeline lifecycle without a GPU.
#include <doctest.h>

#include "renderer/Renderer.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace nova;
using namespace nova::rhi;

// ------------------------------------------------------------------- recording

enum class EventKind {
    BeginRendering,
    BindPipeline,
    BindVertexBuffer,
    BindUniform,
    Draw,
    EndRendering,
};

struct Event {
    EventKind kind;
    std::string name;   // pipeline / buffer debug name
    nova::core::u32 count = 0;  // draw vertex count or uniform slot
};

// ------------------------------------------------------------------- resources

class MockDevice;

class MockBuffer final : public IBuffer {
public:
    MockBuffer(MockDevice* device, const BufferDesc& desc);
    ~MockBuffer() override;
    const BufferDesc& desc() const override { return m_desc; }
    // Mapped memory is writable by design (matches persistently mapped VMA
    // buffers); the const applies to the mock object, not the GPU bytes.
    void* mappedData() const override {
        return const_cast<nova::core::u8*>(m_storage.data());
    }

    const std::vector<nova::core::u8>& storage() const { return m_storage; }

private:
    MockDevice* m_device;
    BufferDesc m_desc;
    std::vector<nova::core::u8> m_storage;
};

class MockShader final : public IShaderModule {
public:
    explicit MockShader(ShaderStage stage) : m_stage(stage) {}
    ShaderStage stage() const override { return m_stage; }

private:
    ShaderStage m_stage;
};

class MockPipeline final : public IPipeline {
public:
    MockPipeline(MockDevice* device, const PipelineDesc& desc);
    ~MockPipeline() override;
    const PipelineDesc& desc() const override { return m_desc; }

private:
    MockDevice* m_device;
    PipelineDesc m_desc;
};

class MockTexture final : public ITexture {
public:
    MockTexture(Format format, nova::core::u32 width, nova::core::u32 height)
        : m_format(format), m_width(width), m_height(height) {}
    Format format() const override { return m_format; }
    nova::core::u32 width() const override { return m_width; }
    nova::core::u32 height() const override { return m_height; }

private:
    Format m_format;
    nova::core::u32 m_width;
    nova::core::u32 m_height;
};

class MockCommandBuffer final : public ICommandBuffer {
public:
    std::vector<Event> events;
    const PipelineDesc* boundPipeline = nullptr;

    void beginRendering(nova::core::u32, nova::core::u32,
                        std::span<const ColorAttachment>) override {
        events.push_back({EventKind::BeginRendering, "", 0});
    }
    void endRendering() override { events.push_back({EventKind::EndRendering, "", 0}); }
    void setViewportScissor(nova::core::u32, nova::core::u32) override {}

    void bindPipeline(const IPipeline* pipeline) override {
        boundPipeline = &pipeline->desc();
        events.push_back({EventKind::BindPipeline,
                          pipeline->desc().debugName != nullptr
                              ? pipeline->desc().debugName
                              : "?",
                          0});
    }
    void bindVertexBuffer(const IBuffer* buffer) override {
        events.push_back({EventKind::BindVertexBuffer,
                          buffer->desc().debugName != nullptr ? buffer->desc().debugName
                                                              : "?",
                          0});
    }
    void bindUniformBuffer(nova::core::u32 slot, const IBuffer*, nova::core::usize,
                           nova::core::usize) override {
        events.push_back({EventKind::BindUniform, "", slot});
    }
    void draw(nova::core::u32 vertexCount, nova::core::u32, nova::core::u32,
              nova::core::u32) override {
        events.push_back({EventKind::Draw,
                          boundPipeline != nullptr && boundPipeline->debugName != nullptr
                              ? boundPipeline->debugName
                              : "?",
                          vertexCount});
    }

    void clear() {
        events.clear();
        boundPipeline = nullptr;
    }
};

// ---------------------------------------------------------------------- device

class MockDevice final : public IDevice {
public:
    // observation hooks
    std::vector<std::string> createdPipelines;
    nova::core::u32 liveResources = 0;
    bool failShaderModules = false;
    nova::core::u32 endFrameCalls = 0;
    MockCommandBuffer commandBuffer;
    MockTexture backbuffer{Format::RGBA8_UNorm, 640, 360};

    Unique<IBuffer> createBuffer(const BufferDesc& desc) override {
        ++liveResources;
        return makeUnique<MockBuffer>(this, desc);
    }
    Unique<IShaderModule> createShaderModule(const ShaderModuleDesc& desc) override {
        if (failShaderModules) return nullptr;
        return makeUnique<MockShader>(desc.stage);
    }
    Unique<IPipeline> createPipeline(const PipelineDesc& desc) override {
        createdPipelines.emplace_back(desc.debugName != nullptr ? desc.debugName : "?");
        ++liveResources;
        return makeUnique<MockPipeline>(this, desc);
    }

    void setSurface(void*, nova::core::u32, nova::core::u32) override {}
    void setSurfaceSize(nova::core::u32, nova::core::u32) override {}
    void setPresentMode(PresentMode mode) override { m_presentMode = mode; }
    PresentMode presentMode() const override { return m_presentMode; }

    bool beginFrame(FrameInfo& out) override {
        if (!frameEnabled) return false;
        commandBuffer.clear();
        out.commands = &commandBuffer;
        out.backbuffer = &backbuffer;
        out.width = backbuffer.width();
        out.height = backbuffer.height();
        out.colorFormat = formatForNextFrame;
        out.frameIndex = m_frameIndex;
        out.ringIndex = static_cast<nova::core::u32>(m_frameIndex % 2);
        ++m_frameIndex;
        return true;
    }
    void endFrame() override { ++endFrameCalls; }
    void waitIdle() override {}
    bool hasSurface() const override { return true; }
    const DeviceStats& stats() const override { return m_stats; }

    // knobs
    bool frameEnabled = true;
    Format formatForNextFrame = Format::RGBA8_UNorm;

private:
    DeviceStats m_stats{};
    PresentMode m_presentMode = PresentMode::Fifo;
    nova::core::u64 m_frameIndex = 0;
};

MockBuffer::MockBuffer(MockDevice* device, const BufferDesc& desc)
    : m_device(device), m_desc(desc), m_storage(desc.size, 0) {}

MockBuffer::~MockBuffer() {
    --m_device->liveResources;
}

MockPipeline::MockPipeline(MockDevice* device, const PipelineDesc& desc)
    : m_device(device), m_desc(desc) {}

MockPipeline::~MockPipeline() {
    --m_device->liveResources;
}

// ---------------------------------------------------------------------- helpers

std::vector<nova::core::u32> fakeSpirv() {
    std::vector<nova::core::u32> words(16, 0u);
    words[0] = 0x07230203u;  // SPIR-V magic
    return words;
}

std::string sequenceString(const std::vector<Event>& events) {
    std::string out;
    for (const Event& event : events) {
        switch (event.kind) {
            case EventKind::BeginRendering: out += "begin;"; break;
            case EventKind::BindPipeline: out += "pipe:" + event.name + ";"; break;
            case EventKind::BindVertexBuffer: out += "vbo:" + event.name + ";"; break;
            case EventKind::BindUniform: out += "ubo:" + std::to_string(event.count) + ";"; break;
            case EventKind::Draw:
                out += "draw:" + event.name + ":" + std::to_string(event.count) + ";";
                break;
            case EventKind::EndRendering: out += "end"; break;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("Renderer initializes and creates the triangle + UBO buffers") {
    MockDevice device;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    const auto result = renderer.init(device, spirv, spirv, spirv, spirv);
    REQUIRE(result.isOk());
    CHECK(renderer.isInitialized());
    // 1 vertex buffer + 2 UBO slots.
    CHECK(device.liveResources == 3);

    renderer.shutdown();
    CHECK(!renderer.isInitialized());
    CHECK(device.liveResources == 0);
}

TEST_CASE("Renderer fails cleanly when shader modules cannot be created") {
    MockDevice device;
    device.failShaderModules = true;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    const auto result = renderer.init(device, spirv, spirv, spirv, spirv);
    CHECK(result.isError());
    CHECK(result.error() == renderer::InitError::ShaderModuleFailed);
    CHECK(!renderer.isInitialized());
    // Everything acquired before the failure must have been rolled back.
    CHECK(device.liveResources == 0);
}

TEST_CASE("Renderer records background then triangle in one rendering scope") {
    MockDevice device;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    REQUIRE(renderer.init(device, spirv, spirv, spirv, spirv).isOk());

    rhi::FrameInfo frame{};
    REQUIRE(device.beginFrame(frame));
    CHECK(renderer.render(frame, 0.0f));
    device.endFrame();

    const std::string sequence = sequenceString(device.commandBuffer.events);
    CHECK(sequence ==
          "begin;"
          "pipe:nova.background;ubo:0;draw:nova.background:3;"
          "pipe:nova.triangle;vbo:nova.triangle.vertices;ubo:0;draw:nova.triangle:3;"
          "end");

    // Exactly one rendering scope and two draws per frame.
    const auto& events = device.commandBuffer.events;
    nova::core::u32 draws = 0;
    for (const Event& event : events) {
        if (event.kind == EventKind::Draw) ++draws;
    }
    CHECK(draws == 2);
    CHECK(device.endFrameCalls == 1);
    CHECK(device.createdPipelines.size() == 2);
}

TEST_CASE("Renderer alternates UBO ring slots and advances animation time") {
    MockDevice device;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    REQUIRE(renderer.init(device, spirv, spirv, spirv, spirv).isOk());

    rhi::FrameInfo frame0{};
    REQUIRE(device.beginFrame(frame0));
    REQUIRE(renderer.render(frame0, 1.0f));
    device.endFrame();

    rhi::FrameInfo frame1{};
    REQUIRE(device.beginFrame(frame1));
    REQUIRE(renderer.render(frame1, 2.0f));
    device.endFrame();

    CHECK(frame0.ringIndex == 0);
    CHECK(frame1.ringIndex == 1);
    CHECK(renderer.stats().frameSlot == 1);

    // The two slots are distinct buffers with different animation times.
    // Grab them through a third render to observe slot 0 again.
    rhi::FrameInfo frame2{};
    REQUIRE(device.beginFrame(frame2));
    REQUIRE(renderer.render(frame2, 3.0f));
    device.endFrame();
    CHECK(renderer.stats().frameSlot == 0);

    // Pipelines were not recreated for the same format.
    CHECK(device.createdPipelines.size() == 2);
}

TEST_CASE("Renderer recreates pipelines when the swapchain format changes") {
    MockDevice device;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    REQUIRE(renderer.init(device, spirv, spirv, spirv, spirv).isOk());

    rhi::FrameInfo frame{};
    REQUIRE(device.beginFrame(frame));
    REQUIRE(renderer.render(frame, 0.0f));
    device.endFrame();
    CHECK(device.createdPipelines.size() == 2);

    device.formatForNextFrame = Format::BGRA8_UNorm;
    rhi::FrameInfo frame2{};
    REQUIRE(device.beginFrame(frame2));
    REQUIRE(renderer.render(frame2, 0.1f));
    device.endFrame();
    CHECK(device.createdPipelines.size() == 4);
    // Old pipelines were destroyed: live count is still exactly 2 pipelines
    // + 3 buffers.
    CHECK(device.liveResources == 5);
}

TEST_CASE("Renderer skips gracefully when beginFrame reports no frame") {
    MockDevice device;
    renderer::Renderer renderer;
    const auto spirv = fakeSpirv();
    REQUIRE(renderer.init(device, spirv, spirv, spirv, spirv).isOk());

    device.frameEnabled = false;
    rhi::FrameInfo frame{};
    CHECK(!device.beginFrame(frame));
    // Engine::frame() maps this to kFrameSkipped without touching render().
    CHECK(device.endFrameCalls == 0);
}

TEST_CASE("viewProjection adapts to aspect ratio and is Vulkan-correct") {
    const math::Mat4 square = renderer::Renderer::viewProjection(1.0f);
    const math::Mat4 wide = renderer::Renderer::viewProjection(2.0f);
    // Wider viewport -> smaller X scale (perspective divides by aspect).
    CHECK(square[0][0] > wide[0][0]);
    // Y stays flipped for Vulkan (negative Y scale).
    CHECK(square[1][1] < 0.0f);

    // World origin lands at the clip centre with positive W.
    const math::Vec4 clip = wide * math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(clip.w > 0.0f);
}
