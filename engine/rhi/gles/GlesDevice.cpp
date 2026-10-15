#include "rhi/gles/GlesDevice.h"
#include "core/Log.h"
#include "renderer/Renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <cstring>
#include <string>
#include <vector>

namespace nova::rhi {
namespace {
// CPU-backed resources survive EGL context loss. Only the device owns GL names.
class Buffer final : public IBuffer {
public:
    explicit Buffer(const BufferDesc& desc) : description(desc), bytes(desc.size) {}
    const BufferDesc& desc() const override { return description; }
    void* mappedData() const override { return const_cast<u8*>(bytes.data()); }
    BufferDesc description;
    std::vector<u8> bytes;
};
class Shader final : public IShaderModule {
public:
    explicit Shader(const ShaderModuleDesc& desc) : kind(desc.stage), source(desc.glsl) {}
    ShaderStage stage() const override { return kind; }
    ShaderStage kind;
    std::string source;
};
class Pipeline final : public IPipeline {
public:
    explicit Pipeline(const PipelineDesc& desc) : description(desc),
        vertex(static_cast<const Shader*>(desc.vertexShader)->source),
        fragment(static_cast<const Shader*>(desc.fragmentShader)->source) {}
    const PipelineDesc& desc() const override { return description; }
    PipelineDesc description;
    std::string vertex, fragment;
    GLuint program = 0;
    u64 generation = 0;
};
class Backbuffer final : public ITexture {
public:
    Format format() const override { return Format::RGBA8_UNorm; }
    u32 width() const override { return w; }
    u32 height() const override { return h; }
    u32 w = 0, h = 0;
};

class Device final : public IDevice, public ICommandBuffer {
public:
    explicit Device(const DeviceDesc& desc) : preferred(desc.preferredGlesMajor) {}
    ~Device() override { destroyContext(); }
    bool initialize() {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) return false;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
        // API 26+ requests ES 3. A failed config/context attempt always retries ES 2.
        for (int major = preferred >= 3 ? 3 : 2; major >= 2; --major) {
            for (EGLint depth : {24, 16}) {
                const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                    EGL_RENDERABLE_TYPE, major == 3 ? 0x0040 : EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                    EGL_DEPTH_SIZE, depth, EGL_NONE};
                EGLint count = 0;
                if (!eglChooseConfig(display, attributes, &config, 1, &count) || count < 1) continue;
                const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, major, EGL_NONE};
                context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
                if (context == EGL_NO_CONTEXT) continue;
                const EGLint pbufferAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
                parking = eglCreatePbufferSurface(display, config, pbufferAttributes);
                if (parking != EGL_NO_SURFACE && eglMakeCurrent(display, parking, parking, context)) {
                    ++generation;
                    glGenBuffers(1, &streamBuffer);
                    name = std::string("OpenGL ES ") + (major == 3 ? "3" : "2") + " / " +
                        reinterpret_cast<const char*>(glGetString(GL_RENDERER));
                    NOVA_LOG_INFO("gles: ready: %s (%s)", name.c_str(), glGetString(GL_VERSION));
                    counters.framesInFlight = 1;
                    counters.imageCount = 1;
                    release();
                    return true;
                }
                if (parking != EGL_NO_SURFACE) eglDestroySurface(display, parking);
                eglDestroyContext(display, context);
                parking = EGL_NO_SURFACE;
                context = EGL_NO_CONTEXT;
            }
        }
        NOVA_LOG_ERROR("gles: cannot create an ES 2/3 context (EGL 0x%x)", eglGetError());
        return false;
    }
    bool usesGlsl() const override { return true; }
    const char* backendName() const override { return name.c_str(); }
    Unique<IBuffer> createBuffer(const BufferDesc& desc) override {
        if (desc.size == 0 || (desc.usage != BufferUsage::Vertex && desc.usage != BufferUsage::Uniform)) return nullptr;
        ++counters.buffersCreated;
        return std::make_unique<Buffer>(desc);
    }
    Unique<IShaderModule> createShaderModule(const ShaderModuleDesc& desc) override {
        if (desc.glsl.empty() || desc.stage == ShaderStage::Compute) return nullptr;
        return std::make_unique<Shader>(desc);
    }
    Unique<IPipeline> createPipeline(const PipelineDesc& desc) override {
        if (!desc.vertexShader || !desc.fragmentShader) return nullptr;
        auto pipeline = std::make_unique<Pipeline>(desc);
        if (!compile(*pipeline)) return nullptr; // called with context current during render
        ++counters.pipelinesCreated;
        return pipeline;
    }
    void setSurface(void* nativeWindow, u32 width, u32 height) override {
        window = static_cast<ANativeWindow*>(nativeWindow); // retained by JNI
        release();
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        surface = EGL_NO_SURFACE;
        if (window && context != EGL_NO_CONTEXT) {
            EGLint visual = 0;
            eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual);
            ANativeWindow_setBuffersGeometry(window, 0, 0, visual);
            surface = eglCreateWindowSurface(display, config, window, nullptr);
            if (surface == EGL_NO_SURFACE) NOVA_LOG_ERROR("gles: window creation failed: 0x%x", eglGetError());
        }
        setSurfaceSize(width, height);
    }
    void setSurfaceSize(u32 width, u32 height) override {
        backbuffer.w = width; backbuffer.h = height;
        counters.width = width; counters.height = height;
    }
    void setPresentMode(PresentMode) override {} // FIFO for consistent mobile pacing
    PresentMode presentMode() const override { return PresentMode::Fifo; }
    bool beginFrame(FrameInfo& out) override {
        if (!window) return false;
        if (context == EGL_NO_CONTEXT && !initialize()) { destroyContext(); return false; }
        if (surface == EGL_NO_SURFACE) setSurface(window, backbuffer.w, backbuffer.h);
        if (surface == EGL_NO_SURFACE) return false;
        if (!eglMakeCurrent(display, surface, surface, context)) {
            recover(eglGetError()); return false;
        }
        eglSwapInterval(display, 1);
        EGLint width = 0, height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        if (width <= 0 || height <= 0) { release(); return false; }
        setSurfaceSize(static_cast<u32>(width), static_cast<u32>(height));
        out = {this, &backbuffer, backbuffer.w, backbuffer.h, backbuffer.format(), ++counters.frameIndex, 0};
        return true;
    }
    void endFrame() override {
        const auto error = glGetError();
        if (error != GL_NO_ERROR && !glErrorReported) {
            NOVA_LOG_ERROR("gles: render error 0x%x", error); glErrorReported = true;
        }
        if (!eglSwapBuffers(display, surface)) recover(eglGetError());
        release(); // EGL context must not stay owned by the render thread between calls
    }
    void waitIdle() override {
        if (context != EGL_NO_CONTEXT && eglMakeCurrent(display, parking, parking, context)) {
            glFinish(); release();
        }
    }
    bool hasSurface() const override { return surface != EGL_NO_SURFACE; }
    const DeviceStats& stats() const override { return counters; }
    void beginRendering(u32 width, u32 height, std::span<const ColorAttachment>) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        setViewportScissor(width, height);
        glDepthMask(GL_TRUE);
        glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void endRendering() override {}
    void setViewportScissor(u32 width, u32 height) override {
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glDisable(GL_SCISSOR_TEST);
    }
    void bindPipeline(const IPipeline* value) override {
        active = const_cast<Pipeline*>(static_cast<const Pipeline*>(value));
        if (active->generation != generation && !compile(*active)) { active = nullptr; return; }
        glUseProgram(active->program);
        glDisable(GL_CULL_FACE); // editor primitives are visible from either side
        if (active->desc().depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(active->desc().depthWrite ? GL_TRUE : GL_FALSE);
        if (active->desc().blend == BlendMode::AlphaBlend) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else glDisable(GL_BLEND);
    }
    void bindVertexBuffer(const IBuffer* value) override { vertex = static_cast<const Buffer*>(value); }
    void bindUniformBuffer(u32, const IBuffer* value, usize offset, usize range) override {
        if (!active || range < sizeof(renderer::FrameUniforms) || offset > value->desc().size ||
            range > value->desc().size - offset) return;
        renderer::FrameUniforms data{};
        std::memcpy(&data, static_cast<const u8*>(value->mappedData()) + offset, sizeof(data));
        const GLuint program = active->program;
        glUniformMatrix4fv(glGetUniformLocation(program, "uTransform"), 1, GL_FALSE, &data.transform[0][0]);
        glUniform4fv(glGetUniformLocation(program, "uColorA"), 1, &data.colorA[0]);
        glUniform4fv(glGetUniformLocation(program, "uColorB"), 1, &data.colorB[0]);
        glUniform4fv(glGetUniformLocation(program, "uAccent"), 1, &data.accent[0]);
        glUniform1f(glGetUniformLocation(program, "uTime"), data.time);
        glUniform1f(glGetUniformLocation(program, "uAspect"), data.aspect);
    }
    void draw(u32 count, u32 instances, u32 first, u32) override {
        if (!active || instances != 1) return;
        glBindBuffer(GL_ARRAY_BUFFER, streamBuffer);
        glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
        const auto& binding = active->desc().vertexBinding;
        if (binding.attributeCount == 0) {
            static constexpr float fullscreen[] = {-1, -1, 3, -1, -1, 3};
            glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreen), fullscreen, GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        } else {
            if (!vertex) return;
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex->bytes.size()), vertex->bytes.data(), GL_STREAM_DRAW);
            for (u32 i = 0; i < binding.attributeCount; ++i) {
                const auto& attr = binding.attributes[i];
                glEnableVertexAttribArray(attr.location);
                glVertexAttribPointer(attr.location, static_cast<GLint>(formatTexelSize(attr.format) / sizeof(float)),
                    GL_FLOAT, GL_FALSE, static_cast<GLsizei>(binding.stride),
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset)));
            }
        }
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), static_cast<GLsizei>(count));
    }
