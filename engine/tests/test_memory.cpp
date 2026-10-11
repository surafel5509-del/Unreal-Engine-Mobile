// Unit tests — custom allocators (LinearArena, StackAllocator, PoolAllocator).
#include <doctest.h>

#include "core/Memory.h"

#include <cstdint>

using nova::core::LinearArena;
using nova::core::PoolAllocator;
using nova::core::StackAllocator;

namespace {

bool isAligned(const void* pointer, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0;
}

}  // namespace

TEST_CASE("LinearArena hands out aligned, non-overlapping blocks") {
    LinearArena arena(1024);
    CHECK(arena.capacity() == 1024);
    CHECK(arena.used() == 0);

    void* a = arena.allocate(16, 16);
    void* b = arena.allocate(32, 32);
    void* c = arena.allocate(8, 8);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(isAligned(a, 16));
    CHECK(isAligned(b, 32));
    CHECK(isAligned(c, 8));
    CHECK(arena.allocationCount() == 3);
    CHECK(arena.used() >= 16 + 32 + 8);

    // No overlap between successive allocations.
    auto* aEnd = static_cast<std::byte*>(a) + 16;
    CHECK(aEnd <= static_cast<std::byte*>(b));
    auto* bEnd = static_cast<std::byte*>(b) + 32;
    CHECK(bEnd <= static_cast<std::byte*>(c));
}

TEST_CASE("LinearArena reports exhaustion instead of overcommitting") {
    LinearArena arena(64);
    CHECK(arena.allocate(64) != nullptr);
    CHECK(arena.allocate(1) == nullptr);
    CHECK(arena.remaining() == 0);
}

TEST_CASE("LinearArena reset reclaims everything") {
    LinearArena arena(128);
    CHECK(arena.allocate(128) != nullptr);
    CHECK(arena.allocate(1) == nullptr);
    arena.reset();
    CHECK(arena.used() == 0);
    CHECK(arena.allocationCount() == 0);
    CHECK(arena.allocate(64) != nullptr);
}

TEST_CASE("StackAllocator supports mark/rewind nesting") {
    StackAllocator stack(256);
    const auto outer = stack.mark();
    void* first = stack.allocate(64, 8);
    REQUIRE(first != nullptr);
    const auto inner = stack.mark();
    void* second = stack.allocate(64, 8);
    REQUIRE(second != nullptr);

    stack.rewind(inner);
    CHECK(stack.used() == 64);
    void* third = stack.allocate(64, 8);
    CHECK(third == second);  // LIFO reuse of the same address

    stack.rewind(outer);
    CHECK(stack.used() == 0);
    CHECK(stack.allocate(256) != nullptr);
    CHECK(stack.allocate(1) == nullptr);
}

TEST_CASE("PoolAllocator recycles freed slots with full accounting") {
    PoolAllocator pool(48, 4);
    CHECK(pool.totalSlots() == 4);
    CHECK(pool.freeSlots() == 4);

    void* slots[4];
    for (int i = 0; i < 4; ++i) {
        slots[i] = pool.allocate();
        REQUIRE(slots[i] != nullptr);
        CHECK(isAligned(slots[i], alignof(std::max_align_t)));
    }
    CHECK(pool.allocate() == nullptr);
    CHECK(pool.freeSlots() == 0);

    // Free one, allocate one: the same slot must come back (LIFO).
    pool.free(slots[2]);
    CHECK(pool.freeSlots() == 1);
    void* recycled = pool.allocate();
    CHECK(recycled == slots[2]);
    CHECK(pool.freeSlots() == 0);

    for (void* slot : slots) pool.free(slot);
    CHECK(pool.freeSlots() == 4);
    // Double-free is a programming error; debug builds assert — we only
    // verify that a null free is a no-op:
    pool.free(nullptr);
    CHECK(pool.freeSlots() == 4);
}

TEST_CASE("PoolAllocator rounds small elements up to pointer size") {
    PoolAllocator pool(1, 8);  // 1-byte elements become sizeof(void*)
    CHECK(pool.elementSize() >= sizeof(void*));
    void* a = pool.allocate();
    void* b = pool.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(static_cast<std::byte*>(b) == static_cast<std::byte*>(a) + pool.elementSize());
}
