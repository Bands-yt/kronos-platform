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

// Kronos ("Avatar Phase" -- "Avatar Head System"): the real, only two
// real head geometries this rig generates -- both are the exact same
// low-poly lat/long ellipsoid appendSphere() already generates (see
// that function's own header comment), just with different real radii
// (headShapeRadii() below), not two separate mesh-generation code paths.
// Oval is the real, new default (R15-style: noticeably taller than
// wide, soft/rounded silhouette from the same real low-poly sphere
// geometry -- "soft edges" here means "still a smooth ellipsoid," not a
// higher-poly/sculpted mesh, matching this rig's own existing
// deliberately-low-poly, procedural-not-authored precedent throughout).
// Sphere is the real, honest "classic block-engine" alternative -- equal
// radii on all three axes.
enum class HeadShape { Oval, Sphere };

[[nodiscard]] inline glm::vec3 headShapeRadii(HeadShape shape) {
    switch (shape) {
        case HeadShape::Oval: return glm::vec3(0.155f, 0.21f, 0.155f);
        case HeadShape::Sphere: return glm::vec3(0.18f, 0.18f, 0.18f);
    }
    return glm::vec3(0.155f, 0.21f, 0.155f);
}

[[nodiscard]] inline const char* headShapeName(HeadShape shape) {
    switch (shape) {
        case HeadShape::Oval: return "Oval";
        case HeadShape::Sphere: return "Sphere";
    }
    return "Oval";
}

// Real, bounds-checked resolution from core::LocalProfile::headShapeIndex
// (0 = Oval, 1 = Sphere) -- any other real value (a corrupt/future
// profile field) real-falls back to Oval, the real, honest default,
// rather than undefined behavior.
[[nodiscard]] inline HeadShape headShapeFromIndex(int index) { return index == 1 ? HeadShape::Sphere : HeadShape::Oval; }
[[nodiscard]] inline int headShapeToIndex(HeadShape shape) { return shape == HeadShape::Sphere ? 1 : 0; }

// Kronos ("Avatar Phase" -- "AvatarEditor: Body Sliders"): five real,
// independent, orthogonal axes -- deliberately not entangled with each
// other:
//   height        -- overall stature. Scales the Y component of every
//                 real joint's bind-pose localPosition (see
//                 applyBodyProportionsToSkeleton() below), so the spine/
//                 neck/head chain AND both legs elongate together. The
//                 torso box's own height already auto-derives from the
//                 pelvis-to-neck joint distance (see buildHumanoidMeshData()'s
//                 own comment on torsoHalfHeight) -- it never needs
//                 touching directly.
//   torsoLength   -- an additional, real multiplier applied only to the
//                 spine chain's own Y offsets (spine_lower/spine_upper/
//                 neck), on top of whatever `height` already did -- lets
//                 "long-torso-short-legs" and "short-torso-long-legs"
//                 builds exist independently of overall stature, without
//                 double-counting: `height` alone already produces a
//                 proportional whole-body change, this is the real,
//                 additional stretch/compress on just the torso segment.
//   width         -- hip breadth + torso girth. Scales the X (lateral)
//                 component of leg_L/R_upper (hip lateral offset) plus
//                 the torso box's own fixed X/Z half-extents in
//                 buildHumanoidMeshData(). Deliberately does not touch
//                 the shoulders -- see shoulderWidth below.
//   shoulderWidth -- shoulder breadth only. Scales the X (lateral)
//                 component of arm_L/R_upper (shoulder lateral offset),
//                 independent of hip width.
//                 Arm/leg *segment* lengths (the lower-arm/lower-leg
//                 joints' own local offsets) are untouched by either
//                 width axis.
//   limbScale     -- limb thickness only. Never touches the skeleton at
//                 all -- purely scales appendSmoothLimb()'s crossSection
//                 argument and the hand/foot terminal box half-extents in
//                 buildHumanoidMeshData(), so a "chunkier" or "thinner"
//                 avatar never changes its own proportions/reach, only
//                 its limb girth.
// Since every one of these only ever scales a real joint's own bind-pose
// local position (consumed as a real, ordinary world position by
// appendSmoothLimb()'s "tube between two joint world positions" design --
// see that function's own header comment) or a real, already-parametric
// mesh-generation dimension, there is no separate "scale the mesh after
// the fact" step and nothing to tear.
struct BodyProportions {
    float height = 1.0f;
    float width = 1.0f;
    float limbScale = 1.0f;
    float torsoLength = 1.0f;
    float shoulderWidth = 1.0f;
};

