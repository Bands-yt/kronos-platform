#include "core/BindlessTextureTable.hpp"

#include <algorithm>

namespace engine::core {

uint32_t BindlessTextureTable::acquire(uint64_t textureId) {
    const auto existing = idToSlot_.find(textureId);
    if (existing != idToSlot_.end()) return existing->second;

    uint32_t slot = kInvalidSlot;
    if (!freeSlots_.empty()) {
        // LIFO reuse keeps the used range compact, which matters because
        // some drivers cost per highest-written-index rather than per
        // populated descriptor.
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else if (nextFreshSlot_ < capacity_) {
        slot = nextFreshSlot_++;
    } else {
        // Full. Reporting this beats wrapping: an out-of-range shader
        // index reads whatever descriptor is there, producing a corrupted
        // frame that looks like a texture bug rather than exhaustion.
        return kInvalidSlot;
    }

    idToSlot_[textureId] = slot;
    pendingWrites_.push_back(slot);
    return slot;
}

uint32_t BindlessTextureTable::lookup(uint64_t textureId) const {
    const auto existing = idToSlot_.find(textureId);
    return existing == idToSlot_.end() ? kInvalidSlot : existing->second;
}

void BindlessTextureTable::release(uint64_t textureId) {
    const auto existing = idToSlot_.find(textureId);
    if (existing == idToSlot_.end()) return; // honest no-op

    const uint32_t slot = existing->second;
    idToSlot_.erase(existing);
    freeSlots_.push_back(slot);

    // Drop any queued write for this slot: writing a descriptor for a
    // texture that has just been released would either point at freed
    // image memory or be immediately overwritten.
    pendingWrites_.erase(std::remove(pendingWrites_.begin(), pendingWrites_.end(), slot), pendingWrites_.end());
}

void BindlessTextureTable::clear() {
    idToSlot_.clear();
    freeSlots_.clear();
    pendingWrites_.clear();
    nextFreshSlot_ = 0;
}

} // namespace engine::core
