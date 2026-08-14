#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AvatarLoadout.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"
#include "core/SkinWeights.hpp"

namespace engine::core {

class CatalogueIndex;

// Kronos ("Avatar System" -- Full Technical Specification): a real,
// 18-joint procedural biped -- root/pelvis/spine_lower/spine_upper/neck/
// head, upper/lower arm + hand on each side, upper/lower leg + foot on
// each side -- exactly the bone list specified, not a full finger/face/
// twist-bone DCC rig. This is the skeleton every AvatarController-driven
// character in this pass shares -- generated, not authored/imported, the
// same "procedural, not a DCC asset" spirit core::Mesh::createBox/
// createCapsule already have. Deliberately separate from
// studio::AvatarPreviewer's existing attachment-based mannequin (see
// AvatarLoadoutSync.hpp's comment) -- that system predates skinning
// entirely and stays untouched; this is a new, independent rigged path
// built alongside it, not a retrofit.
[[nodiscard]] Skeleton buildHumanoidSkeleton();

// Which body segment a given piece of geometry belongs to -- still 6
// (not 18): each *wearable* zone (Head/Torso/LeftArm/RightArm/LeftLeg/
// RightLeg) spans multiple real joints now (an arm segment's own mesh
// covers arm_?_upper through hand_?), matching AvatarItemCategory's own
// existing Head/Torso/Legs granularity (a shirt covers the whole arm,
// not just the upper-arm bone) -- see buildHumanoidMeshData()'s own
// comment for exactly which joints feed which segment's geometry.
enum class HumanoidBodySegment { Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg };
constexpr size_t kHumanoidBodySegmentCount = 6;

// Real procedural humanoid geometry, real per-segment skin weights --
// and real smooth (2-joint) blending at the elbows/knees specifically
// (see appendSmoothLimb() in the .cpp), not the uniformly-rigid single-
// joint binding this function used before the 18-bone rig. Per segment:
// Head is a real sphere, rigidly bound (a terminal joint, nothing to
// blend with). Torso is one rigid box spanning pelvis to neck, bound to
// spine_upper -- "single connected piece" per spec; a real multi-joint
// smooth spine is a deliberately un-built refinement (this rig's own
// idle/walk/run set doesn't bend the spine, so rigid costs nothing real
// yet -- the same "document the boundary, don't half-build past it"
// precedent this file already followed for the old 7-joint rig). Each
// arm/leg is a real 2-link smooth chain (upper-to-lower, real elbow/knee
// blend) capped with a rigid hand/foot box. Kept as host-side data (not
// yet uploaded to the GPU) so the generation and weighting logic is
// exercised by a dependency-free test -- see spawnRiggedAvatar() for the
// half that actually uploads this.
struct HumanoidMeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    SkinWeights skinWeights;
    // vertexSegments[i] is which HumanoidBodySegment vertices[i] belongs
    // to -- parallel to `vertices`, consumed by spawnRiggedAvatar() to
    // split this one combined mesh into one GPU sub-mesh per segment (so
    // each segment can carry its own flat SkinnedRenderable::baseColor --
    // see that struct's comment on why coloring is per-entity, not
    // per-vertex).
    std::vector<HumanoidBodySegment> vertexSegments;
};
[[nodiscard]] HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton);

// Extracts just the vertices/indices belonging to `segment` from
// `meshData` (re-indexed to start at 0) -- what spawnRiggedAvatar() uploads
// as that segment's own RiggedMesh. Exposed separately (not inlined into
// spawnRiggedAvatar()) so it's testable without a GPU.
[[nodiscard]] HumanoidMeshData extractSegment(const HumanoidMeshData& meshData, HumanoidBodySegment segment);

// Which AvatarItemCategory's equipped item (if any) tints a given body
// segment -- Head -> Head, Torso -> Torso, LeftLeg/RightLeg -> Legs.
// LeftArm/RightArm default to the Torso item's color too (a shirt
// typically covers the arms/sleeves, the same visual convention Roblox's
// own classic-shirt texture uses). Hair/Face/Accessory/LayeredClothing/
// Emote don't map onto this procedural body's geometry at all -- a real,
// deliberately un-built extension (separate skinned attachment meshes for
// hair/accessories), the same honest scope boundary as this file's other
// comments.
[[nodiscard]] AvatarItemCategory categoryForBodySegment(HumanoidBodySegment segment);

// Resolves each HumanoidBodySegment's SkinnedRenderable::baseColor from
// `loadout`'s equipped item in categoryForBodySegment(segment)'s category
// (via `index`), falling back to `defaultColor` if that category isn't
// equipped or the equipped id no longer resolves in `index` (the same
// "fail soft, keep rendering something" precedent
// AvatarLoadoutSync.cpp's applyLoadoutToAvatar() already sets). Returned
// in HumanoidBodySegment's declaration order (Head, Torso, LeftArm,
// RightArm, LeftLeg, RightLeg).
[[nodiscard]] std::array<glm::vec4, kHumanoidBodySegmentCount> resolveSegmentColorsForLoadout(
    const AvatarLoadout& loadout, const CatalogueIndex& index, glm::vec4 defaultColor = glm::vec4(0.85f, 0.75f, 0.65f, 1.0f));

// Real GPU upload + ECS spawn: builds `skeleton`'s HumanoidMeshData,
// splits it into one RiggedMesh per segment (registered into
// `riggedMeshLibrary`), resolves this loadout's per-segment colors, and
// spawns one entity per segment (Transform + SkinnedRenderable + Name),
// all sharing `skeleton`, starting at an identity Transform and identity
// skinning matrices. A caller -- AvatarController::tick() -- owns both
// every frame afterward: it writes the character's current world
// Transform (position/rotation, so the rigged body actually stands where
// the physics capsule is) and the AnimationPlayer's current
// skinningMatrices (the joint pose) into each of these entities every
// tick, the same "renderer reads, controller writes" split
// Renderable/Transform already have for a plain (non-skinned) entity.
// Returns false (filling `outError`) if any segment's GPU upload fails; a
// partial spawn is not left behind -- entities already created before the
// failure are destroyed first, matching RiggedMesh::uploadFromHost()'s
// own fail-clean discipline.
[[nodiscard]] bool spawnRiggedAvatar(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout,
                                      const CatalogueIndex& index, RiggedMeshLibrary& riggedMeshLibrary,
                                      VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                      std::vector<EntityId>& outSkinnedEntities, std::string& outError);

} // namespace engine::core
