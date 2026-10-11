#include "platform/InputQueue.h"

#include "core/Time.h"

#include <algorithm>
#include <array>

namespace nova::platform {

TouchAction touchActionFromAndroid(i32 androidAction) {
    // Values mirror android.view.MotionEvent ACTION_* constants.
    switch (androidAction) {
        case 0: return TouchAction::Down;
        case 1: return TouchAction::Up;
        case 2: return TouchAction::Move;
        case 3: return TouchAction::Cancel;
        case 5: return TouchAction::PointerDown;
        case 6: return TouchAction::PointerUp;
        default: return TouchAction::Unknown;
    }
}

InputQueue::InputQueue(usize capacity) : m_ring(capacity != 0 ? capacity : kDefaultCapacity) {}

void InputQueue::pushTouch(const TouchEvent& event) {
    StoredEvent stored;
    stored.isTouch = true;
    stored.touch = event;
    pushCommon(std::move(stored));
}

void InputQueue::pushKey(const KeyEvent& event) {
    StoredEvent stored;
    stored.isTouch = false;
    stored.key = event;
    pushCommon(std::move(stored));
}

void InputQueue::pushCommon(StoredEvent event) {
    const bool isTouch = event.isTouch;
    const TouchEvent touch = event.touch;  // copy: event is moved into the ring

    std::lock_guard<std::mutex> guard(m_lock);
    if (m_count == m_ring.size()) {
        // Drop the oldest event (index m_head is the oldest slot).
        m_head = (m_head + 1) % m_ring.size();
        --m_count;
        ++m_dropped;
    }
    m_ring[(m_head + m_count) % m_ring.size()] = std::move(event);
    ++m_count;

    if (!isTouch) return;

    auto match = std::find_if(m_pointers.begin(), m_pointers.end(),
                              [&](const PointerPosition& p) {
                                  return p.used && p.id == touch.pointerId;
                              });
    const bool released = touch.action == TouchAction::Up ||
                          touch.action == TouchAction::Cancel ||
                          touch.action == TouchAction::PointerUp;
    if (released) {
        if (match != m_pointers.end()) match->used = false;
        return;
    }
    auto slot = (match != m_pointers.end())
                    ? match
                    : std::find_if(m_pointers.begin(), m_pointers.end(),
                                   [](const PointerPosition& p) { return !p.used; });
    if (slot != m_pointers.end()) {
        slot->used = true;
        slot->id = touch.pointerId;
        slot->x = touch.x;
        slot->y = touch.y;
    }
}

usize InputQueue::drain(std::vector<TouchEvent>& outTouch, std::vector<KeyEvent>& outKey) {
    std::lock_guard<std::mutex> guard(m_lock);
    usize drained = 0;
    for (usize i = 0; i < m_count; ++i) {
        const StoredEvent& event = m_ring[(m_head + i) % m_ring.size()];
        if (event.isTouch) {
            outTouch.push_back(event.touch);
        } else {
            outKey.push_back(event.key);
        }
        ++drained;
    }
    m_head = 0;
    m_count = 0;
    return drained;
}

usize InputQueue::pendingCount() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_count;
}

u64 InputQueue::droppedCount() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_dropped;
}

bool InputQueue::lastPosition(u32 pointerId, f32& x, f32& y) const {
    std::lock_guard<std::mutex> guard(m_lock);
    for (const auto& p : m_pointers) {
        if (p.used && p.id == pointerId) {
            x = p.x;
            y = p.y;
            return true;
        }
    }
    return false;
}

void InputQueue::clear() {
    std::lock_guard<std::mutex> guard(m_lock);
    m_head = 0;
    m_count = 0;
    for (auto& p : m_pointers) p.used = false;
}

}  // namespace nova::platform
