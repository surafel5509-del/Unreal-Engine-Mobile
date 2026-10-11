#include "Engine.h"

#include "core/Assert.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace nova {
namespace {

constexpr const char* kVersion = "0.1.0 (M1)";

}  // namespace

const char* engineErrorMessage(EngineError error) {
    switch (error) {
        case EngineError::Ok: return "ok";
        case EngineError::InvalidArgument: return "invalid argument";
        case EngineError::DeviceCreateFailed: return "Vulkan device creation failed";
        case EngineError::AssetLoadFailed: return "shader/asset load failed";
        case EngineError::RendererInitFailed: return "renderer initialization failed";
        case EngineError::AlreadyInitialized: return "engine already initialized";
    }
    return "unknown error";
}

Engine::~Engine() {
    shutdown();
}

Result<void, EngineError> Engine::init(const EngineConfig& config) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_initialized) {
        return Result<void, EngineError>::failure(EngineError::AlreadyInitialized);
    }

    core::log::initialize();
    NOVA_LOG_INFO("Nova Engine v%s initializing", kVersion);
    m_uptime = core::Stopwatch();
    m_sinceLastFrame = core::Stopwatch();

    // --- mounts + file logging -------------------------------------------
    if (!config.dataPath.empty()) {
        const std::string logPath = config.dataPath + "/logs/nova.log";
        if (!core::log::setFilePath(logPath)) {
            NOVA_LOG_WARN("engine: could not open log file '%s' (continuing)",
                          logPath.c_str());
        }
    }
    if (config.apkAssets != nullptr) {
        m_vfs.mountApk(config.apkAssets);
    }
    if (!config.dataPath.empty()) {
        m_vfs.mountData(config.dataPath);
    }

    // --- device ------------------------------------------------------------
    rhi::DeviceDesc deviceDesc{};
    deviceDesc.appName = "NovaEngine";
    deviceDesc.enableValidation = config.enableValidation;
    deviceDesc.presentMode = rhi::PresentMode::Fifo;
    m_device = rhi::createDevice(deviceDesc);
    if (m_device == nullptr) {
        NOVA_LOG_ERROR("engine: device creation failed — see messages above");
        shutdownLocked();
        return Result<void, EngineError>::failure(EngineError::DeviceCreateFailed);
    }

    // --- shaders -----------------------------------------------------------
    std::vector<core::u32> backgroundVert;
    std::vector<core::u32> backgroundFrag;
    std::vector<core::u32> triangleVert;
    std::vector<core::u32> triangleFrag;
    if (!loadShader("shaders/background.vert.spv", backgroundVert) ||
        !loadShader("shaders/background.frag.spv", backgroundFrag) ||
        !loadShader("shaders/triangle.vert.spv", triangleVert) ||
        !loadShader("shaders/triangle.frag.spv", triangleFrag)) {
        shutdownLocked();
        return Result<void, EngineError>::failure(EngineError::AssetLoadFailed);
    }

    // --- renderer ----------------------------------------------------------
    const auto rendererResult =
        m_renderer.init(*m_device, backgroundVert, backgroundFrag, triangleVert,
                        triangleFrag);
    if (!rendererResult) {
        NOVA_LOG_ERROR("engine: renderer init failed: %s",
                       renderer::initErrorMessage(rendererResult.error()));
        shutdownLocked();
        return Result<void, EngineError>::failure(EngineError::RendererInitFailed);
    }

    registerConsoleCommands();
    m_initialized = true;
    m_paused = false;
    m_sinceLastFrame.restart();
    NOVA_LOG_INFO("engine: ready (validation=%s)",
                  config.enableValidation ? "requested" : "off");
    return Result<void, EngineError>::success();
}

void Engine::shutdownLocked() {
    // Caller holds m_mutex.
    if (m_device != nullptr) {
        m_renderer.shutdown();
        m_device->waitIdle();
        m_device.reset();
    } else {
        m_renderer.shutdown();
    }
    m_initialized = false;
    m_paused = false;
    m_frameCount = 0;
    m_fpsEma = 0.0;
    m_engineTime = 0.0f;
    m_deviceLostReported = false;
}

