#pragma once

// Nova Engine — core type aliases and small utilities.
// Naming: PascalCase types, camelCase functions, m_ members, k constants.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nova {

/// Engine-wide smart pointer aliases (coding standards: raw owning pointers
/// are banned — everything owned goes through Unique/Shared).
template <typename T>
using Unique = std::unique_ptr<T>;

template <typename T>
using Shared = std::shared_ptr<T>;

template <typename T, typename... Args>
Unique<T> makeUnique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

namespace core {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

// Convenience: core code may keep spelling these nova::Unique/...
using nova::makeUnique;
using nova::Shared;
using nova::Unique;

/// Byte buffer used across asset loading and GPU uploads.
using ByteBuffer = std::vector<std::byte>;

/// Cache line size assumed for hot data layout on ARM64 (and x86-64).
inline constexpr std::size_t kCacheLineSize = 64;

/// Non-copyable, non-movable base to make ownership intent explicit.
class NonCopyable {
public:
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = delete;
    NonCopyable& operator=(NonCopyable&&) = delete;
};

}  // namespace core

// Scalar aliases are also visible at nova:: scope (EngineStats, JNI glue…).
using core::f32;
using core::f64;
using core::i16;
using core::i32;
using core::i64;
using core::i8;
using core::u16;
using core::u32;
using core::u64;
using core::u8;
using core::usize;

}  // namespace nova
