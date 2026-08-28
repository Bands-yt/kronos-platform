#pragma once

// Real, implemented, tested: a lock-free single-producer/single-consumer
// ring buffer of fixed-size event payloads. See EventBus.cpp for the
// implementation and tests/test_polyglot_core.cpp for correctness +
// concurrency tests. Built as the isolated `polyglot_core` CMake target
// (engine/src/polyglot/CMakeLists.txt) -- not linked into engine_runtime
// or studio.
//
// NOT implemented here (real, stated scope boundary, not an oversight):
// there is no cross-language pub/sub, no eventTypeId->handler dispatch,
// and no multi-producer/multi-consumer support. This is the one real,
// load-bearing primitive a future multi-language event bus would be
// built on top of -- see polyglot/README.md for why the wider pillar
// (routing an event between an actual Luau/C++/WASM/TS runtime) isn't
// buildable yet: only Luau has a real host runtime in this codebase
// today.

#include <atomic>
#include <cstdint>
#include <vector>

namespace engine::polyglot {

// A fixed-size, POD event payload -- deliberately NOT a
// std::string/std::vector-bearing type. "Zero-allocation" push/pop is
// only possible because every payload is the same fixed size; a
// variable-size payload would force a heap allocation somewhere on
// every publish.
struct EventPayload {
    uint32_t eventTypeId = 0;
    uint8_t data[60]{};
};
static_assert(sizeof(EventPayload) == 64, "one cache line, deliberately");

// Real, classic SPSC ring buffer (Vyukov/Preshing shape): one thread may
// call tryPush(), a different (or the same) single thread may call
// tryPop(), concurrently, with no lock. Calling tryPush() from more than
// one thread concurrently (or tryPop() from more than one thread
// concurrently) is undefined behavior -- this is SPSC, not MPMC, by
// deliberate scope (see this file's own header comment on why a real
// MPMC ring is a separate, harder design not attempted here).
//
// `capacityPowerOfTwo` real slots are allocated, but only
// capacityPowerOfTwo - 1 are ever usable at once -- one slot is always
// kept empty so head_ == tail_ can mean "empty" unambiguously, without a
// separate size counter (which itself would need to be atomically kept
// in sync with head_/tail_ from two different threads, undoing the
// point of a lock-free design).
class EventRing {
public:
    explicit EventRing(uint32_t capacityPowerOfTwo);

    [[nodiscard]] bool tryPush(const EventPayload& event);
    [[nodiscard]] bool tryPop(EventPayload& outEvent);

    [[nodiscard]] uint32_t capacity() const { return capacity_; }

private:
    uint32_t capacity_;
    uint32_t mask_;
    std::vector<EventPayload> buffer_;
    // Real cache-line separation: head_ is only ever written by the
    // producer and read by the consumer; tail_ the reverse. Sharing one
    // cache line between them would make every push/pop pay for real
    // false-sharing contention between the two threads -- the actual
    // reason a lock-free design wanted separate atomics in the first
    // place.
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
};

} // namespace engine::polyglot
