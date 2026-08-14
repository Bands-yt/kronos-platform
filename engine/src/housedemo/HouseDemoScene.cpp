#include "housedemo/HouseDemoScene.hpp"

#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/ParticleSystem.hpp"
#include "housedemo/HouseLayout.hpp"

namespace engine::housedemo {

namespace {
// `meshHandle` must be a unit box (half-extents 0.5, see boxMesh in
// buildHouseDemoScene() below) -- `halfExtents` scales it to the real
// requested size via Transform::scale, same "unit mesh + scale" approach
// the wall-part loop below uses.
void spawnRenderable(core::ECS& ecs, uint32_t meshHandle, glm::vec3 worldPosition, glm::vec3 halfExtents,
                      glm::vec3 color, float metallic, float roughness, float yawDegrees, const char* name) {
    auto entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = worldPosition;
        transform->scale = halfExtents / 0.5f;
        if (yawDegrees != 0.0f) transform->rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    }
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(color, 1.0f);
    renderable.metallic = metallic;
    renderable.roughness = roughness;
}
} // namespace

void buildHouseDemoScene(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::Terrain& terrain, VmaAllocator allocator,
                          VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec2 worldOriginXZ) {
    // Real, honest footprint leveling -- rolling hills means the raw
    // terrain height varies across an 8x6m footprint; flatten() (the
    // same real brush TerrainEditorPlugin's own UI uses) levels a real
    // circular area to the height at the house's own center point before
    // anything is placed, so the floor sits flush instead of clipping
    // through a hillside. Radius 6 comfortably covers the house's own
    // ~5m footprint diagonal plus a little clearance.
    terrain.flatten(worldOriginXZ.x, worldOriginXZ.y, 6.0f, 1.0f);
    float groundY = terrain.heightAt(worldOriginXZ.x, worldOriginXZ.y);
    glm::vec3 worldOrigin{worldOriginXZ.x, groundY, worldOriginXZ.y};

    uint32_t boxMesh = meshLibrary.registerMesh(core::Mesh::createBox(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
    uint32_t wedgeMesh = meshLibrary.registerMesh(core::Mesh::createWedge(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));

    for (const HousePart& part : computeHouseLayout()) {
        uint32_t meshHandle = part.kind == HousePartKind::RoofWedge ? wedgeMesh : boxMesh;
        glm::vec3 worldPosition = worldOrigin + part.localPosition;
        // The registered mesh is a unit box/wedge (half-extents 0.5);
        // Transform::scale stretches it to this part's real half-extents
        // (2x, since Mesh::createBox/createWedge's own halfExtents param
        // is the *mesh's* half-size, not a scale factor).
        auto entity = ecs.createEntity(part.kind == HousePartKind::RoofWedge  ? "RoofWedge"
                                        : part.kind == HousePartKind::Floor   ? "Floor"
                                        : part.kind == HousePartKind::Wall    ? "Wall"
                                                                               : "Furniture");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = worldPosition;
            transform->scale = part.halfExtents / 0.5f;
            if (part.yawDegrees != 0.0f) {
                transform->rotation = glm::angleAxis(glm::radians(part.yawDegrees), glm::vec3(0, 1, 0));
            }
        }
        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(part.color, 1.0f);
        renderable.metallic = part.metallic;
        renderable.roughness = part.roughness;
    }

    // Real, working front door -- the exact InteractableDoor pattern
    // main.cpp's own bring-up scene uses (main.cpp:~1502-1519), placed
    // in the 1.2m gap HouseLayout.hpp's front-wall segments already
    // leave open.
    {
        auto door = ecs.createEntity("FrontDoor");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(door)) {
            transform->position = worldOrigin + glm::vec3(0.0f, 1.1f, -3.0f);
            transform->scale = {1.2f, 2.2f, 0.1f};
        }
        auto& doorRenderable = ecs.addComponent<core::Renderable>(door);
        doorRenderable.meshHandle = boxMesh;
        doorRenderable.baseColor = {0.45f, 0.30f, 0.18f, 1.0f};
        doorRenderable.metallic = 0.05f;
        doorRenderable.roughness = 0.7f;
        auto& doorInteractable = ecs.addComponent<core::Interactable>(door);
        doorInteractable.prompt = "Press E to open/close door";
        core::Door doorState;
        doorState.closedRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        doorState.openRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ecs.addComponent<core::Door>(door, doorState);
    }

    // 2 real windows -- fixed panes (not openable; see
    // docs/HOUSE_DEMO.md's own judgment-call note), semi-transparent
    // tinted glass, filling the gaps HouseLayout.hpp's wall segments
    // leave open.
    spawnRenderable(ecs, boxMesh, worldOrigin + glm::vec3(0.0f, 1.6f, 3.0f), {0.7f, 0.6f, 0.05f},
                     {0.65f, 0.80f, 0.85f}, 0.1f, 0.05f, 0.0f, "KitchenWindow");
    // Half-extents already thin-in-X/wide-in-Z to match the west wall's
    // own local orientation (thickness along X, gap spans Z) -- no yaw
    // needed on top of that, unlike the kitchen window above which sits
    // flush on a Z-facing wall.
    spawnRenderable(ecs, boxMesh, worldOrigin + glm::vec3(-4.0f, 1.6f, 0.0f), {0.05f, 0.6f, 0.7f},
                     {0.65f, 0.80f, 0.85f}, 0.1f, 0.05f, 0.0f, "LivingRoomWindow");

    // Real fireplace -- living room, east side (opposite the kitchen's
    // back-wall corner) -- a real Light (warm glow) + a real
    // ParticleEmitter (rising embers), the exact settings main.cpp's own
    // "EmberEmitter" bring-up entity already uses, plus a small hearth
    // stack of boxes so the light/particles have something to sit in.
    glm::vec3 hearthPosition = worldOrigin + glm::vec3(3.3f, 0.0f, -1.5f);
    spawnRenderable(ecs, boxMesh, hearthPosition + glm::vec3(0.0f, 0.3f, 0.0f), {0.7f, 0.3f, 0.5f},
                     {0.35f, 0.33f, 0.32f}, 0.0f, 0.9f, 0.0f, "HearthBase");
    spawnRenderable(ecs, boxMesh, hearthPosition + glm::vec3(0.0f, 1.1f, -0.45f), {0.7f, 1.1f, 0.15f},
                     {0.35f, 0.33f, 0.32f}, 0.0f, 0.9f, 0.0f, "HearthBack");

    {
        auto light = ecs.createEntity("FireplaceLight");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(light)) {
            transform->position = hearthPosition + glm::vec3(0.0f, 0.5f, 0.0f);
        }
        auto& lightComponent = ecs.addComponent<core::Light>(light);
        lightComponent.color = {1.0f, 0.55f, 0.20f};
        lightComponent.intensity = 3.5f;
        lightComponent.radius = 6.0f;
    }
    {
        auto emitter = ecs.createEntity("FireplaceEmbers");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(emitter)) {
            transform->position = hearthPosition + glm::vec3(0.0f, 0.6f, 0.0f);
        }
        auto& particleEmitter = ecs.addComponent<core::ParticleEmitter>(emitter);
        particleEmitter.settings.looping = true;
        particleEmitter.settings.emissionRate = 30.0f;
        particleEmitter.settings.particleLifetime = 1.8f;
        particleEmitter.settings.particleLifetimeVariance = 0.4f;
        particleEmitter.settings.velocityMin = {-0.3f, 1.2f, -0.3f};
        particleEmitter.settings.velocityMax = {0.3f, 2.5f, 0.3f};
        particleEmitter.settings.gravity = {0.0f, 0.6f, 0.0f};
        particleEmitter.settings.sizeStart = 0.12f;
        particleEmitter.settings.sizeEnd = 0.02f;
        particleEmitter.settings.colorStart = {1.0f, 0.75f, 0.25f, 1.0f};
        particleEmitter.settings.colorEnd = {1.0f, 0.15f, 0.02f, 0.0f};
    }
}

} // namespace engine::housedemo
