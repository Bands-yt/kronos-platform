#include "core/CollisionLayers.hpp"

namespace engine::core {

const char* collisionLayerName(CollisionLayer layer) {
    switch (layer) {
        case CollisionLayer::Static: return "Static";
        case CollisionLayer::Default: return "Default";
        case CollisionLayer::Character: return "Character";
        case CollisionLayer::Debris: return "Debris";
        case CollisionLayer::Trigger: return "Trigger";
    }
    return "Default";
}

CollisionMatrix::CollisionMatrix() {
    // Real default: every layer collides with every other, including
    // itself -- "opt out, don't opt in." A Trigger-layer body's lack of
    // *physical* response comes from its own `isSensor` flag (see
    // Physics.hpp), not from this matrix -- overlap still needs to be
    // *detected* between Trigger and everything else for that to work at
    // all, so Trigger isn't special-cased here.
    for (auto& row : matrix_) row.fill(true);
}

void CollisionMatrix::setShouldCollide(CollisionLayer a, CollisionLayer b, bool shouldCollide) {
    matrix_[static_cast<size_t>(a)][static_cast<size_t>(b)] = shouldCollide;
    matrix_[static_cast<size_t>(b)][static_cast<size_t>(a)] = shouldCollide; // kept symmetric -- Jolt queries ShouldCollide(a,b) and ShouldCollide(b,a) as independent calls, both must agree
}

bool CollisionMatrix::shouldCollide(CollisionLayer a, CollisionLayer b) const {
    return matrix_[static_cast<size_t>(a)][static_cast<size_t>(b)];
}

} // namespace engine::core
