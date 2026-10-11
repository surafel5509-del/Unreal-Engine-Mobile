#include "core/Memory.h"

#include "core/Assert.h"

#include <cstdlib>
#include <cstring>

namespace nova::core {
namespace {

constexpr usize kPoolMinElementSize = sizeof(void*);
constexpr usize kPoolAlignment = alignof(std::max_align_t);

/// Allocators hand out blocks up to this alignment, so the base address of
/// the backing block is over-aligned by the same amount.
constexpr usize kBlockBaseAlignment = 256;

usize alignUp(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Allocate `capacity + kBlockBaseAlignment` bytes and return an aligned
/// view (caller frees `rawBlock`).
std::byte* allocateAlignedBlock(usize capacity, std::byte*& rawBlock) {
    rawBlock = static_cast<std::byte*>(std::malloc(capacity + kBlockBaseAlignment));
    if (rawBlock == nullptr) return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(rawBlock);
    const auto aligned = (base + kBlockBaseAlignment - 1) & ~(kBlockBaseAlignment - 1);
    return reinterpret_cast<std::byte*>(aligned);
}

}  // namespace

// ----------------------------------------------------------------- LinearArena

LinearArena::LinearArena(usize capacityBytes) : m_capacity(capacityBytes) {
    if (capacityBytes > 0) {
        m_block = allocateAlignedBlock(capacityBytes, m_rawBlock);
    }
}

LinearArena::~LinearArena() {
    std::free(m_rawBlock);
    m_rawBlock = nullptr;
    m_block = nullptr;
}

void* LinearArena::allocate(usize size, usize alignment) {
    if (m_block == nullptr || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    const usize aligned = alignUp(m_offset, alignment);
    if (aligned > m_capacity || size > m_capacity - aligned) {
        return nullptr;
    }
    m_offset = aligned + size;
    ++m_allocationCount;
    return m_block + aligned;
}

void LinearArena::reset() {
    m_offset = 0;
    m_allocationCount = 0;
#if !defined(NDEBUG)
    if (m_block != nullptr && m_capacity > 0) {
        std::memset(m_block, 0xDD, m_capacity);
    }
#endif
}

// ------------------------------------------------------------- StackAllocator

StackAllocator::StackAllocator(usize capacityBytes) : m_capacity(capacityBytes) {
    if (capacityBytes > 0) {
        m_block = allocateAlignedBlock(capacityBytes, m_rawBlock);
    }
}

StackAllocator::~StackAllocator() {
    std::free(m_rawBlock);
    m_rawBlock = nullptr;
    m_block = nullptr;
}

void* StackAllocator::allocate(usize size, usize alignment) {
    if (m_block == nullptr || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    const usize aligned = alignUp(m_offset, alignment);
    if (aligned > m_capacity || size > m_capacity - aligned) {
        return nullptr;
    }
    m_offset = aligned + size;
    return m_block + aligned;
}

void StackAllocator::rewind(Mark mark) {
    NOVA_ENSURE(mark <= m_capacity);
    m_offset = mark;
}

void StackAllocator::reset() {
    m_offset = 0;
}

// -------------------------------------------------------------- PoolAllocator

PoolAllocator::PoolAllocator(usize elementSize, usize elementCount)
    : m_elementSize(alignUp(elementSize < kPoolMinElementSize ? kPoolMinElementSize
                                                             : elementSize,
                           kPoolAlignment)),
      m_slotCount(elementCount),
      m_freeCount(elementCount) {
    if (elementCount == 0) return;
    m_block = static_cast<std::byte*>(std::calloc(elementCount, m_elementSize));
    if (m_block == nullptr) {
        m_slotCount = 0;
        m_freeCount = 0;
        return;
    }
    // Build the LIFO freelist: slot i's first sizeof(void*) bytes store next.
    for (usize i = 0; i < elementCount; ++i) {
        void** next = reinterpret_cast<void**>(m_block + i * m_elementSize);
        *next = (i + 1 < elementCount) ? (m_block + (i + 1) * m_elementSize) : nullptr;
    }
    m_head = m_block;
}

PoolAllocator::~PoolAllocator() {
    std::free(m_block);
    m_block = nullptr;
}

void* PoolAllocator::allocate() {
    if (m_head == nullptr) return nullptr;
    void* slot = m_head;
    m_head = *static_cast<void**>(slot);
    --m_freeCount;
    return slot;
}

void PoolAllocator::free(void* element) {
    if (element == nullptr) return;
#if !defined(NDEBUG)
    const auto* bytes = static_cast<std::byte*>(element);
    NOVA_ASSERT(bytes >= m_block && bytes < m_block + m_slotCount * m_elementSize,
                "PoolAllocator::free: pointer %p does not belong to this pool",
                element);
#endif
    *static_cast<void**>(element) = m_head;
    m_head = element;
    ++m_freeCount;
}

}  // namespace nova::core
