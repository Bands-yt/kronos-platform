#include "core/AvatarAccessories.hpp"

#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/AvatarItem.hpp"
#include "core/AvatarItemManifest.hpp"
#include "core/Components.hpp"

namespace engine::core {

namespace {

// Same real, small, local-to-this-file mesh-gen duplication precedent
// core::AvatarFace.cpp's own appendFeatureBox() already establishes (see
// that file's own header comment on why this codebase duplicates small
// primitive generators per-file rather than sharing one).
void appendAccessoryBox(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center,
                         glm::vec3 halfExtents, SkinWeights& skinWeights) {
    glm::vec3 h = halfExtents;
    uint32_t base = static_cast<uint32_t>(vertices.size());
    std::array<Vertex, 24> box = {{
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}},   {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}}, {{h.x, h.y, -h.z}, {0, 1, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {1, 1}},   {{-h.x, h.y, h.z}, {0, 1, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {1, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {1, 1}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, 1}},
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}},  {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}},  {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}}, {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    }};
    for (auto& v : box) {
        v.position += center;
        vertices.push_back(v);
        VertexSkinWeights sw;
        sw.jointIndices = {0, -1, -1, -1};
        sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        skinWeights.perVertex.push_back(sw);
    }
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t faceBase = base + face * 4;
        indices.insert(indices.end(), {faceBase, faceBase + 1, faceBase + 2, faceBase, faceBase + 2, faceBase + 3});
    }
}

void setJointIndex(SkinWeights& skinWeights, int jointIndex) {
    for (VertexSkinWeights& sw : skinWeights.perVertex) sw.jointIndices[0] = jointIndex;
}

struct AccessorySlotSpec {
    AvatarItemCategory category;
    const char* jointName;
    glm::vec3 halfExtents;
    const char* entityName;
};
// Kronos ("Avatar 2.0" -- "Accessory Rigging"): the real, five slots the
// spec names -- hats/face items/hair/back items/handhelds. `Accessory`
// (the catalogue category, see AvatarItem.hpp) is the real, existing
// "handheld" category -- rigged to the real, already-existing hand_R
// joint (a right-hand-by-convention default, not a new joint).
const std::array<AccessorySlotSpec, 5> kAccessorySlots = {{
    {AvatarItemCategory::Head, "attach_hat", {0.11f, 0.05f, 0.11f}, "AccessoryHat"},
    {AvatarItemCategory::Hair, "attach_hair", {0.1f, 0.04f, 0.09f}, "AccessoryHair"},
    {AvatarItemCategory::Face, "attach_face_accessory", {0.09f, 0.015f, 0.01f}, "AccessoryFace"},
    {AvatarItemCategory::Back, "attach_back", {0.12f, 0.14f, 0.03f}, "AccessoryBack"},
    {AvatarItemCategory::Accessory, "hand_R", {0.03f, 0.03f, 0.09f}, "AccessoryHandheld"},
}};

} // namespace

bool spawnAvatarAccessories(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, const CatalogueIndex& index,
                             RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                             VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outAccessoryEntities,
                             std::string& outError) {
    std::vector<glm::mat4> bindWorld = skeleton.bindPoseMatrices();
    std::vector<EntityId> spawned;

    for (const AccessorySlotSpec& slot : kAccessorySlots) {
        std::string itemId = loadout.equippedItemId(slot.category);
        if (itemId.empty()) continue; // real, honest -- nothing equipped, nothing spawned

        const AvatarItemManifest* manifest = index.findById(itemId);
        if (manifest == nullptr) continue; // real, honest fail-soft, same as resolveSegmentColorsForLoadout()

        int jointIndex = skeleton.findJointIndex(slot.jointName);
        if (jointIndex < 0) {
            outError = std::string("skeleton has no real \"") + slot.jointName + "\" joint";
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        glm::vec3 jointWorldPos = glm::vec3(bindWorld[static_cast<size_t>(jointIndex)][3]);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;
        appendAccessoryBox(vertices, indices, jointWorldPos, slot.halfExtents, skinWeights);
        setJointIndex(skinWeights, jointIndex);

        RiggedMesh riggedMesh;
        std::string uploadError;
        if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices, skinWeights, skeleton,
                                        uploadError)) {
            outError = std::string("failed to upload \"") + slot.entityName + "\": " + uploadError;
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));

        EntityId entity = ecs.createEntity(slot.entityName);
        auto& skinned = ecs.addComponent<SkinnedRenderable>(entity);
        skinned.riggedMeshHandle = handle;
        skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        skinned.baseColor = manifest->item.baseColor;
        spawned.push_back(entity);
    }

    outAccessoryEntities = std::move(spawned);
    return true;
}

float computeBackAccessorySwayDegrees(float phase) { return 3.0f * std::sin(phase); }

void applyAccessoryDynamicsToSkinningMatrices(std::vector<glm::mat4>& skinningMatrices, const Skeleton& skeleton,
                                               float phase) {
    int index = skeleton.findJointIndex("attach_back");
    if (index < 0 || static_cast<size_t>(index) >= skinningMatrices.size()) return;
    std::vector<glm::mat4> bindWorld = skeleton.bindPoseMatrices();
    if (static_cast<size_t>(index) >= bindWorld.size()) return;
    glm::vec3 jointPos = glm::vec3(bindWorld[static_cast<size_t>(index)][3]);
    float swayDegrees = computeBackAccessorySwayDegrees(phase);
    glm::mat4 sway = glm::translate(glm::mat4(1.0f), jointPos) *
                      glm::rotate(glm::mat4(1.0f), glm::radians(swayDegrees), glm::vec3(1.0f, 0.0f, 0.0f)) *
                      glm::translate(glm::mat4(1.0f), -jointPos);
    skinningMatrices[static_cast<size_t>(index)] = skinningMatrices[static_cast<size_t>(index)] * sway;
}

} // namespace engine::core