// Real, deliberately modest clamp range -- this rig's procedural
// generation has no safeguards against, say, a torso box thinner than
// its own limb cross-sections at extreme values; [0.85, 1.15] keeps every
// slider combination visually plausible without needing per-combination
// validation.
constexpr float kBodyProportionMin = 0.85f;
constexpr float kBodyProportionMax = 1.15f;

[[nodiscard]] inline float clampBodyProportionValue(float value) {
    return value < kBodyProportionMin ? kBodyProportionMin : (value > kBodyProportionMax ? kBodyProportionMax : value);
}

[[nodiscard]] inline BodyProportions clampBodyProportions(BodyProportions proportions) {
    proportions.height = clampBodyProportionValue(proportions.height);
    proportions.width = clampBodyProportionValue(proportions.width);
    proportions.limbScale = clampBodyProportionValue(proportions.limbScale);
    proportions.torsoLength = clampBodyProportionValue(proportions.torsoLength);
    proportions.shoulderWidth = clampBodyProportionValue(proportions.shoulderWidth);
    return proportions;
}

// Real, pure skeleton transform -- returns a copy of `base` with height/
// width applied to the real joints named in BodyProportions's own header
// comment above. Callers (spawnLocalPlayerAvatar(), AvatarEditor) call
// this once and pass the *result* to both spawnRiggedAvatar() (so mesh
// generation and GPU inverse-bind computation see the same scaled bind
// pose) and AvatarController's own constructor (so animation playback
// skins against that same scaled bind pose too) -- the same "one real
// skeleton object, every consumer shares it" discipline this file's
// skinTone/headShape params don't need (they don't touch the skeleton),
// but a joint-position change does.
[[nodiscard]] Skeleton applyBodyProportionsToSkeleton(const Skeleton& base, BodyProportions proportions);

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
// `headShape` is real, new, optional (defaults to the new real Oval
// default -- see HeadShape's own comment) -- feeds directly into the
// head's own appendSphere() call via headShapeRadii(), the one real
// place this rig's head geometry is actually generated.
// `bodyProportions` is real, new, optional (defaults to the identity
// {1,1,1} -- every pre-existing call site keeps compiling and looking
// identical). Only its `width` (torso box X/Z half-extents) and
// `limbScale` (limb cross-sections + hand/foot box half-extents) are
// consumed here directly -- `height` has already been applied to
// `skeleton` itself by the caller via applyBodyProportionsToSkeleton()
// before this is called, so this function needs no separate height
// handling of its own (see BodyProportions's own header comment).
[[nodiscard]] HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton, HeadShape headShape = HeadShape::Oval,
                                                       BodyProportions bodyProportions = {});

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
// Emote/Shoes/Back don't map onto this procedural body's geometry at all
// -- a real, deliberately un-built extension (separate skinned attachment
// meshes for hair/hats/shoes/backpacks/capes), the same honest scope
// boundary as this file's other comments. Equipping one of these is
// still real (AvatarLoadout state, ownership, persistence, catalogue
// validation all apply identically) -- it just has no visual effect on
// this procedural rigged body yet, same as Head's own "recolors the
// whole head sphere, no separate hat mesh" honest simplification.
[[nodiscard]] AvatarItemCategory categoryForBodySegment(HumanoidBodySegment segment);

