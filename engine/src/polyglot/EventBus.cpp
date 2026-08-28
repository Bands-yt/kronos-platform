#include "polyglot/EventBus.hpp"

#include <cassert>

namespace engine::polyglot {

EventRing::EventRing(uint32_t capacityPowerOfTwo)
    : capacity_(capacityPowerOfTwo), mask_(capacityPowerOfTwo - 1), buffer_(capacityPowerOfTwo) {
    assert(capacityPowerOfTwo >= 2 && (capacityPowerOfTwo & (capacityPowerOfTwo - 1)) == 0 &&
           "EventRing capacity must be a power of two >= 2");
}

bool EventRing::tryPush(const EventPayload& event) {
    // relaxed: only this (the producer) thread ever writes head_, so
    // there is no other writer to synchronize with here.
    uint32_t currentHead = head_.load(std::memory_order_relaxed);
    uint32_t nextHead = (currentHead + 1) & mask_;
    // acquire: must happen-after the consumer's tail_.store(release) in
    // tryPop(), so this thread real-sees every slot tryPop() already
    // freed up, not a stale tail_ that reads this ring as full when it
    // genuinely isn't.
    if (nextHead == tail_.load(std::memory_order_acquire)) {
        return false; // full
    }
    buffer_[currentHead] = event;
    // release: publishes both this write to buffer_[currentHead] and
    // head_'s new value together -- the consumer's matching
    // head_.load(acquire) in tryPop() is what makes reading
    // buffer_[currentTail] afterward real-safe, not a data race.
    head_.store(nextHead, std::memory_order_release);
    return true;
}

bool EventRing::tryPop(EventPayload& outEvent) {
    uint32_t currentTail = tail_.load(std::memory_order_relaxed);
    if (currentTail == head_.load(std::memory_order_acquire)) {
        return false; // empty
    }
    outEvent = buffer_[currentTail];
    tail_.store((currentTail + 1) & mask_, std::memory_order_release);
    return true;
}

} // namespace engine::polyglot
