#pragma once

// Nova Engine — device/swapchain interface.
//
// Frame model (single-threaded per device, serialized by nova::Engine):
//
//   DeviceDesc ... createDevice()          // instance/device/VMA, no surface
//   device->setSurface(window, w, h)       // creates surface + swapchain
//   loop:
//     FrameInfo frame;
//     if (device->beginFrame(frame)) {     // acquire + wait + open commands
//         frame.commands->beginRendering(...);
//         ... record ...
//         frame.commands->endRendering();
//         device->endFrame();              // submit + present
//     }
//   device->setSurfaceSize(w, h)           // deferred swapchain recreate
//   device->setSurface(nullptr, 0, 0)      // immediate teardown (no frame in flight)
//
// Threading contract: beginFrame/endFrame run on the render thread;
// setSurface/setSurfaceSize are called from the UI thread ONLY when no frame
// is in flight (nova::Engine holds its frame mutex across both sides).

#include "core/Types.h"
#include "rhi/RhiResources.h"
#include "rhi/RhiTypes.h"

namespace nova::rhi {

struct DeviceDesc {
    const char* appName = "NovaEngine";
    int preferredGlesMajor = 2;
    bool enableValidation = false;
    /// Requested present pacing (falls back to FIFO when unsupported).
    PresentMode presentMode = PresentMode::Fifo;
};

struct FrameInfo {
    ICommandBuffer* commands = nullptr;
    ITexture* backbuffer = nullptr;
    u32 width = 0;
    u32 height = 0;
    Format colorFormat = Format::Unknown;
    /// Monotonic index, increments once per successful beginFrame.
    u64 frameIndex = 0;
    /// Index into the frames-in-flight ring (UBO slot selection).
    u32 ringIndex = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual bool usesGlsl() const { return false; }
    virtual const char* backendName() const { return "Vulkan"; }

    // -- resources ---------------------------------------------------------
    virtual Unique<IBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual Unique<IShaderModule> createShaderModule(const ShaderModuleDesc& desc) = 0;
    virtual Unique<IPipeline> createPipeline(const PipelineDesc& desc) = 0;

    // -- surface / swapchain ----------------------------------------------
    /// Attach or replace the native window (Android: ANativeWindow*, taken
    /// as void* so the RHI stays header-clean). nullptr tears everything
    /// surface-related down immediately and must be called with no frame in
    /// flight. Non-null (re)creates the surface and swapchain eagerly.
    virtual void setSurface(void* nativeWindow, u32 width, u32 height) = 0;

    /// Record a pending size; applied on the next beginFrame by recreating
    /// the swapchain. No-op when no surface is attached.
    virtual void setSurfaceSize(u32 width, u32 height) = 0;

    /// Change present pacing; applied with the next swapchain (re)create.
    virtual void setPresentMode(PresentMode mode) = 0;
    virtual PresentMode presentMode() const = 0;

    // -- frame loop ---------------------------------------------------------
    /// Returns false when no frame should be recorded (no surface, swapchain
    /// being recreated, device lost). On true, `out` is valid until
    /// endFrame() (or the next beginFrame after a skipped call).
    virtual bool beginFrame(FrameInfo& out) = 0;

    /// Submit + present the frame opened by beginFrame().
    virtual void endFrame() = 0;

    /// Block until all GPU work completes.
    virtual void waitIdle() = 0;

    /// True while a swapchain is attached and usable.
    virtual bool hasSurface() const = 0;

    virtual const DeviceStats& stats() const = 0;
};

/// Create the highest-priority backend compiled into this build
/// (Vulkan when NOVA_ENABLE_RHI_VULKAN, otherwise an error Result).
/// Returns nullptr on failure; details are logged.
nova::Unique<IDevice> createDevice(const DeviceDesc& desc);

}  // namespace nova::rhi