// Kronos ("Avatar Phase" -- "Default Avatar Redesign" -- "Simple
// baked-in clothing (shirt + trousers)"): the real, honest default look
// for a fresh avatar with nothing equipped -- a plain shirt (Torso/
// LeftArm/RightArm) and trousers (LeftLeg/RightLeg), not bare/skin-toned
// limbs. Still real, ordinary SkinnedRenderable::baseColor data (the same
// per-draw, per-entity color every other segment already uses -- see that
// struct's own comment) -- fully tintable, and instantly overridden the
// moment a real clothing item is equipped in that category; nothing is
// baked into the mesh itself.
//
// Kronos ("Avatar Silhouette Pass" -- style-reference update): real,
// darker charcoal/near-black tones (was a teal shirt + slate trousers),
// matching a real reference image's dark jacket + dark jeans color
// story. The reference's own lighter-blue undershirt (visible only in a
// small V at the chest) is a real, honest, stated gap this pass doesn't
// build -- this rig's Torso segment is one flat shell color, and showing
// two garments at once needs real layered clothing geometry
// (AvatarItemCategory::LayeredClothing exists as a real equip slot, but
// has no mesh-generation path yet -- the same "real slot, no fabricated
// visual behind it yet" pattern this codebase already states honestly
// for Shoes/Back/Accessory).
constexpr glm::vec4 kDefaultShirtColor(0.13f, 0.13f, 0.15f, 1.0f);
constexpr glm::vec4 kDefaultTrouserColor(0.15f, 0.16f, 0.20f, 1.0f);

// Resolves each HumanoidBodySegment's SkinnedRenderable::baseColor from
// `loadout`'s equipped item in categoryForBodySegment(segment)'s category
// (via `index`), falling back to a real, honest per-segment default when
// that category isn't equipped or the equipped id no longer resolves in
// `index` (the same "fail soft, keep rendering something" precedent
// AvatarLoadoutSync.cpp's applyLoadoutToAvatar() already sets): `skinColor`
// for Head (a head has no baked-in clothing), kDefaultShirtColor for
// Torso/LeftArm/RightArm, kDefaultTrouserColor for LeftLeg/RightLeg -- see
// those constants' own comment for why this isn't just `skinColor`
// everywhere anymore. Returned in HumanoidBodySegment's declaration order
// (Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg).
[[nodiscard]] std::array<glm::vec4, kHumanoidBodySegmentCount> resolveSegmentColorsForLoadout(
    const AvatarLoadout& loadout, const CatalogueIndex& index, glm::vec4 skinColor = glm::vec4(0.85f, 0.75f, 0.65f, 1.0f));

// Kronos ("Avatar 2.0" -- "Visual Fidelity: per-segment color gradients"):
// real, pure, small RGB darkening applied per segment for a subtle
// core-to-extremity depth cue (a cheap, honest stand-in for real ambient
// occlusion -- this rig's Vertex format has no per-vertex AO/color
// channel to compute a true one against, see this feature's own design
// note on why a per-vertex approach isn't used here). Deliberately
// leaves Head untouched (multiplier 1.0) -- a player's own chosen skin
// tone (core::LocalProfile::skinToneIndex) shouldn't silently drift from
// what they actually picked; only the baked-in/equipped clothing
// segments (Torso/Arms/Legs) get the gradient. A real, separate step
// from resolveSegmentColorsForLoadout() itself (which stays an exact,
// already-tested "what color IS this segment" resolution) -- callers
// that assign the result straight to SkinnedRenderable::baseColor
// (spawnRiggedAvatar() below, plus every live re-tint call site) apply
// this on top; callers that just want the resolved logical color (tests,
// UI swatches) call resolveSegmentColorsForLoadout() alone, unaffected.
[[nodiscard]] glm::vec4 applySegmentShadingGradient(HumanoidBodySegment segment, glm::vec4 color);

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
// Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection"): `skinTone`
// is real, new, and optional (defaults to the exact real color this
// function always used before this feature existed -- see
// core::kDefaultSkinToneColor's own comment) -- every pre-existing call
// site keeps compiling and looking identical. Feeds
// resolveSegmentColorsForLoadout()'s own `defaultColor` parameter, so it
// applies the same real, consistent way to every body segment an
// equipped item doesn't already cover -- Head is always skin-toned (no
// "Head" clothing category covers it), Torso/Arms/Legs fall back to it
// only where no clothing item is equipped there, exactly matching this
// codebase's own existing "clothing wins, skin tone is the real
// underneath-it-all default" precedent (resolveSegmentColorsForLoadout()
// itself, unchanged by this pass).
// `headShape` is real, new, optional (defaults to HeadShape::Oval, the
// new real default -- see that enum's own header comment) -- forwarded
// directly to buildHumanoidMeshData().
// `bodyProportions` is real, new, optional (defaults to identity). Unlike
// skinTone/headShape, `skeleton` itself must already reflect the desired
// height/width (via applyBodyProportionsToSkeleton()) *before* calling
// this -- it's passed through unmodified to both buildHumanoidMeshData()
// (bind-pose joint positions) and RiggedMesh::uploadFromHost() (inverse-
// bind matrices), so the two stay consistent under animation. This
// function only forwards `bodyProportions` on to buildHumanoidMeshData()
// for its own non-skeletal `width`/`limbScale` mesh dimensions.
[[nodiscard]] bool spawnRiggedAvatar(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout,
                                      const CatalogueIndex& index, RiggedMeshLibrary& riggedMeshLibrary,
                                      VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                      std::vector<EntityId>& outSkinnedEntities, std::string& outError,
                                      glm::vec4 skinTone = glm::vec4(0.85f, 0.75f, 0.65f, 1.0f),
                                      HeadShape headShape = HeadShape::Oval, BodyProportions bodyProportions = {});

