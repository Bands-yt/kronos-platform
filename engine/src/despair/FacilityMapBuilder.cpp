#include "despair/FacilityMapBuilder.hpp"

#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "despair/EscapeGameLoop.hpp"
#include "despair/FacilityLayout.hpp"
#include "despair/HorrorAIManager.hpp"
#include "despair/InteractionSystem.hpp"

namespace engine::despair {

namespace {

glm::vec4 keycardColor(KeycardTier tier) {
    switch (tier) {
        case KeycardTier::Red: return {0.85f, 0.10f, 0.10f, 1.0f};
        case KeycardTier::Blue: return {0.10f, 0.35f, 0.85f, 1.0f};
        case KeycardTier::Gold: return {0.85f, 0.70f, 0.10f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

} // namespace

void buildFacilityScene(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                         VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 worldOrigin) {
    uint32_t boxMesh = meshLibrary.registerMesh(core::Mesh::createBox(allocator, device, cmdPool, queue, {0.5f, 0.5f, 0.5f}));
    const FacilityLayout layout = computeFacilityLayout();

    // -------------------------------------------------------------
    // Generic geometry: every wall/floor slab gets a real static
    // collider (Physics::createStaticBox already creates its own
    // entity, with a default Renderable and Transform{} attached --
    // this fills both in with this part's real position/scale/color,
    // rather than a second ecs.createEntity() call).
    for (const FacilityWall& part : layout.geometry) {
        glm::vec3 worldPosition = worldOrigin + part.localPosition;
        core::EntityId entity = physics.createStaticBox(ecs, worldPosition, part.halfExtents);

        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = worldPosition;
            transform->scale = part.halfExtents / 0.5f;
        }
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
            renderable->meshHandle = boxMesh;
            renderable->baseColor = glm::vec4(part.color, 1.0f);
            renderable->metallic = part.kind == FacilityWallKind::Floor ? 0.05f : 0.0f;
            renderable->roughness = 0.85f;
        }
    }

    // -------------------------------------------------------------
    // Locked door(s) -- visual + interactable only, no collider (see
    // FacilityDoorSpec's own comment: matches core::Door's documented
    // scope, the same non-blocking-door precedent HouseDemoScene's own
    // front door already establishes).
    for (const FacilityDoorSpec& doorSpec : layout.doors) {
        auto door = ecs.createEntity("FacilityDoor");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(door)) {
            transform->position = worldOrigin + doorSpec.localPosition;
            transform->scale = doorSpec.scale;
        }
        auto& renderable = ecs.addComponent<core::Renderable>(door);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = {0.30f, 0.30f, 0.33f, 1.0f};
        renderable.metallic = 0.6f;
        renderable.roughness = 0.4f;

        auto& interactable = ecs.addComponent<core::Interactable>(door);
        interactable.prompt = "Press E to open/close door";

        core::Door doorState;
        doorState.closedRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        doorState.openRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ecs.addComponent<core::Door>(door, doorState);

        if (doorSpec.locked) {
            LockedDoor lockedDoor;
            lockedDoor.requiredTier = doorSpec.requiredTier;
            lockedDoor.locked = true;
            ecs.addComponent<LockedDoor>(door, lockedDoor);
            interactable.prompt = "Press E to unlock door";

            // Real static collider matching the door leaf's own rendered
            // box exactly (half of `doorSpec.scale`, the same halfExtents/
            // full-size relationship the generic wall geometry above
            // uses) -- unlike an unlocked core::Door (still deliberately
            // walk-through, see this spec's own comment above), a *locked*
            // gate has to actually block movement or the keycard/tier
            // requirement is cosmetic. Static layer, same as the walls, so
            // it also occludes SanitySystem's gaze raycasts and
            // HorrorAIManager's line-of-sight checks until unlocked.
            // Detached the moment the player actually unlocks it (see
            // Application.cpp's own DoorUnlockResult::Unlocked handling) --
            // attachBodyToEntity() reads this entity's Transform, already
            // set above, for the collider's spawn position/rotation.
            (void)physics.attachBodyToEntity(door, ecs, core::ColliderShape{core::ColliderShapeKind::Box, doorSpec.scale / 2.0f},
                                              core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                              core::CollisionLayer::Static);
        }
    }

    // -------------------------------------------------------------
    // Hiding spots -- a small locker prop the player can step into.
    for (const FacilityHidingSpotSpec& spotSpec : layout.hidingSpots) {
        auto spot = ecs.createEntity("HidingSpot");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(spot)) {
            transform->position = worldOrigin + spotSpec.localPosition;
            transform->scale = {0.6f, 1.8f, 0.6f};
        }
        auto& renderable = ecs.addComponent<core::Renderable>(spot);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = {0.25f, 0.30f, 0.35f, 1.0f};
        renderable.metallic = 0.3f;
        renderable.roughness = 0.6f;

        auto& interactable = ecs.addComponent<core::Interactable>(spot);
        interactable.prompt = "Press E to hide";

        HidingSpot hidingSpot;
        hidingSpot.interiorPosition = worldOrigin + spotSpec.interiorPosition;
        ecs.addComponent<HidingSpot>(spot, hidingSpot);
    }

    // -------------------------------------------------------------
    // Loot containers (a duffel bag is just one with a longer
    // searchDurationSeconds -- see InteractionSystem.hpp's own comment).
    for (const FacilityContainerSpec& containerSpec : layout.containers) {
        auto container = ecs.createEntity("LootContainer");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(container)) {
            transform->position = worldOrigin + containerSpec.localPosition;
            transform->scale = {0.8f, 0.6f, 0.5f};
        }
        auto& renderable = ecs.addComponent<core::Renderable>(container);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = {0.42f, 0.38f, 0.28f, 1.0f};
        renderable.metallic = 0.1f;
        renderable.roughness = 0.75f;

