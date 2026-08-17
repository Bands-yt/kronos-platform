#include "core/RiggedAvatar.hpp"

#include <cmath>

#include "core/CatalogueIndex.hpp"
#include "core/Components.hpp"

namespace engine::core {

namespace {

const char* jointNameFor(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return "head";
        case HumanoidBodySegment::Torso: return "spine_upper";
        case HumanoidBodySegment::LeftArm: return "arm_L_upper";
        case HumanoidBodySegment::RightArm: return "arm_R_upper";
        case HumanoidBodySegment::LeftLeg: return "leg_L_upper";
        case HumanoidBodySegment::RightLeg: return "leg_R_upper";
    }
    return "";
}

// Same 24-vertex, 6-face, flat-normal box shape core::Mesh::createBox()
// uploads -- duplicated here as a host-only (no GPU) generator, appending
// into an existing vertex/index array at `center` rather than always at
// the origin, and tagged with which HumanoidBodySegment it belongs to.
// Same "no shared VulkanUtils-style helper TU in this codebase" precedent
// RiggedMesh.cpp's own file-local staging-buffer duplication already set.
// Real, rigid (single-joint) skin weight -- the right choice for a
// terminal piece (head/hand/foot) that never needs to bend internally.
void appendBox(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                glm::vec3 center, glm::vec3 halfExtents, int jointIndex, HumanoidBodySegment segment,
                SkinWeights& skinWeights) {
    glm::vec3 h = halfExtents;
    uint32_t base = static_cast<uint32_t>(vertices.size());

    std::array<Vertex, 24> box = {{
        // +X
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}}, {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        // -X
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        // +Y
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}}, {{h.x, h.y, -h.z}, {0, 1, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {0, 1, 0}, {0, 1}},
        // -Y
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {1, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {1, 1}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, 1}},
        // +Z
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}}, {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}}, {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        // -Z
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}}, {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    }};

    for (auto& v : box) {
        v.position += center;
        vertices.push_back(v);
        segments.push_back(segment);
        VertexSkinWeights sw;
        sw.jointIndices = {jointIndex, -1, -1, -1};
        sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        skinWeights.perVertex.push_back(sw);
    }

    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t faceBase = base + face * 4;
        indices.insert(indices.end(), {faceBase, faceBase + 1, faceBase + 2, faceBase, faceBase + 2, faceBase + 3});
    }
}

