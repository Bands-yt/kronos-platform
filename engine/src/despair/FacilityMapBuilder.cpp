#include "despair/FacilityMapBuilder.hpp"

#include <algorithm>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/Interactable.hpp"
#include "core/WorldProp.hpp"
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

// Spawns the two child entities every AI tier's silhouette needs, parented
// to `aiEntity` (whose own Transform stays feet-level/y=0 -- see
// FacilityAiSpawnSpec's own comment -- so nothing here ever touches it):
//
// 1. A human-sized, fully unlit black box (Renderable::unlitSilhouette,
//    see Components.hpp's own comment on why that's a true silhouette, not
//    just a dark material) -- local Y offset by half its own height so it
//    stands on the floor instead of being centered underground.
// 2. A small, low-intensity emissive "eye" box plus a real core::Light on
//    the same entity (Light reads position from its own Transform via
//    hierarchy::computeWorldMatrix(), the same convention every other
//    Light in this engine uses) near head height -- both a visible glint
//    (emissiveIntensity, bloomed by the post pipeline, see Components.hpp's
//    own comment) and a real, subtle light the player can actually see cast
//    into a dark hallway ahead of the silhouette itself. Tinted green at
//    spawn (every tier starts Dormant/Idle -- see HorrorAIManager.hpp's own
//    struct defaults) and re-tinted every tick after that by
//    HorrorAIManager's tick*() methods via the AiEyeGlowRef this function
//    attaches to `aiEntity` below, per that state's own
//    stalker/tormentor/cullerStateColor() mapping (Green idle/patrol, Red
//    hunting/chasing, Yellow investigating/searching).
//
// Neither AI creature has a facing/turning system (see HorrorAIManager.hpp's
// own top comment -- they're omni-directional), so there's no real "front"
// to bias the eye glow toward; it sits at the box's own forward-Z face by a
// fixed, arbitrary convention, not a tracked facing direction.
void spawnAiSilhouette(core::ECS& ecs, core::EntityId aiEntity, uint32_t boxMesh) {
    constexpr glm::vec3 kSilhouetteSize{0.6f, 1.9f, 0.4f};
    constexpr float kEyeHeightFraction = 0.90f;
    constexpr float kEyeSize = 0.10f;

    auto silhouette = ecs.createEntity("AiSilhouette");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(silhouette)) {
        transform->position = {0.0f, kSilhouetteSize.y * 0.5f, 0.0f};
        transform->scale = kSilhouetteSize;
    }
    auto& silhouetteRenderable = ecs.addComponent<core::Renderable>(silhouette);
    silhouetteRenderable.meshHandle = boxMesh;
    silhouetteRenderable.unlitSilhouette = true;
    silhouetteRenderable.baseColor = {0.02f, 0.02f, 0.02f, 1.0f};
    silhouetteRenderable.castsShadow = false; // a flat unlit silhouette has no real lit shading to shadow-cast believably
    core::hierarchy::setParent(ecs, silhouette, aiEntity);

    // Green -- every tier's default-constructed AI state starts
    // Dormant/Idle (see HorrorAIManager.hpp's own struct defaults), and
    // that's the very first color the real state-driven ticks below would
    // write anyway.
    glm::vec3 eyeColor{0.0f, 1.0f, 0.0f};
    auto eye = ecs.createEntity("AiEyeGlow");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(eye)) {
        transform->position = {0.0f, kSilhouetteSize.y * kEyeHeightFraction, kSilhouetteSize.z * 0.5f};
        transform->scale = glm::vec3(kEyeSize);
    }
    auto& eyeRenderable = ecs.addComponent<core::Renderable>(eye);
    eyeRenderable.meshHandle = boxMesh;
    eyeRenderable.baseColor = glm::vec4(eyeColor, 1.0f);
    eyeRenderable.emissiveColor = eyeColor;
    eyeRenderable.emissiveIntensity = 1.5f; // subtle -- a glint, not a beacon; see this function's own header comment
    eyeRenderable.castsShadow = false;
    core::Light eyeLight;
    eyeLight.color = eyeColor;
    eyeLight.intensity = 0.6f; // low-intensity -- enough to catch in a dark hallway, not to light the whole room
    eyeLight.radius = 3.0f;
    ecs.addComponent<core::Light>(eye, eyeLight);
    core::hierarchy::setParent(ecs, eye, aiEntity);

    ecs.addComponent<AiEyeGlowRef>(aiEntity, AiEyeGlowRef{eye});
}