        auto& interactable = ecs.addComponent<core::Interactable>(container);
        interactable.prompt = containerSpec.prompt;

        LootContainer lootContainer;
        lootContainer.searchDurationSeconds = containerSpec.searchDurationSeconds;
        ecs.addComponent<LootContainer>(container, lootContainer);

        // LootSystem reconciliation: what tickContainerSearches() reports
        // finished actually grants something (see grantContainerLoot()'s
        // own comment) -- attached even for LootKind::None, so a
        // "searched it, found nothing" container is still a real,
        // explicit LootDrop rather than an absent component some future
        // caller might misread as "not yet wired up."
        ecs.addComponent<LootDrop>(container, containerSpec.loot);
    }

    // -------------------------------------------------------------
    // Keycards -- real, pickup-able world entities (core::Pickup +
    // despair::Keycard on the same entity, same pairing
    // InteractionSystem.hpp's own comment documents).
    for (const FacilityKeycardSpec& keycardSpec : layout.keycards) {
        auto keycard = ecs.createEntity("Keycard");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(keycard)) {
            transform->position = worldOrigin + keycardSpec.localPosition;
            transform->scale = {0.15f, 0.02f, 0.1f};
        }
        auto& renderable = ecs.addComponent<core::Renderable>(keycard);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = keycardColor(keycardSpec.tier);
        renderable.metallic = 0.2f;
        renderable.roughness = 0.3f;

        auto& interactable = ecs.addComponent<core::Interactable>(keycard);
        interactable.prompt = "Press E to pick up keycard";

        ecs.addComponent<core::Pickup>(keycard);
        ecs.addComponent<Keycard>(keycard, Keycard{keycardSpec.tier});
    }

    // -------------------------------------------------------------
    // AI spawns -- plain Transform-driven, feet-level (see
    // FacilityAiSpawnSpec's own comment), struct defaults so every
    // tier starts Dormant/Idle exactly as HorrorAIManager expects.
    for (const FacilityAiSpawnSpec& aiSpec : layout.aiSpawns) {
        switch (aiSpec.tier) {
            case FacilityAiTier::Stalker: {
                auto entity = ecs.createEntity("StalkerAI");
                if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                    transform->position = worldOrigin + aiSpec.localPosition;
                }
                ecs.addComponent<StalkerAIState>(entity);
                break;
            }
            case FacilityAiTier::Tormentor: {
                auto entity = ecs.createEntity("TormentorAI");
                if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                    transform->position = worldOrigin + aiSpec.localPosition;
                }
                ecs.addComponent<TormentorAIState>(entity);
                break;
            }
            case FacilityAiTier::Culler: {
                auto entity = ecs.createEntity("DespairCullerAI");
                if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                    transform->position = worldOrigin + aiSpec.localPosition;
                }
                ecs.addComponent<DespairCullerAIState>(entity);
                break;
            }
        }
    }

    // -------------------------------------------------------------
    // Power breaker(s) -- the blastDoor gate's second precondition
    // alongside the Gold Master Keycard (see EscapeGameLoop.hpp's own
    // comment on tryEscapeThroughBlastDoor()). A plain wall-mounted box,
    // same core::Interactable + component-on-the-same-entity pairing
    // convention every other world prop here uses.
    for (const FacilityBreakerSpec& breakerSpec : layout.breakers) {
        auto breaker = ecs.createEntity("PowerBreaker");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(breaker)) {
            transform->position = worldOrigin + breakerSpec.localPosition;
            transform->scale = {0.3f, 0.4f, 0.15f};
        }
        auto& renderable = ecs.addComponent<core::Renderable>(breaker);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = {0.75f, 0.15f, 0.10f, 1.0f};
        renderable.metallic = 0.4f;
        renderable.roughness = 0.5f;

        auto& interactable = ecs.addComponent<core::Interactable>(breaker);
        interactable.prompt = "Press E to activate the breaker";

        ecs.addComponent<PowerBreaker>(breaker);
    }

    // -------------------------------------------------------------
    // Blast door -- the facility's real exit, mirroring the locked-door
    // spawn block above exactly (same core::Door/core::Interactable/
    // LockedDoor stack and the same real static collider), since
    // tryEscapeThroughBlastDoor() operates on the very same LockedDoor
    // component every other gate in this facility uses.
    {
        auto door = ecs.createEntity("BlastDoor");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(door)) {
            transform->position = worldOrigin + layout.blastDoor.localPosition;
            transform->scale = layout.blastDoor.scale;
        }
        auto& renderable = ecs.addComponent<core::Renderable>(door);
        renderable.meshHandle = boxMesh;
        renderable.baseColor = {0.30f, 0.30f, 0.33f, 1.0f};
        renderable.metallic = 0.6f;
        renderable.roughness = 0.4f;

        auto& interactable = ecs.addComponent<core::Interactable>(door);
        interactable.prompt = "Press E to escape";

        core::Door doorState;
        doorState.closedRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        doorState.openRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ecs.addComponent<core::Door>(door, doorState);

        LockedDoor lockedDoor;
        lockedDoor.requiredTier = layout.blastDoor.requiredTier;
        lockedDoor.locked = true;
        ecs.addComponent<LockedDoor>(door, lockedDoor);
        ecs.addComponent<BlastDoorTag>(door);

        (void)physics.attachBodyToEntity(door, ecs, core::ColliderShape{core::ColliderShapeKind::Box, layout.blastDoor.scale / 2.0f},
                                          core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                          core::CollisionLayer::Static);
    }
}

} // namespace engine::despair
