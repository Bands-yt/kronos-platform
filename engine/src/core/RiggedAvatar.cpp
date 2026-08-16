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
    skeleton.addJoint(head);

    Joint armLUpper;
    armLUpper.name = "arm_L_upper";
    armLUpper.parentIndex = spineUpperIndex;
    armLUpper.localPosition = {-0.25f, 0.1f, 0.0f};
    int armLUpperIndex = skeleton.addJoint(armLUpper);

    Joint armLLower;
    armLLower.name = "arm_L_lower";
    armLLower.parentIndex = armLUpperIndex;
    armLLower.localPosition = {-0.32f, 0.0f, 0.0f};
    int armLLowerIndex = skeleton.addJoint(armLLower);

    Joint handL;
    handL.name = "hand_L";
    handL.parentIndex = armLLowerIndex;
    handL.localPosition = {-0.28f, 0.0f, 0.0f};
    skeleton.addJoint(handL);

    Joint armRUpper;
    armRUpper.name = "arm_R_upper";
    armRUpper.parentIndex = spineUpperIndex;
    armRUpper.localPosition = {0.25f, 0.1f, 0.0f};
    int armRUpperIndex = skeleton.addJoint(armRUpper);

    Joint armRLower;
    armRLower.name = "arm_R_lower";
    armRLower.parentIndex = armRUpperIndex;
    armRLower.localPosition = {0.32f, 0.0f, 0.0f};
    int armRLowerIndex = skeleton.addJoint(armRLower);

    Joint handR;
    handR.name = "hand_R";
    handR.parentIndex = armRLowerIndex;
    handR.localPosition = {0.28f, 0.0f, 0.0f};
    skeleton.addJoint(handR);

    Joint legLUpper;
    legLUpper.name = "leg_L_upper";
    legLUpper.parentIndex = pelvisIndex;
    legLUpper.localPosition = {-0.18f, -0.1f, 0.0f};
    int legLUpperIndex = skeleton.addJoint(legLUpper);

    Joint legLLower;
    legLLower.name = "leg_L_lower";
    legLLower.parentIndex = legLUpperIndex;
    legLLower.localPosition = {0.0f, -0.45f, 0.0f};
    int legLLowerIndex = skeleton.addJoint(legLLower);

    Joint footL;
    footL.name = "foot_L";
    footL.parentIndex = legLLowerIndex;
    footL.localPosition = {0.0f, -0.45f, 0.05f};
    skeleton.addJoint(footL);

    Joint legRUpper;
    legRUpper.name = "leg_R_upper";
    legRUpper.parentIndex = pelvisIndex;
    legRUpper.localPosition = {0.18f, -0.1f, 0.0f};
    int legRUpperIndex = skeleton.addJoint(legRUpper);

    Joint legRLower;
    legRLower.name = "leg_R_lower";
    legRLower.parentIndex = legRUpperIndex;
    legRLower.localPosition = {0.0f, -0.45f, 0.0f};
    int legRLowerIndex = skeleton.addJoint(legRLower);

    Joint footR;
    footR.name = "foot_R";
    footR.parentIndex = legRLowerIndex;
    footR.localPosition = {0.0f, -0.45f, 0.05f};
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

    // Head -- a real ellipsoid, rigidly bound (a head has no internal
    // joint to blend with). Real, chosen radii per headShape -- see
    // HeadShape's own header comment.
    appendSphere(data.vertices, data.indices, data.vertexSegments, worldPos("head"), headShapeRadii(headShape),
                 jointIndexFor("head"), HumanoidBodySegment::Head, data.skinWeights);

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
        // shoulder silhouette" (the top ring is the widest).
        std::vector<glm::vec2> torsoProfile = {
            {0.20f * w, 0.12f * w}, // waist (bottom)
            {0.24f * w, 0.14f * w}, // chest (mid)
            {0.27f * w, 0.14f * w}, // shoulders (top)
        };
        appendProfiledBarrel(data.vertices, data.indices, data.vertexSegments, torsoCenter, torsoHalfHeight,
                              torsoProfile, jointIndexFor("spine_upper"), HumanoidBodySegment::Torso, data.skinWeights);
    }

    // Arms -- real smooth-blended upper-to-lower chain (the actual elbow
    // bend the spec asks for), capped with a rigid "mitten" hand box. A
    // real, slight taper (shoulder wider than elbow, elbow wider than
    // wrist) -- continuous across the elbow (upper arm's own end radius
    // equals lower arm's own start radius, so there's no visible step).
    // Cross-sections and the hand box scale with `bodyProportions.limbScale`
    // only -- see that field's own header comment.
    float ls = bodyProportions.limbScale;
    glm::vec2 shoulderCrossSection(0.095f * ls, 0.095f * ls);
    glm::vec2 elbowCrossSection(0.075f * ls, 0.075f * ls);
    glm::vec2 wristCrossSection(0.065f * ls, 0.065f * ls);
    glm::vec3 handBoxHalfExtents(0.09f * ls, 0.11f * ls, 0.05f * ls);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_upper"), worldPos("arm_L_lower"),
                      shoulderCrossSection, elbowCrossSection, jointIndexFor("arm_L_upper"), jointIndexFor("arm_L_lower"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_lower"), worldPos("hand_L"),
                      elbowCrossSection, wristCrossSection, jointIndexFor("arm_L_lower"), jointIndexFor("hand_L"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("hand_L"), handBoxHalfExtents,
              jointIndexFor("hand_L"), HumanoidBodySegment::LeftArm, data.skinWeights);

    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_upper"), worldPos("arm_R_lower"),
                      shoulderCrossSection, elbowCrossSection, jointIndexFor("arm_R_upper"), jointIndexFor("arm_R_lower"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_lower"), worldPos("hand_R"),
                      elbowCrossSection, wristCrossSection, jointIndexFor("arm_R_lower"), jointIndexFor("hand_R"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("hand_R"), handBoxHalfExtents,
              jointIndexFor("hand_R"), HumanoidBodySegment::RightArm, data.skinWeights);

    // Legs -- same real smooth knee bend (continuous taper across it, same
    // reasoning as the arms above), capped with a rigid "simple block"
    // foot. Same limbScale-only scaling as the arms above.
    glm::vec2 hipCrossSection(0.13f * ls, 0.13f * ls);
    glm::vec2 kneeCrossSection(0.105f * ls, 0.105f * ls);
    glm::vec2 ankleCrossSection(0.085f * ls, 0.085f * ls);
    glm::vec3 footBoxHalfExtents(0.1f * ls, 0.06f * ls, 0.18f * ls);
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

        spawned.push_back(entity);
    }

    outSkinnedEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
