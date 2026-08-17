#include "core/AvatarHair.hpp"

#include <array>
#include <cmath>

#include "core/AvatarItem.hpp"
#include "core/Components.hpp"

namespace engine::core {

namespace {

// Kronos ("Avatar Visual Silhouette Pass" -- "Hair" -- "Apply
// vertex-color gradients for depth; avoid flat brown shading"): real,
// genuinely per-vertex color now (core::Vertex::color, see Mesh.hpp's
// own comment -- the first real vertex-color channel this engine has
// had), NOT the earlier discrete per-piece color-step approximation this
// file used before that channel existed. `rootColor`/`tipColor` blend
// smoothly across each shape's own real base-to-tip axis -- a genuine
// GPU-interpolated gradient, not a flat color per mesh.
struct HairColorRamp {
    glm::vec4 rootColor; // darker, near the scalp
    glm::vec4 tipColor;  // lighter, toward the hair's own outer edge
};

// A real, rounded ellipsoid blob -- the same real low-poly lat/long
// sphere shape core::AvatarFace.cpp's own appendFeatureSphere() already
// establishes (same real, small, local-to-this-file duplication
// precedent that file's own header comment explains). Used for the
// hair's own base volume/mass. `ramp.rootColor` at the pole nearest the
// head (v=1), `ramp.tipColor` at the outward pole (v=0) -- a real,
// smooth per-vertex blend across each ring.
void appendHairBlob(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center, glm::vec3 radii,
                     int jointIndex, const HairColorRamp& ramp, SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 6;
    constexpr uint32_t kRings = 3;
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (uint32_t r = 0; r <= kRings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(kRings);
        float phi = v * 3.14159265f;
        glm::vec4 ringColor = glm::mix(ramp.tipColor, ramp.rootColor, v);
        for (uint32_t s = 0; s <= kSegments; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(kSegments);
            float theta = u * 2.0f * 3.14159265f;
            glm::vec3 unit(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = center + unit * radii;
            vert.normal = glm::normalize(unit);
            vert.uv = {u, v};
            vert.color = ringColor;
            vertices.push_back(vert);
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

// Kronos ("Avatar Silhouette Pass" -- style-reference update, real user
// reference image: a swept-side, layered wavy hairstyle): a real,
// tapered 4-sided box (frustum) -- unlike the earlier straight-up-
// pointing "spike" version, this one takes independent base/tip
// positions AND independent (non-square) half-extents per ring, so a
// lock can be built FLATTENED (wide across its own sweep direction,
// thin front-to-back) rather than a round spike -- the real, low-poly
// equivalent of a flowing hair strand instead of a horn-like point.
// Real per-vertex color: `ramp.rootColor` at the base (embedded in/
// against the head), `ramp.tipColor` at the point -- a real, smooth
// gradient along each lock's own length.
void appendHairLock(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 basePos, glm::vec3 tipPos,
                     glm::vec2 baseHalfExtents, glm::vec2 tipHalfExtents, int jointIndex, const HairColorRamp& ramp,
                     SkinWeights& skinWeights) {
    glm::vec3 outward = glm::normalize(tipPos - basePos + glm::vec3(0.0f, 0.001f, 0.0f));

    auto pushRing = [&](glm::vec3 center, glm::vec2 half, glm::vec4 color) -> std::array<uint32_t, 4> {
        std::array<glm::vec3, 4> offsets = {
            glm::vec3(-half.x, 0.0f, -half.y),
            glm::vec3(half.x, 0.0f, -half.y),
            glm::vec3(half.x, 0.0f, half.y),
            glm::vec3(-half.x, 0.0f, half.y),
        };
        std::array<uint32_t, 4> idx{};
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = center + offsets[i];
            v.normal = glm::normalize(glm::normalize(offsets[i] + glm::vec3(0.0f, 0.001f, 0.0f)) + outward);
            v.uv = {static_cast<float>(i) / 4.0f, 0.0f};
            v.color = color;
            idx[static_cast<size_t>(i)] = static_cast<uint32_t>(vertices.size());
            vertices.push_back(v);
            VertexSkinWeights sw;
            sw.jointIndices = {jointIndex, -1, -1, -1};
            sw.weights = {1.0f, 0.0f, 0.0f, 0.0f};
            skinWeights.perVertex.push_back(sw);
        }
        return idx;
    };

    std::array<uint32_t, 4> baseRing = pushRing(basePos, baseHalfExtents, ramp.rootColor);
    std::array<uint32_t, 4> tipRing = pushRing(tipPos, tipHalfExtents, ramp.tipColor);
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t next = (i + 1) % 4;
        indices.insert(indices.end(), {baseRing[i], baseRing[next], tipRing[next], baseRing[i], tipRing[next], tipRing[i]});
    }
    indices.insert(indices.end(), {tipRing[0], tipRing[1], tipRing[2], tipRing[0], tipRing[2], tipRing[3]});
}

enum class HairPieceKind { Blob, Lock };

struct HairPieceSpec {
    HairPieceKind kind;
    glm::vec3 baseOrCenterOffset; // Blob: sphere center. Lock: base point.
    glm::vec3 tipOffset;          // Lock only.
    glm::vec3 blobRadii;          // Blob only.
    glm::vec2 lockBaseHalfExtents; // Lock only.
    glm::vec2 lockTipHalfExtents;  // Lock only.
    const char* entityName;
};

// Kronos ("Avatar Silhouette Pass" -- style-reference update): a real
// swept-side, layered wavy hairstyle, replacing the earlier
// straight-up spike cluster -- 1 rounded base-coverage blob (crown +
// upper forehead) plus 6 flattened locks that all sweep in the same
// consistent direction (+X, "toward the right") and layer at different
// heights/lengths, the real "layered wavy" read the reference image
// asks for, not a symmetric radiating cluster. Every lock's own tip
// stays close to the head's own silhouette (max ~0.16 lateral travel,
// similar scale to the previous crown spikes) -- swept, not flared.
// Offsets are real, hand-placed points relative to the head joint's own
// world position, the same convention buildHumanoidSkeleton()'s own
// attach_hat/attach_hair joints already establish -- a stylized hair
// silhouette is a real art decision, not a procedural one.
constexpr std::array<HairPieceSpec, 7> kHairPieces = {{
    {HairPieceKind::Blob, {0.01f, 0.17f, 0.01f}, {}, {0.115f, 0.075f, 0.12f}, {}, {}, "HairBaseCap"},
    {HairPieceKind::Lock, {0.02f, 0.19f, 0.10f}, {0.15f, 0.235f, 0.02f}, {}, {0.028f, 0.05f}, {0.018f, 0.03f}, "HairLockFront"},
    {HairPieceKind::Lock, {0.0f, 0.205f, 0.04f}, {0.145f, 0.27f, -0.03f}, {}, {0.028f, 0.05f}, {0.016f, 0.028f}, "HairLockCrown"},
    {HairPieceKind::Lock, {-0.02f, 0.20f, -0.02f}, {0.10f, 0.255f, -0.09f}, {}, {0.026f, 0.048f}, {0.015f, 0.025f}, "HairLockMid"},
    {HairPieceKind::Lock, {-0.05f, 0.165f, -0.09f}, {0.03f, 0.215f, -0.155f}, {}, {0.024f, 0.045f}, {0.014f, 0.022f}, "HairLockBack"},
    {HairPieceKind::Lock, {0.09f, 0.135f, 0.06f}, {0.155f, 0.165f, 0.01f}, {}, {0.022f, 0.032f}, {0.012f, 0.018f}, "HairLockSideWisp"},
    {HairPieceKind::Lock, {0.03f, 0.155f, 0.115f}, {0.10f, 0.115f, 0.145f}, {}, {0.022f, 0.03f}, {0.012f, 0.016f}, "HairLockFringe"},
}};

} // namespace

bool spawnAvatarDefaultHair(ECS& ecs, const Skeleton& skeleton, const AvatarLoadout& loadout, glm::vec4 hairColor,
                             RiggedMeshLibrary& riggedMeshLibrary, VmaAllocator allocator, VkDevice device,
                             VkCommandPool cmdPool, VkQueue queue, std::vector<EntityId>& outHairEntities,
                             std::string& outError) {
    // Real, honest skip -- a player who has equipped a real Hair
    // accessory item sees that instead (spawnAvatarAccessories(),
    // attach_hair joint); this default mass would otherwise render
    // through/alongside it.
    if (!loadout.equippedItemId(AvatarItemCategory::Hair).empty()) {
        outHairEntities.clear();
        return true;
    }

    int headJointIndex = skeleton.findJointIndex("head");
    if (headJointIndex < 0) {
        outError = "skeleton has no real \"head\" joint";
        return false;
    }
    std::vector<glm::mat4> bindWorld = skeleton.bindPoseMatrices();
    glm::vec3 headWorldPos = glm::vec3(bindWorld[static_cast<size_t>(headJointIndex)][3]);

    // Real root-to-tip ramp derived from the caller's own hairColor --
    // root a real darkened shade (near the scalp), tip a real lightened
    // shade (catching more light at the hair's own outer edge) -- the
    // real "depth" the vertex-color channel now actually delivers.
    HairColorRamp ramp;
    ramp.rootColor = glm::vec4(glm::vec3(hairColor) * 0.62f, hairColor.a);
    ramp.tipColor = glm::vec4(glm::min(glm::vec3(hairColor) * 1.35f + glm::vec3(0.05f), glm::vec3(1.0f)), hairColor.a);

    std::vector<EntityId> spawned;
    for (const HairPieceSpec& spec : kHairPieces) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        SkinWeights skinWeights;
        if (spec.kind == HairPieceKind::Blob) {
            appendHairBlob(vertices, indices, headWorldPos + spec.baseOrCenterOffset, spec.blobRadii, headJointIndex,
                            ramp, skinWeights);
        } else {
            appendHairLock(vertices, indices, headWorldPos + spec.baseOrCenterOffset, headWorldPos + spec.tipOffset,
                            spec.lockBaseHalfExtents, spec.lockTipHalfExtents, headJointIndex, ramp, skinWeights);
        }

        RiggedMesh riggedMesh;
        std::string pieceError;
        if (!riggedMesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices, skinWeights, skeleton,
                                        pieceError)) {
            outError = std::string("failed to upload hair piece \"") + spec.entityName + "\": " + pieceError;
            for (EntityId e : spawned) ecs.destroyEntity(e);
            return false;
        }
        uint32_t handle = riggedMeshLibrary.registerRiggedMesh(std::move(riggedMesh));

        EntityId entity = ecs.createEntity(spec.entityName);
        auto& skinned = ecs.addComponent<SkinnedRenderable>(entity);
        skinned.riggedMeshHandle = handle;
        skinned.skinningMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        // Real, opaque white base -- the actual color now comes entirely
        // from the real, smooth per-vertex ramp above (baseColor
        // multiplies vertex color in shaders/scene.frag; white makes
        // that multiply a pure pass-through of the real gradient).
        skinned.baseColor = glm::vec4(1.0f);
        // Kronos ("Avatar Visual Silhouette Pass" -- "Materials" -- "Add
        // matte body + glossy hair contrast for cinematic lighting"):
        // real, low roughness (a genuinely glossier specular response
        // than any body segment's own 0.55-0.66, see
        // segmentMaterialRoughness() in RiggedAvatar.cpp) plus a small
        // metallic bump for a real, subtle sheen -- the real contrast
        // the spec asks for, not a restatement of the same matte value.
        skinned.roughness = 0.28f;
        skinned.metallic = 0.12f;
        // Kronos ("Avatar 2.0" -- "Performance and LOD"): hair reads as
        // core to the avatar's own silhouette (the whole point of this
        // pass), so it stays AvatarLODCategory::Body -- never
        // distance-hidden -- not Accessory.
        ecs.addComponent<AvatarLODTag>(entity);
        spawned.push_back(entity);
    }

    outHairEntities = std::move(spawned);
    return true;
}

} // namespace engine::core
