#pragma once

// Nova Engine — high resolution timing helpers.

#include "core/Types.h"

#include <chrono>

namespace nova::core {

/// Monotonic microsecond clock; the single time source for the engine
/// (frame pacing, logging timestamps, profiling marks).
u64 nowMicros();

/// A stopwatch for measuring elapsed wall time on the monotonic clock.
/// Thread-safety: not thread-safe; use one instance per thread.
class Stopwatch {
public:
    Stopwatch() : m_start(std::chrono::steady_clock::now()) {}

    /// Restart the stopwatch and return the elapsed seconds since the
    /// previous restart/construction.
    f64 restart();

    /// Seconds since construction / last restart.
    f64 elapsedSeconds() const;

    /// Seconds since construction / last restart, as milliseconds.
    f64 elapsedMillis() const { return elapsedSeconds() * 1000.0; }

private:
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace nova::core
