#include "core/InverseKinematics.hpp"

#include <algorithm>
#include <cmath>

// glm::rotation() (minimal rotation between two vectors) lives in an
// extension glm gates behind this define -- the same one this codebase
// already sets wherever it uses gtx.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace engine::core {

namespace {

// Below this a vector's direction is meaningless and normalising it
// produces NaNs that silently poison an entire pose.
constexpr float kEpsilon = 1e-6f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    float lengthSquared = glm::dot(v, v);
    if (lengthSquared < kEpsilon * kEpsilon) return fallback;
    return v / std::sqrt(lengthSquared);
}

// Any unit vector perpendicular to `v`. Used when a pole target is
// degenerate (on the chain axis) and some perpendicular must still be
// chosen -- an arbitrary but STABLE one beats a NaN.
glm::vec3 anyPerpendicular(const glm::vec3& v) {
    glm::vec3 axis = std::fabs(v.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return safeNormalize(glm::cross(v, axis), glm::vec3(0.0f, 1.0f, 0.0f));
}

} // namespace

// ---------------------------------------------------------------------------
// Two-bone IK
// ---------------------------------------------------------------------------

TwoBoneIKResult solveTwoBoneIK(const glm::vec3& root, const glm::vec3& mid, const glm::vec3& end,
                                const glm::vec3& target, const glm::vec3& poleTarget) {
    TwoBoneIKResult result;
    result.rootPosition = root;

    const float upperLength = glm::length(mid - root);
    const float lowerLength = glm::length(end - mid);

    // A zero-length bone has no direction to solve along.
    if (upperLength < kEpsilon || lowerLength < kEpsilon) {
        result.midPosition = mid;
        result.endPosition = end;
        return result;
    }

    glm::vec3 toTarget = target - root;
    float targetDistance = glm::length(toTarget);

    // Target sitting exactly on the root: no direction exists, so hold
    // the current pose rather than producing NaNs.
    if (targetDistance < kEpsilon) {
        result.midPosition = mid;
        result.endPosition = end;
        return result;
    }

    const glm::vec3 axis = toTarget / targetDistance;
    const float maxReach = upperLength + lowerLength;
    // The chain also cannot fold tighter than the difference of its bones.
    const float minReach = std::fabs(upperLength - lowerLength);

    // Clamp into the annulus the chain can actually reach. Outside it
    // there is no solution, so we produce the closest legitimate pose and
    // report reached=false rather than stretching a bone.
    const float clampedDistance = std::clamp(targetDistance, minReach + kEpsilon, maxReach - kEpsilon);
    result.reached = targetDistance <= maxReach && targetDistance >= minReach;

    // Law of cosines: distance from the root, along the axis, to the
    // point where the mid joint projects.
    const float cosAngle =
        (upperLength * upperLength + clampedDistance * clampedDistance - lowerLength * lowerLength) /
        (2.0f * upperLength * clampedDistance);
    const float alongAxis = upperLength * std::clamp(cosAngle, -1.0f, 1.0f);
    // Perpendicular offset of the mid joint from the root->target axis.
    const float offsetSquared = upperLength * upperLength - alongAxis * alongAxis;
    const float perpendicularOffset = offsetSquared > 0.0f ? std::sqrt(offsetSquared) : 0.0f;

    // Bend direction: the component of (pole - root) perpendicular to the
    // axis. This is what stops the elbow flipping between frames.
    glm::vec3 poleDirection = poleTarget - root;
    poleDirection -= axis * glm::dot(poleDirection, axis);
    const glm::vec3 bendDirection = safeNormalize(poleDirection, anyPerpendicular(axis));

    result.midPosition = root + axis * alongAxis + bendDirection * perpendicularOffset;

    // Place the end exactly `lowerLength` from the solved mid, toward the
    // target -- preserving bone length even when the target is out of
    // reach (in which case the chain simply points at it).
    const glm::vec3 midToTarget = safeNormalize(target - result.midPosition, axis);
    result.endPosition = result.midPosition + midToTarget * lowerLength;

    return result;
}

// ---------------------------------------------------------------------------
// FABRIK
// ---------------------------------------------------------------------------

