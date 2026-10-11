// Host-check shim for <android/asset_manager.h> (NDK API subset used by
// Nova Engine's VFS). Only active with -DNOVA_HOST_CHECK=ON.
#pragma once

#include <sys/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AAssetDir;
struct AAsset;
struct AAssetManager;

typedef struct AAssetDir AAssetDir;
typedef struct AAsset AAsset;
typedef struct AAssetManager AAssetManager;

enum {
    AASSET_MODE_UNKNOWN = 0,
    AASSET_MODE_RANDOM = 1,
    AASSET_MODE_STREAMING = 2,
    AASSET_MODE_BUFFER = 3,
};

AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* path);
void AAssetDir_close(AAssetDir* assetDir);
const char* AAssetDir_getNextFileName(AAssetDir* assetDir);

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);

int AAsset_read(AAsset* asset, void* buf, size_t count);
int AAsset_seek(AAsset* asset, off_t offset, int whence);
off_t AAsset_getLength(AAsset* asset);
off_t AAsset_getRemainingLength(AAsset* asset);
void AAsset_close(AAsset* asset);

#ifdef __cplusplus
}
#endif