// A real, low-poly lat/long sphere (8 segments x 4 rings) -- "Head
// (simple sphere/oval)" per the Avatar spec -- appended host-only the
// same way appendBox() is, rigidly bound to one joint (a head has no
// internal joint to blend with).
void appendSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                   glm::vec3 center, glm::vec3 radii, int jointIndex, HumanoidBodySegment segment,
                   SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 8;
    constexpr uint32_t kRings = 4;
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (uint32_t r = 0; r <= kRings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(kRings); // 0 (top pole) -> 1 (bottom pole)
        float phi = v * 3.14159265f;
        for (uint32_t s = 0; s <= kSegments; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(kSegments);
            float theta = u * 2.0f * 3.14159265f;
            glm::vec3 unit(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = center + unit * radii;
            vert.normal = glm::normalize(unit);
            vert.uv = {u, v};
            vertices.push_back(vert);
            segments.push_back(segment);
            VertexSkinWeights sw;
            sw.jointIndices = {jointIndex, -1, -1, -1};
            sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
            skinWeights.perVertex.push_back(sw);
        }
    }

    uint32_t ringStride = kSegments + 1;
    for (uint32_t r = 0; r < kRings; ++r) {
        for (uint32_t s = 0; s < kSegments; ++s) {
            uint32_t a = base + r * ringStride + s;
            uint32_t b = base + r * ringStride + s + 1;
            uint32_t c = base + (r + 1) * ringStride + s + 1;
            uint32_t d = base + (r + 1) * ringStride + s;
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }
}

// Kronos ("Avatar Visual Silhouette Pass" -- "Head" -- "reshape the head
// to a stylised human oval... add cheek curvature and a subtle jawline
// for personality", revised after a live-screenshot check showed a
// first, much more aggressive curvature profile reading as snout-like/
// animal next to the first hair design -- explicit user feedback:
// "Revert the avatar head to a humanoid shape. Do not use animal or
// novelty meshes."): the same real, low-poly lat/long sphere
// appendSphere() above generates, but each of its 5 real latitude rings
// (top pole through bottom pole) gets its own real horizontal (X/Z only,
// vertical Y untouched) width multiplier instead of one uniform radius
// -- a real, visible cheekbone bulge and jaw taper, tuned to sit between
// that first over-aggressive pass (0.55 chin scale, read as a snout) and
// an overly-subtle in-between revision -- enough real curvature to read
// as "personality," not just a smoothed sphere. Normals stay the pure
// spherical `normalize(unit)` appendSphere() already uses (not
// re-derived for the per-ring anisotropy) -- the same accepted "cheap,
// approximate ring normal" precedent appendProfiledBarrel() already
// establishes for the torso; imperceptible at this rig's flat, stylized
// low-poly scale.
void appendProfiledHead(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                         std::vector<HumanoidBodySegment>& segments, glm::vec3 center, glm::vec3 radii, int jointIndex,
                         HumanoidBodySegment segment, SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 8;
    constexpr uint32_t kRings = 4;
    constexpr std::array<float, kRings + 1> kRingWidthScale = {0.90f, 1.0f, 1.06f, 0.90f, 0.72f};
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (uint32_t r = 0; r <= kRings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(kRings);
        float phi = v * 3.14159265f;
        float widthScale = kRingWidthScale[r];
        for (uint32_t s = 0; s <= kSegments; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(kSegments);
            float theta = u * 2.0f * 3.14159265f;
            glm::vec3 unit(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = center + glm::vec3(unit.x * radii.x * widthScale, unit.y * radii.y, unit.z * radii.z * widthScale);
            vert.normal = glm::normalize(unit);
            vert.uv = {u, v};
            vertices.push_back(vert);
            segments.push_back(segment);
            VertexSkinWeights sw;
            sw.jointIndices = {jointIndex, -1, -1, -1};
            sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
            skinWeights.perVertex.push_back(sw);
        }
    }

    uint32_t ringStride = kSegments + 1;
    for (uint32_t r = 0; r < kRings; ++r) {
        for (uint32_t s = 0; s < kSegments; ++s) {
            uint32_t a = base + r * ringStride + s;
            uint32_t b = base + r * ringStride + s + 1;
            uint32_t c = base + (r + 1) * ringStride + s + 1;
            uint32_t d = base + (r + 1) * ringStride + s;
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }
}

// Kronos ("Avatar System" -- 18-bone rig, real smooth skinning; "Default
// Avatar Redesign" -- "Arms/Legs: cylindrical with slight taper"): a
// real, low-poly octagonal "tube" between two joints (e.g. shoulder to
// elbow) -- cylindrical at this rig's own established low-poly scale, the
// same 8-segment convention appendSphere()'s own rings already use, in
// place of the old 4-corner rectangular cross-section. 3 real
// cross-section rings (start/mid/end) -- the middle ring's skin weight is
// a real 50/50 blend between the two joints, so when the joint rotates
// this ring interpolates smoothly between both bones' influence instead
// of snapping at a hard boundary. This is what actually delivers "smooth
// bending at elbows/knees" -- appendBox()'s single rigid joint per vertex
// can't. The start ring is 100% the start joint, the end ring 100% the
// end joint (matching how a rigid box/sphere appended at either end of
// this chain rigidly continues from there, e.g. a hand box rigidly bound
// to hand_L picks up exactly where this chain's own end ring left off).
// `crossSectionStart`/`crossSectionEnd` are real, independent radii (not
// one shared value) -- the real, honest "slight taper" the redesign asks
// for; the mid ring uses their real average, so the taper interpolates
// smoothly rather than stepping partway through.
void appendSmoothLimb(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                       std::vector<HumanoidBodySegment>& segments, glm::vec3 startPos, glm::vec3 endPos,
                       glm::vec2 crossSectionStart, glm::vec2 crossSectionEnd, int startJoint, int endJoint,
                       HumanoidBodySegment segment, SkinWeights& skinWeights) {
    constexpr uint32_t kLimbSegments = 8;
    glm::vec3 midPos = (startPos + endPos) * 0.5f;
    glm::vec2 crossSectionMid = (crossSectionStart + crossSectionEnd) * 0.5f;

    auto makeRing = [&](glm::vec3 center, glm::vec2 crossSection, int jointA, float weightA, int jointB,
                         float weightB) -> std::vector<uint32_t> {
        std::vector<uint32_t> result(kLimbSegments);
        for (uint32_t i = 0; i < kLimbSegments; ++i) {
            float theta = (static_cast<float>(i) / static_cast<float>(kLimbSegments)) * 2.0f * 3.14159265f;
            glm::vec3 offset(std::cos(theta) * crossSection.x, 0.0f, std::sin(theta) * crossSection.y);
            Vertex v;
            v.position = center + offset;
            v.normal = glm::vec3(std::cos(theta), 0.0f, std::sin(theta));
            v.uv = {static_cast<float>(i) / static_cast<float>(kLimbSegments), 0.0f};
            uint32_t index = static_cast<uint32_t>(vertices.size());
            vertices.push_back(v);
            segments.push_back(segment);
            VertexSkinWeights sw;
            sw.jointIndices = {jointA, weightB > 0.0f ? jointB : -1, -1, -1};
            sw.weights = {weightA, weightB, 0.0f, 0.0f};
            skinWeights.perVertex.push_back(sw);
            result[i] = index;
        }
        return result;
    };

    std::vector<uint32_t> ringStart = makeRing(startPos, crossSectionStart, startJoint, 1.0f, -1, 0.0f);
    std::vector<uint32_t> ringMid = makeRing(midPos, crossSectionMid, startJoint, 0.5f, endJoint, 0.5f);
    std::vector<uint32_t> ringEnd = makeRing(endPos, crossSectionEnd, endJoint, 1.0f, -1, 0.0f);

    auto connect = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
        for (uint32_t i = 0; i < kLimbSegments; ++i) {
            uint32_t next = (i + 1) % kLimbSegments;
            indices.insert(indices.end(), {a[i], a[next], b[next], a[i], b[next], b[i]});
        }
    };
    connect(ringStart, ringMid);
    connect(ringMid, ringEnd);
}

// Kronos ("Avatar Phase" -- "Default Avatar Redesign" -- "Torso:
// redesigned (not a box), rounded front/back, clear shoulder
// silhouette"): a real, low-poly "profiled barrel" -- the same real
// 8-segment ring convention appendSphere()/appendSmoothLimb() already
// use, but each of `ringRadii`'s own rings (bottom-to-top, evenly spaced
// across `halfHeight`) carries its own independent elliptical {radiusX,
// radiusZ} instead of one fixed sphere radius. A flattened ellipse
// (radiusZ < radiusX) is what makes each ring "rounded front/back" rather
// than flat like a box's own rectangular cross-section; a wider top ring
// than bottom ring is what gives the real "shoulder silhouette" the
// spec asks for. Flat-capped top/bottom (a real fan triangulation, not a
// rounded pole) -- a torso's top/bottom are real, open attachment
// boundaries to the neck/hips, not a rounded cap the way a head's own
// poles are. Real, rigid (single-joint) skin weight, same as the box this
// replaces -- still "one connected piece" per the Avatar System spec (see
// this function's own real caller for why a multi-joint smooth spine
// stays a deliberately un-built refinement).
void appendProfiledBarrel(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                           std::vector<HumanoidBodySegment>& segments, glm::vec3 center, float halfHeight,
                           const std::vector<glm::vec2>& ringRadii, int jointIndex, HumanoidBodySegment segment,
                           SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 8;
    uint32_t ringCount = static_cast<uint32_t>(ringRadii.size());
    uint32_t base = static_cast<uint32_t>(vertices.size());
    uint32_t ringStride = kSegments;

    auto pushVertex = [&](glm::vec3 position, glm::vec3 normal, glm::vec2 uv) {
        Vertex v;
        v.position = position;
        v.normal = normal;
        v.uv = uv;
        vertices.push_back(v);
        segments.push_back(segment);
        VertexSkinWeights sw;
        sw.jointIndices = {jointIndex, -1, -1, -1};
        sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        skinWeights.perVertex.push_back(sw);
    };

    for (uint32_t r = 0; r < ringCount; ++r) {
        float v = ringCount > 1 ? static_cast<float>(r) / static_cast<float>(ringCount - 1) : 0.0f;
        float y = center.y + (v * 2.0f - 1.0f) * halfHeight;
        glm::vec2 radii = ringRadii[r];
        for (uint32_t s = 0; s < kSegments; ++s) {
            float theta = (static_cast<float>(s) / static_cast<float>(kSegments)) * 2.0f * 3.14159265f;
            glm::vec3 offset(std::cos(theta) * radii.x, 0.0f, std::sin(theta) * radii.y);
            glm::vec3 n(std::cos(theta) / std::max(radii.x, 1e-4f), 0.0f, std::sin(theta) / std::max(radii.y, 1e-4f));
            pushVertex(glm::vec3(center.x, y, center.z) + offset, glm::normalize(n),
                       {static_cast<float>(s) / static_cast<float>(kSegments), v});
        }
    }

    for (uint32_t r = 0; r + 1 < ringCount; ++r) {
        for (uint32_t s = 0; s < kSegments; ++s) {
            uint32_t next = (s + 1) % kSegments;
            uint32_t a = base + r * ringStride + s;
            uint32_t b = base + r * ringStride + next;
            uint32_t c = base + (r + 1) * ringStride + next;
            uint32_t d = base + (r + 1) * ringStride + s;
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }

    // Flat top/bottom caps -- a real fan triangulation from a real,
    // separate center vertex (not shared with any ring vertex, so its own
    // flat normal doesn't get blended into the ring's own curved
    // normals).
    auto appendCap = [&](uint32_t ringIndex, bool facesDown) {
        float v = ringCount > 1 ? static_cast<float>(ringIndex) / static_cast<float>(ringCount - 1) : 0.0f;
        float y = center.y + (v * 2.0f - 1.0f) * halfHeight;
        uint32_t centerIndex = static_cast<uint32_t>(vertices.size());
        pushVertex(glm::vec3(center.x, y, center.z), facesDown ? glm::vec3(0, -1, 0) : glm::vec3(0, 1, 0), {0.5f, 0.5f});
        for (uint32_t s = 0; s < kSegments; ++s) {
            uint32_t next = (s + 1) % kSegments;
            uint32_t a = base + ringIndex * ringStride + s;
            uint32_t b = base + ringIndex * ringStride + next;
            if (facesDown) {
                indices.insert(indices.end(), {centerIndex, b, a});
            } else {
                indices.insert(indices.end(), {centerIndex, a, b});
            }
        }
    };
    appendCap(0, /*facesDown=*/true);
    appendCap(ringCount - 1, /*facesDown=*/false);
}

// Kronos ("Avatar Visual Silhouette Pass" -- "Hands"): a real palm box
// (bigger than the old plain "mitten" box -- see the caller's own
// palmHalfExtents comment) plus 4 real, small, rigid finger blocks
// protruding from the palm's distal (fingertip-side) face. "Stylised
// blocks," not individually-jointed fingers, per the spec -- every
// finger box is 100% rigidly bound to the exact same hand_L/hand_R joint
// the palm itself uses (setJointIndex-style single-joint weighting, no
// new joints, no new skin-weight complexity), so "deformation remains
// clean under animation" is automatic: whatever the hand joint does, the
// whole hand (palm + fingers) moves as one rigid piece, the same
// guarantee every other terminal box (head/old hand/feet) in this rig
// already has. `sideSign` (-1 left, +1 right) is real, not arbitrary --
// the shoulder->elbow->wrist chain's own bind-pose joints sit purely
// along local X from each other (see buildHumanoidSkeleton()'s own arm
// joint offsets), so continuing that same X direction past the wrist is
// the real, anatomical "toward the fingertips" axis, not a guess.
void appendHand(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<HumanoidBodySegment>& segments,
                 glm::vec3 wristPos, float sideSign, glm::vec3 palmHalfExtents, int jointIndex,
                 HumanoidBodySegment segment, SkinWeights& skinWeights) {
    appendBox(vertices, indices, segments, wristPos, palmHalfExtents, jointIndex, segment, skinWeights);

    constexpr int kFingerCount = 4;
    glm::vec3 fingerHalfExtents(palmHalfExtents.x * 0.6f, palmHalfExtents.y * 0.34f, palmHalfExtents.z * 0.34f);
    float palmDistalX = wristPos.x + sideSign * palmHalfExtents.x;
    float fingerSpread = palmHalfExtents.z * 2.0f - fingerHalfExtents.z * 2.0f;
    for (int i = 0; i < kFingerCount; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kFingerCount) - 0.5f; // -0.375 .. 0.375
        glm::vec3 fingerCenter(palmDistalX + sideSign * fingerHalfExtents.x, wristPos.y, wristPos.z + t * fingerSpread);
        appendBox(vertices, indices, segments, fingerCenter, fingerHalfExtents, jointIndex, segment, skinWeights);
    }
}

} // namespace

Skeleton applyBodyProportionsToSkeleton(const Skeleton& base, BodyProportions proportions) {
    Skeleton scaled = base;
    for (Joint& joint : scaled.joints) {
        joint.localPosition.y *= proportions.height;
        if (joint.name == "spine_lower" || joint.name == "spine_upper" || joint.name == "neck") {
            joint.localPosition.y *= proportions.torsoLength;
        }
        if (joint.name == "leg_L_upper" || joint.name == "leg_R_upper") {
            joint.localPosition.x *= proportions.width;
        }
        if (joint.name == "arm_L_upper" || joint.name == "arm_R_upper") {
            joint.localPosition.x *= proportions.shoulderWidth;
        }
    }
    return scaled;
}

Skeleton buildHumanoidSkeleton() {
    Skeleton skeleton;
    skeleton.name = "Humanoid";

    // Kronos ("Avatar System" -- Full Technical Specification): the real
    // 18-bone rig, exactly as specified (root/pelvis/spine_lower/
    // spine_upper/neck/head, upper/lower arms + hands, upper/lower legs +
    // feet on both sides). Bind pose is a real, honest T-ish pose (arms
    // out to the sides), standing with feet at the skeleton's own local
    // Y=0 -- root sits at the ground, pelvis at real waist height above
    // it, matching this engine's existing ~1.8-2.0 unit character-height
    // convention (CharacterController's own capsule).
    Joint root;
    root.name = "root";
    int rootIndex = skeleton.addJoint(root);

    Joint pelvis;
    pelvis.name = "pelvis";
    pelvis.parentIndex = rootIndex;
    pelvis.localPosition = {0.0f, 1.0f, 0.0f};
    int pelvisIndex = skeleton.addJoint(pelvis);

    Joint spineLower;
    spineLower.name = "spine_lower";
    spineLower.parentIndex = pelvisIndex;
    spineLower.localPosition = {0.0f, 0.2f, 0.0f};
    int spineLowerIndex = skeleton.addJoint(spineLower);

    Joint spineUpper;
    spineUpper.name = "spine_upper";
    spineUpper.parentIndex = spineLowerIndex;
    spineUpper.localPosition = {0.0f, 0.3f, 0.0f};
    int spineUpperIndex = skeleton.addJoint(spineUpper);

    Joint neck;
    neck.name = "neck";
    neck.parentIndex = spineUpperIndex;
    neck.localPosition = {0.0f, 0.35f, 0.0f};
    int neckIndex = skeleton.addJoint(neck);

    Joint head;
    head.name = "head";
    head.parentIndex = neckIndex;
    head.localPosition = {0.0f, 0.2f, 0.0f};
    int headIndex = skeleton.addJoint(head);

    // Kronos ("Avatar 2.0" -- "Facial System"): five real, new attachment
    // joints, children of "head" -- NOT a vertex morph-target/blend-shape
    // system (this rig's GPU skinning pipeline has no per-vertex blend
    // weight support to build that against without a much larger render-
    // pipeline change, see AvatarFace.hpp's own header comment for the
    // real, honest scope this took instead). Each real facial feature
    // mesh (spawnAvatarFace(), AvatarFace.cpp) is rigidly bound 100% to
    // its own joint here, so "expression" is real, per-joint procedural
    // transform (scale/rotate/offset), applied the exact same
    // right-multiply-onto-the-skinning-matrix way the existing head-bob
    // (AvatarController::tick()) already proves out -- not four separate
    // new mechanisms. Positions are real, hand-placed offsets on the
    // real head ellipsoid's own front hemisphere (+Z is this rig's own
    // real "forward" -- see CharacterController::tick()'s own
    // faceDir = (sin(yaw), 0, cos(yaw)), which is 0,0,1 at yaw 0).
    Joint leftEye;
    leftEye.name = "face_left_eye";
    leftEye.parentIndex = headIndex;
    leftEye.localPosition = {-0.06f, 0.03f, 0.14f};
    skeleton.addJoint(leftEye);

    Joint rightEye;
    rightEye.name = "face_right_eye";
    rightEye.parentIndex = headIndex;
    rightEye.localPosition = {0.06f, 0.03f, 0.14f};
    skeleton.addJoint(rightEye);

    Joint leftBrow;
    leftBrow.name = "face_left_brow";
    leftBrow.parentIndex = headIndex;
    leftBrow.localPosition = {-0.06f, 0.09f, 0.135f};
    skeleton.addJoint(leftBrow);

    Joint rightBrow;
    rightBrow.name = "face_right_brow";
    rightBrow.parentIndex = headIndex;
    rightBrow.localPosition = {0.06f, 0.09f, 0.135f};
    skeleton.addJoint(rightBrow);

    Joint mouth;
    mouth.name = "face_mouth";
    mouth.parentIndex = headIndex;
    mouth.localPosition = {0.0f, -0.07f, 0.145f};
    skeleton.addJoint(mouth);

    // Kronos ("Avatar 2.0" -- "Accessory Rigging"): four real, new
    // attachment joints -- "handheld" reuses the existing hand_L/hand_R
    // joints below (already real; no new joint needed for it). Real,
    // hand-placed offsets on the head ellipsoid/torso, same convention
    // the five facial joints just above already establish.
    Joint attachHat;
    attachHat.name = "attach_hat";
    attachHat.parentIndex = headIndex;
    attachHat.localPosition = {0.0f, 0.19f, 0.0f};
    skeleton.addJoint(attachHat);

    Joint attachHair;
    attachHair.name = "attach_hair";
    attachHair.parentIndex = headIndex;
    attachHair.localPosition = {0.0f, 0.15f, -0.09f};
    skeleton.addJoint(attachHair);

    Joint attachFaceAccessory;
    attachFaceAccessory.name = "attach_face_accessory";
    attachFaceAccessory.parentIndex = headIndex;
    attachFaceAccessory.localPosition = {0.0f, 0.03f, 0.165f};
    skeleton.addJoint(attachFaceAccessory);

    // attach_back is parented to spine_upper (the torso's own real
    // attachment joint, same one the torso mesh itself binds to), not
    // head -- a real, distinct location for backpacks/capes.
    Joint attachBack;
    attachBack.name = "attach_back";
    attachBack.parentIndex = spineUpperIndex;
    attachBack.localPosition = {0.0f, 0.05f, -0.17f};
    skeleton.addJoint(attachBack);

    // Kronos ("Avatar Visual Silhouette Pass" -- "Arms, Legs, and Feet"):
    // arm segment lengths real-increased from the original 0.32/0.28
    // (total 0.6) to 0.51/0.44 (total 0.95), together with a new, more
    // vertical idle/walk/run/jump_start rest angle (85 degrees off
    // horizontal, up from the previous T-pose-fix's 50 degrees -- see
    // idle.anim's own real, recomposed keyframes) -- forward-kinematics
    // verified (scripted, not eyeballed) to land the idle-pose hand just
    // below mid-thigh height (world y ~= 0.654, vs. the real hip/knee
    // midpoint at 0.675), matching a classic blocky-avatar silhouette
    // rather than a realistic human reach. A length-only change at the
    // old 50-degree rest angle would have needed an even longer,
    // disproportionate arm to reach the same target -- the rest-angle
    // change is a real, necessary part of this, not a separate, optional
    // tweak (see docs/progress.md for the full FK math).
    //
    // The shoulder's own lateral offset also real-widened, 0.25 -> 0.36
    // (a real, separate fix found via live screenshot, not part of the
    // original plan): the torso's own new, wider shoulder-bulge ring
    // (see the torso profile below, 0.29 max half-width) meant a
    // near-vertical arm starting at the OLD 0.25 offset began *inside*
    // the torso's own silhouette and stayed hugging it for its whole
    // length, reading as almost invisible from the front -- exactly the
    // "reads clearly from all camera angles" requirement failing. 0.36
    // clears the torso's 0.29 boundary plus the arm's own 0.095 cross-
    // section radius with real margin, at every point along the arm's
    // length. Every .anim file's own arm_L_upper/arm_R_upper keyframes
    // bake this same position (this rig's animation format stores
    // absolute position per keyframe, not a bind-pose delta) and were
    // updated to match.
    Joint armLUpper;
    armLUpper.name = "arm_L_upper";
    armLUpper.parentIndex = spineUpperIndex;
    armLUpper.localPosition = {-0.36f, 0.1f, 0.0f};
    int armLUpperIndex = skeleton.addJoint(armLUpper);

    Joint armLLower;
    armLLower.name = "arm_L_lower";
    armLLower.parentIndex = armLUpperIndex;
    armLLower.localPosition = {-0.51f, 0.0f, 0.0f};
    int armLLowerIndex = skeleton.addJoint(armLLower);

    Joint handL;
    handL.name = "hand_L";
    handL.parentIndex = armLLowerIndex;
    handL.localPosition = {-0.44f, 0.0f, 0.0f};
    skeleton.addJoint(handL);

    Joint armRUpper;
    armRUpper.name = "arm_R_upper";
    armRUpper.parentIndex = spineUpperIndex;
    armRUpper.localPosition = {0.36f, 0.1f, 0.0f};
    int armRUpperIndex = skeleton.addJoint(armRUpper);

    Joint armRLower;
    armRLower.name = "arm_R_lower";
    armRLower.parentIndex = armRUpperIndex;
    armRLower.localPosition = {0.51f, 0.0f, 0.0f};
    int armRLowerIndex = skeleton.addJoint(armRLower);

    Joint handR;
    handR.name = "hand_R";
    handR.parentIndex = armRLowerIndex;
    handR.localPosition = {0.44f, 0.0f, 0.0f};
    skeleton.addJoint(handR);

    Joint legLUpper;
    legLUpper.name = "leg_L_upper";
    legLUpper.parentIndex = pelvisIndex;
    legLUpper.localPosition = {-0.18f, -0.1f, 0.0f};
    int legLUpperIndex = skeleton.addJoint(legLUpper);

    // Kronos ("Avatar Visual Silhouette Pass" -- "Shorten upper legs
    // slightly for balance"): upper leg (thigh) real-shortened 0.45 ->
    // 0.36, lower leg (shin) real-lengthened 0.45 -> 0.54 by the exact
    // same amount -- total hip-to-foot leg length is unchanged (0.9), so
    // feet stay real-grounded at the skeleton's own y=0 convention (see
    // this function's own class-level comment) instead of floating or
    // sinking. A shorter thigh shifts the knee bend point down, reading
    // as a stubbier, lower-center-of-mass silhouette without changing
    // overall height. walk.anim/run.anim/jump_start.anim/jump_air.anim/
    // jump_land.anim all bake this same joint's own local position into
    // their leg_L_lower/leg_R_lower keyframes (this rig's animation
    // format stores absolute position per keyframe, not a bind-pose
    // delta -- see AnimationPlayer's own doc) and were updated to match
    // this exact value; skipping any of them would pop the leg length
    // during that specific clip.
    Joint legLLower;
    legLLower.name = "leg_L_lower";
    legLLower.parentIndex = legLUpperIndex;
    legLLower.localPosition = {0.0f, -0.36f, 0.0f};
    int legLLowerIndex = skeleton.addJoint(legLLower);

    Joint footL;
    footL.name = "foot_L";
    footL.parentIndex = legLLowerIndex;
    footL.localPosition = {0.0f, -0.54f, 0.05f};
    skeleton.addJoint(footL);

    Joint legRUpper;
    legRUpper.name = "leg_R_upper";
    legRUpper.parentIndex = pelvisIndex;
    legRUpper.localPosition = {0.18f, -0.1f, 0.0f};
    int legRUpperIndex = skeleton.addJoint(legRUpper);

    Joint legRLower;
    legRLower.name = "leg_R_lower";
    legRLower.parentIndex = legRUpperIndex;
    legRLower.localPosition = {0.0f, -0.36f, 0.0f};
    int legRLowerIndex = skeleton.addJoint(legRLower);

    Joint footR;
    footR.name = "foot_R";
    footR.parentIndex = legRLowerIndex;
    footR.localPosition = {0.0f, -0.54f, 0.05f};
    skeleton.addJoint(footR);

    return skeleton;
}

HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton, HeadShape headShape, BodyProportions bodyProportions) {
    HumanoidMeshData data;
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();

    auto worldPos = [&](const char* jointName) -> glm::vec3 {
        int index = skeleton.findJointIndex(jointName);
        return index >= 0 ? glm::vec3(world[static_cast<size_t>(index)][3]) : glm::vec3(0.0f);
    };
    auto jointIndexFor = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };

    // Head -- rigidly bound (a head has no internal joint to blend
    // with). Real, chosen radii per headShape -- see HeadShape's own
    // header comment. Only the real, new default Oval shape gets the
    // real per-ring cheek/jaw curvature (see appendProfiledHead()'s own
    // comment) -- Sphere stays the exact, unmodified appendSphere() call
    // it always was, preserving its own explicit "perfect sphere, equal
    // radii on all three axes, the classic block-engine alternative"
    // contract (HeadShape::Sphere's own header comment) rather than
    // silently curving the one shape whose entire point is to be
    // uncurved.
    if (headShape == HeadShape::Oval) {
        appendProfiledHead(data.vertices, data.indices, data.vertexSegments, worldPos("head"), headShapeRadii(headShape),
                            jointIndexFor("head"), HumanoidBodySegment::Head, data.skinWeights);
    } else {
        appendSphere(data.vertices, data.indices, data.vertexSegments, worldPos("head"), headShapeRadii(headShape),
                     jointIndexFor("head"), HumanoidBodySegment::Head, data.skinWeights);
    }

    // Torso -- "single connected piece" per spec: one rigid, real profiled
    // barrel (see appendProfiledBarrel()'s own comment for why this
    // replaced the old box) spanning pelvis to neck, bound to spine_upper
    // (the real chest reference this rig's arms also attach to). A
    // multi-joint smooth-blended spine is a real, deliberately un-built
    // refinement -- this Alpha's idle/walk/run set doesn't bend the
    // spine, so the honest, simpler rigid choice here doesn't cost
    // anything real yet (see class-level scope note below).
    {
        glm::vec3 pelvisPos = worldPos("pelvis");
        glm::vec3 neckPos = worldPos("neck");
        glm::vec3 torsoCenter = (pelvisPos + neckPos) * 0.5f;
        float torsoHalfHeight = glm::length(neckPos - pelvisPos) * 0.5f;
        float w = bodyProportions.width;
        // Waist (narrower) -> chest -> shoulders (wider) -- a real,
        // gentle taper, not a uniform box, giving the torso both "rounded
        // front/back" (radiusZ < radiusX at every ring) and a "clear
        // shoulder silhouette". Kronos ("Avatar Visual Silhouette Pass" --
        // "Torso and Shoulders" -- "Add shoulder rounding and a gentle
        // taper toward the waist"): real, 4 rings now (was 3) -- the
        // widest ring sits just below the very top (a real shoulder
        // bulge, 0.29 vs. the old flat 0.27 max), then narrows back in
        // slightly at the neckline ring, so the silhouette actually
        // rounds over the shoulder into the neck instead of stopping flat
        // at its own widest point. The waist ring is unchanged -- the
        // "gentle taper toward the waist" was already real here before
        // this pass.
        std::vector<glm::vec2> torsoProfile = {
            {0.20f * w, 0.12f * w}, // waist (bottom)
            {0.24f * w, 0.14f * w}, // chest (mid)
            {0.29f * w, 0.15f * w}, // shoulder bulge
            {0.24f * w, 0.14f * w}, // neckline (rounds back in)
        };
        appendProfiledBarrel(data.vertices, data.indices, data.vertexSegments, torsoCenter, torsoHalfHeight,
                              torsoProfile, jointIndexFor("spine_upper"), HumanoidBodySegment::Torso, data.skinWeights);
    }

    // Arms -- real smooth-blended upper-to-lower chain (the actual elbow
    // bend the spec asks for), capped with a real palm + finger-block
    // hand (see appendHand()'s own comment). A real, slight taper
    // (shoulder wider than elbow, elbow wider than wrist) -- continuous
    // across the elbow (upper arm's own end radius equals lower arm's
    // own start radius, so there's no visible step). Cross-sections and
    // the hand scale with `bodyProportions.limbScale` only -- see that
    // field's own header comment.
    float ls = bodyProportions.limbScale;
    glm::vec2 shoulderCrossSection(0.095f * ls, 0.095f * ls);
    glm::vec2 elbowCrossSection(0.075f * ls, 0.075f * ls);
    glm::vec2 wristCrossSection(0.065f * ls, 0.065f * ls);
    // Kronos ("Avatar Visual Silhouette Pass" -- "Hands" -- "Add palm
    // volume"): real, modestly bigger than the old plain hand box
    // (0.09/0.11/0.05) it replaces.
    glm::vec3 palmHalfExtents(0.10f * ls, 0.12f * ls, 0.065f * ls);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_upper"), worldPos("arm_L_lower"),
                      shoulderCrossSection, elbowCrossSection, jointIndexFor("arm_L_upper"), jointIndexFor("arm_L_lower"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_lower"), worldPos("hand_L"),
                      elbowCrossSection, wristCrossSection, jointIndexFor("arm_L_lower"), jointIndexFor("hand_L"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendHand(data.vertices, data.indices, data.vertexSegments, worldPos("hand_L"), -1.0f, palmHalfExtents,
               jointIndexFor("hand_L"), HumanoidBodySegment::LeftArm, data.skinWeights);

    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_upper"), worldPos("arm_R_lower"),
                      shoulderCrossSection, elbowCrossSection, jointIndexFor("arm_R_upper"), jointIndexFor("arm_R_lower"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_lower"), worldPos("hand_R"),
                      elbowCrossSection, wristCrossSection, jointIndexFor("arm_R_lower"), jointIndexFor("hand_R"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendHand(data.vertices, data.indices, data.vertexSegments, worldPos("hand_R"), 1.0f, palmHalfExtents,
               jointIndexFor("hand_R"), HumanoidBodySegment::RightArm, data.skinWeights);

    // Legs -- same real smooth knee bend (continuous taper across it, same
    // reasoning as the arms above), capped with a rigid "simple block"
    // foot. Same limbScale-only scaling as the arms above. Kronos
    // ("Avatar Visual Silhouette Pass" -- target silhouette "broad
    // shoulders, narrow legs"): real, slightly slimmer than the
    // original 0.13/0.105/0.085 -- a deliberate, real contrast against
    // the torso's own widened 0.29 shoulder bulge, not a proportional
    // side effect of anything else in this pass.
    glm::vec2 hipCrossSection(0.11f * ls, 0.11f * ls);
    glm::vec2 kneeCrossSection(0.09f * ls, 0.09f * ls);
    glm::vec2 ankleCrossSection(0.07f * ls, 0.07f * ls);
    // Kronos ("Avatar Visual Silhouette Pass" -- "Widen feet for
    // stability and clearer silhouette"): X (width) real-increased
    // 0.1 -> 0.13, Y (thickness) real-increased 0.06 -> 0.07 for a
    // chunkier, more stable-reading stylized foot -- Z (length) is
    // unchanged.
    glm::vec3 footBoxHalfExtents(0.13f * ls, 0.07f * ls, 0.18f * ls);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_L_upper"), worldPos("leg_L_lower"),
                      hipCrossSection, kneeCrossSection, jointIndexFor("leg_L_upper"), jointIndexFor("leg_L_lower"),
                      HumanoidBodySegment::LeftLeg, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_L_lower"), worldPos("foot_L"),
                      kneeCrossSection, ankleCrossSection, jointIndexFor("leg_L_lower"), jointIndexFor("foot_L"),
                      HumanoidBodySegment::LeftLeg, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("foot_L") + glm::vec3(0.0f, -0.04f, 0.08f),
              footBoxHalfExtents, jointIndexFor("foot_L"), HumanoidBodySegment::LeftLeg, data.skinWeights);

    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_R_upper"), worldPos("leg_R_lower"),
                      hipCrossSection, kneeCrossSection, jointIndexFor("leg_R_upper"), jointIndexFor("leg_R_lower"),
                      HumanoidBodySegment::RightLeg, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_R_lower"), worldPos("foot_R"),
                      kneeCrossSection, ankleCrossSection, jointIndexFor("leg_R_lower"), jointIndexFor("foot_R"),
                      HumanoidBodySegment::RightLeg, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("foot_R") + glm::vec3(0.0f, -0.04f, 0.08f),
              footBoxHalfExtents, jointIndexFor("foot_R"), HumanoidBodySegment::RightLeg, data.skinWeights);

    return data;
}

HumanoidMeshData extractSegment(const HumanoidMeshData& meshData, HumanoidBodySegment segment) {
    HumanoidMeshData out;
    std::vector<uint32_t> remap(meshData.vertices.size(), ~0u);

    for (size_t i = 0; i < meshData.vertices.size(); ++i) {
        if (meshData.vertexSegments[i] != segment) continue;
        remap[i] = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(meshData.vertices[i]);
        out.skinWeights.perVertex.push_back(meshData.skinWeights.perVertex[i]);
        out.vertexSegments.push_back(segment);
    }

    for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3) {
        uint32_t a = meshData.indices[i];
        uint32_t b = meshData.indices[i + 1];
        uint32_t c = meshData.indices[i + 2];
        if (remap[a] == ~0u || remap[b] == ~0u || remap[c] == ~0u) continue; // triangle not fully in this segment
        out.indices.push_back(remap[a]);
        out.indices.push_back(remap[b]);
        out.indices.push_back(remap[c]);
    }

    return out;
}