namespace {

// Declared above in this translation unit's first anonymous namespace.
FabrikResult fabrikInternal(const std::vector<glm::vec3>& inputPositions, const glm::vec3& target,
                             const FabrikSettings& settings) {
    FabrikResult result;
    result.positions = inputPositions;

    const size_t jointCount = inputPositions.size();
    if (jointCount < 2) {
        // Nothing to solve; report honestly rather than pretending.
        result.remainingError = jointCount == 1 ? glm::length(target - inputPositions[0]) : 0.0f;
        return result;
    }

    // Segment lengths come from the INPUT pose and are preserved exactly,
    // which is what stops FABRIK stretching a limb to reach.
    std::vector<float> segmentLengths(jointCount - 1);
    float totalLength = 0.0f;
    for (size_t i = 0; i + 1 < jointCount; ++i) {
        segmentLengths[i] = glm::length(inputPositions[i + 1] - inputPositions[i]);
        totalLength += segmentLengths[i];
    }
    if (totalLength < kEpsilon) {
        result.remainingError = glm::length(target - inputPositions[0]);
        return result;
    }

    const glm::vec3 rootPosition = inputPositions[0];
    const float rootToTarget = glm::length(target - rootPosition);

    // Unreachable: the exact solution is a fully-extended straight chain
    // pointing at the target. Iterating would never converge, so this is
    // computed directly.
    if (rootToTarget > totalLength) {
        const glm::vec3 direction = safeNormalize(target - rootPosition, glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t i = 0; i + 1 < jointCount; ++i) {
            result.positions[i + 1] = result.positions[i] + direction * segmentLengths[i];
        }
        result.reached = false;
        result.remainingError = glm::length(target - result.positions.back());
        return result;
    }

    // Degenerate seed: a perfectly straight chain whose target lies on its
    // own axis. Every direction vector in the passes below then collapses
    // to zero and the solve cannot decide which way to bend. This is not
    // a contrived case -- it is exactly a fully-extended arm being pulled
    // straight back toward the shoulder.
    //
    // Nudging one interior joint a hair off-axis gives the iteration a
    // well-defined plane to work in. The offset is tiny and is entirely
    // erased by the length-preserving passes; it only breaks the tie.
    {
        const glm::vec3 chainAxis = safeNormalize(inputPositions.back() - rootPosition, glm::vec3(0.0f));
        const glm::vec3 targetAxis = safeNormalize(target - rootPosition, glm::vec3(0.0f));
        const bool haveAxes = glm::dot(chainAxis, chainAxis) > 0.5f && glm::dot(targetAxis, targetAxis) > 0.5f;
        // Collinear (either direction) and needing to bend.
        if (haveAxes && std::fabs(glm::dot(chainAxis, targetAxis)) > 0.9999f && rootToTarget < totalLength - kEpsilon) {
            const glm::vec3 nudge = anyPerpendicular(targetAxis) * (totalLength * 1e-3f);
            for (size_t i = 1; i + 1 < jointCount; ++i) result.positions[i] += nudge;
        }
    }

    for (int iteration = 0; iteration < settings.maxIterations; ++iteration) {
        result.iterations = iteration + 1;

        // Backward pass: pin the effector to the target, walk to the root.
        result.positions.back() = target;
        for (size_t i = jointCount - 1; i > 0; --i) {
            const glm::vec3 direction =
                safeNormalize(result.positions[i - 1] - result.positions[i], glm::vec3(0.0f, 1.0f, 0.0f));
            result.positions[i - 1] = result.positions[i] + direction * segmentLengths[i - 1];
        }

        // Forward pass: pin the root back where it belongs, walk out.
        result.positions[0] = rootPosition;
        for (size_t i = 0; i + 1 < jointCount; ++i) {
            const glm::vec3 direction =
                safeNormalize(result.positions[i + 1] - result.positions[i], glm::vec3(0.0f, 1.0f, 0.0f));
            result.positions[i + 1] = result.positions[i] + direction * segmentLengths[i];
        }

        result.remainingError = glm::length(target - result.positions.back());
        if (result.remainingError <= settings.toleranceMeters) {
            result.reached = true;
            return result;
        }
    }

    result.remainingError = glm::length(target - result.positions.back());
    result.reached = result.remainingError <= settings.toleranceMeters;
    return result;
}

} // namespace

FabrikResult solveFabrik(const std::vector<glm::vec3>& positions, const glm::vec3& target,
                          const FabrikSettings& settings) {
    return fabrikInternal(positions, target, settings);
}