// Shared footprint for every wardrobe/locker/desk greybox below, and for
// the padded Trigger sensor each spawn loop attaches on top of it (see
// kInteractPadding's own comment) -- a single source of truth so the
// visual box and the raycast hitbox around it can never drift apart.
constexpr glm::vec3 kWardrobeSize{0.9f, 2.0f, 0.6f};
constexpr glm::vec3 kLockerSize{0.8f, 0.8f, 0.5f};
constexpr glm::vec3 kDeskSize{1.0f, 0.9f, 0.6f};

// Added to every furniture/prop collider's own half-extents before it's
// handed to attachBodyToEntity() -- the showcase's own bug report asked
// for hitboxes that "register reliably from a reasonable distance," and a
// raycast that only ever hits the exact rendered surface is exactly the
// failure mode a player reads as "the prompt won't show up." A Trigger
// sensor generates no contact response (see the keycard spawn loop's own
// comment on that pairing), so growing it past the visual mesh never
// blocks movement or shoves anything -- it only makes the invisible
// raycast target easier to hit.
constexpr float kInteractPadding = 0.15f;

// A simple double-door wardrobe: one body box plus a vertical seam and two
// handles on its front (+Z) face, each a child entity parented to `root`
// (same "compound mesh via hierarchy::setParent(), no Renderable on the
// parent itself" idiom spawnAiSilhouette() already establishes above) --
// this engine has no runtime CSG boolean solver, so a "procedural greybox"
// here means composing primitive boxMesh instances, not cutting one.
// `root`'s own Transform.position is already the caller's world-space
// center (the vertical-center convention every FacilityLayout spec uses),
// so every child position below is a small LOCAL offset from that, never
// an absolute coordinate.
void addWardrobeGreyboxVisuals(core::ECS& ecs, core::EntityId root, uint32_t boxMesh) {
    auto body = ecs.createEntity("WardrobeBody");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(body)) {
        transform->scale = kWardrobeSize;
    }
    auto& bodyRenderable = ecs.addComponent<core::Renderable>(body);
    bodyRenderable.meshHandle = boxMesh;
    bodyRenderable.baseColor = {0.28f, 0.20f, 0.14f, 1.0f};
    bodyRenderable.metallic = 0.05f;
    bodyRenderable.roughness = 0.8f;
    core::hierarchy::setParent(ecs, body, root);

    auto seam = ecs.createEntity("WardrobeDoorSeam");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(seam)) {
        transform->position = {0.0f, 0.0f, kWardrobeSize.z * 0.5f + 0.005f};
        transform->scale = {0.02f, kWardrobeSize.y * 0.94f, 0.01f};
    }
    auto& seamRenderable = ecs.addComponent<core::Renderable>(seam);
    seamRenderable.meshHandle = boxMesh;
    seamRenderable.baseColor = {0.03f, 0.03f, 0.03f, 1.0f};
    seamRenderable.metallic = 0.0f;
    seamRenderable.roughness = 0.9f;
    core::hierarchy::setParent(ecs, seam, root);

    for (float side : {-1.0f, 1.0f}) {
        auto handle = ecs.createEntity("WardrobeDoorHandle");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(handle)) {
            transform->position = {side * kWardrobeSize.x * 0.12f, 0.0f, kWardrobeSize.z * 0.5f + 0.02f};
            transform->scale = {0.04f, 0.16f, 0.04f};
        }
        auto& handleRenderable = ecs.addComponent<core::Renderable>(handle);
        handleRenderable.meshHandle = boxMesh;
        handleRenderable.baseColor = {0.55f, 0.55f, 0.58f, 1.0f};
        handleRenderable.metallic = 0.7f;
        handleRenderable.roughness = 0.3f;
        core::hierarchy::setParent(ecs, handle, root);
    }
}

