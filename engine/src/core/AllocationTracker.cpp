#include "core/AllocationTracker.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace engine::core {

namespace {
// Relaxed atomics: these counters are read after the measured region has
// finished, never used to synchronise anything, and the cost of stronger
// ordering would show up in the very loops this is meant to measure.
std::atomic<bool> g_enabled{false};
std::atomic<uint64_t> g_allocationCount{0};
std::atomic<uint64_t> g_deallocationCount{0};
std::atomic<uint64_t> g_bytesAllocated{0};
} // namespace

void beginAllocationTracking() { g_enabled.store(true, std::memory_order_relaxed); }
void endAllocationTracking() { g_enabled.store(false, std::memory_order_relaxed); }
bool isAllocationTrackingEnabled() { return g_enabled.load(std::memory_order_relaxed); }

AllocationStats allocationStats() {
    AllocationStats stats;
    stats.allocationCount = g_allocationCount.load(std::memory_order_relaxed);
    stats.deallocationCount = g_deallocationCount.load(std::memory_order_relaxed);
    stats.bytesAllocated = g_bytesAllocated.load(std::memory_order_relaxed);
    return stats;
}

void resetAllocationStats() {
    g_allocationCount.store(0, std::memory_order_relaxed);
    g_deallocationCount.store(0, std::memory_order_relaxed);
    g_bytesAllocated.store(0, std::memory_order_relaxed);
}

// Called from the operator new/delete overrides below.
void noteAllocation(size_t bytes) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    g_bytesAllocated.fetch_add(bytes, std::memory_order_relaxed);
}

void noteDeallocation() {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    g_deallocationCount.fetch_add(1, std::memory_order_relaxed);
}

AllocationScope::AllocationScope() {
    wasEnabled_ = isAllocationTrackingEnabled();
    if (!wasEnabled_) beginAllocationTracking();
    start_ = allocationStats();
}

AllocationScope::~AllocationScope() {
    if (!wasEnabled_) endAllocationTracking();
}

uint64_t AllocationScope::allocationsSinceStart() const {
    return allocationStats().allocationCount - start_.allocationCount;
}

uint64_t AllocationScope::bytesSinceStart() const {
    return allocationStats().bytesAllocated - start_.bytesAllocated;
}

} // namespace engine::core

// ---------------------------------------------------------------------------
// Global operator new/delete overrides.
//
// Replacing these is the only way to see EVERY C++ heap allocation,
// including ones inside std:: containers we never wrote a call to. They
// stay cheap when tracking is off: a single relaxed atomic load.
//
// All sized/aligned/nothrow forms are provided. Missing one would let
// allocations through uncounted, which would make a "zero allocations"
// result quietly meaningless -- the exact failure this is built to avoid.
// ---------------------------------------------------------------------------

namespace {
void* trackedAllocate(size_t size) {
    // operator new must never return null; it throws instead.
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) throw std::bad_alloc();
    engine::core::noteAllocation(size);
    return memory;
}

void* trackedAllocateNoThrow(size_t size) noexcept {
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory != nullptr) engine::core::noteAllocation(size);
    return memory;
}

void trackedFree(void* memory) noexcept {
    if (memory == nullptr) return;
    engine::core::noteDeallocation();
    std::free(memory);
}
} // namespace

void* operator new(size_t size) { return trackedAllocate(size); }
void* operator new[](size_t size) { return trackedAllocate(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return trackedAllocateNoThrow(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return trackedAllocateNoThrow(size); }

void operator delete(void* memory) noexcept { trackedFree(memory); }
void operator delete[](void* memory) noexcept { trackedFree(memory); }
void operator delete(void* memory, size_t) noexcept { trackedFree(memory); }
void operator delete[](void* memory, size_t) noexcept { trackedFree(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { trackedFree(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { trackedFree(memory); }
