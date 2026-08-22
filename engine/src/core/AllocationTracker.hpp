#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::core {

// Heap-allocation tracking, for asserting that a hot loop really does not
// allocate.
//
// "Zero-heap render tick" has been asserted in this codebase's own
// documentation without anything actually measuring it. This makes the
// claim checkable: the test binary overrides global operator new/delete
// (see AllocationTracker.cpp) and counts every call, so a test can wrap a
// frame and fail if the count moved.
//
// Deliberately counts rather than samples. An allocation that happens on
// one frame in a thousand is exactly the kind that causes a stutter
// nobody can reproduce, and a sampling profiler would miss it.
//
// Scope, stated plainly: this counts allocations made through global
// operator new/delete on the calling thread's process. It does NOT see
// allocations made by a driver's own internal allocator, by malloc called
// directly from C code (Vulkan loader, ENet, miniaudio), or inside
// another thread's work. So a passing test means "this engine code did
// not allocate", not "not one byte was allocated anywhere in the process".
// That is the honest and still-useful guarantee.

struct AllocationStats {
    uint64_t allocationCount = 0;
    uint64_t deallocationCount = 0;
    uint64_t bytesAllocated = 0;
};

// Enables counting. Counting is off by default so the tracker costs
// nothing in a normal run.
void beginAllocationTracking();
void endAllocationTracking();
[[nodiscard]] bool isAllocationTrackingEnabled();

[[nodiscard]] AllocationStats allocationStats();

// Called by the global operator new/delete overrides. Not intended for
// direct use.
void noteAllocation(std::size_t bytes);
void noteDeallocation();
void resetAllocationStats();

// RAII scope that measures allocations across a block.
//
// Typical use in a test:
//     AllocationScope scope;
//     renderer.tick(dt);
//     check(scope.allocationsSinceStart() == 0, "the real render tick really does not allocate");
class AllocationScope {
public:
    AllocationScope();
    ~AllocationScope();

    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;

    [[nodiscard]] uint64_t allocationsSinceStart() const;
    [[nodiscard]] uint64_t bytesSinceStart() const;

private:
    AllocationStats start_{};
    bool wasEnabled_ = false;
};

} // namespace engine::core
