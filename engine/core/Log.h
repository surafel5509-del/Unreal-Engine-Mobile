#pragma once

// Nova Engine — logging.
//
// Features:
//  * printf-style leveled logging (NOVA_LOG_INFO(...) macros),
//  * pluggable sinks: stderr (all platforms), logcat (Android builds),
//    file sink (engine data directory),
//  * a fixed-size ring buffer retaining the most recent records for the
//    in-engine console, crash dumps and tests.
//
// Thread-safety: log::write() and the ring queries are safe to call from any
// thread; a single mutex serializes records (logging is not a hot path).
// Sinks are invoked while holding the log lock.

#include "core/Types.h"

#include <functional>

namespace nova::core {

enum class LogLevel : u8 { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

/// One retained log record (ring buffer entry).
struct LogRecord {
    static constexpr usize kMessageCapacity = 1024;

    LogLevel level = LogLevel::Info;
    u64 sequence = 0;
    u64 timestampMicros = 0;
    u32 threadHash = 0;
    char message[kMessageCapacity] = {};
};

/// A log destination. Implementations must tolerate being called while the
/// global log lock is held (i.e. do not call back into the logger).
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;
};

namespace log {

/// Install default sinks (stderr, and logcat on Android). Idempotent.
void initialize();

/// Flush and remove all sinks; keeps the ring buffer intact. Idempotent.
void shutdown();

/// Records below `level` are dropped (ring + sinks).
void setLevel(LogLevel level);
LogLevel level();

/// Take ownership of a sink. The sink is destroyed by shutdown().
void addSink(Unique<ILogSink> sink);

/// Attach the file sink at `path` (append mode, one file, flushed per record).
/// Returns false when the file cannot be opened.
bool setFilePath(const std::string& path);

/// Formatted record write. `fmt` is a printf format string.
void write(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/// Number of records currently retained in the ring.
usize retainedCount();

/// Iterate retained records oldest -> newest under the log lock.
void forEachRetained(const std::function<void(const LogRecord&)>& fn);

/// Immediately terminate the process after flushing logs (used by NOVA_ASSERT).
[[noreturn]] void abortProcess();

}  // namespace log
}  // namespace nova::core

#define NOVA_LOG_TRACE(...)                                        \
    ::nova::core::log::write(::nova::core::LogLevel::Trace, __VA_ARGS__)
#define NOVA_LOG_DEBUG(...)                                        \
    ::nova::core::log::write(::nova::core::LogLevel::Debug, __VA_ARGS__)
#define NOVA_LOG_INFO(...)                                        \
    ::nova::core::log::write(::nova::core::LogLevel::Info, __VA_ARGS__)
#define NOVA_LOG_WARN(...)                                        \
    ::nova::core::log::write(::nova::core::LogLevel::Warn, __VA_ARGS__)
#define NOVA_LOG_ERROR(...)                                        \
    ::nova::core::log::write(::nova::core::LogLevel::Error, __VA_ARGS__)
