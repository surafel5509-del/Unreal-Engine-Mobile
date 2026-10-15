#pragma once

// Nova Engine — top-level engine facade.
//
// Owns every subsystem for the current process lifetime:
//   VFS (APK + data mounts), logger file sink, console registry,
//   the RHI device, the scene World + hierarchy, the renderer, the input queue
//   and frame statistics.
//
// Per frame the engine runs the scene stage (animation of demo transforms ->
// hierarchy evaluation -> frustum cull -> front-to-back sort -> draw list) and
// hands the result to the renderer as a SceneSnapshot.
//
// Serialization: a single mutex covers all public calls. The JNI layer runs
// control operations (init, surface, lifecycle, console) on the UI thread and
// frame() on the render thread; frame() holds the mutex for its full
// duration, so surface teardown can never overlap a GPU submission.
//
// Lifecycle:  init() -> [setSurface / resize / frame / touch / key / exec]*
//             -> shutdown(). init() may be called again after shutdown().

#include "core/Console.h"
#include "editor_core/SceneDocument.h"
#include "core/Log.h"
#include "core/Result.h"
#include "core/Time.h"
#include "core/Types.h"
#include "platform/InputQueue.h"
#include "platform/VirtualFileSystem.h"
#include "rhi/IDevice.h"
#include "renderer/DrawList.h"
#include "renderer/Renderer.h"
#include "scene/Camera.h"
#include "scene/Culling.h"
#include "scene/SceneHierarchy.h"
#include "scene/Transform.h"
#include "scene/World.h"

#include <mutex>
#include <string>
#include <vector>

struct AAssetManager;

namespace nova {

/// Error codes returned by Engine::init and mapped 1:1 by nativeInit.
enum class EngineError : int {
    Ok = 0,
    InvalidArgument = 1,
    DeviceCreateFailed = 2,
    AssetLoadFailed = 3,
    RendererInitFailed = 4,
    AlreadyInitialized = 5,
};

const char* engineErrorMessage(EngineError error);

/// Values returned by Engine::frame (mirrored in NativeBridge.kt).
inline constexpr int kFrameRendered = 0;
inline constexpr int kFrameSkipped = 1;

struct EngineConfig {
    AAssetManager* apkAssets = nullptr;   ///< may be null (headless/host tests)
    std::string dataPath;                 ///< writable root (logs, caches)
    unsigned initialWidth = 0;
    unsigned initialHeight = 0;
    bool enableValidation = false;
    int preferredGlesMajor = 2;
};

/// Aggregate statistics surfaced through `exec("stats")`.
struct EngineStats {
    u64 frameCount = 0;
    f64 framesPerSecondEma = 0.0;
    f64 lastFrameMillis = 0.0;
    f64 lastFrameCpuMillis = 0.0;
    f32 engineTimeSeconds = 0.0f;
    bool paused = false;
};

class Engine : public core::NonCopyable {
public:
    Engine() = default;
    ~Engine();

    /// Boot all subsystems. Deterministic failure codes; partial state is
    /// rolled back so shutdown() stays safe.
    Result<void, EngineError> init(const EngineConfig& config);

    /// Tear everything down. Idempotent; also invoked by the destructor.
    void shutdown();

    /// Attach/replace the native window (ANativeWindow*, null to detach).
    void setSurface(void* nativeWindow, unsigned width, unsigned height);

    /// Notify a surface size change; applied on the next frame.
    void resize(unsigned width, unsigned height);

    /// Render one frame. Returns kFrameRendered / kFrameSkipped.
    int frame(float dtSeconds);

    void onPause();
    void onResume();

    /// Input ingestion (UI thread). `androidAction` is a MotionEvent action.
    void onTouch(int androidAction, unsigned pointerId, float x, float y);
    void onKey(int keyCode, bool down);

    /// Execute a console line; returns the output (never empty for errors).
    std::string exec(const std::string& line);

    std::string setScene(const std::string& json);

    bool isInitialized() const;
    bool isPaused() const;
    EngineStats stats() const;

private:
    /// Teardown body shared by shutdown() and init() rollback; caller holds
    /// m_mutex.
    void shutdownLocked();

    void registerConsoleCommands();
    bool loadShader(const char* path, std::vector<core::u32>& outWords);

    /// Build the M2 demo scene: a camera entity and one drawable entity.
    /// Idempotent — re-running after shutdown() recreates the entities.
    void createDemoScene();

    /// Scene stage for one frame: animate, evaluate the hierarchy, cull, order
    /// and fill m_drawCommands. Writes the camera's view-projection and the
    /// resulting draw list into `snapshot` (whose time/aspect the caller sets).
    /// Returns false when the scene has no usable camera. Caller holds m_mutex.
    bool updateScene(f32 width, f32 height, renderer::SceneSnapshot& snapshot);

    mutable std::mutex m_mutex;
    bool m_initialized = false;
    bool m_paused = false;

    platform::VirtualFileSystem m_vfs;
    platform::InputQueue m_input;
    core::Console m_console;

    rhi::Unique<rhi::IDevice> m_device;
    renderer::Renderer m_renderer;

    // scene (ECS) — owned here so the World outlives every per-frame view
    scene::World m_world;
    scene::SceneHierarchy m_hierarchy;
    scene::Entity m_cameraEntity = scene::kNullEntity;
    scene::Entity m_demoEntity = scene::kNullEntity;
    std::vector<scene::Entity> m_visible;         // reused, never reallocated after warm-up
    std::vector<renderer::DrawCommand> m_drawCommands;  // reused, see SceneSnapshot
    scene::CullStats m_lastCull{};

    std::vector<scene::Entity> m_editorEntities;
    std::vector<editor::Object> m_editorObjects;
    bool m_playing = false;

    // frame bookkeeping
    core::Stopwatch m_uptime;
    u64 m_frameCount = 0;
    f64 m_fpsEma = 0.0;
    f64 m_lastFrameMillis = 0.0;
    f64 m_lastFrameCpuMillis = 0.0;
    f32 m_engineTime = 0.0f;
    core::Stopwatch m_sinceLastFrame;
    bool m_deviceLostReported = false;
};

}  // namespace nova
