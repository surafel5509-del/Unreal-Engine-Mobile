// Host-check shim for <android/native_window.h> (NDK API subset used by
// Nova Engine's JNI bridge and the Vulkan Android surface path). Only
// active with -DNOVA_HOST_CHECK=ON.
#pragma once

#include <stdint.h>

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;

enum {
    NATIVE_WINDOW_SET_USAGE = 0,
    NATIVE_WINDOW_CONSUME_USAGE = 1,
    NATIVE_WINDOW_SET_CROP = 2,
    NATIVE_WINDOW_SET_BUFFER_COUNT = 3,
};

ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
void ANativeWindow_acquire(ANativeWindow* window);
void ANativeWindow_release(ANativeWindow* window);
int32_t ANativeWindow_getWidth(ANativeWindow* window);
int32_t ANativeWindow_getHeight(ANativeWindow* window);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow* window, int32_t width,
                                         int32_t height, int32_t format);

#ifdef __cplusplus
}
#endif
