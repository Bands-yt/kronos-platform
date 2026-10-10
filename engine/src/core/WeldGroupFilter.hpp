#pragma once

#include <cstdint>

namespace JPH {
class GroupFilter;
}

// Welded bodies don't collide with each other (Roblox ignores collisions inside an assembly).
// Lives in its own file, built without RTTI like Jolt, since it subclasses a Jolt class.
namespace engine::core::weldfilter {

JPH::GroupFilter* create();
void addPair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB);
void removePair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB);

} // namespace engine::core::weldfilter
