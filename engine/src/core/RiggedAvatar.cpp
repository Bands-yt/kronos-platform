#include "core/RiggedAvatar.hpp"

#include "core/CatalogueIndex.hpp"
#include "core/Components.hpp"

namespace engine::core {

namespace {

const char* jointNameFor(HumanoidBodySegment segment) {
    switch (segment) {
        case HumanoidBodySegment::Head: return "head";
        case HumanoidBodySegment::Torso: return "torso";
        case HumanoidBodySegment::LeftArm: return "leftArm";
        case HumanoidBodySegment::RightArm: return "rightArm";
        case HumanoidBodySegment::LeftLeg: return "leftLeg";
        case HumanoidBodySegment::RightLeg: return "rightLeg";
    }
    return "";
}

// Same 24-vertex, 6-face, flat-normal box shape core::Mesh::createBox()
// uploads -- duplicated here as a host-only (no GPU) generator, appending
// into an existing vertex/index array at `center` rather than always at
// the origin, and tagged with which HumanoidBodySegment it belongs to.
// Same "no shared VulkanUtils-style helper TU in this codebase" precedent
// RiggedMesh.cpp's own file-local staging-buffer duplication already set.
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

} // namespace

Skeleton buildHumanoidSkeleton() {
    Skeleton skeleton;
    skeleton.name = "Humanoid";

    Joint hips;
    hips.name = "hips";
    hips.localPosition = {0.0f, 1.0f, 0.0f}; // waist height off the ground
    int hipsIndex = skeleton.addJoint(hips);

    Joint torso;
    torso.name = "torso";
    torso.parentIndex = hipsIndex;
    torso.localPosition = {0.0f, 0.5f, 0.0f};
    int torsoIndex = skeleton.addJoint(torso);

    Joint head;
    head.name = "head";
    head.parentIndex = torsoIndex;
    head.localPosition = {0.0f, 0.55f, 0.0f};
    skeleton.addJoint(head);

    Joint leftArm;
    leftArm.name = "leftArm";
    leftArm.parentIndex = torsoIndex;
    leftArm.localPosition = {-0.65f, 0.15f, 0.0f};
    skeleton.addJoint(leftArm);

    Joint rightArm;
    rightArm.name = "rightArm";
    rightArm.parentIndex = torsoIndex;
    rightArm.localPosition = {0.65f, 0.15f, 0.0f};
    skeleton.addJoint(rightArm);

    Joint leftLeg;
    leftLeg.name = "leftLeg";
    leftLeg.parentIndex = hipsIndex;
    leftLeg.localPosition = {-0.2f, -0.5f, 0.0f};
    skeleton.addJoint(leftLeg);

    Joint rightLeg;
    rightLeg.name = "rightLeg";
    rightLeg.parentIndex = hipsIndex;
    rightLeg.localPosition = {0.2f, -0.5f, 0.0f};
    skeleton.addJoint(rightLeg);

    return skeleton;
}

HumanoidMeshData buildHumanoidMeshData(const Skeleton& skeleton) {
    HumanoidMeshData data;
    std::vector<glm::mat4> world = skeleton.bindPoseMatrices();

    auto centerFor = [&](const char* jointName) -> glm::vec3 {
        int index = skeleton.findJointIndex(jointName);
        return index >= 0 ? glm::vec3(world[static_cast<size_t>(index)][3]) : glm::vec3(0.0f);
    };
    auto jointIndexFor = [&](const char* jointName) { return skeleton.findJointIndex(jointName); };

    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("head"), glm::vec3(0.25f, 0.25f, 0.25f),
              jointIndexFor("head"), HumanoidBodySegment::Head, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("torso"), glm::vec3(0.35f, 0.5f, 0.2f),
              jointIndexFor("torso"), HumanoidBodySegment::Torso, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("leftArm"), glm::vec3(0.12f, 0.35f, 0.12f),
              jointIndexFor("leftArm"), HumanoidBodySegment::LeftArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("rightArm"), glm::vec3(0.12f, 0.35f, 0.12f),
              jointIndexFor("rightArm"), HumanoidBodySegment::RightArm, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("leftLeg"), glm::vec3(0.15f, 0.5f, 0.15f),
              jointIndexFor("leftLeg"), HumanoidBodySegment::LeftLeg, data.skinWeights);
    appendBox(data.vertices, data.indices, data.vertexSegments, centerFor("rightLeg"), glm::vec3(0.15f, 0.5f, 0.15f),
              jointIndexFor("rightLeg"), HumanoidBodySegment::RightLeg, data.skinWeights);

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
