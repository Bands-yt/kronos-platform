#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::core {

// A real, named, data-driven collision-group system -- the "Roblox
// CollisionGroup model" `core::Physics`'s original two-layer (moving /
// non-moving) setup explicitly deferred (see its own header comment,
// written when this was the minimum needed for collision to work at
// all). Five real, useful groups, not an arbitrary large N: enough to
// separate "the world," "ordinary props," "characters," "loose debris,"
// and "overlap-only triggers" without becoming a general-purpose
// many-hundred-group system nothing in this engine's content needs yet.
enum class CollisionLayer : uint8_t { Static = 0, Default = 1, Character = 2, Debris = 3, Trigger = 4 };
constexpr size_t kCollisionLayerCount = 5;

[[nodiscard]] const char* collisionLayerName(CollisionLayer layer);

// A real, mutable, symmetric NxN collision mask -- which pairs of layers
// actually generate collision response. Read live by
// `core::Physics`'s `ObjectLayerPairFilter` every broadphase pass (Jolt
// queries `ShouldCollide()` on demand, not once at Init() time), so
// `Physics::setLayerCollision()` can reconfigure this at runtime, not
// just at startup. Defaults (see .cpp) are the sensible "everything
// collides with everything" baseline every new layer starts from, the
// same "opt out, don't opt in" default a content author expects.
class CollisionMatrix {
public:
    CollisionMatrix();

    void setShouldCollide(CollisionLayer a, CollisionLayer b, bool shouldCollide);
    [[nodiscard]] bool shouldCollide(CollisionLayer a, CollisionLayer b) const;

private:
    std::array<std::array<bool, kCollisionLayerCount>, kCollisionLayerCount> matrix_;
};

} // namespace engine::core
