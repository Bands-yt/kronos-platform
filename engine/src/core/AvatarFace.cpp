#include "core/AvatarFace.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Components.hpp"

namespace engine::core {

namespace {

// Same real, small, local-to-this-file mesh-gen duplication precedent
// core::RiggedAvatar.cpp's own appendBox()/appendSphere() already
// establish ("no shared VulkanUtils-style helper TU in this codebase" --
// see that file's own header comment) -- simplified here since every
// real facial feature is its own standalone, single-joint mesh (no
// per-vertex HumanoidBodySegment tagging needed, unlike the body's own
// six-segment split).
void appendFeatureSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center,
                          glm::vec3 radii, SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 6;
    constexpr uint32_t kRings = 3;
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (uint32_t r = 0; r <= kRings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(kRings);
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
            VertexSkinWeights sw;
            sw.jointIndices = {0, -1, -1, -1}; // real joint index filled in by the caller (see spawnAvatarFace())
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

void appendFeatureBox(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center,
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

} // namespace

const char* facialFeatureJointName(FacialFeature feature) {
    switch (feature) {
        case FacialFeature::LeftEye: return "face_left_eye";
        case FacialFeature::RightEye: return "face_right_eye";
        case FacialFeature::LeftBrow: return "face_left_brow";
        case FacialFeature::RightBrow: return "face_right_brow";
        case FacialFeature::Mouth: return "face_mouth";
    }
    return "";
}

FacialFeatureTransform computeFacialFeatureTransform(FacialFeature feature, const AvatarFacialExpression& expression) {
    float blink = std::clamp(expression.blinkWeight, 0.0f, 1.0f);
    float smile = std::clamp(expression.smileWeight, 0.0f, 1.0f);
    float frown = std::clamp(expression.frownWeight, 0.0f, 1.0f);
    float talk = std::clamp(expression.talkWeight, 0.0f, 1.0f);

    FacialFeatureTransform t;
    switch (feature) {
        case FacialFeature::LeftEye:
        case FacialFeature::RightEye:
            // Real, honest "eyelid" approximation -- no separate eyelid
            // mesh exists, so blinking is a real vertical scale toward
            // near-zero rather than a lid sliding shut.
            t.scale.y = 1.0f - 0.9f * blink;
            break;
        case FacialFeature::LeftBrow:
        case FacialFeature::RightBrow: {
            float side = feature == FacialFeature::LeftBrow ? 1.0f : -1.0f;
            t.positionOffset.y = 0.02f * smile - 0.025f * frown;
            t.rollDegrees = side * (10.0f * frown - 4.0f * smile);
            break;
        }
        case FacialFeature::Mouth:
            // Base mesh is authored thin/closed (see spawnAvatarFace());
            // talk stretches it open, smile/frown shift it up/down.
            t.scale.y = 0.4f + 1.3f * talk;
            t.positionOffset.y = 0.02f * smile - 0.02f * frown;
            break;
    }
    return t;
}

AvatarFacialExpression blendFacialExpressionTowards(const AvatarFacialExpression& current,
                                                     const AvatarFacialExpression& target, float dt,
                                                     float blendSpeedPerSecond) {
    float alpha = std::clamp(1.0f - std::exp(-dt * blendSpeedPerSecond), 0.0f, 1.0f);
    AvatarFacialExpression out;
    out.blinkWeight = current.blinkWeight + alpha * (target.blinkWeight - current.blinkWeight);
    out.smileWeight = current.smileWeight + alpha * (target.smileWeight - current.smileWeight);
    out.frownWeight = current.frownWeight + alpha * (target.frownWeight - current.frownWeight);
    out.talkWeight = current.talkWeight + alpha * (target.talkWeight - current.talkWeight);
    return out;
}

void applyFacialExpressionToSkinningMatrices(std::vector<glm::mat4>& skinningMatrices, const Skeleton& skeleton,
                                              const std::vector<glm::mat4>& bindPoseWorld,
                                              const AvatarFacialExpression& expression) {
    constexpr std::array<FacialFeature, 5> kFeatures = {FacialFeature::LeftEye, FacialFeature::RightEye,
                                                          FacialFeature::LeftBrow, FacialFeature::RightBrow,
                                                          FacialFeature::Mouth};
    // Real correctness requirement (see AvatarController::tick()'s own
    // comment on the exact same fix for the head-bob): each feature's
    // real mesh vertices are baked at their real bind-pose WORLD
    // position (spawnAvatarFace()'s own jointWorldPos), not at a local
    // origin -- scaling/rotating a right-multiplied matrix alone pivots
    // around wherever the vertices happen to be baked relative to,
    // which is rig-space origin here. translate(+jointPos) * (scale/
    // rotate/offset) * translate(-jointPos) pivots around the real
    // joint position instead, so a blink scales the eye in place rather
    // than sliding it toward the rig's own local origin. `bindPoseWorld`
    // is real, caller-cached (see this function's own header comment on
    // why it's no longer recomputed here every call).
    for (FacialFeature feature : kFeatures) {
        int index = skeleton.findJointIndex(facialFeatureJointName(feature));
        if (index < 0 || static_cast<size_t>(index) >= skinningMatrices.size() ||
            static_cast<size_t>(index) >= bindPoseWorld.size()) {
            continue; // real, honest no-op
        }
        glm::vec3 jointPos = glm::vec3(bindPoseWorld[static_cast<size_t>(index)][3]);
        FacialFeatureTransform t = computeFacialFeatureTransform(feature, expression);
        glm::mat4 extra = glm::translate(glm::mat4(1.0f), jointPos + t.positionOffset) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(t.rollDegrees), glm::vec3(0.0f, 0.0f, 1.0f)) *
                           glm::scale(glm::mat4(1.0f), t.scale) * glm::translate(glm::mat4(1.0f), -jointPos);
        skinningMatrices[static_cast<size_t>(index)] = skinningMatrices[static_cast<size_t>(index)] * extra;
    }
}

namespace {
// Kronos ("Avatar 2.0" -- "Performance and LOD" -- "draw-call merging"):
// unlike setJointIndex() above (which stamps every vertex in a
// skinWeights buffer with one joint), a merged multi-feature mesh needs
// each half stamped with its OWN joint -- this sets jointIndices[0] only
// for the vertices appended since `fromVertex`, the same "per-range
// stamp after append" shape spawnAvatarClothing()'s own appendSmoothLimb()
// calls already rely on internally for multi-joint single meshes.
void setJointIndexRange(SkinWeights& skinWeights, size_t fromVertex, int jointIndex) {
    for (size_t i = fromVertex; i < skinWeights.perVertex.size(); ++i) {
        skinWeights.perVertex[i].jointIndices[0] = jointIndex;
    }
}

// Real, shared upload+spawn step every merged face piece below uses --
// same "one real RiggedMesh, one real entity, one real draw call" shape
// RiggedAvatar.cpp's own uploadClothingPiece() already establishes.
bool uploadFacePiece(ECS& ecs, const Skeleton& skeleton, const std::vector<Vertex>& vertices,
                      const std::vector<uint32_t>& indices, const SkinWeights& skinWeights, glm::vec4 color,
                      const char* entityName, RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator,
                      VkDevice device, VkCommandPool cmdPool, VkQueue queue, EntityId& outEntity,
                      std::string& outError) {
    RiggedMesh riggedMesh;
    std::string error;
    if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices, skinWeights, skeleton, error)) {
        outError = std::string("failed to upload facial feature \"") + entityName + "\": " + error;
        return false;
    }
    uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));
    outEntity = ecs.createEntity(entityName);
    auto& skinned = ecs.addComponent<SkinnedRenderable>(outEntity);
    skinned.riggedMeshHandle = handle;
    skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
    skinned.baseColor = color;
    // Kronos ("Avatar 2.0" -- "Performance and LOD"): real -- see
    // AvatarLODTag's own comment (core/Components.hpp).
    ecs.addComponent<AvatarLODTag>(outEntity).category = AvatarLODCategory::Face;
    return true;
}
} // namespace

