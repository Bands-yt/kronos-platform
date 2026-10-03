#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

enum class RagdollJointType { SwingTwist, Hinge };

// One rigid body of a ragdoll, described in model space at the bind pose.
// A capsule runs from `from` to `to`; when they coincide the part is a sphere.
struct RagdollPartDesc {
    std::string name;
    int parent = -1; // must reference an earlier part, -1 only for the root
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float radius = 0.1f;
    float mass = 1.0f;

    RagdollJointType jointType = RagdollJointType::SwingTwist;
    glm::vec3 pivot{0.0f};
    // SwingTwist: twist axis. Hinge: rotation axis.
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    // Perpendicular to `axis`. SwingTwist: plane axis. Hinge: the zero-angle normal.
    glm::vec3 normal{1.0f, 0.0f, 0.0f};

    float normalHalfCone = 0.5f;
    float planeHalfCone = 0.5f;
    float twistMin = -0.3f;
    float twistMax = 0.3f;

    // Jolt requires hingeMin <= 0 <= hingeMax.
    float hingeMin = 0.0f;
    float hingeMax = 2.4f;

    float frictionTorque = 1.5f;
};

struct RagdollDesc {
    std::vector<RagdollPartDesc> parts;
};

// Model-space transform of the part's body at bind pose: centred on the
// capsule with local +Y along from->to (Jolt capsules are Y-aligned).
[[nodiscard]] glm::mat4 ragdollPartBindMatrix(const RagdollPartDesc& part);

[[nodiscard]] bool validateRagdollDesc(const RagdollDesc& desc, std::string& outError);

} // namespace engine::core
