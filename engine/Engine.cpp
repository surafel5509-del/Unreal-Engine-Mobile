#include "Engine.h"
#include "editor_core/Picking.h"
#include <algorithm>

#include "core/Assert.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace nova {
namespace {

constexpr const char* kVersion = "0.2.0 (M2)";

}  // namespace

const char* engineErrorMessage(EngineError error) {
    switch (error) {
        case EngineError::Ok: return "ok";
        case EngineError::InvalidArgument: return "invalid argument";
        case EngineError::DeviceCreateFailed: return "graphics device creation failed";
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
    deviceDesc.preferredGlesMajor = config.preferredGlesMajor;
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
    if (!m_device->usesGlsl() && (!loadShader("shaders/background.vert.spv", backgroundVert) ||
        !loadShader("shaders/background.frag.spv", backgroundFrag) ||
        !loadShader("shaders/triangle.vert.spv", triangleVert) ||
        !loadShader("shaders/triangle.frag.spv", triangleFrag))) {
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

    // --- scene -------------------------------------------------------------
    createDemoScene();

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
    // Scene teardown: drop the entities so a later init() starts from a clean
    // World (generations bump, indices are recycled).
    if (m_world.isAlive(m_demoEntity)) {
        m_world.destroy(m_demoEntity);
    }
    if (m_world.isAlive(m_cameraEntity)) {
        m_world.destroy(m_cameraEntity);
    }
    for (auto entity : m_editorEntities) m_world.destroy(entity);
    m_editorEntities.clear();
    m_editorObjects.clear();
    m_playing = false;
    m_cameraEntity = scene::kNullEntity;
    m_demoEntity = scene::kNullEntity;
    m_hierarchy.reset();
    m_visible.clear();
    m_drawCommands.clear();
    m_lastCull = scene::CullStats{};

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

void Engine::createDemoScene() {
    // Idempotent: shutdownLocked() destroys the entities, so a re-init rebuilds
    // them; a second call with a live camera is a no-op.
    if (m_world.isAlive(m_cameraEntity) && m_world.has<scene::Camera>(m_cameraEntity)) {
        return;
    }

    // Camera at (0, 0, 3) looking down -Z with up = +Y. That placement makes
    // viewMatrix() equal the translation M1's lookAt() produced, so the on
    // screen image is pixel-identical to the hard-coded renderer.
    m_cameraEntity = m_world.create();
    m_world.add<scene::Transform>(m_cameraEntity,
                                  scene::Transform::fromPosition(math::Vec3(0.0f, 0.0f, 3.0f)));
    scene::Camera camera{};
    camera.projection = scene::ProjectionType::Perspective;
    camera.fovYDegrees = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    camera.aspectRatio = 16.0f / 9.0f;  // overwritten from the swapchain per frame
    m_world.add<scene::Camera>(m_cameraEntity, camera);

    // The hero triangle: one drawable entity at the origin, spinning in place.
    m_demoEntity = m_world.create();
    m_world.add<scene::Transform>(m_demoEntity, scene::Transform{});
    m_world.add<scene::Bounds>(m_demoEntity,
                               scene::Bounds{math::Vec3(0.0f, 0.0f, 0.0f), math::Vec3(0.75f)});
    renderer::MeshRef meshRef{};
    meshRef.mesh = renderer::BuiltinMesh::kTriangle;
    meshRef.tint = math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    m_world.add<renderer::MeshRef>(m_demoEntity, meshRef);

    m_hierarchy.reset();
    m_visible.clear();
    m_drawCommands.clear();
    m_lastCull = scene::CullStats{};

    NOVA_LOG_INFO("engine: demo scene created (entities=%u camera=%u.%u object=%u.%u)",
                  static_cast<unsigned>(m_world.aliveCount()),
                  static_cast<unsigned>(m_cameraEntity.index),
                  static_cast<unsigned>(m_cameraEntity.generation),
                  static_cast<unsigned>(m_demoEntity.index),
                  static_cast<unsigned>(m_demoEntity.generation));
}

std::string Engine::setScene(const std::string& json) {
    auto parsed = editor::parseScene(json);
    if (!parsed) return "error: " + parsed.error();
    auto terrain = editor::parseTerrain(json);
    if (!terrain) return "error: " + terrain.error();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) return "error: engine not initialized";
    if (m_world.isAlive(m_demoEntity)) m_world.destroy(m_demoEntity);
    m_demoEntity = scene::kNullEntity;
    for (auto entity : m_editorEntities) m_world.destroy(entity);
    m_editorEntities.clear();
    m_editorObjects = std::move(parsed.value());
    m_renderer.setTerrain(terrain.value());
    for (const auto& object : m_editorObjects) {
        const auto entity = m_world.create();
        m_world.add<scene::Transform>(entity, object.transform);
        m_world.add<scene::Bounds>(entity, renderer::builtinBounds(object.mesh));
        if(object.enabled) m_world.add<renderer::MeshRef>(entity, renderer::MeshRef{object.mesh, object.tint});
        m_editorEntities.push_back(entity);
    }
    m_hierarchy.reset();
    return "ok";
}

bool Engine::updateScene(f32 width, f32 height, renderer::SceneSnapshot& snapshot) {
    // Caller holds m_mutex.
    const f32 aspect = height > 0.0f ? width / height : 1.0f;

    // Animate: the same slow tumble the M1 renderer baked into its UBO, now
    // expressed as a Transform so the hierarchy owns the world matrix.
    if (scene::Transform* transform = m_world.get<scene::Transform>(m_demoEntity)) {
        transform->rotation =
            math::quatRotationY(m_engineTime * 0.9f) * math::quatRotationZ(m_engineTime * 0.55f);
    }

    for (usize i = 0; i < m_editorEntities.size(); ++i) {
        if (auto* transform = m_world.get<scene::Transform>(m_editorEntities[i])) {
            *transform = m_editorObjects[i].transform;
            if (m_playing && m_editorObjects[i].spin) transform->rotation = math::quatRotationY(m_engineTime * 0.6f) * transform->rotation;
        }
    }
    renderer::SceneStageInput stageInput{};
    stageInput.world = &m_world;
    stageInput.hierarchy = &m_hierarchy;
    stageInput.cameraEntity = m_cameraEntity;
    stageInput.timeSeconds = m_engineTime;
    stageInput.aspect = aspect;

    renderer::SceneStageOutput stageOutput{};
    if (!renderer::runSceneStage(stageInput, m_visible, m_drawCommands, snapshot, &stageOutput)) {
        m_lastCull = scene::CullStats{};
        return false;
    }

    m_lastCull = stageOutput.cull;
    return true;
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

    if (m_playing) m_engineTime += dt;

    // Scene stage: animate -> hierarchy -> cull -> sort -> draw list. It runs
    // after beginFrame() because the camera's aspect ratio comes from the
    // swapchain extent of the frame we are about to record.
    renderer::SceneSnapshot snapshot{};
    snapshot.timeSeconds = m_engineTime;
    snapshot.aspect = frameInfo.height > 0
                          ? static_cast<f32>(frameInfo.width) / static_cast<f32>(frameInfo.height)
                          : 1.0f;
    if (!updateScene(static_cast<f32>(frameInfo.width), static_cast<f32>(frameInfo.height),
                     snapshot)) {
        m_device->endFrame();
        return kFrameSkipped;
    }

    if (!m_renderer.render(frameInfo, snapshot)) {
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
    m_console.registerCommand("view", "view <scene|game>", [this](core::Console::Arguments args) {
        if(args.size()!=1 || (args[0]!="scene" && args[0]!="game")) return std::string("usage: view scene|game");
        m_renderer.setGrid(args[0]=="scene"); return std::string("View: ")+args[0];
    });
    m_console.registerCommand("orbit", "orbit <yaw pitch distance targetX targetY targetZ>", [this](core::Console::Arguments args) {
        if(args.size()!=6) return std::string("usage: orbit yaw pitch distance x y z");
        float v[6]{};
        for(int i=0;i<6;++i) {
            char* end=nullptr; v[i]=std::strtof(args[i].c_str(),&end);
            if(end==args[i].c_str() || *end || !std::isfinite(v[i]) || std::abs(v[i])>1000) return std::string("error: invalid orbit value");
        }
        if(v[1]<-80 || v[1]>80 || v[2]<1.5f || v[2]>40) return std::string("error: orbit range");
        auto* camera=m_world.get<scene::Transform>(m_cameraEntity);
        if(!camera) return std::string("error: no camera");
        const auto yaw=math::toRadians(v[0]), pitch=math::toRadians(v[1]);
        const math::Vec3 target(v[3],v[4],v[5]);
        camera->position=target+math::Vec3(std::sin(yaw)*std::cos(pitch), std::sin(pitch),std::cos(yaw)*std::cos(pitch))*v[2];
        camera->rotation=glm::quat_cast(math::inverse(math::lookAt(camera->position,target,math::Vec3(0,1,0))));
        return std::string("ok");
    });
    m_console.registerCommand("pick", "pick <normalized screen x y>", [this](core::Console::Arguments args) {
        if(args.size()!=2 || !m_device) return std::string("-1");
        float xy[2]{};
        for(int i=0;i<2;++i) { char* end=nullptr; xy[i]=std::strtof(args[i].c_str(),&end);
            if(end==args[i].c_str() || *end || !std::isfinite(xy[i]) || xy[i]<0 || xy[i]>1) return std::string("-1"); }
        const auto* camera=m_world.get<scene::Camera>(m_cameraEntity);
        const auto* transform=m_world.get<scene::Transform>(m_cameraEntity);
        if(!camera || !transform) return std::string("-1");
        const auto inverseVP=math::inverse(scene::projectionMatrix(*camera)*math::inverse(transform->localMatrix()));
        auto a=inverseVP*math::Vec4(xy[0]*2-1,xy[1]*2-1,0,1);
        auto b=inverseVP*math::Vec4(xy[0]*2-1,xy[1]*2-1,1,1);
        const math::Vec3 origin(a/a.w), direction=glm::normalize(math::Vec3(b/b.w)-origin);
        float nearest=10000; int selected=-1;
        for(usize i=0;i<m_editorObjects.size();++i) {
            const auto& object=m_editorObjects[i]; if(!object.enabled) continue;
            const auto inverse=math::inverse(object.transform.localMatrix());
            const math::Vec3 localOrigin(inverse*math::Vec4(origin,1)), localDirection(inverse*math::Vec4(direction,0));
            if(editor::hitBounds(localOrigin,localDirection,renderer::builtinBounds(object.mesh))<0) continue;
            const float distance=m_renderer.pick(object.mesh,localOrigin,localDirection);
            if(distance>=0 && distance<nearest) {nearest=distance; selected=static_cast<int>(i);}
        }
        return std::to_string(selected);
    });
    m_console.registerCommand("scene", "List authored native objects and positions", [this](core::Console::Arguments) {
        std::ostringstream out;
        for (usize i = 0; i < m_editorObjects.size(); ++i) {
            const auto& object = m_editorObjects[i];
            const auto& p = object.transform.position;
            out << i << " " << (object.mesh == renderer::BuiltinMesh::kCube ? "Cube" : "Triangle")
                << " position=" << p.x << "," << p.y << "," << p.z
                << " scale=" << object.transform.scale.x << "\n";
        }
        return m_editorObjects.empty() ? std::string("Empty scene") : out.str();
    });
    m_console.registerCommand("camera", "camera <distance 1.5..20>", [this](core::Console::Arguments args) {
        if (args.size() != 1) return std::string("usage: camera <1.5..20>");
        char* end = nullptr;
        const float distance = std::strtof(args[0].c_str(), &end);
        if (end == args[0].c_str() || *end != '\0' || !std::isfinite(distance) || distance < 1.5f || distance > 20.0f)
            return std::string("camera distance must be 1.5 to 20");
        if (auto* transform = m_world.get<scene::Transform>(m_cameraEntity)) transform->position.z = distance;
        return std::string("Camera distance: ") + args[0];
    });
    m_console.registerCommand("play", "Play preview animation", [this](core::Console::Arguments) {
        m_playing = true; return std::string("Playing");
    });
    m_console.registerCommand("stop", "Stop preview and restore authored transforms", [this](core::Console::Arguments) {
        m_playing = false; m_engineTime = 0; return std::string("Stopped");
    });
    m_console.registerCommand("graphics", "Show active graphics backend", [this](core::Console::Arguments) {
        return std::string(m_device ? m_device->backendName() : "no device");
    });
    m_console.registerCommand(
        "stats", "Print runtime statistics (fps, frame, swapchain, memory counters)",
        [this](core::Console::Arguments) -> std::string {
            std::ostringstream out;
            out << "editor_objects=" << m_editorObjects.size() << " playing=" << (m_playing ? "yes" : "no") << "\n";
            out << "fps=" << m_fpsEma << " frame_ms=" << m_lastFrameMillis
                << " cpu_ms=" << m_lastFrameCpuMillis
                << " frames=" << m_frameCount
                << " time=" << m_engineTime << "s"
                << " paused=" << (m_paused ? "yes" : "no") << "\n";
            if (m_device != nullptr) {
                const rhi::DeviceStats& deviceStats = m_device->stats();
                out << "backend=" << m_device->backendName() << "\n";
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
            const scene::World::Stats& worldStats = m_world.stats();
            out << "entities=" << worldStats.aliveEntities << "/" << worldStats.entitySlots
                << " components=" << worldStats.components
                << " types=" << worldStats.componentTypes
                << " ecs_bytes=" << worldStats.memoryBytes << "\n";
            out << "draw_commands=" << m_drawCommands.size()
                << " visible=" << m_lastCull.visible << " culled=" << m_lastCull.culled
                << " unbounded=" << m_lastCull.unbounded
                << " candidates=" << m_lastCull.candidates << "\n";
            const renderer::RenderStats& renderStats = m_renderer.stats();
            out << "draw_calls=" << renderStats.drawCalls
                << " dropped_commands=" << renderStats.droppedCommands
                << " pipelines=" << renderStats.pipelines
                << " frame_slot=" << renderStats.frameSlot << "\n";
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