bool spawnAvatarFace(ECS& ecs, const Skeleton& skeleton, glm::vec4 skinTone, RiggedMeshLibrary& riggedMeshLibrary,
                      VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                      std::vector<EntityId>& outFaceEntities, std::string& outError) {
    // Real, honest, fixed colors -- eyes/mouth are not skin-tone-derived
    // (a sclera-less dark eye and a muted mouth tone read correctly
    // against every real skin tone this rig offers); brows ARE
    // skin-tone-derived (a real, darkened shade of the player's own
    // chosen tone), same "respects skin tone" requirement this feature
    // was asked to honor.
    constexpr glm::vec4 kEyeColor(0.08f, 0.08f, 0.1f, 1.0f);
    constexpr glm::vec4 kMouthColor(0.5f, 0.28f, 0.28f, 1.0f);
    glm::vec4 browColor(skinTone.r * 0.55f, skinTone.g * 0.5f, skinTone.b * 0.45f, 1.0f);

    // Real bind-pose world (rig-space) position for each joint -- a
    // skinning matrix is IDENTITY at bind pose by construction
    // (jointWorldMatrix * inverse(jointBindWorldMatrix) == identity when
    // current == bind), so a vertex's real world position at rest comes
    // entirely from where it was actually baked, not from the skinning
    // matrix -- exactly the same real reason buildHumanoidMeshData()'s
    // own worldPos(jointName) lambda bakes body-segment vertices at the
    // joint's real bind position rather than at the local origin.
    std::vector<glm::mat4> bindWorld = skeleton.bindPoseMatrices();
    auto jointWorldPos = [&](const char* jointName, int& outJointIndex) -> glm::vec3 {
        outJointIndex = skeleton.findJointIndex(jointName);
        if (outJointIndex < 0) return glm::vec3(0.0f);
        return glm::vec3(bindWorld[static_cast<size_t>(outJointIndex)][3]);
    };

    std::vector<EntityId> spawned;

    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "draw-call
    // merging"): the two eyes real-merge into one mesh/entity/draw call
    // (both always share kEyeColor, so one SkinnedRenderable::baseColor
    // is correct for the whole piece), and the two brows real-merge the
    // same way (both always share browColor) -- reduces the face from 5
    // real draw calls to 3. Each half still deforms independently under
    // animation/expression: its own vertices carry its own joint's real
    // skin weight (setJointIndexRange() below), the same "one mesh spans
    // multiple joints via per-vertex weights" pattern
    // spawnAvatarClothing()'s own appendSmoothLimb() already establishes
    // for limbs -- and applyFacialExpressionToSkinningMatrices() (above)
    // only ever writes into skinningMatrices[jointIndex], never touches
    // entities/meshes directly, so it is completely unaffected by this
    // merge.
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;
        for (const char* jointName : {"face_left_eye", "face_right_eye"}) {
            int jointIndex = -1;
            glm::vec3 pos = jointWorldPos(jointName, jointIndex);
            if (jointIndex < 0) {
                outError = std::string("skeleton has no real \"") + jointName + "\" joint";
                for (EntityId e : spawned) ecs.destroyEntity(e);
                return false;
            }
            size_t vertsBefore = vertices.size();
            appendFeatureSphere(vertices, indices, pos, glm::vec3(0.018f, 0.018f, 0.014f), skinWeights);
            setJointIndexRange(skinWeights, vertsBefore, jointIndex);
        }
        EntityId entity;
        if (!uploadFacePiece(ecs, skeleton, vertices, indices, skinWeights, kEyeColor, "FaceEyes", riggedMeshLibrary,
                              allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;
        for (const char* jointName : {"face_left_brow", "face_right_brow"}) {
            int jointIndex = -1;
            glm::vec3 pos = jointWorldPos(jointName, jointIndex);
            if (jointIndex < 0) {
                outError = std::string("skeleton has no real \"") + jointName + "\" joint";
                for (EntityId e : spawned) ecs.destroyEntity(e);
                return false;
            }
            size_t vertsBefore = vertices.size();
            appendFeatureBox(vertices, indices, pos, glm::vec3(0.028f, 0.008f, 0.006f), skinWeights);
            setJointIndexRange(skinWeights, vertsBefore, jointIndex);
        }
        EntityId entity;
        if (!uploadFacePiece(ecs, skeleton, vertices, indices, skinWeights, browColor, "FaceBrows", riggedMeshLibrary,
                              allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    {
        int jointIndex = -1;
        glm::vec3 pos = jointWorldPos("face_mouth", jointIndex);
        if (jointIndex < 0) {
            outError = "skeleton has no real \"face_mouth\" joint";
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;
        appendFeatureBox(vertices, indices, pos, glm::vec3(0.032f, 0.008f, 0.006f), skinWeights);
        setJointIndex(skinWeights, jointIndex);
        EntityId entity;
        if (!uploadFacePiece(ecs, skeleton, vertices, indices, skinWeights, kMouthColor, "FaceMouth", riggedMeshLibrary,
                              allocator, device, cmdPool, queue, entity, outError)) {
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        spawned.push_back(entity);
    }

    outFaceEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