AvatarItemCategory categoryForBodySegment(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return AvatarItemCategory::Head;
        case HumanoidBodySegment::Torso: return AvatarItemCategory::Torso;
        case HumanoidBodySegment::LeftArm: return AvatarItemCategory::Torso;
        case HumanoidBodySegment::RightArm: return AvatarItemCategory::Torso;
        case HumanoidBodySegment::LeftLeg: return AvatarItemCategory::Legs;
        case HumanoidBodySegment::RightLeg: return AvatarItemCategory::Legs;
    }
    return AvatarItemCategory::Torso;
}

std::array<glm::vec4, kHumanoidBodySegmentCount> resolveSegmentColorsForLoadout(const AvatarLoadout& loadout,
                                                                                 const CatalogueIndex& index,
                                                                                 glm::vec4 skinColor) {
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors;
    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        switch (static_cast<HumanoidBodySegment>(i)) {
            case HumanoidBodySegment::Head:
                colors[i] = skinColor;
                break;
            case HumanoidBodySegment::LeftLeg:
            case HumanoidBodySegment::RightLeg:
                colors[i] = kDefaultTrouserColor;
                break;
            case HumanoidBodySegment::Torso:
            case HumanoidBodySegment::LeftArm:
            case HumanoidBodySegment::RightArm:
                colors[i] = kDefaultShirtColor;
                break;
        }
    }

    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        auto segment = static_cast<HumanoidBodySegment>(i);
        AvatarItemCategory category = categoryForBodySegment(segment);
        std::string itemId = loadout.equippedItemId(category);
        if (itemId.empty()) continue;
        const AvatarItemManifest* manifest = index.findById(itemId);
        if (manifest == nullptr) continue; // equipped id no longer resolves -- fail soft, keep the real, honest default color
        colors[i] = manifest->item.baseColor;
    }
    return colors;
}

