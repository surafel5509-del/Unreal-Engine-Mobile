// Unit tests — platform input queue.
#include <doctest.h>

#include "core/Time.h"
#include "platform/InputQueue.h"

#include <thread>
#include <vector>

using nova::platform::InputQueue;
using nova::platform::KeyEvent;
using nova::platform::TouchAction;
using nova::platform::TouchEvent;

namespace {

TouchEvent touch(TouchAction action, nova::core::u32 id, float x, float y) {
    TouchEvent event;
    event.action = action;
    event.pointerId = id;
    event.x = x;
    event.y = y;
    event.timestampMicros = nova::core::nowMicros();
    return event;
}

}  // namespace

TEST_CASE("touchActionFromAndroid maps MotionEvent constants") {
    CHECK(nova::platform::touchActionFromAndroid(0) == TouchAction::Down);
    CHECK(nova::platform::touchActionFromAndroid(1) == TouchAction::Up);
    CHECK(nova::platform::touchActionFromAndroid(2) == TouchAction::Move);
    CHECK(nova::platform::touchActionFromAndroid(3) == TouchAction::Cancel);
    CHECK(nova::platform::touchActionFromAndroid(5) == TouchAction::PointerDown);
    CHECK(nova::platform::touchActionFromAndroid(6) == TouchAction::PointerUp);
    CHECK(nova::platform::touchActionFromAndroid(42) == TouchAction::Unknown);
}

TEST_CASE("InputQueue preserves FIFO order across mixed streams") {
    InputQueue queue(16);
    queue.pushTouch(touch(TouchAction::Down, 0, 10.0f, 20.0f));
    KeyEvent key{36, true, nova::core::nowMicros()};
    queue.pushKey(key);
    queue.pushTouch(touch(TouchAction::Move, 0, 11.0f, 21.0f));
    queue.pushTouch(touch(TouchAction::Up, 0, 11.0f, 21.0f));

    CHECK(queue.pendingCount() == 4);

    std::vector<TouchEvent> touches;
    std::vector<KeyEvent> keys;
    const nova::core::usize drained = queue.drain(touches, keys);
    CHECK(drained == 4);
    CHECK(touches.size() == 3);
    CHECK(keys.size() == 1);
    CHECK(touches[0].action == TouchAction::Down);
    CHECK(touches[1].action == TouchAction::Move);
    CHECK(touches[2].action == TouchAction::Up);
    CHECK(keys[0].keyCode == 36);
    CHECK(keys[0].down);
    CHECK(queue.pendingCount() == 0);
}

TEST_CASE("InputQueue drops oldest events on overflow") {
    InputQueue queue(4);
    for (int i = 0; i < 6; ++i) {
        queue.pushTouch(touch(TouchAction::Move, 0, static_cast<float>(i), 0.0f));
    }
    CHECK(queue.pendingCount() == 4);
    CHECK(queue.droppedCount() == 2);

    std::vector<TouchEvent> touches;
    std::vector<KeyEvent> keys;
    queue.drain(touches, keys);
    REQUIRE(touches.size() == 4);
    // Events 0 and 1 were dropped: the queue holds x = 2..5.
    CHECK(touches.front().x == 2.0f);
    CHECK(touches.back().x == 5.0f);
}

TEST_CASE("lastPosition tracks contacts and releases them on Up") {
    InputQueue queue(16);
    queue.pushTouch(touch(TouchAction::Down, 0, 100.0f, 200.0f));
    queue.pushTouch(touch(TouchAction::Down, 1, 300.0f, 400.0f));

    float x = 0.0f;
    float y = 0.0f;
    CHECK(queue.lastPosition(0, x, y));
    CHECK(x == 100.0f);
    CHECK(y == 200.0f);
    CHECK(queue.lastPosition(1, x, y));
    CHECK(x == 300.0f);

    queue.pushTouch(touch(TouchAction::Up, 0, 100.0f, 200.0f));
    CHECK(!queue.lastPosition(0, x, y));
    CHECK(queue.lastPosition(1, x, y));

    queue.clear();
    CHECK(!queue.lastPosition(1, x, y));
    CHECK(queue.pendingCount() == 0);
}

TEST_CASE("InputQueue tolerates concurrent producers") {
    InputQueue queue(4096);
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&queue, t]() {
            for (int i = 0; i < 500; ++i) {
                queue.pushTouch(touch(TouchAction::Move, static_cast<nova::core::u32>(t),
                                      static_cast<float>(i), 0.0f));
            }
        });
    }
    for (auto& producer : producers) producer.join();
    // 4 x 500 = 2000 pushes into a 4096-slot ring: nothing is dropped.
    CHECK(queue.pendingCount() == 2000);
    CHECK(queue.droppedCount() == 0);
}
