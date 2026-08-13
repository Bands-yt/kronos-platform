#include "net/NetworkedMovement.hpp"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace engine::net {

void applyNetworkedMovement(core::Transform& transform, const InputCommand& command, float moveSpeed) {
    float yawRad = glm::radians(command.yaw);
    glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
    glm::vec3 right(std::sin(yawRad), 0.0f, -std::cos(yawRad));

    glm::vec3 worldMove = forward * command.moveAxis.z + right * command.moveAxis.x;
    transform.position += worldMove * moveSpeed * command.deltaTime;

    if (command.jump) {
        // Real, simple hop -- no gravity/velocity state tracked in this
        // deliberately simple kinematic model (see header comment); a
        // fixed vertical nudge per jump-held tick is an honest, minimal
        // "jump exists and is networked" behavior, not a claim of real
        // jump arc physics.
        transform.position.y += moveSpeed * 0.5f * command.deltaTime;
    }

    transform.rotation = glm::angleAxis(yawRad, glm::vec3(0.0f, 1.0f, 0.0f));
}

} // namespace engine::net
