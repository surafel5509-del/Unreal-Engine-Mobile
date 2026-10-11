// Host-check shim implementations.
//
// Provides linkable no-op definitions for the Android NDK functions used by
// engine sources when they are compiled on a desktop host with
// -DNOVA_HOST_CHECK=ON. This target never runs on a device: it exists so CI
// can compile (and unit tests can link) the exact Android code paths without
// an NDK. See tools/host_check/README.md.

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_window.h>

#include <cstdarg>
#include <cstdio>

extern "C" {

int __android_log_write(int /*prio*/, const char* /*tag*/, const char* /*text*/) {
    return 0;
}

int __android_log_print(int /*prio*/, const char* tag, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[shim:%s] ", tag != nullptr ? tag : "?");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    return 0;
}

int __android_log_vprint(int /*prio*/, const char* /*tag*/, const char* /*fmt*/,
                         va_list /*args*/) {
    return 0;
}

AAssetDir* AAssetManager_openDir(AAssetManager* /*mgr*/, const char* /*path*/) {
    return nullptr;
}
void AAssetDir_close(AAssetDir* /*assetDir*/) {}
const char* AAssetDir_getNextFileName(AAssetDir* /*assetDir*/) { return nullptr; }

AAsset* AAssetManager_open(AAssetManager* /*mgr*/, const char* /*filename*/, int /*mode*/) {
    return nullptr;
}

int AAsset_read(AAsset* /*asset*/, void* /*buf*/, size_t /*count*/) { return -1; }
int AAsset_seek(AAsset* /*asset*/, off_t /*offset*/, int /*whence*/) { return -1; }
off_t AAsset_getLength(AAsset* /*asset*/) { return 0; }
off_t AAsset_getRemainingLength(AAsset* /*asset*/) { return 0; }
void AAsset_close(AAsset* /*asset*/) {}

AAssetManager* AAssetManager_fromJava(JNIEnv* /*env*/, jobject /*assetManager*/) {
    return nullptr;
}

ANativeWindow* ANativeWindow_fromSurface(JNIEnv* /*env*/, jobject /*surface*/) {
    return nullptr;
}
void ANativeWindow_acquire(ANativeWindow* /*window*/) {}
void ANativeWindow_release(ANativeWindow* /*window*/) {}
int32_t ANativeWindow_getWidth(ANativeWindow* /*window*/) { return 0; }
int32_t ANativeWindow_getHeight(ANativeWindow* /*window*/) { return 0; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* /*window*/, int32_t /*width*/,
                                         int32_t /*height*/, int32_t /*format*/) {
    return 0;
}

}  // extern "C"
