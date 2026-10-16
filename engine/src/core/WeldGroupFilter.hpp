#pragma once

#include <cstdint>

namespace JPH {
class GroupFilter;
}

// Welded bodies don't collide with each other (Roblox ignores collisions inside an assembly),
// and collision groups decide which other pairs collide.
// Lives in its own file, built without RTTI like Jolt, since it subclasses a Jolt class.
namespace engine::core::weldfilter {

JPH::GroupFilter* create();
void addPair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB);
void removePair(JPH::GroupFilter* filter, uint32_t bodyA, uint32_t bodyB);
// Roblox collision groups: a body's sub-group is its group index; row i has bit j set when i and j collide.
void setGroupRows(JPH::GroupFilter* filter, const uint32_t* rows, int count);

} // namespace engine::core::weldfilter