// Kronos ("Avatar 2.0" -- "Clothing Meshes"): real, only two real
// options -- "how much larger than the body underneath is the clothing
// shell." Not a continuous slider: a real, honest, small enumerable
// choice (matching this rig's own "real, small, enumerable choice, not
// a free color pick" precedent -- see core::skinToneIndex's own
// comment), persisted as LocalProfile::clothingFitIndex.
enum class ClothingFit { Tight, Loose };
[[nodiscard]] inline float clothingFitScaleMultiplier(ClothingFit fit) {
    return fit == ClothingFit::Tight ? 1.06f : 1.18f;
}
[[nodiscard]] inline ClothingFit clothingFitFromIndex(int index) { return index == 1 ? ClothingFit::Loose : ClothingFit::Tight; }
[[nodiscard]] inline int clothingFitToIndex(ClothingFit fit) { return fit == ClothingFit::Loose ? 1 : 0; }

// Kronos ("Avatar 2.0" -- "Clothing Meshes"): real, separate procedural
// geometry -- NOT the pre-existing tint-only look (resolveSegmentColorsForLoadout()
// recoloring the body segment's own mesh in place, still real and
// unchanged by this function, still what shows through if this call is
// never made or a slot has nothing equipped in it and no honest default
// shell is spawned for it). A real shirt shell (the torso's own
// appendProfiledBarrel() profile, scaled outward by
// clothingFitScaleMultiplier(), plus two short sleeves over the upper
// arms via the exact same appendSmoothLimb() the body's own arms use)
// and a real pants shell (both full legs, hip to ankle, same real
// smooth-limb technique) -- each one real, single, combined RiggedMesh
// (one real draw call for the whole shirt, one for the whole pants, a
// real, small step toward "merge draw calls" for free), bound to the
// exact same real joints (spine_upper/arm_*_upper/arm_*_lower/
// leg_*_upper/leg_*_lower/foot_*) the body's own segments already use --
// "shared rig weights" in the real, literal sense: the same joint
// indices, not a separate skinning scheme. Always spawns both shells
// (the real, honest baked-in-clothing default color when nothing's
// equipped, same philosophy resolveSegmentColorsForLoadout() already
// establishes for the body's own tint) -- a fresh avatar looks dressed,
// not shirtless, from the first spawn. `outError` is only ever set on a
// genuine GPU upload failure; a missing/unresolvable equipped item is a
// real, honest fallback to the default color, not an error.
[[nodiscard]] bool spawnAvatarClothing(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout,
                                        const CatalogueIndex& index, BodyProportions bodyProportions, ClothingFit fit,
                                        RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                                        VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outClothingEntities,
                                        std::string& outError);

} // namespace engine::core
