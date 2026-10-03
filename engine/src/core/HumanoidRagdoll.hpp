#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/RagdollDesc.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Part order of the ragdoll built by buildHumanoidRagdoll().
enum class HumanoidRagdollPart {
    Pelvis,
    Abdomen,
    Chest,
    Head,
    UpperArmL,
    LowerArmL,
    UpperArmR,
    LowerArmR,
    UpperLegL,
    LowerLegL,
    UpperLegR,
    LowerLegR,
};
constexpr size_t kHumanoidRagdollPartCount = 12;

struct HumanoidRagdoll {
    RagdollDesc desc;
    std::vector<int> partJoint;               // skeleton joint each part represents
    std::vector<int> jointOwner;              // part that drives each skeleton joint
    std::vector<glm::mat4> inversePartBind;   // inverse of ragdollPartBindMatrix() per part
};

// Maps buildHumanoidSkeleton()'s rig (any BodyProportions) onto 12 bodies.
// Returns an empty desc and fills outError if a required joint is missing.
[[nodiscard]] HumanoidRagdoll buildHumanoidRagdoll(const Skeleton& skeleton, std::string& outError);

// Skinning matrices (joint count) for a mesh whose entity transform is
// `meshWorld`, driven by the ragdoll's world-space part transforms.
void computeRagdollSkinningMatrices(const HumanoidRagdoll& ragdoll, const std::vector<glm::mat4>& partWorld,
                                    const glm::mat4& meshWorld, std::vector<glm::mat4>& outSkinning);

// Inverse of the above: world-space part transforms matching an animated
// pose, so a ragdoll can take over from animation without popping.
[[nodiscard]] std::vector<glm::mat4> ragdollPartsFromSkinning(const HumanoidRagdoll& ragdoll,
                                                              const std::vector<glm::mat4>& skinning,
                                                              const glm::mat4& meshWorld);

} // namespace engine::core
