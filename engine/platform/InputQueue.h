#pragma once

// Nova Engine — platform input stream.
//
// Android touch/key events arrive from JNI (UI thread) and are pushed into
// this queue; gameplay and editor systems drain it on the render/update
// thread each frame. The queue is a fixed-capacity ring guarded by a mutex
// (input volume is tiny; contention is negligible).
//
// Coordinates are in surface pixels with the origin at the top-left, exactly
// as delivered by MotionEvent. Key codes are Android keycodes (forwarded
// verbatim); keeping them raw lets platform-independent systems compare
// against the constants they care about.
//
// Thread-safety: push* may be called from any producer thread; drain() and
// the queries from a single consumer thread.

#include "core/Types.h"

#include <array>
#include <mutex>
#include <vector>

namespace nova::platform {

using nova::core::f32;
using nova::core::i32;
using nova::core::u32;
using nova::core::u64;
using nova::core::u8;
using nova::core::usize;

enum class TouchAction : u8 {
    Down = 0,
    Up = 1,
    Move = 2,
    Cancel = 3,
    PointerDown = 4,
    PointerUp = 5,
    Unknown = 255,
};

/// Map an Android MotionEvent action (ACTION_DOWN = 0, ...) to TouchAction.
/// Unknown values map to TouchAction::Unknown.
TouchAction touchActionFromAndroid(i32 androidAction);

struct TouchEvent {
    TouchAction action = TouchAction::Unknown;
    u32 pointerId = 0;
    f32 x = 0.0f;
    f32 y = 0.0f;
    u64 timestampMicros = 0;
};

struct KeyEvent {
    i32 keyCode = 0;
    bool down = false;
    u64 timestampMicros = 0;
};

/// Fixed-capacity multi-producer input ring. On overflow the OLDEST event
/// is dropped and the drop counter increments (latest input always wins).
class InputQueue : public nova::core::NonCopyable {
public:
    static constexpr usize kDefaultCapacity = 256;

    explicit InputQueue(usize capacity = kDefaultCapacity);

    void pushTouch(const TouchEvent& event);
    void pushKey(const KeyEvent& event);

    /// Move all pending events into `outTouch`/`outKey` (appended).
    /// Returns the number of events drained.
    usize drain(std::vector<TouchEvent>& outTouch, std::vector<KeyEvent>& outKey);

    /// Number of events currently buffered.
    usize pendingCount() const;

    /// Events dropped due to overflow since construction.
    u64 droppedCount() const;

    /// Most recent position per pointer id (state cache for gameplay).
    /// Returns false when the pointer has no recorded position.
    bool lastPosition(u32 pointerId, f32& x, f32& y) const;

    void clear();

private:
    struct StoredEvent {
        bool isTouch = false;
        TouchEvent touch{};
        KeyEvent key{};
    };

    void pushCommon(StoredEvent event);

    mutable std::mutex m_lock;
    std::vector<StoredEvent> m_ring;
    usize m_head = 0;  // next write index
    usize m_count = 0;
    u64 m_dropped = 0;

    struct PointerPosition {
        u32 id = 0;
        f32 x = 0.0f;
        f32 y = 0.0f;
        bool used = false;
    };
    static constexpr usize kMaxTrackedPointers = 10;
    std::array<PointerPosition, kMaxTrackedPointers> m_pointers{};
};

}  // namespace nova::platform