// A squat, single-door locker/footlocker: body plus one off-center door
// seam and handle -- same child-entity composition as the wardrobe above,
// just narrower/shorter and single-door instead of double.
void addLockerGreyboxVisuals(core::ECS& ecs, core::EntityId root, uint32_t boxMesh) {
    auto body = ecs.createEntity("LockerBody");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(body)) {
        transform->scale = kLockerSize;
    }
    auto& bodyRenderable = ecs.addComponent<core::Renderable>(body);
    bodyRenderable.meshHandle = boxMesh;
    bodyRenderable.baseColor = {0.42f, 0.38f, 0.28f, 1.0f};
    bodyRenderable.metallic = 0.1f;
    bodyRenderable.roughness = 0.75f;
    core::hierarchy::setParent(ecs, body, root);

    auto seam = ecs.createEntity("LockerDoorSeam");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(seam)) {
        transform->position = {kLockerSize.x * 0.5f - 0.05f, 0.0f, kLockerSize.z * 0.5f + 0.005f};
        transform->scale = {0.02f, kLockerSize.y * 0.9f, 0.01f};
    }
    auto& seamRenderable = ecs.addComponent<core::Renderable>(seam);
    seamRenderable.meshHandle = boxMesh;
    seamRenderable.baseColor = {0.03f, 0.03f, 0.03f, 1.0f};
    seamRenderable.metallic = 0.0f;
    seamRenderable.roughness = 0.9f;
    core::hierarchy::setParent(ecs, seam, root);

    auto handle = ecs.createEntity("LockerDoorHandle");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(handle)) {
        transform->position = {-kLockerSize.x * 0.08f, 0.0f, kLockerSize.z * 0.5f + 0.02f};
        transform->scale = {0.05f, 0.14f, 0.04f};
    }
    auto& handleRenderable = ecs.addComponent<core::Renderable>(handle);
    handleRenderable.meshHandle = boxMesh;
    handleRenderable.baseColor = {0.55f, 0.55f, 0.58f, 1.0f};
    handleRenderable.metallic = 0.7f;
    handleRenderable.roughness = 0.3f;
    core::hierarchy::setParent(ecs, handle, root);
}

