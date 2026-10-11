// Host-check shim for <android/asset_manager_jni.h> (NDK API subset used by
// Nova Engine's JNI bridge). Only active with -DNOVA_HOST_CHECK=ON.
#pragma once

#include <jni.h>

#include <android/asset_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

AAssetManager* AAssetManager_fromJava(JNIEnv* env, jobject assetManager);

#ifdef __cplusplus
}
#endif