glm::vec4 applySegmentShadingGradient(HumanoidBodySegment segment, glm::vec4 color) {
    float multiplier = 1.0f;
    switch (segment) {
        case HumanoidBodySegment::Head: multiplier = 1.0f; break;
        case HumanoidBodySegment::Torso: multiplier = 1.0f; break;
        case HumanoidBodySegment::LeftArm:
        case HumanoidBodySegment::RightArm: multiplier = 0.95f; break;
        case HumanoidBodySegment::LeftLeg:
        case HumanoidBodySegment::RightLeg: multiplier = 0.90f; break;
    }
    return glm::vec4(color.r * multiplier, color.g * multiplier, color.b * multiplier, color.a);
}

// Kronos ("Avatar Visual Silhouette Pass" -- "Material Pass" -- "Add
// subtle specular... to separate limbs visually. Keep the style
// consistent with Kronos's cinematic lighting"): real, small per-segment
// roughness variation on top of SkinnedRenderable's own existing
// metallic/roughness fields (0.05/0.6 defaults, previously identical on
// every segment) -- head/torso read very slightly smoother (skin/shirt),
// arms a touch rougher, legs (trousers) rougher still, a real, subtle
// specular-highlight size/sharpness difference under this engine's
// existing PBR directional/point lighting (SceneLighting -- no new
// rendering feature, just real per-entity material data every other
// SkinnedRenderable field already uses). AO itself already has a real,
// honest stand-in -- see applySegmentShadingGradient()'s own comment on
// why the color multiplier IS this rig's real AO substitute (no
// per-vertex AO channel exists to compute a true one against); this
// function only adds the specular half. Metallic is left untouched
// (0.05 everywhere) -- an avatar's skin/cloth is genuinely non-metallic,
// varying it wouldn't read as a real material difference the way
// roughness does.
constexpr float kHeadRoughness = 0.55f;
constexpr float kTorsoRoughness = 0.58f;
constexpr float kArmRoughness = 0.62f;
constexpr float kLegRoughness = 0.66f;

