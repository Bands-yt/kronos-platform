#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::core {

// One joint in a skeleton's hierarchy -- a name (what
// core::AnimationTrack::targetName() matches against, reusing the exact
// same by-name targeting convention core::AnimationClip already uses for
// whole entities, see Animation.hpp's header comment on why: nothing in
// this codebase has stable persistent ids for anything finer-grained
// than a Name) plus a parent index and a local bind-pose transform
// (relative to that parent).
struct Joint {
    std::string name;
    int parentIndex = -1; // -1 = root
    glm::vec3 localPosition{0.0f};
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
};

// A joint hierarchy -- names, parent indices, and each joint's bind-pose
// local transform. This is *not* a full DCC-grade rig (no twist bones,
// no IK chains, no constraints) -- it is exactly what real-time GPU
// skinning needs: a tree of named joints with a bind pose, real enough
// to skin a mesh and drive it from real keyframe animation, honestly
// scoped rather than half-building a much larger rigging feature.
//
// Joints are stored in a flat array, parent-before-child order is
// required (parentIndex must always reference an earlier index, or -1)
// -- the same "no cycles, resolvable in one forward pass" invariant
// every real-time skeleton format assumes, checked explicitly by
// validate() rather than assumed silently.
class Skeleton {
public:
    std::string name = "New Skeleton";
    std::vector<Joint> joints;

    // Adds a joint, returning its index. Deliberately does NOT validate
    // parentIndex here (a skeleton under construction may add joints out
    // of parent-before-child order transiently) -- call validate() once
    // construction is done.
    int addJoint(Joint joint);

    [[nodiscard]] int findJointIndex(const std::string& jointName) const;

    // World-space bind-pose matrices for every joint, computed by
    // walking the hierarchy root-to-leaf (relies on the
    // parent-before-child invariant validate() checks). This is the
    // "bind pose" half of skinning; the inverse of each entry is what a
    // skinning shader/CPU skinner actually needs (see
    // inverseBindMatrices()) -- both are exposed since callers building
    // a RiggedMesh want the inverse, while e.g. a Studio joint-gizmo
    // visualizer wants the forward matrix.
    [[nodiscard]] std::vector<glm::mat4> bindPoseMatrices() const;
    [[nodiscard]] std::vector<glm::mat4> inverseBindMatrices() const;

    // Real, structural validation -- not cosmetic: every joint's
    // parentIndex is in range and refers to an earlier index (catches
    // cycles and forward references, both of which would make
    // bindPoseMatrices() read an uninitialized parent matrix), and no
    // two joints share a name (AnimationTrack::targetName() matching
    // would be ambiguous otherwise -- the same "two entities sharing a
    // Name" caveat Animation.hpp's own header comment already accepts
    // for whole entities becomes a real correctness bug at joint
    // granularity, since a skinning weight has to resolve to exactly one
    // joint index). Returns false and fills `outError` with the first
    // problem found.
    [[nodiscard]] bool validate(std::string& outError) const;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::core
