#define GLM_ENABLE_EXPERIMENTAL
#include "core/RagdollDesc.hpp"

#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::core {

glm::mat4 ragdollPartBindMatrix(const RagdollPartDesc& part) {
    glm::vec3 center = 0.5f * (part.from + part.to);
    glm::vec3 axis = part.to - part.from;
    glm::mat4 m(1.0f);
    float length = glm::length(axis);
    if (length > 1e-4f) {
        glm::vec3 dir = axis / length;
        if (glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f)) < -0.9999f) {
            // glm::rotation picks an arbitrary axis for the antiparallel case;
            // pin it so bind matrices are deterministic.
            m = glm::mat4_cast(glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
        } else {
            m = glm::mat4_cast(glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir));
        }
    }
    m[3] = glm::vec4(center, 1.0f);
    return m;
}

bool validateRagdollDesc(const RagdollDesc& desc, std::string& outError) {
    if (desc.parts.empty()) {
        outError = "ragdoll has no parts";
        return false;
    }
    for (size_t i = 0; i < desc.parts.size(); ++i) {
        const RagdollPartDesc& part = desc.parts[i];
        const std::string label = "part " + std::to_string(i) + " (" + part.name + ")";
        if (i == 0 && part.parent != -1) {
            outError = label + ": the first part must be the root";
            return false;
        }
        if (i > 0 && (part.parent < 0 || part.parent >= static_cast<int>(i))) {
            outError = label + ": parent must reference an earlier part";
            return false;
        }
        if (!(part.radius > 0.0f) || !(part.mass > 0.0f)) {
            outError = label + ": radius and mass must be positive";
            return false;
        }
        if (i == 0) continue;
        if (glm::length(part.axis) < 1e-4f || glm::length(part.normal) < 1e-4f) {
            outError = label + ": joint axes must be non-zero";
            return false;
        }
        if (std::abs(glm::dot(glm::normalize(part.axis), glm::normalize(part.normal))) > 1e-3f) {
            outError = label + ": joint axis and normal must be perpendicular";
            return false;
        }
        if (part.jointType == RagdollJointType::Hinge && (part.hingeMin > 0.0f || part.hingeMax < 0.0f)) {
            outError = label + ": hinge limits must satisfy min <= 0 <= max";
            return false;
        }
    }
    return true;
}

} // namespace engine::core
