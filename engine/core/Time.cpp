#include "core/Time.h"

namespace nova::core {

u64 nowMicros() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

f64 Stopwatch::restart() {
    const auto previous = m_start;
    m_start = std::chrono::steady_clock::now();
    return std::chrono::duration<f64>(m_start - previous).count();
}

f64 Stopwatch::elapsedSeconds() const {
    return std::chrono::duration<f64>(std::chrono::steady_clock::now() - m_start).count();
}

}  // namespace nova::core
