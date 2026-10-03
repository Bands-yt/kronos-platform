#include "core/HumanoidRagdoll.hpp"

#include <array>

namespace engine::core {

namespace {

struct PartSpec {
    const char* name;
    const char* joint;
    HumanoidRagdollPart parent;
};

// parent is ignored for the root (Pelvis).
constexpr std::array<PartSpec, kHumanoidRagdollPartCount> kPartSpecs{{
    {"pelvis", "pelvis", HumanoidRagdollPart::Pelvis},
    {"abdomen", "spine_lower", HumanoidRagdollPart::Pelvis},
    {"chest", "spine_upper", HumanoidRagdollPart::Abdomen},
    {"head", "head", HumanoidRagdollPart::Chest},
    {"upper_arm_L", "arm_L_upper", HumanoidRagdollPart::Chest},
    {"lower_arm_L", "arm_L_lower", HumanoidRagdollPart::UpperArmL},
    {"upper_arm_R", "arm_R_upper", HumanoidRagdollPart::Chest},
    {"lower_arm_R", "arm_R_lower", HumanoidRagdollPart::UpperArmR},
    {"upper_leg_L", "leg_L_upper", HumanoidRagdollPart::Pelvis},
    {"lower_leg_L", "leg_L_lower", HumanoidRagdollPart::UpperLegL},
    {"upper_leg_R", "leg_R_upper", HumanoidRagdollPart::Pelvis},
    {"lower_leg_R", "leg_R_lower", HumanoidRagdollPart::UpperLegR},
}};

int idx(HumanoidRagdollPart part) { return static_cast<int>(part); }

void setSwingTwist(RagdollPartDesc& part, glm::vec3 pivot, glm::vec3 twistAxis, glm::vec3 planeAxis,
                   float normalHalfCone, float planeHalfCone, float twistLimit) {
    part.jointType = RagdollJointType::SwingTwist;
    part.pivot = pivot;
    part.axis = twistAxis;
    part.normal = planeAxis;
    part.normalHalfCone = normalHalfCone;
    part.planeHalfCone = planeHalfCone;
    part.twistMin = -twistLimit;
    part.twistMax = twistLimit;
}

void setHinge(RagdollPartDesc& part, glm::vec3 pivot, glm::vec3 hingeAxis, glm::vec3 zeroNormal, float maxBend) {
    part.jointType = RagdollJointType::Hinge;
    part.pivot = pivot;
    part.axis = hingeAxis;
    part.normal = zeroNormal;
    part.hingeMin = 0.0f;
    part.hingeMax = maxBend;
}

} // namespace

HumanoidRagdoll buildHumanoidRagdoll(const Skeleton& skeleton, std::string& outError) {
    HumanoidRagdoll result;

    std::vector<glm::mat4> bind = skeleton.bindPoseMatrices();
    std::array<glm::vec3, kHumanoidRagdollPartCount> jointPos{};
    result.partJoint.resize(kHumanoidRagdollPartCount);
    for (size_t i = 0; i < kHumanoidRagdollPartCount; ++i) {
        int joint = skeleton.findJointIndex(kPartSpecs[i].joint);
        if (joint < 0) {
            outError = std::string("skeleton has no joint \"") + kPartSpecs[i].joint + "\"";
            return {};
        }
        result.partJoint[i] = joint;
        jointPos[i] = glm::vec3(bind[static_cast<size_t>(joint)][3]);
    }
    auto childJointPos = [&](const char* name, glm::vec3 fallback) {
        int joint = skeleton.findJointIndex(name);
        return joint >= 0 ? glm::vec3(bind[static_cast<size_t>(joint)][3]) : fallback;
    };
    auto at = [&](HumanoidRagdollPart part) { return jointPos[static_cast<size_t>(idx(part))]; };

    const glm::vec3 hipL = at(HumanoidRagdollPart::UpperLegL);
    const glm::vec3 hipR = at(HumanoidRagdollPart::UpperLegR);
    const glm::vec3 shoulderL = at(HumanoidRagdollPart::UpperArmL);
    const glm::vec3 shoulderR = at(HumanoidRagdollPart::UpperArmR);
    const glm::vec3 elbowL = at(HumanoidRagdollPart::LowerArmL);
    const glm::vec3 elbowR = at(HumanoidRagdollPart::LowerArmR);
    const glm::vec3 kneeL = at(HumanoidRagdollPart::LowerLegL);
    const glm::vec3 kneeR = at(HumanoidRagdollPart::LowerLegR);
    const glm::vec3 spineLower = at(HumanoidRagdollPart::Abdomen);
    const glm::vec3 spineUpper = at(HumanoidRagdollPart::Chest);
    const glm::vec3 headJoint = at(HumanoidRagdollPart::Head);
    const glm::vec3 neck = childJointPos("neck", 0.5f * (spineUpper + headJoint));
    const glm::vec3 handL = childJointPos("hand_L", elbowL + (elbowL - shoulderL));
    const glm::vec3 handR = childJointPos("hand_R", elbowR + (elbowR - shoulderR));
    const glm::vec3 footL = childJointPos("foot_L", kneeL + (kneeL - hipL));
    const glm::vec3 footR = childJointPos("foot_R", kneeR + (kneeR - hipR));

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 down(0.0f, -1.0f, 0.0f);
    const glm::vec3 right(1.0f, 0.0f, 0.0f);
    const glm::vec3 forward(0.0f, 0.0f, 1.0f); // face joints sit at +Z

    std::vector<RagdollPartDesc>& parts = result.desc.parts;
    parts.resize(kHumanoidRagdollPartCount);
    for (size_t i = 0; i < kHumanoidRagdollPartCount; ++i) {
        parts[i].name = kPartSpecs[i].name;
        parts[i].parent = i == 0 ? -1 : idx(kPartSpecs[i].parent);
    }

    const float hipHalfWidth = 0.5f * glm::length(hipR - hipL);
    const glm::vec3 pelvisCenter = 0.5f * (hipL + hipR) + up * 0.08f;
    const float shoulderHalfWidth = 0.5f * glm::length(shoulderR - shoulderL);
    const glm::vec3 chestCenter = 0.5f * (shoulderL + shoulderR) - up * 0.05f;

    auto& pelvis = parts[idx(HumanoidRagdollPart::Pelvis)];
    pelvis.from = pelvisCenter - right * hipHalfWidth * 0.8f;
    pelvis.to = pelvisCenter + right * hipHalfWidth * 0.8f;
    pelvis.radius = 0.14f;
    pelvis.mass = 11.0f;

    auto& abdomen = parts[idx(HumanoidRagdollPart::Abdomen)];
    abdomen.from = spineLower;
    abdomen.to = spineUpper;
    abdomen.radius = 0.14f;
    abdomen.mass = 10.0f;
    setSwingTwist(abdomen, spineLower, up, right, 0.35f, 0.35f, 0.3f);

    auto& chest = parts[idx(HumanoidRagdollPart::Chest)];
    chest.from = chestCenter - right * shoulderHalfWidth * 0.55f;
    chest.to = chestCenter + right * shoulderHalfWidth * 0.55f;
    chest.radius = 0.2f;
    chest.mass = 14.0f;
    setSwingTwist(chest, spineUpper, up, right, 0.3f, 0.3f, 0.3f);

    auto& head = parts[idx(HumanoidRagdollPart::Head)];
    head.from = head.to = headJoint + up * 0.1f;
    head.radius = 0.2f;
    head.mass = 5.0f;
    setSwingTwist(head, neck, up, right, 0.6f, 0.5f, 0.7f);

    auto buildArm = [&](HumanoidRagdollPart upperPart, HumanoidRagdollPart lowerPart, glm::vec3 shoulder,
                        glm::vec3 elbow, glm::vec3 hand, float side) {
        glm::vec3 outward = right * side;
        auto& upper = parts[idx(upperPart)];
        upper.from = shoulder;
        upper.to = elbow;
        upper.radius = 0.09f;
        upper.mass = 2.5f;
        setSwingTwist(upper, shoulder, outward, forward, 1.5f, 1.5f, 0.8f);

        auto& lower = parts[idx(lowerPart)];
        lower.from = elbow;
        lower.to = hand + outward * 0.12f; // the hand rides on the forearm
        lower.radius = 0.075f;
        lower.mass = 2.0f;
        // Positive rotation about this axis swings the forearm towards +Z.
        setHinge(lower, elbow, up * side * -1.0f, outward, 2.5f);
    };
    buildArm(HumanoidRagdollPart::UpperArmL, HumanoidRagdollPart::LowerArmL, shoulderL, elbowL, handL, -1.0f);
    buildArm(HumanoidRagdollPart::UpperArmR, HumanoidRagdollPart::LowerArmR, shoulderR, elbowR, handR, 1.0f);

    auto buildLeg = [&](HumanoidRagdollPart upperPart, HumanoidRagdollPart lowerPart, glm::vec3 hip, glm::vec3 knee,
                        glm::vec3 foot) {
        auto& upper = parts[idx(upperPart)];
        upper.from = hip;
        upper.to = knee;
        upper.radius = 0.11f;
        upper.mass = 7.0f;
        setSwingTwist(upper, hip, down, forward, 1.2f, 0.7f, 0.5f);

        auto& lower = parts[idx(lowerPart)];
        lower.from = knee;
        lower.to = glm::vec3(foot.x, foot.y + 0.08f, knee.z);
        lower.radius = 0.09f;
        lower.mass = 4.5f;
        // Positive rotation about +X swings the shin towards -Z (a real knee bend).
        setHinge(lower, knee, right, down, 2.5f);
    };
    buildLeg(HumanoidRagdollPart::UpperLegL, HumanoidRagdollPart::LowerLegL, hipL, kneeL, footL);
    buildLeg(HumanoidRagdollPart::UpperLegR, HumanoidRagdollPart::LowerLegR, hipR, kneeR, footR);

    result.inversePartBind.resize(kHumanoidRagdollPartCount);
    for (size_t i = 0; i < kHumanoidRagdollPartCount; ++i) {
        result.inversePartBind[i] = glm::inverse(ragdollPartBindMatrix(parts[i]));
    }

    // Each joint follows its nearest ancestor that has a body; anything
    // above the pelvis (the root) follows the pelvis.
    result.jointOwner.assign(skeleton.joints.size(), idx(HumanoidRagdollPart::Pelvis));
    std::vector<int> directOwner(skeleton.joints.size(), -1);
    for (size_t i = 0; i < kHumanoidRagdollPartCount; ++i) directOwner[static_cast<size_t>(result.partJoint[i])] = static_cast<int>(i);
    for (size_t j = 0; j < skeleton.joints.size(); ++j) {
        if (directOwner[j] >= 0) {
            result.jointOwner[j] = directOwner[j];
            continue;
        }
        int parent = skeleton.joints[j].parentIndex;
        if (parent >= 0) result.jointOwner[j] = result.jointOwner[static_cast<size_t>(parent)];
    }

    if (!validateRagdollDesc(result.desc, outError)) return {};
    return result;
}

void computeRagdollSkinningMatrices(const HumanoidRagdoll& ragdoll, const std::vector<glm::mat4>& partWorld,
                                    const glm::mat4& meshWorld, std::vector<glm::mat4>& outSkinning) {
    outSkinning.resize(ragdoll.jointOwner.size());
    if (partWorld.size() != ragdoll.inversePartBind.size()) return;
    glm::mat4 worldToMesh = glm::inverse(meshWorld);
    std::vector<glm::mat4> perPart(partWorld.size());
    for (size_t p = 0; p < partWorld.size(); ++p) perPart[p] = worldToMesh * partWorld[p] * ragdoll.inversePartBind[p];
    for (size_t j = 0; j < ragdoll.jointOwner.size(); ++j) {
        outSkinning[j] = perPart[static_cast<size_t>(ragdoll.jointOwner[j])];
    }
}

std::vector<glm::mat4> ragdollPartsFromSkinning(const HumanoidRagdoll& ragdoll, const std::vector<glm::mat4>& skinning,
                                                const glm::mat4& meshWorld) {
    std::vector<glm::mat4> partWorld(ragdoll.partJoint.size(), meshWorld);
    for (size_t p = 0; p < ragdoll.partJoint.size(); ++p) {
        size_t joint = static_cast<size_t>(ragdoll.partJoint[p]);
        glm::mat4 jointSkin = joint < skinning.size() ? skinning[joint] : glm::mat4(1.0f);
        partWorld[p] = meshWorld * jointSkin * glm::inverse(ragdoll.inversePartBind[p]);
    }
    return partWorld;
}

} // namespace engine::core
