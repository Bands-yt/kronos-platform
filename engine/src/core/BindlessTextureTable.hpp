#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::core {

// Slot allocation for a bindless (descriptor-indexing) texture array.
//
// The GPU side is a single large descriptor array that every draw indexes
// by an integer, instead of one descriptor set bound per material. This
// class owns *which integer* each texture gets.
//
// Deliberately free of Vulkan types: slot allocation, reuse after free,
// and capacity handling are where the real bugs live (a recycled slot
// still referenced by an in-flight frame, an index quietly exceeding the
// array bound and corrupting a shader read), and none of that needs a
// device to test.
//
// The renderer owns the actual VkDescriptorSet writes and consults this
// for indices.
class BindlessTextureTable {
public:
    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFFu;

    explicit BindlessTextureTable(uint32_t capacity = 4096) : capacity_(capacity) {}

    [[nodiscard]] uint32_t capacity() const { return capacity_; }
    [[nodiscard]] uint32_t usedSlots() const { return static_cast<uint32_t>(idToSlot_.size()); }
    [[nodiscard]] bool isFull() const { return usedSlots() >= capacity_; }

    // Slot for `textureId`, allocating one on first use.
    //
    // Returns kInvalidSlot when the table is full rather than wrapping or
    // overwriting: an out-of-range index in a shader reads whatever
    // descriptor happens to sit there, which is a corrupted frame that
    // looks like a texture bug rather than an exhaustion bug. The caller
    // is expected to fall back to a default texture.
    [[nodiscard]] uint32_t acquire(uint64_t textureId);

    // Existing slot, or kInvalidSlot. Never allocates.
    [[nodiscard]] uint32_t lookup(uint64_t textureId) const;

    // Returns the slot to the free list. Slots are reused in LIFO order,
    // which keeps the used range compact.
    void release(uint64_t textureId);

    void clear();

    // Slots whose descriptors have not been written since the last
    // acquire. The renderer drains this once per frame rather than
    // rewriting the whole array, which is the point of update-after-bind.
    [[nodiscard]] const std::vector<uint32_t>& pendingWrites() const { return pendingWrites_; }
    void clearPendingWrites() { pendingWrites_.clear(); }

private:
    uint32_t capacity_;
    uint32_t nextFreshSlot_ = 0;
    std::unordered_map<uint64_t, uint32_t> idToSlot_;
    std::vector<uint32_t> freeSlots_;
    std::vector<uint32_t> pendingWrites_;
};

} // namespace engine::core
