// Host-check shim for <android/native_window_jni.h> (NDK API subset used by
// Nova Engine's JNI bridge). Only active with -DNOVA_HOST_CHECK=ON.
//
// The NDK splits the ANativeWindow API across two headers: the window
// operations live in <android/native_window.h> and the JNIEnv bridge lives
// here. The shim mirrors that split, because a declaration placed in the wrong
// header compiles on the host and then fails on a real NDK build.
#pragma once

#include <jni.h>

#include <android/native_window.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Return the ANativeWindow associated with a Java Surface, acquiring it.
/// Returns NULL when the surface is invalid.
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);

#ifdef __cplusplus
}
#endif