// Internal linkage -- unlike applySegmentShadingGradient() (also called
// live from AvatarEditor.cpp/Application.cpp's own re-tint paths),
// roughness is a static per-segment material property set once at
// spawnRiggedAvatar() time, never re-applied on an equip/skin-tone
// change, so nothing outside this file needs to call it.
[[nodiscard]] static float segmentMaterialRoughness(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return kHeadRoughness;
        case HumanoidBodySegment::Torso: return kTorsoRoughness;
        case HumanoidBodySegment::LeftArm:
        case HumanoidBodySegment::RightArm: return kArmRoughness;
        case HumanoidBodySegment::LeftLeg:
        case HumanoidBodySegment::RightLeg: return kLegRoughness;
    }
    return kTorsoRoughness;
}

bool spawnRiggedAvatar(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                        RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                        VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outSkinnedEntities,
                        std::string& outError, glm::vec4 skinTone, HeadShape headShape, BodyProportions bodyProportions) {
    HumanoidMeshData fullBody = buildHumanoidMeshData(skeleton, headShape, bodyProportions);
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors = resolveSegmentColorsForLoadout(loadout, index, skinTone);

    std::vector<EntityId> spawned;
    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        auto segment = static_cast<HumanoidBodySegment>(i);
        HumanoidMeshData segmentData = extractSegment(fullBody, segment);

        RiggedMesh riggedMesh;
        std::string segmentError;
        if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, segmentData.vertices, segmentData.indices,
                                        segmentData.skinWeights, skeleton, segmentError)) {
            outError = std::string("failed to upload rigged mesh for segment \"") + jointNameFor(segment) +
                       "\": " + segmentError;
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));

        // createEntity() already attaches an identity Transform -- see
        // ECS.hpp -- which AvatarController::tick() overwrites every
        // frame with the character's actual world placement (this
        // function's own header comment explains the split).
        EntityId entity = ecs.createEntity(std::string("AvatarSegment_") + jointNameFor(segment));
        auto& skinned = ecs.addComponent<SkinnedRenderable>(entity);
        skinned.riggedMeshHandle = handle;
        skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        skinned.baseColor = applySegmentShadingGradient(segment, colors[i]);
        // Kronos ("Avatar Visual Silhouette Pass" -- "Material Pass"):
        // real, static per-segment roughness -- see
        // segmentMaterialRoughness()'s own comment.
        skinned.roughness = segmentMaterialRoughness(segment);
        // Kronos ("Avatar 2.0" -- "Performance and LOD"): body segments
        // stay AvatarLODCategory::Body (the default) -- see
        // AvatarLODTag's own comment for why this category is never
        // distance-hidden.
        ecs.addComponent<AvatarLODTag>(entity);

        spawned.push_back(entity);
    }

    outSkinnedEntities = std::move(spawned);
    return true;
}

