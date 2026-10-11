#include "core/Log.h"

#include <cstdarg>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace nova::core {
namespace {

constexpr usize kRingCapacity = 512;

const char* levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "T";
        case LogLevel::Debug: return "D";
        case LogLevel::Info: return "I";
        case LogLevel::Warn: return "W";
        case LogLevel::Error: return "E";
        case LogLevel::Off: return "-";
    }
    return "?";
}

u32 currentThreadHash() {
    return static_cast<u32>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

class StderrSink final : public ILogSink {
public:
    void write(const LogRecord& record) override {
        std::fprintf(stderr, "[%s] [%s] %s\n", "nova", levelTag(record.level),
                     record.message);
        std::fflush(stderr);
    }
};

#if defined(__ANDROID__)
class LogcatSink final : public ILogSink {
public:
    void write(const LogRecord& record) override {
        android_LogPriority priority = ANDROID_LOG_INFO;
        switch (record.level) {
            case LogLevel::Trace:
            case LogLevel::Debug: priority = ANDROID_LOG_DEBUG; break;
            case LogLevel::Info: priority = ANDROID_LOG_INFO; break;
            case LogLevel::Warn: priority = ANDROID_LOG_WARN; break;
            case LogLevel::Error: priority = ANDROID_LOG_ERROR; break;
            case LogLevel::Off: break;
        }
        __android_log_write(priority, "nova", record.message);
    }
};
#endif

class FileSink final : public ILogSink {
public:
    explicit FileSink(const std::string& path) {
        m_file = std::fopen(path.c_str(), "a");
    }

    ~FileSink() override {
        if (m_file != nullptr) {
            std::fflush(m_file);
            std::fclose(m_file);
        }
    }

    bool isValid() const { return m_file != nullptr; }

    void write(const LogRecord& record) override {
        if (m_file == nullptr) return;
        const std::time_t seconds = static_cast<std::time_t>(record.timestampMicros / 1000000ull);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        const unsigned millis =
            static_cast<unsigned>((record.timestampMicros % 1000000ull) / 1000ull);
        std::fprintf(m_file, "%s.%03u [%s] [t%08x] %s\n", stamp, millis,
                     levelTag(record.level), record.threadHash, record.message);
        std::fflush(m_file);
    }

private:
    std::FILE* m_file = nullptr;
};

struct LogState {
    std::mutex lock;
    LogLevel minLevel = LogLevel::Trace;
    bool initialized = false;

    std::array<LogRecord, kRingCapacity> ring{};
    usize ringHead = 0;   // next write slot
    usize ringCount = 0;  // number of valid entries (<= kRingCapacity)
    u64 nextSequence = 1;

    std::vector<Unique<ILogSink>> sinks;
    Unique<FileSink> fileSink;
};

LogState& state() {
    static LogState instance;
    return instance;
}

void addDefaultSinksLocked(LogState& s) {
    s.sinks.push_back(makeUnique<StderrSink>());
#if defined(__ANDROID__)
    s.sinks.push_back(makeUnique<LogcatSink>());
#endif
}

void ensureInitializedLocked(LogState& s) {
    if (s.initialized) return;
    s.initialized = true;
    addDefaultSinksLocked(s);
}

}  // namespace

namespace log {

void initialize() {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    ensureInitializedLocked(s);
}

void shutdown() {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    s.sinks.clear();
    s.fileSink.reset();
    s.initialized = false;
}

void setLevel(LogLevel level) {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    s.minLevel = level;
}

LogLevel level() {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    return s.minLevel;
}

void addSink(Unique<ILogSink> sink) {
    if (sink == nullptr) return;
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    ensureInitializedLocked(s);
    s.sinks.push_back(std::move(sink));
}

bool setFilePath(const std::string& path) {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    ensureInitializedLocked(s);
    auto file = makeUnique<FileSink>(path);
    if (!file->isValid()) {
        return false;
    }
    s.fileSink = std::move(file);
    return true;
}

void write(LogLevel level, const char* fmt, ...) {
    LogState& s = state();

    char message[LogRecord::kMessageCapacity];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    LogRecord record;
    record.level = level;
    record.timestampMicros = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    record.threadHash = currentThreadHash();
    std::snprintf(record.message, sizeof(record.message), "%s", message);

    std::lock_guard<std::mutex> guard(s.lock);
    if (level < s.minLevel) return;
    ensureInitializedLocked(s);

    record.sequence = s.nextSequence++;
    s.ring[s.ringHead] = record;
    s.ringHead = (s.ringHead + 1) % kRingCapacity;
    if (s.ringCount < kRingCapacity) ++s.ringCount;

    for (auto& sink : s.sinks) {
        sink->write(record);
    }
    if (s.fileSink != nullptr) {
        s.fileSink->write(record);
    }
}

usize retainedCount() {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    return s.ringCount;
}

void forEachRetained(const std::function<void(const LogRecord&)>& fn) {
    LogState& s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    if (s.ringCount == 0) return;
    const usize start = (s.ringHead + kRingCapacity - s.ringCount) % kRingCapacity;
    for (usize i = 0; i < s.ringCount; ++i) {
        fn(s.ring[(start + i) % kRingCapacity]);
    }
}

void abortProcess() {
    // Best effort: the lock is held by write() when asserts fire there, so
    // only flush stderr.
    std::fflush(stderr);
    std::abort();
}

}  // namespace log
}  // namespace nova::core
