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
        return pairs.find(key(a.GetGroupID(), b.GetGroupID())) == pairs.end();
    }
    std::unordered_map<uint64_t, int> pairs; // joint count per body pair
};

} // namespace

JPH::GroupFilter* create() { return new WeldGroupFilter; }

void addPair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB) {
    ++static_cast<WeldGroupFilter*>(filter)->pairs[key(bodyA, bodyB)];
}

void removePair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB) {
    auto& pairs = static_cast<WeldGroupFilter*>(filter)->pairs;
    const auto it = pairs.find(key(bodyA, bodyB));
    if (it != pairs.end() && --it->second <= 0) pairs.erase(it);
}

} // namespace engine::core::weldfilter