void Engine::shutdown() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized && m_device == nullptr) return;
    NOVA_LOG_INFO("engine: shutting down (frames rendered: %llu)",
                  static_cast<unsigned long long>(m_frameCount));
    shutdownLocked();
}

bool Engine::loadShader(const char* path, std::vector<core::u32>& outWords) {
    core::ByteBuffer bytes;
    if (!m_vfs.readAsset(path, bytes)) {
        NOVA_LOG_ERROR("engine: missing shader asset '%s'", path);
        return false;
    }
    if (bytes.size() % sizeof(core::u32) != 0 || bytes.size() < 4 * sizeof(core::u32)) {
        NOVA_LOG_ERROR("engine: shader '%s' is not valid SPIR-V (%zu bytes)", path,
                       bytes.size());
        return false;
    }
    outWords.resize(bytes.size() / sizeof(core::u32));
    std::memcpy(outWords.data(), bytes.data(), bytes.size());

    // Minimal SPIR-V sanity: magic 0x07230203 (little endian).
    if (outWords[0] != 0x07230203u) {
        NOVA_LOG_ERROR("engine: '%s' has a bad SPIR-V magic number", path);
        outWords.clear();
        return false;
    }
    return true;
}

void Engine::setSurface(void* nativeWindow, unsigned width, unsigned height) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_device == nullptr) return;
    if (nativeWindow == nullptr) {
        NOVA_LOG_INFO("engine: surface detached");
    }
    m_device->setSurface(nativeWindow, width, height);
}

void Engine::resize(unsigned width, unsigned height) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_device == nullptr) return;
    m_device->setSurfaceSize(width, height);
}

int Engine::frame(float dtSeconds) {
    core::Stopwatch cpu;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized || m_paused || m_device == nullptr) {
        return kFrameSkipped;
    }

    // Clamp: resume gaps and debugger pauses must not jump the animation.
    float dt = dtSeconds;
    if (!(dt > 0.0f) || dt > 0.25f) {
        dt = 1.0f / 60.0f;
    }

    // Drain the input queue (M1 logs contacts; gameplay consumes from M2).
    std::vector<platform::TouchEvent> touches;
    std::vector<platform::KeyEvent> keys;
    m_input.drain(touches, keys);
    for (const platform::TouchEvent& touch : touches) {
        if (touch.action == platform::TouchAction::Down) {
            NOVA_LOG_DEBUG("input: touch down id=%u (%.1f, %.1f)", touch.pointerId,
                           touch.x, touch.y);
        }
    }

    rhi::FrameInfo frameInfo{};
    if (!m_device->beginFrame(frameInfo)) {
        return kFrameSkipped;
    }

    m_engineTime += dt;
    if (!m_renderer.render(frameInfo, m_engineTime)) {
        // Pipeline creation failed for this format; keep the frame open so
        // endFrame() stays paired with beginFrame().
        m_device->endFrame();
        return kFrameSkipped;
    }
    m_device->endFrame();

    ++m_frameCount;
    m_lastFrameMillis = m_sinceLastFrame.restart() * 1000.0;
    m_lastFrameCpuMillis = cpu.elapsedMillis();
    const f64 instantaneousFps = 1.0 / static_cast<f64>(dt);
    m_fpsEma = m_fpsEma == 0.0
                   ? instantaneousFps
                   : m_fpsEma + 0.1 * (instantaneousFps - m_fpsEma);
    return kFrameRendered;
}

void Engine::onPause() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) return;
    m_paused = true;
    if (m_device != nullptr) {
        m_device->waitIdle();
    }
    NOVA_LOG_INFO("engine: paused");
}

void Engine::onResume() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) return;
    m_paused = false;
    m_sinceLastFrame.restart();
    NOVA_LOG_INFO("engine: resumed");
}

