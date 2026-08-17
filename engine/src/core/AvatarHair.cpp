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

// Kronos ("Avatar Visual Silhouette Pass" -- "layered tufts/spikes",
// revised after explicit user feedback that the first pass's long,
// wide-flaring spikes read as horns): a real, SHORT, tapered 4-sided
// box (frustum) -- deliberately short (base-to-tip travel a few
// centimeters, not the first pass's much longer reach) and clustered
// tightly at the crown with only a small lateral offset, so even at
// full extension nothing points far enough outward/sideways to read as
// a horn. Real per-vertex color: `ramp.rootColor` at the base (embedded
// in/against the head), `ramp.tipColor` at the point -- a real, smooth
// gradient along each spike's own length, not a flat color.
void appendHairSpike(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 basePos,
                      glm::vec3 tipPos, float baseHalfExtent, float tipHalfExtent, int jointIndex,
                      const HairColorRamp& ramp, SkinWeights& skinWeights) {
    glm::vec3 outward = glm::normalize(tipPos - basePos + glm::vec3(0.0f, 0.001f, 0.0f));

    auto pushRing = [&](glm::vec3 center, float half, glm::vec4 color) -> std::array<uint32_t, 4> {
        std::array<glm::vec3, 4> offsets = {
            glm::vec3(-half, 0.0f, -half),
            glm::vec3(half, 0.0f, -half),
            glm::vec3(half, 0.0f, half),
            glm::vec3(-half, 0.0f, half),
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

    std::array<uint32_t, 4> baseRing = pushRing(basePos, baseHalfExtent, ramp.rootColor);
    std::array<uint32_t, 4> tipRing = pushRing(tipPos, tipHalfExtent, ramp.tipColor);
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t next = (i + 1) % 4;
        indices.insert(indices.end(), {baseRing[i], baseRing[next], tipRing[next], baseRing[i], tipRing[next], tipRing[i]});
    }
    indices.insert(indices.end(), {tipRing[0], tipRing[1], tipRing[2], tipRing[0], tipRing[2], tipRing[3]});
}

enum class HairPieceKind { Blob, Spike };

struct HairPieceSpec {
    HairPieceKind kind;
    glm::vec3 baseOrCenterOffset; // Blob: sphere center. Spike: base point.
    glm::vec3 tipOffset;          // Spike only.
    glm::vec3 blobRadii;          // Blob only.
    float spikeBaseHalfExtent;    // Spike only.
    float spikeTipHalfExtent;     // Spike only.
    const char* entityName;
};

// Kronos ("Avatar Visual Silhouette Pass" -- target silhouette "a
// stylised hair mass" -- "layered tufts/spikes"): a real, layered
// combination -- 2 rounded blobs for the hair's own base volume/mass
// (back of the crown, and a small front fringe), plus 5 short spike
// tufts clustered tightly at the very top of the crown, all rising
// mostly straight up (each spike's own lateral travel is at most ~0.06
// units over ~0.09-0.10 units of height -- roughly 30-34 degrees off
// vertical, well short of the first pass's much wider, longer flare
// that read as horns). Offsets are real, hand-placed points relative to
// the head joint's own world position, the same convention
// buildHumanoidSkeleton()'s own attach_hat/attach_hair joints already
// establish -- a stylized hair silhouette is a real art decision, not a
// procedural one.
constexpr std::array<HairPieceSpec, 7> kHairPieces = {{
    {HairPieceKind::Blob, {0.0f, 0.16f, -0.12f}, {}, {0.09f, 0.08f, 0.10f}, 0.0f, 0.0f, "HairBlobBack"},
    {HairPieceKind::Blob, {0.0f, 0.15f, 0.10f}, {}, {0.07f, 0.045f, 0.06f}, 0.0f, 0.0f, "HairBlobFringe"},
    {HairPieceKind::Spike, {0.0f, 0.20f, -0.01f}, {0.0f, 0.30f, -0.02f}, {}, 0.045f, 0.02f, "HairSpikeCenter"},
    {HairPieceKind::Spike, {-0.045f, 0.19f, -0.03f}, {-0.06f, 0.28f, -0.05f}, {}, 0.035f, 0.015f, "HairSpikeLeft"},
    {HairPieceKind::Spike, {0.045f, 0.19f, -0.03f}, {0.06f, 0.28f, -0.05f}, {}, 0.035f, 0.015f, "HairSpikeRight"},
    {HairPieceKind::Spike, {0.0f, 0.18f, -0.08f}, {0.01f, 0.27f, -0.12f}, {}, 0.035f, 0.015f, "HairSpikeBack"},
    {HairPieceKind::Spike, {0.0f, 0.19f, 0.04f}, {-0.02f, 0.27f, 0.08f}, {}, 0.035f, 0.015f, "HairSpikeFront"},
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
            appendHairSpike(vertices, indices, headWorldPos + spec.baseOrCenterOffset, headWorldPos + spec.tipOffset,
                             spec.spikeBaseHalfExtent, spec.spikeTipHalfExtent, headJointIndex, ramp, skinWeights);
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
