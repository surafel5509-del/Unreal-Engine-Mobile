#pragma once

// Nova Engine — custom allocators.
//
// LinearArena : bump allocator; free-all via reset(). Ideal for per-frame
//               transient data (matrix palettes, culling results).
// StackAllocator : linear allocation with mark/rewind, for nested transient
//               scopes inside a frame.
// PoolAllocator : fixed-size slot freelist (LIFO free), for repeated small
//               objects (input events, render nodes).
//
// Thread-safety: none of the allocators are thread-safe; give each worker
// thread its own instance (documented per coding standards).

#include "core/Types.h"

namespace nova::core {

/// Bump-pointer arena over one contiguous block. Alignment must be a power
/// of two <= 256. Returns nullptr when the block is exhausted (callers must
/// handle OOM — engine code never assumes allocation success).
class LinearArena : public NonCopyable {
public:
    explicit LinearArena(usize capacityBytes);
    ~LinearArena();

    /// Allocate `size` bytes aligned to `alignment`. nullptr on exhaustion.
    void* allocate(usize size, usize alignment = alignof(std::max_align_t));

    /// Release every allocation made since construction/last reset.
    /// O(1). The returned memory is poisoned in debug builds.
    void reset();

    usize capacity() const { return m_capacity; }
    usize used() const { return m_offset; }
    usize remaining() const { return m_capacity - m_offset; }

    /// Number of live allocations (debug accounting).
    usize allocationCount() const { return m_allocationCount; }

private:
    std::byte* m_rawBlock = nullptr;  // malloc() pointer (freed as-is)
    std::byte* m_block = nullptr;      // 256-byte aligned view of m_rawBlock
    usize m_capacity = 0;
    usize m_offset = 0;
    usize m_allocationCount = 0;
};

/// Linear allocator with explicit marks: allocate() advances the cursor,
/// rewind(mark) releases everything after the mark. Marks stay valid until
/// the next reset().
class StackAllocator : public NonCopyable {
public:
    using Mark = usize;

    explicit StackAllocator(usize capacityBytes);
    ~StackAllocator();

    void* allocate(usize size, usize alignment = alignof(std::max_align_t));
    void rewind(Mark mark);
    Mark mark() const { return m_offset; }

    void reset();
    usize used() const { return m_offset; }
    usize capacity() const { return m_capacity; }

private:
    std::byte* m_rawBlock = nullptr;  // malloc() pointer (freed as-is)
    std::byte* m_block = nullptr;      // 256-byte aligned view of m_rawBlock
    usize m_capacity = 0;
    usize m_offset = 0;
};

/// Fixed-size element pool with an intrusive LIFO freelist.
/// All slots live in one contiguous block; free() pushes the slot back.
class PoolAllocator : public NonCopyable {
public:
    /// @param elementSize size of one element (rounded up to 8 bytes).
    /// @param elementCount number of slots.
    PoolAllocator(usize elementSize, usize elementCount);
    ~PoolAllocator();

    /// Acquire one element; nullptr when exhausted.
    void* allocate();

    /// Return an element previously obtained from allocate().
    /// Passing a foreign pointer corrupts the freelist — debug builds assert.
    void free(void* element);

    usize elementSize() const { return m_elementSize; }
    usize totalSlots() const { return m_slotCount; }
    usize freeSlots() const { return m_freeCount; }

private:
    std::byte* m_block = nullptr;
    usize m_elementSize = 0;
    usize m_slotCount = 0;
    usize m_freeCount = 0;
    void* m_head = nullptr;  // freelist head (intrusive next-pointer per slot)
};

}  // namespace nova::core
