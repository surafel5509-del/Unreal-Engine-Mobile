#include "platform/VirtualFileSystem.h"

#include "core/Log.h"

#include <cstdio>
#include <vector>

#if defined(__ANDROID__) || defined(NOVA_HOST_ANDROID)
#include <android/asset_manager.h>
#endif

namespace nova::platform {

void VirtualFileSystem::mountApk(AAssetManager* assetManager) {
    m_apk = assetManager;
    NOVA_LOG_INFO("vfs: apk mount %s", assetManager != nullptr ? "ok" : "null");
}

void VirtualFileSystem::mountData(std::string rootPath) {
    m_dataRoot = std::move(rootPath);
    if (!m_dataRoot.empty() && m_dataRoot.back() == '/') {
        m_dataRoot.pop_back();
    }
    NOVA_LOG_INFO("vfs: data mount '%s'", m_dataRoot.c_str());
}

bool VirtualFileSystem::readFromData(const std::string& path, nova::core::ByteBuffer& out) const {
    if (m_dataRoot.empty()) return false;
    const std::string full = m_dataRoot + "/" + path;
    std::FILE* file = std::fopen(full.c_str(), "rb");
    if (file == nullptr) return false;

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        return false;
    }

    nova::core::ByteBuffer bytes(static_cast<nova::core::usize>(size));
    const size_t read =
        bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (read != bytes.size()) {
        return false;
    }
    out = std::move(bytes);
    return true;
}

bool VirtualFileSystem::readAsset(const std::string& path, nova::core::ByteBuffer& out) const {
#if defined(__ANDROID__) || defined(NOVA_HOST_ANDROID)
    if (m_apk != nullptr) {
        AAsset* asset = AAssetManager_open(m_apk, path.c_str(), AASSET_MODE_BUFFER);
        if (asset != nullptr) {
            const off_t length = AAsset_getLength(asset);
            nova::core::ByteBuffer bytes(static_cast<nova::core::usize>(length));
            const int read =
                bytes.empty() ? 0 : AAsset_read(asset, bytes.data(), bytes.size());
            AAsset_close(asset);
            if (read == length) {
                out = std::move(bytes);
                return true;
            }
            NOVA_LOG_WARN("vfs: short read for apk asset '%s'", path.c_str());
        }
    }
#endif
    return readFromData(path, out);
}

bool VirtualFileSystem::readText(const std::string& path, std::string& out) const {
    nova::core::ByteBuffer bytes;
    if (!readAsset(path, bytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool VirtualFileSystem::exists(const std::string& path) const {
    nova::core::ByteBuffer bytes;
    return readAsset(path, bytes);
}

std::string VirtualFileSystem::resolveDataPath(const std::string& relative) const {
    if (m_dataRoot.empty()) return {};
    if (relative.empty()) return m_dataRoot;
    return m_dataRoot + "/" + relative;
}

}  // namespace nova::platform
