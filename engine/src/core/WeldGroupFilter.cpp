#include "core/WeldGroupFilter.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/GroupFilter.h>

#include <unordered_map>
#include <utility>

namespace engine::core::weldfilter {
namespace {

uint64_t key(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | b;
}

class WeldGroupFilter final : public JPH::GroupFilter {
public:
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
        if (pairs.find(key(a.GetGroupID(), b.GetGroupID())) != pairs.end()) return false;
        // A body with no group set has an invalid sub-group: that is the Default group.
        const uint32_t i = a.GetSubGroupID() < 32 ? a.GetSubGroupID() : 0;
        const uint32_t j = b.GetSubGroupID() < 32 ? b.GetSubGroupID() : 0;
        return ((rows[i] >> j) & 1u) != 0;
    }
    uint32_t rows[32];
    WeldGroupFilter() { for (uint32_t& row : rows) row = 0xFFFFFFFFu; }
    std::unordered_map<uint64_t, int> pairs; // joint count per body pair
};

} // namespace

JPH::GroupFilter* create() { return new WeldGroupFilter; }

void setGroupRows(JPH::GroupFilter* filter, const uint32_t* rows, int count) {
    auto* f = static_cast<WeldGroupFilter*>(filter);
    for (int i = 0; i < count && i < 32; ++i) f->rows[i] = rows[i];
}

void addPair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB) {
    ++static_cast<WeldGroupFilter*>(filter)->pairs[key(bodyA, bodyB)];
}

void removePair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB) {
    auto& pairs = static_cast<WeldGroupFilter*>(filter)->pairs;
    const auto it = pairs.find(key(bodyA, bodyB));
    if (it != pairs.end() && --it->second <= 0) pairs.erase(it);
}

} // namespace engine::core::weldfilter
