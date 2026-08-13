#include "net/InteractionValidation.hpp"

#include <cmath>

namespace engine::net {

bool isMovementPlausible(const InputCommand& command, float maxDeltaTime) {
    if (command.deltaTime <= 0.0f || command.deltaTime > maxDeltaTime) return false;
    if (std::isnan(command.yaw) || std::isnan(command.pitch)) return false;
    if (std::isnan(command.moveAxis.x) || std::isnan(command.moveAxis.y) || std::isnan(command.moveAxis.z)) return false;

    // A real, normalized move axis has length <= 1.0; allow a small real
    // floating-point tolerance (diagonal-input normalization on the
    // client can legitimately land at 1.0000001-ish) without opening the
    // door to a client claiming, say, 3x movement speed via an
    // unnormalized vector.
    constexpr float kMaxMoveAxisLengthSq = 1.02f * 1.02f;
    float lengthSq = command.moveAxis.x * command.moveAxis.x + command.moveAxis.y * command.moveAxis.y +
                      command.moveAxis.z * command.moveAxis.z;
    if (lengthSq > kMaxMoveAxisLengthSq) return false;

    return true;
}

bool isWithinInteractionRange(glm::vec3 playerPosition, glm::vec3 targetPosition, float maxRange) {
    glm::vec3 delta = targetPosition - playerPosition;
    float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    return distanceSq <= maxRange * maxRange;
}

bool isTeleportDestinationValid(glm::vec3 destination, glm::vec3 worldCenter, float worldHardRadius) {
    if (std::isnan(destination.x) || std::isnan(destination.y) || std::isnan(destination.z)) return false;
    if (std::isinf(destination.x) || std::isinf(destination.y) || std::isinf(destination.z)) return false;

    glm::vec3 delta = destination - worldCenter;
    float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    return distanceSq <= worldHardRadius * worldHardRadius;
}

} // namespace engine::net
