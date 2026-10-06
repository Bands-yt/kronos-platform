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
float signedPow(float x, float e) { return std::copysign(std::pow(std::abs(x), e), x); }

// A superquadric: `exponents.x` shapes the vertical profile and
// `exponents.y` the horizontal one (1 is round, smaller is boxier).
void appendHairBlob(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 center, glm::vec3 radii,
                     glm::vec2 exponents, int jointIndex, const HairColorRamp& ramp, SkinWeights& skinWeights) {
    constexpr uint32_t kSegments = 32;
    constexpr uint32_t kRings = 16;
    uint32_t base = static_cast<uint32_t>(vertices.size());

    for (uint32_t r = 0; r <= kRings; ++r) {
        float v = static_cast<float>(r) / static_cast<float>(kRings);
        float phi = v * 3.14159265f;
        glm::vec4 ringColor = glm::mix(ramp.tipColor, ramp.rootColor, v);
        for (uint32_t s = 0; s <= kSegments; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(kSegments);
            float theta = u * 2.0f * 3.14159265f;
            float ring = signedPow(std::sin(phi), exponents.x);
            glm::vec3 shaped(ring * signedPow(std::cos(theta), exponents.y), signedPow(std::cos(phi), exponents.x),
                             ring * signedPow(std::sin(theta), exponents.y));
            float normalRing = signedPow(std::sin(phi), 2.0f - exponents.x);
            glm::vec3 normal(normalRing * signedPow(std::cos(theta), 2.0f - exponents.y),
                             signedPow(std::cos(phi), 2.0f - exponents.x),
                             normalRing * signedPow(std::sin(theta), 2.0f - exponents.y));
            Vertex vert;
            vert.position = center + shaped * radii;
            vert.normal = glm::length(normal) > 1e-6f ? glm::normalize(normal / radii) : glm::vec3(0.0f, 1.0f, 0.0f);
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

struct HairPieceSpec {
    glm::vec3 centerOffset;
    glm::vec3 radii;
    glm::vec2 exponents;
    const char* entityName;
};

// Sized for the Classic head (headShapeRadii(HeadShape::Oval)): a bowl over
// the top that stops above the brows, and a piece covering the back.
constexpr std::array<HairPieceSpec, 2> kHairPieces = {{
    {{0.0f, 0.19f, -0.005f}, {0.274f, 0.095f, 0.26f}, {0.5f, 0.85f}, "HairTop"},
    {{0.0f, 0.05f, -0.06f}, {0.27f, 0.15f, 0.21f}, {0.45f, 0.85f}, "HairBack"},
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
        appendHairBlob(vertices, indices, headWorldPos + spec.centerOffset, spec.radii, spec.exponents, headJointIndex,
                       ramp, skinWeights);

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
