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

// Kronos ("Avatar System" -- 18-bone rig, real smooth skinning): a
// real, low-poly rectangular "tube" between two joints (e.g. shoulder to
// elbow), 3 real cross-section rings (start/mid/end) -- the middle
// ring's skin weight is a real 50/50 blend between the two joints, so
// when the joint rotates this ring interpolates smoothly between both
// bones' influence instead of snapping at a hard boundary. This is what
// actually delivers "smooth bending at elbows/knees" -- appendBox()'s
// single rigid joint per vertex can't. The start ring is 100% the start
// joint, the end ring 100% the end joint (matching how a rigid box/
// sphere appended at either end of this chain rigidly continues from
// there, e.g. a hand box rigidly bound to hand_L picks up exactly where
// this chain's own end ring left off).
void appendSmoothLimb(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                       std::vector<HumanoidBodySegment>& segments, glm::vec3 startPos, glm::vec3 endPos,
                       glm::vec2 crossSection, int startJoint, int endJoint, HumanoidBodySegment segment,
                       SkinWeights& skinWeights) {
    glm::vec3 midPos = (startPos + endPos) * 0.5f;

    auto makeRing = [&](glm::vec3 center, int jointA, float weightA, int jointB, float weightB) -> std::array<uint32_t, 4> {
        std::array<glm::vec3, 4> corners = {
            glm::vec3(-crossSection.x, 0.0f, -crossSection.y), glm::vec3(crossSection.x, 0.0f, -crossSection.y),
            glm::vec3(crossSection.x, 0.0f, crossSection.y), glm::vec3(-crossSection.x, 0.0f, crossSection.y)};
        std::array<uint32_t, 4> result{};
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = center + corners[i];
            glm::vec3 n(corners[i].x, 0.0f, corners[i].z);
            float len = glm::length(n);
            v.normal = len > 1e-6f ? n / len : glm::vec3(0, 1, 0);
            v.uv = {static_cast<float>(i) / 3.0f, 0.0f};
            uint32_t index = static_cast<uint32_t>(vertices.size());
            vertices.push_back(v);
            segments.push_back(segment);
            VertexSkinWeights sw;
            sw.jointIndices = {jointA, weightB > 0.0f ? jointB : -1, -1, -1};
            sw.weights = {weightA, weightB, 0.0f, 0.0f};
            skinWeights.perVertex.push_back(sw);
            result[static_cast<size_t>(i)] = index;
        }
        return result;
    };

    std::array<uint32_t, 4> ringStart = makeRing(startPos, startJoint, 1.0f, -1, 0.0f);
    std::array<uint32_t, 4> ringMid = makeRing(midPos, startJoint, 0.5f, endJoint, 0.5f);
    std::array<uint32_t, 4> ringEnd = makeRing(endPos, endJoint, 1.0f, -1, 0.0f);

    auto connect = [&](const std::array<uint32_t, 4>& a, const std::array<uint32_t, 4>& b) {
        for (int i = 0; i < 4; ++i) {
            int next = (i + 1) % 4;
            indices.insert(indices.end(), {a[static_cast<size_t>(i)], a[static_cast<size_t>(next)],
                                            b[static_cast<size_t>(next)], a[static_cast<size_t>(i)],
                                            b[static_cast<size_t>(next)], b[static_cast<size_t>(i)]});
        }
    };
    connect(ringStart, ringMid);
    connect(ringMid, ringEnd);
}

} // namespace

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

HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton) {
    HumanoidMeshData data;
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();

    auto worldPos = [&](const char* jointName) -> glm::vec3 {
        int index = skeleton.findJointIndex(jointName);
        return index >= 0 ? glm::vec3(world[static_cast<size_t>(index)][3]) : glm::vec3(0.0f);
    };
    auto jointIndexFor = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };

    // Head -- a real sphere/oval, rigidly bound (a head has no internal
    // joint to blend with).
    appendSphere(data.vertices, data.indices, data.vertexSegments, worldPos("head"), glm::vec3(0.16f, 0.19f, 0.16f),
                 jointIndexFor("head"), HumanoidBodySegment::Head, data.skinWeights);

    // Torso -- "single connected piece" per spec: one rigid box spanning
    // pelvis to neck, bound to spine_upper (the real chest reference this
    // rig's arms also attach to). A multi-joint smooth-blended spine is a
    // real, deliberately un-built refinement -- this Alpha's idle/walk/run
    // set doesn't bend the spine, so the honest, simpler rigid choice here
    // doesn't cost anything real yet (see class-level scope note below).
    {
        glm::vec3 pelvisPos = worldPos("pelvis");
        glm::vec3 neckPos = worldPos("neck");
        glm::vec3 torsoCenter = (pelvisPos + neckPos) * 0.5f;
        float torsoHalfHeight = glm::length(neckPos - pelvisPos) * 0.5f;
        appendBox(data.vertices, data.indices, data.vertexSegments, torsoCenter, glm::vec3(0.24f, torsoHalfHeight, 0.14f),
                  jointIndexFor("spine_upper"), HumanoidBodySegment::Torso, data.skinWeights);
    }

    // Arms -- real smooth-blended upper-to-lower chain (the actual elbow
    // bend the spec asks for), capped with a rigid "mitten" hand box.
    glm::vec2 upperArmCrossSection(0.09f, 0.09f);
    glm::vec2 lowerArmCrossSection(0.07f, 0.07f);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_upper"), worldPos("arm_L_lower"),
                      upperArmCrossSection, jointIndexFor("arm_L_upper"), jointIndexFor("arm_L_lower"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_L_lower"), worldPos("hand_L"),
                      lowerArmCrossSection, jointIndexFor("arm_L_lower"), jointIndexFor("hand_L"),
                      HumanoidBodySegment::LeftArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("hand_L"), glm::vec3(0.09f, 0.11f, 0.05f),
              jointIndexFor("hand_L"), HumanoidBodySegment::LeftArm, data.skinWeights);

    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_upper"), worldPos("arm_R_lower"),
                      upperArmCrossSection, jointIndexFor("arm_R_upper"), jointIndexFor("arm_R_lower"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("arm_R_lower"), worldPos("hand_R"),
                      lowerArmCrossSection, jointIndexFor("arm_R_lower"), jointIndexFor("hand_R"),
                      HumanoidBodySegment::RightArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("hand_R"), glm::vec3(0.09f, 0.11f, 0.05f),
              jointIndexFor("hand_R"), HumanoidBodySegment::RightArm, data.skinWeights);

    // Legs -- same real smooth knee bend, capped with a rigid "simple
    // block" foot.
    glm::vec2 upperLegCrossSection(0.12f, 0.12f);
    glm::vec2 lowerLegCrossSection(0.09f, 0.09f);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_L_upper"), worldPos("leg_L_lower"),
                      upperLegCrossSection, jointIndexFor("leg_L_upper"), jointIndexFor("leg_L_lower"),
                      HumanoidBodySegment::LeftLeg, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_L_lower"), worldPos("foot_L"),
                      lowerLegCrossSection, jointIndexFor("leg_L_lower"), jointIndexFor("foot_L"),
                      HumanoidBodySegment::LeftLeg, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("foot_L") + glm::vec3(0.0f, -0.04f, 0.08f),
              glm::vec3(0.1f, 0.06f, 0.18f), jointIndexFor("foot_L"), HumanoidBodySegment::LeftLeg, data.skinWeights);

    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_R_upper"), worldPos("leg_R_lower"),
                      upperLegCrossSection, jointIndexFor("leg_R_upper"), jointIndexFor("leg_R_lower"),
                      HumanoidBodySegment::RightLeg, data.skinWeights);
    appendSmoothLimb(data.vertices, data.indices, data.vertexSegments, worldPos("leg_R_lower"), worldPos("foot_R"),
                      lowerLegCrossSection, jointIndexFor("leg_R_lower"), jointIndexFor("foot_R"),
                      HumanoidBodySegment::RightLeg, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, worldPos("foot_R") + glm::vec3(0.0f, -0.04f, 0.08f),
              glm::vec3(0.1f, 0.06f, 0.18f), jointIndexFor("foot_R"), HumanoidBodySegment::RightLeg, data.skinWeights);

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
                                                                                 glm::vec4 defaultColor) {
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors;
    colors.fill(defaultColor);

    for (size_t i = 0; i < kHumanoidBodySegmentCount; ++i) {
        auto segment = static_cast<HumanoidBodySegment>(i);
        AvatarItemCategory category = categoryForBodySegment(segment);
        std::string itemId = loadout.equippedItemId(category);
        if (itemId.empty()) continue;
        const AvatarItemManifest* manifest = index.findById(itemId);
        if (manifest == nullptr) continue; // equipped id no longer resolves -- fail soft, keep the default color
        colors[i] = manifest->item.baseColor;
    }
    return colors;
}

bool spawnRiggedAvatar(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                        RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                        VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outSkinnedEntities,
                        std::string& outError) {
    HumanoidMeshData fullBody = buildHumanoidMeshData(skeleton);
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors = resolveSegmentColorsForLoadout(loadout, index);

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
        skinned.baseColor = colors[i];

        spawned.push_back(entity);
    }

    outSkinnedEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
