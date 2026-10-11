#pragma once

// Nova Engine — virtual filesystem with mount points.
//
// Mounts (checked in order):
//   apk   — read-only: the application package (AAssetManager*), resolved
//           only on Android builds; on other platforms this mount reports
//           "not found" and the data mount is consulted.
//   data  — read/write directory in internal storage (filesDir/nova), used
//           for logs, cooked assets and hot-reload staging.
//
// Threading: mount* may only be called before the engine starts loading;
// readAsset is safe from any thread once mounted (AAssetManager is
// thread-safe; std::filesystem reads are independent per call).

#include "core/Types.h"

#include <string>

struct AAssetManager;

namespace nova::platform {

class VirtualFileSystem : public nova::core::NonCopyable {
public:
    VirtualFileSystem() = default;

    /// Mount the APK asset manager (takes a non-owning pointer; the JNI
    /// layer pins the Java object for the engine's lifetime).
    void mountApk(AAssetManager* assetManager);

    /// Mount a host directory as the data root.
    void mountData(std::string rootPath);

    bool hasApkMount() const { return m_apk != nullptr; }
    bool hasDataMount() const { return !m_dataRoot.empty(); }
    const std::string& dataRoot() const { return m_dataRoot; }

    /// Read a whole file. APK assets are tried first (e.g. "shaders/
    /// background.vert.spv"), then the data mount. Returns false and leaves
    /// `out` untouched when the file does not exist or is unreadable.
    bool readAsset(const std::string& path, nova::core::ByteBuffer& out) const;

    /// Convenience: read as UTF-8 text (no BOM handling).
    bool readText(const std::string& path, std::string& out) const;

    /// True when readAsset() would succeed.
    bool exists(const std::string& path) const;

    /// Resolve a path against the data mount ("" when no data mount).
    /// Used by log setup; does not touch the filesystem.
    std::string resolveDataPath(const std::string& relative) const;

private:
    bool readFromData(const std::string& path, nova::core::ByteBuffer& out) const;

    AAssetManager* m_apk = nullptr;
    std::string m_dataRoot;
};

}  // namespace nova::platform