// A desk: thin tabletop, four legs, and a drawer block -- gives keycards
// spawned "on top of" it (see FacilityLayout.cpp's own comment on the two
// standalone keycards) a real flat surface at kDeskSize.y to rest on.
void addDeskGreyboxVisuals(core::ECS& ecs, core::EntityId root, uint32_t boxMesh) {
    constexpr float kTopThickness = 0.05f;
    constexpr float kLegSize = 0.05f;

    auto top = ecs.createEntity("DeskTop");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(top)) {
        transform->position = {0.0f, kDeskSize.y * 0.5f - kTopThickness * 0.5f, 0.0f};
        transform->scale = {kDeskSize.x, kTopThickness, kDeskSize.z};
    }
    auto& topRenderable = ecs.addComponent<core::Renderable>(top);
    topRenderable.meshHandle = boxMesh;
    topRenderable.baseColor = {0.35f, 0.26f, 0.16f, 1.0f};
    topRenderable.metallic = 0.05f;
    topRenderable.roughness = 0.7f;
    core::hierarchy::setParent(ecs, top, root);

    float legHeight = kDeskSize.y - kTopThickness;
    for (float xSide : {-1.0f, 1.0f}) {
        for (float zSide : {-1.0f, 1.0f}) {
            auto leg = ecs.createEntity("DeskLeg");
            if (auto* transform = ecs.tryGetComponent<core::Transform>(leg)) {
                transform->position = {xSide * (kDeskSize.x * 0.5f - kLegSize * 0.5f), -kTopThickness * 0.5f,
                                        zSide * (kDeskSize.z * 0.5f - kLegSize * 0.5f)};
                transform->scale = {kLegSize, legHeight, kLegSize};
            }
            auto& legRenderable = ecs.addComponent<core::Renderable>(leg);
            legRenderable.meshHandle = boxMesh;
            legRenderable.baseColor = {0.20f, 0.15f, 0.10f, 1.0f};
            legRenderable.metallic = 0.05f;
            legRenderable.roughness = 0.8f;
            core::hierarchy::setParent(ecs, leg, root);
        }
    }

    auto drawer = ecs.createEntity("DeskDrawer");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(drawer)) {
        transform->position = {kDeskSize.x * 0.25f, kDeskSize.y * 0.15f, kDeskSize.z * 0.5f - 0.04f};
        transform->scale = {kDeskSize.x * 0.35f, kDeskSize.y * 0.4f, 0.08f};
    }
    auto& drawerRenderable = ecs.addComponent<core::Renderable>(drawer);
    drawerRenderable.meshHandle = boxMesh;
    drawerRenderable.baseColor = {0.30f, 0.22f, 0.14f, 1.0f};
    drawerRenderable.metallic = 0.05f;
    drawerRenderable.roughness = 0.75f;
    core::hierarchy::setParent(ecs, drawer, root);
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
        // Hinge on the leaf's own wide axis (X or Z, whichever doorSpec.scale
        // is larger on -- the other is thickness) so opening it actually
        // clears the doorway gap instead of re-centering on it.
        doorState.hingeAxisLocal = doorSpec.scale.x >= doorSpec.scale.z ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
        doorState.hingeHalfWidth = std::max(doorSpec.scale.x, doorSpec.scale.z) / 2.0f;
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
        }
        addWardrobeGreyboxVisuals(ecs, spot, boxMesh);

        auto& interactable = ecs.addComponent<core::Interactable>(spot);
        interactable.prompt = "Press E to hide";

        HidingSpot hidingSpot;
        hidingSpot.interiorPosition = worldOrigin + spotSpec.interiorPosition;
        ecs.addComponent<HidingSpot>(spot, hidingSpot);

        // Real Jolt body, sensor-only -- see the keycard spawn loop's own
        // comment on why a body-less Interactable is invisible to
        // Physics::raycast(); Trigger + isSensor=true keeps the player able
        // to walk through/into the wardrobe to hide rather than bouncing
        // off a solid collider. Padded (kInteractPadding) so the prompt
        // shows up from a step or two back, not only nose-to-door.
        (void)physics.attachBodyToEntity(spot, ecs,
                                          core::ColliderShape{core::ColliderShapeKind::Box, kWardrobeSize / 2.0f + glm::vec3(kInteractPadding)},
                                          core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                          core::CollisionLayer::Trigger, /*isSensor=*/true);
    }

    // -------------------------------------------------------------
    // Loot containers (a duffel bag is just one with a longer
    // searchDurationSeconds -- see InteractionSystem.hpp's own comment).
    for (const FacilityContainerSpec& containerSpec : layout.containers) {
        auto container = ecs.createEntity("LootContainer");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(container)) {
            transform->position = worldOrigin + containerSpec.localPosition;
        }

        // Which greybox this container's own visuals get is purely a
        // render/geometry choice (see FacilityFurnitureKind's own comment)
        // -- `None` keeps the original single scaled box exactly as it
        // always rendered (the duffel bag's soft-sided shape doesn't read
        // as rigid "furniture" the way a locker or desk does), while
        // Locker/Desk get the compound greybox helpers above. The
        // collider half-extents below (`furnitureHalfExtents`) track
        // whichever box actually got rendered, so the padded Trigger
        // sensor always matches what the player sees.
        glm::vec3 furnitureHalfExtents{0.4f, 0.3f, 0.25f}; // None: half of the original {0.8,0.6,0.5} box
        switch (containerSpec.furniture) {
            case FacilityFurnitureKind::Locker:
                addLockerGreyboxVisuals(ecs, container, boxMesh);
                furnitureHalfExtents = kLockerSize / 2.0f;
                break;
            case FacilityFurnitureKind::Desk:
                addDeskGreyboxVisuals(ecs, container, boxMesh);
                furnitureHalfExtents = kDeskSize / 2.0f;
                break;
            case FacilityFurnitureKind::None: {
                auto& renderable = ecs.addComponent<core::Renderable>(container);
                renderable.meshHandle = boxMesh;
                renderable.baseColor = {0.42f, 0.38f, 0.28f, 1.0f};
                renderable.metallic = 0.1f;
                renderable.roughness = 0.75f;
                if (auto* transform = ecs.tryGetComponent<core::Transform>(container)) {
                    transform->scale = {0.8f, 0.6f, 0.5f};
                }
                break;
            }
        }

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

        // Real Jolt body, sensor-only -- identical bug class and fix to
        // the keycard/wardrobe spawn loops above: with no live body here,
        // tickContainerSearches()'s strict `entity == lookAtTarget` check
        // (InteractionSystem.cpp) could never be satisfied no matter how
        // correct LootContainer/LootDrop/Interactable were, since
        // Physics::raycast() only ever sees live Jolt bodies. Trigger +
        // isSensor=true, padded by kInteractPadding, same as every other
        // interactable prop in this file.
        (void)physics.attachBodyToEntity(container, ecs,
                                          core::ColliderShape{core::ColliderShapeKind::Box, furnitureHalfExtents + glm::vec3(kInteractPadding)},
                                          core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                          core::CollisionLayer::Trigger, /*isSensor=*/true);
    }

    // -------------------------------------------------------------
    // Keycards -- real, pickup-able world entities (core::Pickup +
    // despair::Keycard on the same entity, same pairing
    // InteractionSystem.hpp's own comment documents).
    constexpr glm::vec3 kKeycardScale{0.15f, 0.02f, 0.1f};
    for (const FacilityKeycardSpec& keycardSpec : layout.keycards) {
        auto keycard = ecs.createEntity("Keycard");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(keycard)) {
            transform->position = worldOrigin + keycardSpec.localPosition;
            transform->scale = kKeycardScale;
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

        // Real Jolt body, sensor-only: Physics::raycast() queries Jolt's
        // NarrowPhaseQuery directly (see its own comment), so an entity
        // with no live body -- which every keycard was, before this --
        // is completely invisible to the player's look-and-press ray no
        // matter how correct Interactable/Pickup/Keycard are. Trigger
        // layer + isSensor=true keeps it fully raycast-hittable while
        // never generating contact response, so a keycard sitting flush
        // on a desk can't shove the player or get walked around like a
        // solid Static prop would.
        (void)physics.attachBodyToEntity(keycard, ecs,
                                          core::ColliderShape{core::ColliderShapeKind::Box, kKeycardScale / 2.0f + glm::vec3(kInteractPadding)},
                                          core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                          core::CollisionLayer::Trigger, /*isSensor=*/true);
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
                spawnAiSilhouette(ecs, entity, boxMesh);
                break;
            }
            case FacilityAiTier::Tormentor: {
                auto entity = ecs.createEntity("TormentorAI");
                if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                    transform->position = worldOrigin + aiSpec.localPosition;
                }
                auto& tormentorState = ecs.addComponent<TormentorAIState>(entity);
                tormentorState.patrolWaypoints.reserve(aiSpec.patrolWaypoints.size());
                for (const glm::vec3& localWaypoint : aiSpec.patrolWaypoints) {
                    tormentorState.patrolWaypoints.push_back(worldOrigin + localWaypoint);
                }
                spawnAiSilhouette(ecs, entity, boxMesh);
                break;
            }
            case FacilityAiTier::Culler: {
                auto entity = ecs.createEntity("DespairCullerAI");
                if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
                    transform->position = worldOrigin + aiSpec.localPosition;
                }
                ecs.addComponent<DespairCullerAIState>(entity);
                spawnAiSilhouette(ecs, entity, boxMesh);
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

        // Real Jolt body, sensor-only -- same missing-body bug class as
        // every other interactable in this file; the breaker had zero
        // collider before this, so it was never raycast-hittable no
        // matter how correct PowerBreaker/Interactable were.
        (void)physics.attachBodyToEntity(breaker, ecs,
                                          core::ColliderShape{core::ColliderShapeKind::Box, glm::vec3{0.3f, 0.4f, 0.15f} / 2.0f + glm::vec3(kInteractPadding)},
                                          core::PhysicsMaterial{}, core::RigidBodyMotionType::Static, 0.0f,
                                          core::CollisionLayer::Trigger, /*isSensor=*/true);
    }

    // -------------------------------------------------------------
    // Breaker-powered lights (BreakerPoweredLight, EscapeGameLoop.hpp) --
    // spawned dark (Light::intensity = 0) since PowerBreaker::activated
    // defaults false; Application.cpp's own breaker-toggle handler is the
    // only thing that ever raises intensity back up to
    // BreakerPoweredLight::litIntensity.
    for (const FacilityLightSpec& lightSpec : layout.lights) {
        auto light = ecs.createEntity("FacilityLight");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(light)) {
            transform->position = worldOrigin + lightSpec.localPosition;
        }
        core::Light lightState;
        lightState.color = lightSpec.color;
        lightState.intensity = 0.0f;
        lightState.radius = lightSpec.radius;
        ecs.addComponent<core::Light>(light, lightState);
        ecs.addComponent<BreakerPoweredLight>(light, BreakerPoweredLight{lightSpec.litIntensity});
    }

    // -------------------------------------------------------------
    // Pushable crates -- core::WorldPropKind::Crate is already a real
    // Dynamic Jolt body (see WorldProp.cpp's own motionTypeForKind()), and
    // the player capsule is Dynamic too (Physics::createCharacterCapsule()),
    // so this is genuine rigid-body push resolution, not scripted movement.
    // Same halfExtent-vs-boxMesh convention main.cpp's own prop table
    // already uses (the mesh's baked 0.5 half-extent, not `halfExtent`,
    // sets the rendered size -- spawnWorldProp() never rescales the mesh).
    for (const FacilityCratePropSpec& crateSpec : layout.crates) {
        core::WorldPropSpawnInfo info;
        info.kind = core::WorldPropKind::Crate;
        info.position = worldOrigin + crateSpec.localPosition;
        info.halfExtent = crateSpec.halfExtent;
        info.meshHandle = boxMesh;
        info.baseColor = {0.45f, 0.32f, 0.18f};
        info.metallic = 0.0f;
        info.roughness = 0.9f;
        core::spawnWorldProp(ecs, physics, info);
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
        doorState.hingeAxisLocal = layout.blastDoor.scale.x >= layout.blastDoor.scale.z ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
        doorState.hingeHalfWidth = std::max(layout.blastDoor.scale.x, layout.blastDoor.scale.z) / 2.0f;
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