private:
    GLuint shader(GLenum type, const std::string& text) {
        GLuint result = glCreateShader(type);
        const char* source = text.c_str();
        glShaderSource(result, 1, &source, nullptr); glCompileShader(result);
        GLint ok = 0; glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{}; glGetShaderInfoLog(result, sizeof(log), nullptr, log);
            NOVA_LOG_ERROR("gles: shader compilation failed: %s", log);
            glDeleteShader(result); return 0;
        }
        return result;
    }
    bool compile(Pipeline& pipeline) {
        GLuint vs = shader(GL_VERTEX_SHADER, pipeline.vertex);
        GLuint fs = shader(GL_FRAGMENT_SHADER, pipeline.fragment);
        if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }
        GLuint program = glCreateProgram();
        glAttachShader(program, vs); glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "aPosition"); glBindAttribLocation(program, 1, "aColor");
        glLinkProgram(program); glDeleteShader(vs); glDeleteShader(fs);
        GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024]{}; glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            NOVA_LOG_ERROR("gles: program link failed: %s", log);
            glDeleteProgram(program); return false;
        }
        programs.push_back(program); pipeline.program = program; pipeline.generation = generation;
        return true;
    }
    void release() {
        if (display != EGL_NO_DISPLAY) eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    void destroyContext() {
        if (display == EGL_NO_DISPLAY) return;
        if (context != EGL_NO_CONTEXT && eglMakeCurrent(display, parking, parking, context)) {
            for (GLuint program : programs) glDeleteProgram(program);
            glDeleteBuffers(1, &streamBuffer);
        }
        release();
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (parking != EGL_NO_SURFACE) eglDestroySurface(display, parking);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
        surface = parking = EGL_NO_SURFACE; context = EGL_NO_CONTEXT; display = EGL_NO_DISPLAY;
        programs.clear(); streamBuffer = 0; active = nullptr; vertex = nullptr;
    }
    void recover(EGLint error) {
        NOVA_LOG_WARN("gles: EGL error 0x%x; recreating surface/context", error);
        if (error == EGL_CONTEXT_LOST) destroyContext();
        else {
            release();
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            surface = EGL_NO_SURFACE;
        }
    }
    int preferred;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface parking = EGL_NO_SURFACE, surface = EGL_NO_SURFACE;
    ANativeWindow* window = nullptr;
    Backbuffer backbuffer;
    DeviceStats counters{};
    std::string name;
    GLuint streamBuffer = 0;
    u64 generation = 0;
    std::vector<GLuint> programs;
    Pipeline* active = nullptr;
    const Buffer* vertex = nullptr;
    bool glErrorReported = false;
};
}
Unique<IDevice> createGlesDevice(const DeviceDesc& desc) {
    auto device = std::make_unique<Device>(desc);
    if (!device->initialize()) return nullptr;
    return device;
}
}
