// Nova Engine — JNI bridge (the ONLY file in app/src/main/cpp).
//
// Contract (see NativeBridge.kt):
//   Java_com_nova_engine_NativeBridge_* entry points, thin — pointers,
//   primitives and framework objects only. All engine state lives behind
//   nova::Engine, which serializes UI-thread control calls against the
//   render thread's nativeFrame().
//
// Threading:
//   * nativeInit / nativeSetSurface / nativeResize / lifecycle / input /
//     nativeExecConsole / nativeShutdown — UI thread.
//   * nativeFrame — render thread ("nova-render").
//   * The Kotlin layer guarantees nativeShutdown runs only after the render
//     thread has joined, so the engine pointer never dangles.
//
// This translation unit is compiled with exceptions enabled (NDK default);
// the engine core itself is -fno-exceptions.

#include <jni.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>  // ANativeWindow_fromSurface

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <memory>
#include <mutex>
#include <string>

#include "Engine.h"

namespace {

constexpr const char* kTag = "nova-jni";

JavaVM* g_javaVm = nullptr;

/// Owned engine instance. Pointer stability: created in nativeInit and
/// destroyed in nativeShutdown, which the Kotlin layer only calls after the
/// render thread has joined (documented in NativeBridge.kt).
nova::Unique<nova::Engine> g_engine;

/// Pins the Kotlin AssetManager object so the raw AAssetManager* stays valid.
jobject g_assetManagerRef = nullptr;

/// ANativeWindow currently owned by the engine (acquired in nativeSetSurface).
ANativeWindow* g_window = nullptr;

void throwIllegalState(JNIEnv* env, const char* message) {
    jclass exceptionClass = env->FindClass("java/lang/IllegalStateException");
    if (exceptionClass != nullptr) {
        env->ThrowNew(exceptionClass, message);
    }
}

/// RAII wrapper for GetStringUTFChars / ReleaseStringUTFChars.
class ScopedUtfChars {
public:
    ScopedUtfChars(JNIEnv* env, jstring value) : m_env(env), m_string(value) {
        if (value != nullptr) {
            m_chars = env->GetStringUTFChars(value, nullptr);
        }
    }
    ~ScopedUtfChars() {
        if (m_chars != nullptr) {
            m_env->ReleaseStringUTFChars(m_string, m_chars);
        }
    }
    ScopedUtfChars(const ScopedUtfChars&) = delete;
    ScopedUtfChars& operator=(const ScopedUtfChars&) = delete;

    const char* c_str() const { return m_chars != nullptr ? m_chars : ""; }
    bool isValid() const { return m_chars != nullptr; }

private:
    JNIEnv* m_env = nullptr;
    jstring m_string = nullptr;
    const char* m_chars = nullptr;
};

bool validationRequestedByProperty() {
#if defined(__ANDROID__)
    char value[PROP_VALUE_MAX] = {};
    const int length = __system_property_get("debug.nova.validation", value);
    return length > 0 && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                          value[0] == 'y' || value[0] == 'Y');
#else
    return false;
#endif
}

}  // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_javaVm = vm;
    return JNI_VERSION_1_6;
}

// ---------------------------------------------------------------------------
// nativeInit(AssetManager, String, int, int, boolean): int
// ---------------------------------------------------------------------------
JNIEXPORT jint JNICALL
Java_com_nova_engine_NativeBridge_nativeInit(JNIEnv* env, jobject /*thiz*/,
                                             jobject assetManager, jstring dataPath,
                                             jint width, jint height,
                                             jboolean enableValidation) {
    if (g_engine != nullptr) {
        return static_cast<jint>(nova::EngineError::AlreadyInitialized);
    }
    if (assetManager == nullptr || dataPath == nullptr) {
        throwIllegalState(env, "nativeInit: assetManager/dataPath must not be null");
        return static_cast<jint>(nova::EngineError::InvalidArgument);
    }

    ScopedUtfChars path(env, dataPath);
    if (!path.isValid()) {
        throwIllegalState(env, "nativeInit: dataPath unreadable");
        return static_cast<jint>(nova::EngineError::InvalidArgument);
    }

    AAssetManager* apkManager = AAssetManager_fromJava(env, assetManager);
    if (apkManager == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
                            "AAssetManager_fromJava returned null");
        return static_cast<jint>(nova::EngineError::InvalidArgument);
    }

    // Keep the Java object alive for the engine's lifetime.
    g_assetManagerRef = env->NewGlobalRef(assetManager);

    const bool validation =
        enableValidation == JNI_TRUE || validationRequestedByProperty();

    nova::EngineConfig config;
    config.apkAssets = apkManager;
    config.dataPath = path.c_str();
    config.initialWidth = static_cast<unsigned>(width > 0 ? width : 0);
    config.initialHeight = static_cast<unsigned>(height > 0 ? height : 0);
    config.enableValidation = validation;

    g_engine = std::make_unique<nova::Engine>();
    const nova::Result<void, nova::EngineError> result = g_engine->init(config);
    if (!result) {
        const nova::EngineError error = result.error();
        __android_log_print(ANDROID_LOG_ERROR, kTag, "engine init failed: %s",
                            nova::engineErrorMessage(error));
        g_engine.reset();
        if (g_assetManagerRef != nullptr) {
            env->DeleteGlobalRef(g_assetManagerRef);
            g_assetManagerRef = nullptr;
        }
        return static_cast<jint>(error);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "engine initialized (validation=%s)",
                        validation ? "on" : "off");
    return 0;
}

