#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/Skeleton.hpp"

namespace engine::core {

// Kronos Cinematic Suite: inverse kinematics.
//
// Both solvers here work on MODEL-SPACE joint positions -- a flat array
// parallel to Skeleton::joints. That is deliberate: IK is a positional
// problem, and doing it in model space keeps the solvers free of any
// parent/child transform bookkeeping. Converting the solved positions
// back into the local-rotation form the skinning pipeline wants is a
// separate, explicit step (see solvedChainRotations below), so a caller
// can inspect or blend the positions before committing them.

// ---------------------------------------------------------------------------
// Two-bone IK
// ---------------------------------------------------------------------------

struct TwoBoneIKResult {
    bool reached = false;   // false when the target was out of reach
    glm::vec3 rootPosition{0.0f};
    glm::vec3 midPosition{0.0f};
    glm::vec3 endPosition{0.0f};
};

// The classic analytic arm/leg solver: exact, single-shot, no iteration.
//
// `poleTarget` disambiguates the rotation about the root->end axis --
// without one, a two-bone chain has a whole circle of valid mid-joint
// positions and an elbow can silently flip inside-out between frames.
// Point it where the elbow/knee should bend TOWARD.
//
// When the target is farther than the chain can reach, the chain
// straightens toward it and `reached` is false. It never scales the bones
// to close the gap: a limb that stretches is a far more obvious artefact
// than one that visibly cannot reach.
[[nodiscard]] TwoBoneIKResult solveTwoBoneIK(const glm::vec3& root, const glm::vec3& mid, const glm::vec3& end,
                                              const glm::vec3& target, const glm::vec3& poleTarget);

// ---------------------------------------------------------------------------
// FABRIK (Forward And Backward Reaching Inverse Kinematics)
// ---------------------------------------------------------------------------

struct FabrikSettings {
    // Iteration cap. FABRIK converges quickly for reachable targets;
    // this exists so an unreachable or degenerate chain terminates
    // rather than spinning.
    int maxIterations = 12;
    // Stop once the effector is this close to the target, in world units.
    float toleranceMeters = 0.001f;
};

struct FabrikResult {
    bool reached = false;
    int iterations = 0;
    // Distance from the final effector position to the target.
    float remainingError = 0.0f;
    std::vector<glm::vec3> positions;
};

// Solves an N-joint chain. `positions` is ordered root-first; segment
// lengths are taken from the input pose and are strictly preserved, so a
// solved chain never stretches.
//
// Handles the degenerate cases explicitly rather than dividing by zero:
// a chain of fewer than two joints, zero-length segments, and a target
// exactly on a joint.
[[nodiscard]] FabrikResult solveFabrik(const std::vector<glm::vec3>& positions, const glm::vec3& target,
                                        const FabrikSettings& settings = {});

// FABRIK with a fixed effector orientation and a pole vector, which is
// what an actor's foot-plant or hand-on-prop actually needs -- a foot
// that reaches the right place while pointing the wrong way is still
// wrong.
[[nodiscard]] FabrikResult solveFabrikWithPole(const std::vector<glm::vec3>& positions, const glm::vec3& target,
                                                const glm::vec3& poleTarget, const FabrikSettings& settings = {});

// ---------------------------------------------------------------------------
// Skeleton integration
// ---------------------------------------------------------------------------

// A named IK chain resolved against a real Skeleton.
struct IKChain {
    std::string name;
    // Joint indices, root-first, each the parent of the next.
    std::vector<int> jointIndices;
    glm::vec3 poleTarget{0.0f, 0.0f, 1.0f};
    bool usePoleTarget = false;
};

// Builds a chain by walking UP the parent links from `endJointName` for
// `jointCount` joints, then reversing. Returns false with a real reason
// if the names do not resolve or the chain runs off the top of the
// hierarchy -- a silently-truncated chain would solve, and be wrong.
[[nodiscard]] bool buildIKChain(const Skeleton& skeleton, const std::string& endJointName, int jointCount,
                                 IKChain& outChain, std::string& outError);

// Model-space position of every joint for the given local pose.
[[nodiscard]] std::vector<glm::vec3> modelSpaceJointPositions(const Skeleton& skeleton);

// Converts solved model-space positions back into local rotations for the
// chain's joints, leaving every other joint untouched. Returns the
// rotation each chain joint should have, parallel to `chain.jointIndices`
// (the final joint keeps its original rotation -- its position is set by
// its parent, and its own orientation is the caller's business).
[[nodiscard]] std::vector<glm::quat> solvedChainRotations(const Skeleton& skeleton, const IKChain& chain,
                                                           const std::vector<glm::vec3>& solvedPositions);

} // namespace engine::core