namespace {
// Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "basic cloth shading"): a
// real, small, uniform darkening -- the same real "cheap depth/material
// cue, not a new shader/material system" spirit applySegmentShadingGradient()
// already establishes for the bare body, applied here so a clothing
// shell reads as a distinct (duller, less "plastic") material from the
// skin/body underneath it, not a same-looking layer.
constexpr float kClothingShadingMultiplier = 0.92f;

glm::vec4 resolveClothingColor(const AvatarLoadout& loadout, const CatalogueIndex& index, AvatarItemCategory category,
                                glm::vec4 defaultColor) {
    std::string itemId = loadout.equippedItemId(category);
    if (itemId.empty()) return defaultColor;
    const AvatarItemManifest* manifest = index.findById(itemId);
    return manifest != nullptr ? manifest->item.baseColor : defaultColor; // real, honest fail-soft, same as resolveSegmentColorsForLoadout()
}

bool uploadClothingPiece(ECS& ecs, const Skeleton& skeleton, const std::vector<Vertex>& vertices,
                          const std::vector<uint32_t>& indices, const SkinWeights& skinWeights, glm::vec4 color,
                          const char* entityName, RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator,
                          VkDevice device, VkCommandPool cmdPool, VkQueue queue, EntityId& outEntity,
                          std::string& outError) {
    RiggedMesh riggedMesh;
    std::string error;
    if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices, skinWeights, skeleton, error)) {
        outError = std::string("failed to upload \"") + entityName + "\": " + error;
        return false;
    }
    uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));
    outEntity = ecs.createEntity(entityName);
    auto& skinned = ecs.addComponent<SkinnedRenderable>(outEntity);
    skinned.riggedMeshHandle = handle;
    skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
    skinned.baseColor = glm::vec4(glm::vec3(color) * kClothingShadingMultiplier, color.a);
    // Kronos ("Avatar 2.0" -- "Performance and LOD"): real -- see
    // AvatarLODTag's own comment.
    ecs.addComponent<AvatarLODTag>(outEntity).category = AvatarLODCategory::Clothing;
    return true;
}
} // namespace