// ---------------------------------------------------------------------------
// nativeSetSurface(Surface?): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeSetSurface(JNIEnv* env, jobject /*thiz*/,
                                                   jobject surface) {
    if (g_engine == nullptr) return;

    ANativeWindow* newWindow = nullptr;
    if (surface != nullptr) {
        newWindow = ANativeWindow_fromSurface(env, surface);
        if (newWindow == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                                "ANativeWindow_fromSurface failed");
            return;
        }
    }

    if (newWindow != nullptr) {
        const int32_t width = ANativeWindow_getWidth(newWindow);
        const int32_t height = ANativeWindow_getHeight(newWindow);
        g_engine->setSurface(newWindow,
                             static_cast<unsigned>(width > 0 ? width : 0),
                             static_cast<unsigned>(height > 0 ? height : 0));
    } else {
        g_engine->setSurface(nullptr, 0, 0);
    }

    // The engine has adopted (or released) the window reference.
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
    }
    g_window = newWindow;
}

// ---------------------------------------------------------------------------
// nativeResize(int, int): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeResize(JNIEnv* /*env*/, jobject /*thiz*/,
                                               jint width, jint height) {
    if (g_engine == nullptr) return;
    if (width <= 0 || height <= 0) return;
    g_engine->resize(static_cast<unsigned>(width), static_cast<unsigned>(height));
}

// ---------------------------------------------------------------------------
// nativeFrame(float): int
// ---------------------------------------------------------------------------
JNIEXPORT jint JNICALL
Java_com_nova_engine_NativeBridge_nativeFrame(JNIEnv* /*env*/, jobject /*thiz*/,
                                              jfloat dtSeconds) {
    if (g_engine == nullptr) {
        return nova::kFrameSkipped;
    }
    return g_engine->frame(static_cast<float>(dtSeconds));
}

// ---------------------------------------------------------------------------
// nativeOnTouch(int, int, float, float): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeOnTouch(JNIEnv* /*env*/, jobject /*thiz*/,
                                                jint action, jint pointerId,
                                                jfloat x, jfloat y) {
    if (g_engine == nullptr) return;
    g_engine->onTouch(static_cast<int>(action), static_cast<unsigned>(pointerId),
                      static_cast<float>(x), static_cast<float>(y));
}

// ---------------------------------------------------------------------------
// nativeOnKey(int, boolean): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeOnKey(JNIEnv* /*env*/, jobject /*thiz*/,
                                              jint keyCode, jboolean down) {
    if (g_engine == nullptr) return;
    g_engine->onKey(static_cast<int>(keyCode), down == JNI_TRUE);
}

// ---------------------------------------------------------------------------
// nativeOnPause(): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeOnPause(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_engine == nullptr) return;
    g_engine->onPause();
}

// ---------------------------------------------------------------------------
// nativeOnResume(): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeOnResume(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_engine == nullptr) return;
    g_engine->onResume();
}

// ---------------------------------------------------------------------------
// nativeShutdown(): void
// ---------------------------------------------------------------------------
JNIEXPORT void JNICALL
Java_com_nova_engine_NativeBridge_nativeShutdown(JNIEnv* env, jobject /*thiz*/) {
    if (g_engine == nullptr) return;

    g_engine->shutdown();
    g_engine.reset();

    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    if (g_assetManagerRef != nullptr) {
        env->DeleteGlobalRef(g_assetManagerRef);
        g_assetManagerRef = nullptr;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "engine shut down");
}

// ---------------------------------------------------------------------------
// nativeExecConsole(String): String
// ---------------------------------------------------------------------------
JNIEXPORT jstring JNICALL
Java_com_nova_engine_NativeBridge_nativeExecConsole(JNIEnv* env, jobject /*thiz*/,
                                                    jstring command) {
    if (command == nullptr) {
        throwIllegalState(env, "nativeExecConsole: command must not be null");
        return nullptr;
    }
    ScopedUtfChars cmd(env, command);
    if (!cmd.isValid()) {
        throwIllegalState(env, "nativeExecConsole: command unreadable");
        return nullptr;
    }
    const std::string output =
        g_engine != nullptr ? g_engine->exec(cmd.c_str()) : "engine not initialized";
    return env->NewStringUTF(output.c_str());
}

}  // extern "C"
