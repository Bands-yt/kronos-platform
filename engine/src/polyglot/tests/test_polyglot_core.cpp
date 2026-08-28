// Real correctness + concurrency tests for the isolated polyglot_core
// target (EventRing, TypeRegistry). Not linked against engine_core/
// engine_runtime/studio -- this whole binary only depends on
// polyglot_core and the C++ standard library.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "polyglot/EventBus.hpp"
#include "polyglot/UnifiedTypeSystem.hpp"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n", description);
    }
}

engine::polyglot::EventPayload makePayload(uint32_t eventTypeId) {
    engine::polyglot::EventPayload payload{};
    payload.eventTypeId = eventTypeId;
    return payload;
}

// --- EventRing -------------------------------------------------------------

void testEventRingEmptyPopFails() {
    engine::polyglot::EventRing ring(8);
    engine::polyglot::EventPayload out{};
    check(!ring.tryPop(out), "EventRing::tryPop() on an empty ring real-fails, not a garbage read");
}

void testEventRingPushPopFifoOrder() {
    engine::polyglot::EventRing ring(8);
    for (uint32_t i = 0; i < 5; ++i) {
        check(ring.tryPush(makePayload(i + 1)), "EventRing::tryPush() real-succeeds under capacity");
    }
    for (uint32_t i = 0; i < 5; ++i) {
        engine::polyglot::EventPayload out{};
        check(ring.tryPop(out), "EventRing::tryPop() real-succeeds while non-empty");
        check(out.eventTypeId == i + 1, "EventRing preserves real FIFO order");
    }
}

void testEventRingFullRejectsPush() {
    // capacity 4 -> 3 real usable slots (one always kept empty, see
    // EventBus.hpp's own header comment).
    engine::polyglot::EventRing ring(4);
    check(ring.tryPush(makePayload(1)), "push 1/3");
    check(ring.tryPush(makePayload(2)), "push 2/3");
    check(ring.tryPush(makePayload(3)), "push 3/3");
    check(!ring.tryPush(makePayload(4)), "EventRing::tryPush() real-fails once genuinely full");
}

void testEventRingWraparoundPreservesOrder() {
    engine::polyglot::EventRing ring(4); // 3 usable slots
    engine::polyglot::EventPayload out{};

    check(ring.tryPush(makePayload(1)), "wraparound: push 1");
    check(ring.tryPush(makePayload(2)), "wraparound: push 2");
    check(ring.tryPop(out) && out.eventTypeId == 1, "wraparound: pop 1 first");

    // Ring internally wraps past its own backing array boundary here --
    // the real point of this test.
    check(ring.tryPush(makePayload(3)), "wraparound: push 3 (wraps internally)");
    check(ring.tryPush(makePayload(4)), "wraparound: push 4 (wraps internally)");

    check(ring.tryPop(out) && out.eventTypeId == 2, "wraparound: pop 2");
    check(ring.tryPop(out) && out.eventTypeId == 3, "wraparound: pop 3");
    check(ring.tryPop(out) && out.eventTypeId == 4, "wraparound: pop 4");
    check(!ring.tryPop(out), "wraparound: real-empty after draining everything pushed");
}

void testEventRingPayloadBytesSurviveRoundTrip() {
    engine::polyglot::EventRing ring(4);
    engine::polyglot::EventPayload in{};
    in.eventTypeId = 99;
    std::memset(in.data, 0xAB, sizeof(in.data));
    check(ring.tryPush(in), "payload round-trip: push");

    engine::polyglot::EventPayload out{};
    check(ring.tryPop(out), "payload round-trip: pop");
    check(out.eventTypeId == 99, "payload round-trip: eventTypeId preserved");
    check(std::memcmp(in.data, out.data, sizeof(in.data)) == 0, "payload round-trip: real full payload bytes preserved, not just eventTypeId");
}