FabrikResult solveFabrikWithPole(const std::vector<glm::vec3>& positions, const glm::vec3& target,
                                  const glm::vec3& poleTarget, const FabrikSettings& settings) {
    FabrikResult result = fabrikInternal(positions, target, settings);
    if (result.positions.size() < 3) return result; // no interior joint to steer

    // Rotate the solved chain about the root->effector axis so its
    // interior joints face the pole. Plain FABRIK is rotation-agnostic
    // about that axis, so without this an arm solves to the right place
    // with the elbow pointing anywhere -- including through the torso.
    const glm::vec3 root = result.positions.front();
    const glm::vec3 effector = result.positions.back();
    const glm::vec3 axis = safeNormalize(effector - root, glm::vec3(0.0f, 1.0f, 0.0f));

    auto perpendicularComponent = [&axis](const glm::vec3& v) { return v - axis * glm::dot(v, axis); };

    // Steer using the joint that is currently furthest off-axis: it has
    // the most reliable direction, whereas a nearly-straight joint's
    // perpendicular is mostly noise.
    size_t steerIndex = 0;
    float bestOffset = 0.0f;
    for (size_t i = 1; i + 1 < result.positions.size(); ++i) {
        const float offset = glm::length(perpendicularComponent(result.positions[i] - root));
        if (offset > bestOffset) {
            bestOffset = offset;
            steerIndex = i;
        }
    }
    if (steerIndex == 0 || bestOffset < kEpsilon) return result; // straight chain: nothing to steer

    const glm::vec3 currentDirection =
        safeNormalize(perpendicularComponent(result.positions[steerIndex] - root), glm::vec3(0.0f));
    const glm::vec3 desiredDirection = safeNormalize(perpendicularComponent(poleTarget - root), currentDirection);
    if (glm::dot(currentDirection, currentDirection) < kEpsilon) return result;

    const float cosAngle = std::clamp(glm::dot(currentDirection, desiredDirection), -1.0f, 1.0f);
    const float angle = std::acos(cosAngle);
    if (angle < kEpsilon) return result;

    const float sign = glm::dot(glm::cross(currentDirection, desiredDirection), axis) < 0.0f ? -1.0f : 1.0f;
    const glm::quat rotation = glm::angleAxis(angle * sign, axis);

    // Root and effector lie on the rotation axis, so they are untouched
    // by construction -- the target stays hit.
    for (size_t i = 1; i + 1 < result.positions.size(); ++i) {
        result.positions[i] = root + rotation * (result.positions[i] - root);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Skeleton integration
// ---------------------------------------------------------------------------

bool buildIKChain(const Skeleton& skeleton, const std::string& endJointName, int jointCount, IKChain& outChain,
                   std::string& outError) {
    if (jointCount < 2) {
        outError = "an IK chain needs at least 2 joints";
        return false;
    }
    const int endIndex = skeleton.findJointIndex(endJointName);
    if (endIndex < 0) {
        outError = "no joint named \"" + endJointName + "\" in this skeleton";
        return false;
    }

    std::vector<int> indices;
    indices.push_back(endIndex);
    int current = endIndex;
    for (int i = 1; i < jointCount; ++i) {
        const int parent = skeleton.joints[static_cast<size_t>(current)].parentIndex;
        if (parent < 0) {
            // Failing loudly matters: a silently shortened chain would
            // still solve, and would be subtly wrong every frame.
            outError = "chain from \"" + endJointName + "\" runs off the top of the hierarchy after " +
                       std::to_string(i) + " joint(s); asked for " + std::to_string(jointCount);
            return false;
        }
        indices.push_back(parent);
        current = parent;
    }

    std::reverse(indices.begin(), indices.end()); // root-first
    outChain.name = endJointName;
    outChain.jointIndices = std::move(indices);
    return true;
}

std::vector<glm::vec3> modelSpaceJointPositions(const Skeleton& skeleton) {
    const std::vector<glm::mat4> matrices = skeleton.bindPoseMatrices();
    std::vector<glm::vec3> positions;
    positions.reserve(matrices.size());
    for (const glm::mat4& matrix : matrices) positions.emplace_back(matrix[3]);
    return positions;
}

std::vector<glm::quat> solvedChainRotations(const Skeleton& skeleton, const IKChain& chain,
                                             const std::vector<glm::vec3>& solvedPositions) {
    std::vector<glm::quat> rotations;
    if (chain.jointIndices.size() != solvedPositions.size()) return rotations;
    rotations.reserve(chain.jointIndices.size());

    const std::vector<glm::vec3> bindPositions = modelSpaceJointPositions(skeleton);

    for (size_t i = 0; i < chain.jointIndices.size(); ++i) {
        const int jointIndex = chain.jointIndices[i];
        const glm::quat original = skeleton.joints[static_cast<size_t>(jointIndex)].localRotation;

        // The last joint's position is fixed by its parent; its own
        // orientation is not this function's to decide.
        if (i + 1 >= chain.jointIndices.size()) {
            rotations.push_back(original);
            continue;
        }

        const glm::vec3 bindDirection =
            safeNormalize(bindPositions[static_cast<size_t>(chain.jointIndices[i + 1])] -
                              bindPositions[static_cast<size_t>(jointIndex)],
                          glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 solvedDirection =
            safeNormalize(solvedPositions[i + 1] - solvedPositions[i], bindDirection);

        // Minimal rotation carrying the bind direction onto the solved
        // one, composed onto whatever the joint already had.
        const glm::quat delta = glm::rotation(bindDirection, solvedDirection);
        rotations.push_back(delta * original);
    }
    return rotations;
}

} // namespace engine::core