void Engine::onTouch(int androidAction, unsigned pointerId, float x, float y) {
    platform::TouchEvent event;
    event.action = platform::touchActionFromAndroid(androidAction);
    event.pointerId = pointerId;
    event.x = x;
    event.y = y;
    event.timestampMicros = core::nowMicros();
    m_input.pushTouch(event);
}

void Engine::onKey(int keyCode, bool down) {
    platform::KeyEvent event;
    event.keyCode = keyCode;
    event.down = down;
    event.timestampMicros = core::nowMicros();
    m_input.pushKey(event);
}

std::string Engine::exec(const std::string& line) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) {
        return "engine not initialized";
    }
    return m_console.execute(line);
}

bool Engine::isInitialized() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_initialized;
}

bool Engine::isPaused() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_paused;
}

EngineStats Engine::stats() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    EngineStats out;
    out.frameCount = m_frameCount;
    out.framesPerSecondEma = m_fpsEma;
    out.lastFrameMillis = m_lastFrameMillis;
    out.lastFrameCpuMillis = m_lastFrameCpuMillis;
    out.engineTimeSeconds = m_engineTime;
    out.paused = m_paused;
    return out;
}

void Engine::registerConsoleCommands() {
    m_console.registerCommand(
        "stats", "Print runtime statistics (fps, frame, swapchain, memory counters)",
        [this](core::Console::Arguments) -> std::string {
            std::ostringstream out;
            out << "fps=" << m_fpsEma << " frame_ms=" << m_lastFrameMillis
                << " cpu_ms=" << m_lastFrameCpuMillis
                << " frames=" << m_frameCount
                << " time=" << m_engineTime << "s"
                << " paused=" << (m_paused ? "yes" : "no") << "\n";
            if (m_device != nullptr) {
                const rhi::DeviceStats& deviceStats = m_device->stats();
                out << "swapchain=" << deviceStats.width << "x" << deviceStats.height
                    << " images=" << deviceStats.imageCount
                    << " fif=" << deviceStats.framesInFlight
                    << " present="
                    << (deviceStats.presentMode == rhi::PresentMode::Fifo
                            ? "fifo"
                            : (deviceStats.presentMode == rhi::PresentMode::Mailbox
                                   ? "mailbox"
                                   : "immediate"))
                    << " validation=" << (deviceStats.validationEnabled ? "on" : "off")
                    << " pipelines=" << deviceStats.pipelinesCreated
                    << " buffers=" << deviceStats.buffersCreated << "\n";
            }
            out << "input_pending=" << m_input.pendingCount()
                << " input_dropped=" << m_input.droppedCount()
                << " log_records=" << core::log::retainedCount() << "\n";
            return out.str();
        });

    m_console.registerCommand(
        "log", "log <message> - write a message to the engine log",
        [](core::Console::Arguments args) -> std::string {
            if (args.empty()) return "usage: log <message>";
            std::string message;
            for (const std::string& arg : args) {
                if (!message.empty()) message.push_back(' ');
                message += arg;
            }
            NOVA_LOG_INFO("console: %s", message.c_str());
            return "logged";
        });

    m_console.registerCommand(
        "vsync", "vsync <on|off> - toggle FIFO frame pacing (applies next frame)",
        [this](core::Console::Arguments args) -> std::string {
            if (args.size() != 1 || (args[0] != "on" && args[0] != "off")) {
                return "usage: vsync <on|off>";
            }
            if (m_device == nullptr) return "no device";
            const rhi::PresentMode mode =
                args[0] == "on" ? rhi::PresentMode::Fifo : rhi::PresentMode::Mailbox;
            m_device->setPresentMode(mode);
            return "vsync " + args[0] +
                   " requested (falls back to FIFO when unsupported); applied next frame";
        });

    m_console.registerCommand(
        "time", "Print engine time and uptime", [this](core::Console::Arguments) -> std::string {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                          "engine_time=%.2fs uptime=%.2fs frames=%llu",
                          static_cast<double>(m_engineTime),
                          static_cast<double>(m_uptime.elapsedSeconds()),
                          static_cast<unsigned long long>(m_frameCount));
            return std::string(buffer);
        });
}

}  // namespace nova