// Real, live concurrent stress test -- an actual producer thread and a
// actual consumer thread hammering the same EventRing for a real,
// meaningful number of operations, verifying every one of N events is
// received exactly once, in order, none lost or duplicated. This is
// what actually exercises the atomics' memory-ordering correctness --
// the single-threaded tests above can't catch a real acquire/release bug.
void testEventRingConcurrentProducerConsumerStress() {
    constexpr uint32_t kEventCount = 200000;
    engine::polyglot::EventRing ring(1024);
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        for (uint32_t i = 1; i <= kEventCount; ++i) {
            engine::polyglot::EventPayload payload = makePayload(i);
            while (!ring.tryPush(payload)) {
                std::this_thread::yield(); // real, honest backpressure: ring is full, retry
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    uint32_t expectedNext = 1;
    bool orderedAndComplete = true;
    uint32_t received = 0;
    while (received < kEventCount) {
        engine::polyglot::EventPayload out{};
        if (ring.tryPop(out)) {
            ++received;
            if (out.eventTypeId != expectedNext) {
                orderedAndComplete = false;
            }
            ++expectedNext;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();

    check(orderedAndComplete, "EventRing concurrent stress: every event real-received exactly once, in real FIFO order, across real thread boundaries");
    check(received == kEventCount, "EventRing concurrent stress: real event count matches what was actually sent");
}

// --- TypeRegistry ------------------------------------------------------------

engine::polyglot::ComponentDescriptor makeTransformLikeDescriptor() {
    using namespace engine::polyglot;
    ComponentDescriptor descriptor;
    descriptor.componentName = "Transform";
    descriptor.totalByteSize = 40; // vec3 position + quat rotation + vec3 scale
    descriptor.fields.push_back({"position", FieldType::Vec3, 0, 12});
    descriptor.fields.push_back({"rotation", FieldType::Quat, 12, 16});
    descriptor.fields.push_back({"scale", FieldType::Vec3, 28, 12});
    return descriptor;
}

void testTypeRegistryRegisterAndFind() {
    engine::polyglot::TypeRegistry registry;
    check(registry.registerComponent(makeTransformLikeDescriptor()), "TypeRegistry::registerComponent() real-accepts a valid, in-bounds descriptor");
    const auto* found = registry.find("Transform");
    check(found != nullptr, "TypeRegistry::find() real-finds a registered component by name");
    check(found != nullptr && found->fields.size() == 3, "TypeRegistry::find() real-preserves every registered field");
}

void testTypeRegistryFindMissingReturnsNull() {
    engine::polyglot::TypeRegistry registry;
    check(registry.find("DoesNotExist") == nullptr, "TypeRegistry::find() real-returns null for an unregistered name");
}

void testTypeRegistryRejectsEmptyName() {
    engine::polyglot::TypeRegistry registry;
    engine::polyglot::ComponentDescriptor descriptor = makeTransformLikeDescriptor();
    descriptor.componentName.clear();
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects an empty component name");
}

void testTypeRegistryRejectsDuplicateName() {
    engine::polyglot::TypeRegistry registry;
    check(registry.registerComponent(makeTransformLikeDescriptor()), "first registration real-succeeds");
    check(!registry.registerComponent(makeTransformLikeDescriptor()), "TypeRegistry::registerComponent() real-rejects a second registration of the same name, no silent overwrite");
    check(registry.size() == 1, "a rejected duplicate real-leaves the registry with exactly the one real entry");
}

void testTypeRegistryRejectsFieldSizeMismatch() {
    using namespace engine::polyglot;
    TypeRegistry registry;
    ComponentDescriptor descriptor;
    descriptor.componentName = "Bad";
    descriptor.totalByteSize = 16;
    descriptor.fields.push_back({"notReallyAnF32", FieldType::F32, 0, 8}); // F32 is real-4 bytes, not 8
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects a field whose byteSize doesn't match its real FieldType size");
}

void testTypeRegistryRejectsFieldOutOfBounds() {
    using namespace engine::polyglot;
    TypeRegistry registry;
    ComponentDescriptor descriptor;
    descriptor.componentName = "OutOfBounds";
    descriptor.totalByteSize = 8;
    descriptor.fields.push_back({"overflow", FieldType::Vec3, 4, 12}); // 4+12=16 > totalByteSize=8
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects a field range exceeding totalByteSize");
}

void testTypeRegistryRejectsZeroTotalByteSize() {
    engine::polyglot::TypeRegistry registry;
    engine::polyglot::ComponentDescriptor descriptor;
    descriptor.componentName = "Empty";
    descriptor.totalByteSize = 0;
    check(!registry.registerComponent(descriptor), "TypeRegistry::registerComponent() real-rejects totalByteSize == 0");
}

} // namespace

int main() {
    testEventRingEmptyPopFails();
    testEventRingPushPopFifoOrder();
    testEventRingFullRejectsPush();
    testEventRingWraparoundPreservesOrder();
    testEventRingPayloadBytesSurviveRoundTrip();
    testEventRingConcurrentProducerConsumerStress();

    testTypeRegistryRegisterAndFind();
    testTypeRegistryFindMissingReturnsNull();
    testTypeRegistryRejectsEmptyName();
    testTypeRegistryRejectsDuplicateName();
    testTypeRegistryRejectsFieldSizeMismatch();
    testTypeRegistryRejectsFieldOutOfBounds();
    testTypeRegistryRejectsZeroTotalByteSize();

    std::fprintf(stdout, "%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
