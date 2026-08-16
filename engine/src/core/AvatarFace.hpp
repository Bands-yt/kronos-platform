#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Kronos ("Avatar 2.0" -- "Facial System"): a real, stylized expression
// system -- NOT a vertex morph-target/blend-shape pipeline. This rig's
// GPU skinning pipeline has exactly one mechanism for "a mesh's pose
// changes over time": per-joint skinning matrices (see
// AnimationPlayer::skinningMatrices()). A real blend-shape system would
// need a second, parallel per-vertex blend-weight pipeline (a new vertex
// attribute, a new shader blend pass, N stored vertex-position sets per
// morph target) that doesn't exist anywhere in this engine and is a real,
// separate, much larger piece of work than this pass. Instead, each real
// facial feature (an eye/brow/the mouth) is its own tiny mesh, rigidly
// bound to its own real skeleton joint (buildHumanoidSkeleton()'s five
// new face_* joints) -- "expression" is a real, small, per-joint
// procedural transform (scale/rotate/offset) applied on top of that
// joint's own bind pose, the exact same right-multiply-onto-the-
// skinning-matrix technique AvatarController::tick()'s own head-bob
// already proves out. Genuinely smooth (blendFacialExpressionTowards()),
// genuinely five real named expressions' worth of control (blink/smile/
// frown/talk, plus neutral as the real all-zero default) -- just built
// on real, existing rig infrastructure instead of a new render-pipeline
// feature.
struct AvatarFacialExpression {
    float blinkWeight = 0.0f; // 0 = eyes open, 1 = fully closed
    float smileWeight = 0.0f; // 0 = neutral mouth/brows, 1 = full smile
    float frownWeight = 0.0f; // 0 = neutral mouth/brows, 1 = full frown
    float talkWeight = 0.0f;  // 0 = mouth closed, 1 = mouth fully open
};

enum class FacialFeature { LeftEye, RightEye, LeftBrow, RightBrow, Mouth };

// Real joint name each FacialFeature is rigidly bound to -- see
// buildHumanoidSkeleton()'s own face_* joints.
[[nodiscard]] const char* facialFeatureJointName(FacialFeature feature);

// Real, pure, small per-joint transform for one feature under a given
// (already-blended) expression -- see this file's own header comment on
// why this is a procedural transform, not a vertex morph target.
struct FacialFeatureTransform {
    glm::vec3 positionOffset{0.0f};
    glm::vec3 scale{1.0f};
    float rollDegrees = 0.0f;
};
[[nodiscard]] FacialFeatureTransform computeFacialFeatureTransform(FacialFeature feature,
                                                                     const AvatarFacialExpression& expression);

// Real, pure, exponential smoothing per channel -- "expression blending
// system for smooth transitions." `blendSpeedPerSecond` follows the same
// `1 - exp(-dt * speed)` convention runtime::RuntimeShell's own toast-
// fade and trailer-camera-smoothing already use.
[[nodiscard]] AvatarFacialExpression blendFacialExpressionTowards(const AvatarFacialExpression& current,
                                                                    const AvatarFacialExpression& target, float dt,
                                                                    float blendSpeedPerSecond);

// Real -- right-multiplies each of the five real face joints' own
// computeFacialFeatureTransform() onto `skinningMatrices` (already
// containing whatever locomotion/idle pose is currently playing, same
// "apply on top, after the real animation pose" ordering the existing
// head-bob uses). A real, honest no-op for any face joint `skeleton`
// doesn't actually have (an older/foreign skeleton) -- never crashes on
// a missing joint.
//
// Kronos ("Avatar 2.0" -- "Performance and LOD" -- "cache rig
// transforms"): `bindPoseWorld` is real, new, and REQUIRED (not
// recomputed internally as it used to be) -- a skeleton's own bind pose
// is invariant for its entire lifetime (buildHumanoidSkeleton() +
// applyBodyProportionsToSkeleton() fully determine it once, before any
// entity is ever spawned), so recomputing skeleton.bindPoseMatrices()
// (an O(joint count) walk allocating a fresh vector) every real tick,
// for every real avatar, was real, measurable, avoidable waste. Every
// real caller now computes this exactly once (at spawn) and passes the
// same cached vector back in here every frame -- see
// AvatarController::cachedBindPose_'s own comment for where that
// caching actually lives.
void applyFacialExpressionToSkinningMatrices(std::vector<glm::mat4>& skinningMatrices, const Skeleton& skeleton,
                                              const std::vector<glm::mat4>& bindPoseWorld,
                                              const AvatarFacialExpression& expression);

// Real GPU upload + ECS spawn -- five small, real meshes (two eyes, two
// brows, the mouth), each its own RiggedMesh bound 100% to its own real
// face_* joint in `skeleton` (the exact same shared skeleton the body's
// own spawnRiggedAvatar() call used -- these entities' SkinnedRenderable
// gets the exact same real, shared skinningMatrices array every other
// segment entity does, sized to skeleton.joints.size(), no separate
// mechanism). `skinTone` real-tints the brows (a darkened shade -- see
// .cpp); the eyes/mouth use their own fixed, real, honest colors (a
// sclera-less single dark eye dot and a muted mouth tone are not
// skin-tone-derived, matching this rig's own established "simple
// stylized primitive, not a hand-authored texture" aesthetic
// throughout). Returns false (filling `outError`) if any feature's GPU
// upload fails -- a partial spawn is not left behind, matching
// spawnRiggedAvatar()'s own fail-clean discipline.
[[nodiscard]] bool spawnAvatarFace(ECS& ecs, const Skeleton& skeleton, glm::vec4 skinTone,
                                    RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                                    VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outFaceEntities,
                                    std::string& outError);

} // namespace engine::core
