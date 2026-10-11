// Unit tests — virtual filesystem (data mount; APK mount needs Android).
#include <doctest.h>

#include "platform/VirtualFileSystem.h"

#include "core/Time.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using nova::platform::VirtualFileSystem;

namespace {

/// Writes `content` at <tmp>/relative, creating parent directories.
void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

struct TempDir {
    TempDir() {
        path = fs::temp_directory_path() /
               ("nova_vfs_test_" +
                std::to_string(
                    static_cast<long long>(nova::core::nowMicros())));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    fs::path path;
};

}  // namespace

TEST_CASE("VFS reads files from the data mount") {
    TempDir temp;
    writeFile(temp.path / "shaders" / "demo.spv", "SPIRV-BYTES");
    writeFile(temp.path / "notes.txt", "hello");

    VirtualFileSystem vfs;
    vfs.mountData(temp.path.string());
    CHECK(vfs.hasDataMount());
    CHECK(!vfs.hasApkMount());

    nova::core::ByteBuffer bytes;
    CHECK(vfs.readAsset("shaders/demo.spv", bytes));
    REQUIRE(bytes.size() == 11);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) ==
          "SPIRV-BYTES");

    std::string text;
    CHECK(vfs.readText("notes.txt", text));
    CHECK(text == "hello");
    CHECK(vfs.exists("notes.txt"));
}

TEST_CASE("VFS reports missing files without touching the output") {
    TempDir temp;
    VirtualFileSystem vfs;
    vfs.mountData(temp.path.string());

    nova::core::ByteBuffer bytes{std::byte{0xAB}};
    CHECK(!vfs.readAsset("does/not/exist.bin", bytes));
    CHECK(bytes.size() == 1);  // untouched
    CHECK(!vfs.exists("does/not/exist.bin"));
    std::string text = "untouched";
    CHECK(!vfs.readText("does/not/exist.txt", text));
    CHECK(text == "untouched");
}

TEST_CASE("VFS resolves data paths and normalizes roots") {
    VirtualFileSystem vfs;
    CHECK(vfs.resolveDataPath("logs/nova.log").empty());  // no mount yet

    TempDir temp;
    std::string root = temp.path.string();
    if (!root.empty() && root.back() == '/') root.pop_back();
    vfs.mountData(root + "/");  // trailing slash must be stripped
    CHECK(vfs.dataRoot() == root);
    CHECK(vfs.resolveDataPath("logs") == root + "/logs");
    CHECK(vfs.resolveDataPath("") == root);
}
