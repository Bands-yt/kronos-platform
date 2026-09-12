#include "core/Baseplate.hpp"

#include <vector>

#include "core/CollisionLayers.hpp"
#include "core/Components.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/Texture.hpp"

namespace engine::core {
namespace {

constexpr float kHalfWidth = 250.0f;    // 500 units, X/Z
constexpr float kHalfHeight = 0.5f;     // 1 unit, Y
constexpr float kTileWorldSize = 10.0f; // major line spacing
constexpr int kTilePixels = 250;
constexpr int kMinorSpacingPx = kTilePixels / 10; // 1-unit minor lines

uint32_t createGridTexture(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                            TextureLibrary& textureLibrary) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kTilePixels) * kTilePixels * 4);
    for (int y = 0; y < kTilePixels; ++y) {
        for (int x = 0; x < kTilePixels; ++x) {
            bool majorEdge = x < 2 || y < 2 || x >= kTilePixels - 2 || y >= kTilePixels - 2;
            bool minorLine = (x % kMinorSpacingPx) == 0 || (y % kMinorSpacingPx) == 0;
            uint8_t r, g, b;
            if (majorEdge) {
                r = g = 150;
                b = 158;
            } else if (minorLine) {
                r = g = 90;
                b = 95;
            } else {
                r = g = 46;
                b = 50;
            }

            size_t i = (static_cast<size_t>(y) * kTilePixels + x) * 4;
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
    }
    Texture tex = Texture::createFromPixels(pixels.data(), kTilePixels, kTilePixels, /*srgb=*/true, allocator,
                                             device, cmdPool, queue);
    return textureLibrary.registerTexture(std::move(tex));
}

// Same 24-vertex flat-shaded box layout as Mesh::createBox(), except top/
// bottom UVs are scaled to `t` instead of [0,1] so the grid texture above
// repeats via the material sampler's REPEAT address mode
// (Renderer::createMaterialResources()) instead of stretching.
Mesh createBaseplateMesh(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    const glm::vec3 h{kHalfWidth, kHalfHeight, kHalfWidth};
    const float t = (kHalfWidth * 2.0f) / kTileWorldSize; // 50

    std::vector<Vertex> vertices = {
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}},  {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}},    {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}},  {{h.x, h.y, -h.z}, {0, 1, 0}, {t, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {t, t}},    {{-h.x, h.y, h.z}, {0, 1, 0}, {0, t}},
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {t, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {t, t}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, t}},
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}},   {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}},   {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}},  {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    };
    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

} // namespace

EntityId spawnDefaultBaseplate(ECS& ecs, MeshLibrary& meshLibrary, TextureLibrary& textureLibrary,
                                VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                Physics* physics) {
    uint32_t meshHandle = meshLibrary.registerMesh(createBaseplateMesh(allocator, device, cmdPool, queue));
    uint32_t gridTexture = createGridTexture(allocator, device, cmdPool, queue, textureLibrary);

    EntityId entity = ecs.createEntity("Baseplate"); // also adds Transform{} + Name{"Baseplate"}
    ecs.tryGetComponent<Transform>(entity)->position = {0.0f, -kHalfHeight, 0.0f};

    ecs.addComponent<MeshSource>(entity, MeshSource{MeshSourceKind::Box, {kHalfWidth, kHalfHeight, kHalfWidth}, ""});

    Renderable renderable;
    renderable.meshHandle = meshHandle;
    renderable.albedoTexture = gridTexture;
    renderable.baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // texture carries the actual grey/grid look
    renderable.metallic = 0.0f;
    renderable.roughness = 0.85f;
    ecs.addComponent<Renderable>(entity, renderable);

    ColliderShape shape{ColliderShapeKind::Box, {kHalfWidth, kHalfHeight, kHalfWidth}, ""};
    if (physics) {
        (void)physics->attachBodyToEntity(entity, ecs, shape, PhysicsMaterial{}, RigidBodyMotionType::Static, 0.0f,
                                           CollisionLayer::Static);
    } else {
        // No live Jolt world (Studio edit-mode): author the shape now.
        // RigidBody{kInvalidBodyId, Static} is required, not optional --
        // PhysicsPreviewPlugin::play() defaults any ColliderShape entity
        // with no RigidBody to Dynamic (see its own comment), which would
        // make the baseplate fall.
        ecs.addComponent<ColliderShape>(entity, shape);
        ecs.addComponent<PhysicsMaterial>(entity, PhysicsMaterial{});
        ecs.addComponent<RigidBody>(entity, RigidBody{RigidBody::kInvalidBodyId, RigidBodyMotionType::Static});
    }

    return entity;
}

} // namespace engine::core