bool spawnAvatarClothing(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                          BodyProportions bodyProportions, ClothingFit fit, RiggedMeshLibrary& riggedMeshLibrary,
                          VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                          std::vector<EntityId>& outClothingEntities, std::string& outError) {
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();
    auto worldPos = [&](const char* jointName) -> glm::vec3 {
        int index2 = skeleton.findJointIndex(jointName);
        return index2 >= 0 ? glm::vec3(world[static_cast<size_t>(index2)][3]) : glm::vec3(0.0f);
    };
    auto jointIndexFor = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };

    float shell = clothingFitScaleMultiplier(fit);
    float w = bodyProportions.width;
    float ls = bodyProportions.limbScale;
    std::vector<HumanoidBodySegment> unusedSegments; // real clothing pieces aren't split via extractSegment(), so this tag is never read back

    std::vector<EntityId> spawned;

    // Shirt: a real, scaled-up torso barrel (short-sleeve -- covers the
    // upper arm only, a real, stated, deliberate simplification that
    // avoids clipping through the hand box at the wrist) -- one combined
    // mesh, one real draw call.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        glm::vec3 pelvisPos = worldPos("pelvis");
        glm::vec3 neckPos = worldPos("neck");
        glm::vec3 torsoCenter = (pelvisPos + neckPos) * 0.5f;
        float torsoHalfHeight = glm::length(neckPos - pelvisPos) * 0.5f * 1.03f; // real, slight over-extension so the shell doesn't clip through the neck/waist seam
        std::vector<glm::vec2> shirtProfile = {
            {0.20f * w * shell, 0.12f * w * shell},
            {0.24f * w * shell, 0.14f * w * shell},
            {0.27f * w * shell, 0.14f * w * shell},
        };
        appendProfiledBarrel(vertices, indices, unusedSegments, torsoCenter, torsoHalfHeight, shirtProfile,
                              jointIndexFor("spine_upper"), HumanoidBodySegment::Torso, skinWeights);

        glm::vec2 shoulderCS(0.095f * ls * shell, 0.095f * ls * shell);
        glm::vec2 sleeveCS(0.085f * ls * shell, 0.085f * ls * shell);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("arm_L_upper"), worldPos("arm_L_lower"), shoulderCS,
                          sleeveCS, jointIndexFor("arm_L_upper"), jointIndexFor("arm_L_lower"), HumanoidBodySegment::LeftArm,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("arm_R_upper"), worldPos("arm_R_lower"), shoulderCS,
                          sleeveCS, jointIndexFor("arm_R_upper"), jointIndexFor("arm_R_lower"),
                          HumanoidBodySegment::RightArm, skinWeights);

        glm::vec4 color = resolveClothingColor(loadout, index, AvatarItemCategory::Torso, kDefaultShirtColor);
        EntityId entity;
        if (!uploadClothingPiece(ecs, skeleton, vertices, indices, skinWeights, color, "AvatarClothing_Shirt",
                                  riggedMeshLibrary, allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    // Pants: both real, full legs (hip to ankle), same real smooth-limb
    // technique, scaled outward the same way.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;

        glm::vec2 hipCS(0.13f * ls * shell, 0.13f * ls * shell);
        glm::vec2 kneeCS(0.105f * ls * shell, 0.105f * ls * shell);
        glm::vec2 ankleCS(0.09f * ls * shell, 0.09f * ls * shell); // real, slightly looser than the bare ankle so trouser cuffs don't clip the foot box

        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_L_upper"), worldPos("leg_L_lower"), hipCS,
                          kneeCS, jointIndexFor("leg_L_upper"), jointIndexFor("leg_L_lower"), HumanoidBodySegment::LeftLeg,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_L_lower"), worldPos("foot_L"), kneeCS, ankleCS,
                          jointIndexFor("leg_L_lower"), jointIndexFor("foot_L"), HumanoidBodySegment::LeftLeg, skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_R_upper"), worldPos("leg_R_lower"), hipCS,
                          kneeCS, jointIndexFor("leg_R_upper"), jointIndexFor("leg_R_lower"), HumanoidBodySegment::RightLeg,
                          skinWeights);
        appendSmoothLimb(vertices, indices, unusedSegments, worldPos("leg_R_lower"), worldPos("foot_R"), kneeCS, ankleCS,
                          jointIndexFor("leg_R_lower"), jointIndexFor("foot_R"), HumanoidBodySegment::RightLeg,
                          skinWeights);

        glm::vec4 color = resolveClothingColor(loadout, index, AvatarItemCategory::Legs, kDefaultTrouserColor);
        EntityId entity;
        if (!uploadClothingPiece(ecs, skeleton, vertices, indices, skinWeights, color, "AvatarClothing_Pants",
                                  riggedMeshLibrary, allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    outClothingEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
