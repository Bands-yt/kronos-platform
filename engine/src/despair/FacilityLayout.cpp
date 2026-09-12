#include "despair/FacilityLayout.hpp"

namespace engine::despair {

FacilityLayout computeFacilityLayout() {
    FacilityLayout layout;

    constexpr float kWallHeight = 3.0f;
    constexpr float kWallHalfHeight = kWallHeight * 0.5f;
    constexpr float kWallThicknessHalf = 0.1f;
    constexpr float kWallCenterY = kWallHalfHeight; // walls sit on the y=0 floor
    constexpr glm::vec3 kWallColor{0.55f, 0.55f, 0.58f};
    constexpr glm::vec3 kFloorColor{0.35f, 0.35f, 0.38f};

    auto& g = layout.geometry;

    // -----------------------------------------------------------------
    // EntryHall: x[-3,3] z[-3,3]. East wall (x=3) has a 2m corridor gap
    // (z[-1,1]) already cut out, closed above by a lintel -- the same
    // "gap segments authored by hand, not a boolean cut" convention
    // HouseLayout.cpp uses.
    g.push_back({FacilityWallKind::Floor, {0.0f, -0.05f, 0.0f}, {3.0f, 0.05f, 3.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {0.0f, kWallCenterY, -3.0f}, {3.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north
    g.push_back({FacilityWallKind::Wall, {0.0f, kWallCenterY, 3.0f}, {3.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // south
    g.push_back({FacilityWallKind::Wall, {-3.0f, kWallCenterY, 0.0f}, {kWallThicknessHalf, kWallHalfHeight, 3.0f}, kWallColor}); // west
    g.push_back({FacilityWallKind::Wall, {3.0f, kWallCenterY, -2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // east, north segment
    g.push_back({FacilityWallKind::Wall, {3.0f, kWallCenterY, 2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // east, south segment
    g.push_back({FacilityWallKind::Wall, {3.0f, 2.6f, 0.0f}, {kWallThicknessHalf, 0.4f, 1.0f}, kWallColor}); // east, lintel over corridor gap

    // -----------------------------------------------------------------
    // Corridor: x[3,7] z[-1,1]. Side walls run the corridor's length;
    // the locked door frame sits at the midpoint (x=5) with its own
    // 1.2m gap (z[-0.6,0.6]) for the door leaf.
    g.push_back({FacilityWallKind::Floor, {5.0f, -0.05f, 0.0f}, {2.0f, 0.05f, 1.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, -1.0f}, {2.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // corridor north side
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, 1.0f}, {2.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // corridor south side
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, -0.8f}, {kWallThicknessHalf, kWallHalfHeight, 0.2f}, kWallColor}); // door frame, left
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, 0.8f}, {kWallThicknessHalf, kWallHalfHeight, 0.2f}, kWallColor}); // door frame, right
    g.push_back({FacilityWallKind::Wall, {5.0f, 2.6f, 0.0f}, {kWallThicknessHalf, 0.4f, 0.6f}, kWallColor}); // door frame, lintel

    // -----------------------------------------------------------------
    // RestrictedWing: x[7,13] z[-3,3]. West wall (x=7) mirrors
    // EntryHall's east wall's corridor gap exactly.
    g.push_back({FacilityWallKind::Floor, {10.0f, -0.05f, 0.0f}, {3.0f, 0.05f, 3.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {10.0f, kWallCenterY, -3.0f}, {3.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north
    g.push_back({FacilityWallKind::Wall, {10.0f, kWallCenterY, 3.0f}, {3.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // south
    g.push_back({FacilityWallKind::Wall, {7.0f, kWallCenterY, -2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // west, north segment
    g.push_back({FacilityWallKind::Wall, {7.0f, kWallCenterY, 2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // west, south segment
    g.push_back({FacilityWallKind::Wall, {7.0f, 2.6f, 0.0f}, {kWallThicknessHalf, 0.4f, 1.0f}, kWallColor}); // west, lintel over corridor gap
    // East wall (x=13) has its own 2m gap (z[-1,1]) for the blastDoor leaf
    // below -- same split-segment-plus-lintel convention as every other
    // gap in this layout, shared with EscapeRoom's own west boundary.
    g.push_back({FacilityWallKind::Wall, {13.0f, kWallCenterY, -2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // east, north segment
    g.push_back({FacilityWallKind::Wall, {13.0f, kWallCenterY, 2.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // east, south segment
    g.push_back({FacilityWallKind::Wall, {13.0f, 2.6f, 0.0f}, {kWallThicknessHalf, 0.4f, 1.0f}, kWallColor}); // east, lintel over blastDoor gap

    // -----------------------------------------------------------------
    // EscapeRoom: x[13,17] z[-2,2]. Only its own north/south/east walls
    // are authored here -- its west boundary is the RestrictedWing east
    // wall segments (plus the blastDoor gap) just above; a shared wall
    // needs only one set of geometry, not two overlapping ones.
    g.push_back({FacilityWallKind::Floor, {15.0f, -0.05f, 0.0f}, {2.0f, 0.05f, 2.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {15.0f, kWallCenterY, -2.0f}, {2.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north
    g.push_back({FacilityWallKind::Wall, {15.0f, kWallCenterY, 2.0f}, {2.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // south
    g.push_back({FacilityWallKind::Wall, {17.0f, kWallCenterY, 0.0f}, {kWallThicknessHalf, kWallHalfHeight, 2.0f}, kWallColor}); // east (far wall, solid -- this room is the exit, nothing beyond it)

    // -----------------------------------------------------------------
    // The corridor's locked door gate, filling that door frame's own
    // 1.2m-wide, 2.2m-tall gap above.
    FacilityDoorSpec corridorDoor;
    corridorDoor.localPosition = {5.0f, 1.1f, 0.0f};
    corridorDoor.scale = {0.2f, 2.2f, 1.2f}; // thickness along X, width along Z -- this frame's gap runs in Z, unlike HouseDemoScene's own X-gap front door
    corridorDoor.locked = true;
    corridorDoor.requiredTier = KeycardTier::Red;
    layout.doors.push_back(corridorDoor);

    // The facility's exit -- fills the blastDoor gap in RestrictedWing's
    // own east wall above. Gated separately from the generic `doors` list
    // (see EscapeGameLoop.hpp's tryEscapeThroughBlastDoor(), which checks
    // this against the Gold Master Keycard AND the facility breaker, not
    // plain tryUnlockDoor()).
    layout.blastDoor.localPosition = {13.0f, 1.1f, 0.0f};
    layout.blastDoor.scale = {0.2f, 2.2f, 1.2f};
    layout.blastDoor.requiredTier = KeycardTier::Gold;

    // The facility breaker -- must be reachable (RestrictedWing, before
    // the blastDoor gate) not inside EscapeRoom itself, or the player
    // could never reach it to open that door in the first place.
    layout.breakers.push_back({{8.5f, 1.0f, -2.7f}});

    // -----------------------------------------------------------------
    // EntryHall gameplay: a quick footlocker (a real Anti-Psychotic
    // Injector -- see LootSystem.hpp), a hiding-spot locker against the
    // west wall, and the Red keycard that unlocks the corridor door ahead.
    FacilityContainerSpec footlocker;
    footlocker.localPosition = {2.0f, 0.4f, -2.0f};
    footlocker.searchDurationSeconds = 3.0f;
    footlocker.prompt = "Hold E to search the footlocker";
    footlocker.loot.kind = LootKind::SanityInjector;
    footlocker.loot.sanityRestoreAmount = 50.0f;
    layout.containers.push_back(footlocker);

    layout.hidingSpots.push_back({{-2.6f, 0.9f, -2.5f}, {-2.85f, 0.7f, -2.5f}});
    layout.keycards.push_back({{0.0f, 0.3f, -2.5f}, KeycardTier::Red});

    // RestrictedWing gameplay: the slower duffel bag -- the Gold "Master
    // Keycard" the blastDoor gate needs (see EscapeGameLoop.hpp) -- guarded
    // by the Tormentor with a dormant Culler waiting in reserve.
    FacilityContainerSpec duffelBag;
    duffelBag.localPosition = {12.0f, 0.3f, 2.0f};
    duffelBag.searchDurationSeconds = 6.0f;
    duffelBag.prompt = "Hold E to search the duffel bag";
    duffelBag.loot.kind = LootKind::Keycard;
    duffelBag.loot.keycardTier = KeycardTier::Gold;
    layout.containers.push_back(duffelBag);

    // -----------------------------------------------------------------
    // AI: Stalker dormant in EntryHall (watches the player from a
    // corner), Tormentor patrolling RestrictedWing, Culler dormant and
    // ready to be armed the instant SanitySystem crosses the
    // hallucination threshold (see HorrorAIManager.hpp's own comment --
    // it must already exist in the ECS view, not be spawned lazily).
    layout.aiSpawns.push_back({FacilityAiTier::Stalker, {2.5f, 0.0f, 2.5f}});
    layout.aiSpawns.push_back({FacilityAiTier::Tormentor, {10.0f, 0.0f, -2.0f}});
    layout.aiSpawns.push_back({FacilityAiTier::Culler, {10.0f, 0.0f, 0.0f}});

    // Capsule-center height above the y=0 floor: default
    // CharacterController::Settings::capsuleHalfHeight (0.55) +
    // capsuleRadius (0.35).
    layout.playerSpawn = {-2.0f, 0.9f, 2.0f};

    return layout;
}

} // namespace engine::despair
