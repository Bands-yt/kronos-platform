#include "despair/FacilityLayout.hpp"

namespace engine::despair {

namespace {

// Real neighbor-open check for appendGridWing()'s door-orientation pick
// below -- out-of-bounds counts as "open" since the only doors this grid
// ever places sit on the wing's own boundary column (the entrance),
// where "off the edge of this grid" really does mean "connects to
// whatever's outside it" (EscapeRoom's own gap), not a wall.
bool isGridCellOpen(const FacilityGridWing& grid, int row, int col) {
    if (row < 0 || row >= static_cast<int>(grid.rows.size())) return true;
    const std::string& line = grid.rows[static_cast<std::size_t>(row)];
    if (col < 0 || col >= static_cast<int>(line.size())) return true;
    return line[static_cast<std::size_t>(col)] != '#';
}

// The ECS-free half of what FacilityMapBuilder.cpp needs from a grid:
// walks every cell once, appending a FacilityWall per '#' (a full-height
// block, no separate floor slab underneath it -- the block's own base
// already sits on y=0) or a floor slab per '.'/'+' (open, walkable), plus
// a real FacilityDoorSpec per '+' -- unlocked (no new keycard tier this
// wing needs), oriented by checking which pair of opposite neighbors is
// actually open: if the door's own left/right are the through-direction,
// the leaf's thin axis is X (matches computeFacilityLayout()'s own
// corridorDoor scale exactly); otherwise the through-direction is Z and
// the leaf is built thin-Z/wide-X instead. Appends into `layout` rather
// than returning a new one -- this is always additive geometry on top of
// whatever computeFacilityLayout() already built.
void appendGridWing(FacilityLayout& layout, const FacilityGridWing& grid) {
    constexpr float kWallHeight = 3.0f;
    constexpr float kWallHalfHeight = kWallHeight * 0.5f;
    constexpr glm::vec3 kWallColor{0.50f, 0.50f, 0.53f}; // slightly darker than the hand-authored rooms -- a real, honest "newer/rougher wing" read, not a bug
    constexpr glm::vec3 kFloorColor{0.32f, 0.32f, 0.35f};

    float halfCell = grid.cellSize * 0.5f;

    for (std::size_t r = 0; r < grid.rows.size(); ++r) {
        const std::string& line = grid.rows[r];
        for (std::size_t c = 0; c < line.size(); ++c) {
            char cell = line[c];
            if (cell == '#') continue; // handled after this loop's own early `continue` below -- see the wall branch just below

            float cellX = grid.originX + static_cast<float>(c) * grid.cellSize;
            float cellZ = grid.originZ + static_cast<float>(r) * grid.cellSize;

            layout.geometry.push_back({FacilityWallKind::Floor,
                                        {cellX, -0.05f, cellZ},
                                        {halfCell, 0.05f, halfCell},
                                        kFloorColor});

            if (cell != '+') continue;

            bool ewOpen = isGridCellOpen(grid, static_cast<int>(r), static_cast<int>(c) - 1) &&
                          isGridCellOpen(grid, static_cast<int>(r), static_cast<int>(c) + 1);
            FacilityDoorSpec door;
            door.localPosition = {cellX, 1.1f, cellZ};
            door.scale = ewOpen ? glm::vec3{0.2f, 2.2f, 1.2f} : glm::vec3{1.2f, 2.2f, 0.2f};
            door.locked = false;
            layout.doors.push_back(door);
        }
    }

    // Second pass for walls, kept separate from the loop above only so
    // the `continue` on '#' up there reads as "floor/door cells are the
    // real body of that loop" -- a wall cell needs nothing but its own
    // block, no floor/door branching to skip past.
    for (std::size_t r = 0; r < grid.rows.size(); ++r) {
        const std::string& line = grid.rows[r];
        for (std::size_t c = 0; c < line.size(); ++c) {
            if (line[c] != '#') continue;
            float cellX = grid.originX + static_cast<float>(c) * grid.cellSize;
            float cellZ = grid.originZ + static_cast<float>(r) * grid.cellSize;
            layout.geometry.push_back({FacilityWallKind::Wall,
                                        {cellX, kWallHalfHeight, cellZ},
                                        {halfCell, kWallHalfHeight, halfCell},
                                        kWallColor});
        }
    }
}

} // namespace

FacilityGridWing computeDeepStorageWingGrid() {
    FacilityGridWing grid;
    grid.originX = 19.0f; // 2m past EscapeRoom's own east wall at x=17
    grid.originZ = -12.0f;
    grid.cellSize = 2.0f;
    grid.rows = {
        "###########",
        "####......#",
        "####...##.#",
        "####...##.#",
        "#####.###.#",
        "####...#..#",
        "+.........#",
        "####...#..#",
        "#####.#####",
        "####...####",
        "####...####",
        "####...####",
        "###########",
    };
    return grid;
}

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
    // HouseLayout.cpp uses. North wall (z=-3) has a matching 2m gap
    // (x[-1,1]) into the Back Corridor below, so EntryHall isn't a
    // dead end behind the locked corridor door.
    g.push_back({FacilityWallKind::Floor, {0.0f, -0.05f, 0.0f}, {3.0f, 0.05f, 3.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {-2.0f, kWallCenterY, -3.0f}, {1.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north, west segment
    g.push_back({FacilityWallKind::Wall, {2.0f, kWallCenterY, -3.0f}, {1.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north, east segment
    g.push_back({FacilityWallKind::Wall, {0.0f, 2.6f, -3.0f}, {1.0f, 0.4f, kWallThicknessHalf}, kWallColor}); // north, lintel over back-corridor gap
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
    // EntryHall's east wall's corridor gap exactly. North wall (z=-3) has
    // its own 2m gap (x[9,11]) into the Back Corridor -- the loop's other
    // end.
    g.push_back({FacilityWallKind::Floor, {10.0f, -0.05f, 0.0f}, {3.0f, 0.05f, 3.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {8.0f, kWallCenterY, -3.0f}, {1.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north, west segment
    g.push_back({FacilityWallKind::Wall, {12.0f, kWallCenterY, -3.0f}, {1.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // north, east segment
    g.push_back({FacilityWallKind::Wall, {10.0f, 2.6f, -3.0f}, {1.0f, 0.4f, kWallThicknessHalf}, kWallColor}); // north, lintel over back-corridor gap
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
    // East wall (x=17) -- no longer the solid "nothing beyond it" far wall:
    // a 2m gap (z[-1,1]) now leads into the Deep Storage Wing (see
    // computeDeepStorageWingGrid()'s own comment), same split-segment-plus-
    // lintel gap convention as every other doorway in this hand-authored
    // half of the layout.
    g.push_back({FacilityWallKind::Wall, {17.0f, kWallCenterY, -1.5f}, {kWallThicknessHalf, kWallHalfHeight, 0.5f}, kWallColor}); // east, south segment
    g.push_back({FacilityWallKind::Wall, {17.0f, kWallCenterY, 1.5f}, {kWallThicknessHalf, kWallHalfHeight, 0.5f}, kWallColor}); // east, north segment
    g.push_back({FacilityWallKind::Wall, {17.0f, 2.6f, 0.0f}, {kWallThicknessHalf, 0.4f, 1.0f}, kWallColor}); // east, lintel over Deep Storage Wing gap

    // -----------------------------------------------------------------
    // Back Corridor: x[-1,11] z[-5,-3], an open, unlocked bypass between
    // EntryHall's and RestrictedWing's new north gaps -- the second leg
    // of the loop, so the locked front corridor door is a shortcut, not
    // the only way through. Middle span (x[3,7]) needs its own filler on
    // the shared z=-3 wall since neither room's own north wall reaches
    // that far; west/east end caps close the corridor's own outer ends.
    g.push_back({FacilityWallKind::Floor, {5.0f, -0.05f, -4.0f}, {6.0f, 0.05f, 1.0f}, kFloorColor});
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, -5.0f}, {6.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // far (north) wall
    g.push_back({FacilityWallKind::Wall, {5.0f, kWallCenterY, -3.0f}, {2.0f, kWallHalfHeight, kWallThicknessHalf}, kWallColor}); // shared-wall filler over the void between the two rooms' own gaps
    g.push_back({FacilityWallKind::Wall, {-1.0f, kWallCenterY, -4.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // west end cap
    g.push_back({FacilityWallKind::Wall, {11.0f, kWallCenterY, -4.0f}, {kWallThicknessHalf, kWallHalfHeight, 1.0f}, kWallColor}); // east end cap

    // -----------------------------------------------------------------
    // The corridor door, filling that door frame's own 1.2m-wide, 2.2m-tall
    // gap above. Unlocked -- the showcase's exit gate is the keycard-count
    // objective at the blastDoor below, so this door no longer needs its
    // own separate keycard gate in front of it.
    FacilityDoorSpec corridorDoor;
    corridorDoor.localPosition = {5.0f, 1.1f, 0.0f};
    corridorDoor.scale = {0.2f, 2.2f, 1.2f}; // thickness along X, width along Z -- this frame's gap runs in Z, unlike HouseDemoScene's own X-gap front door
    corridorDoor.locked = false;
    layout.doors.push_back(corridorDoor);

    // EscapeRoom -> Deep Storage Wing gate. Unlocked (no new keycard tier
    // this wing needs) -- it's the showcase's bigger back half, not
    // another gameplay gate.
    FacilityDoorSpec deepStorageDoor;
    deepStorageDoor.localPosition = {17.0f, 1.1f, 0.0f};
    deepStorageDoor.scale = {0.2f, 2.2f, 1.2f};
    deepStorageDoor.locked = false;
    layout.doors.push_back(deepStorageDoor);

    // The facility's exit -- fills the blastDoor gap in RestrictedWing's
    // own east wall above. Gated separately from the generic `doors` list
    // (see EscapeGameLoop.hpp's tryUnlockExitWithObjective(), which checks
    // this against the showcase's kKeycardsRequiredForExit count, not a
    // specific tier).
    layout.blastDoor.localPosition = {13.0f, 1.1f, 0.0f};
    layout.blastDoor.scale = {0.2f, 2.2f, 1.2f};

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
    footlocker.furniture = FacilityFurnitureKind::Locker;
    footlocker.loot.kind = LootKind::SanityInjector;
    footlocker.loot.sanityRestoreAmount = 50.0f;
    layout.containers.push_back(footlocker);

    // Wardrobe height grew from the old single-box hiding spot's 1.8m to
    // the greybox builder's real 2.0m cabinet (see
    // FacilityMapBuilder.cpp's kWardrobeSize) -- localPosition.y (this
    // spec's own vertical-center convention, same as every other
    // world-prop root Transform in this file) and interiorPosition.y both
    // move up by the same 0.1m so the compound visual and the "step
    // inside" position both still line up with the new cabinet, not the
    // old one.
    layout.hidingSpots.push_back({{-2.6f, 1.0f, -2.5f}, {-2.85f, 0.8f, -2.5f}});

    // EntryHall's own Red keycard now sits directly on top of a new
    // greybox desk (FacilityFurnitureKind::Desk) rather than floating over
    // bare floor -- same (x, z) as the desk beneath it, raised to the
    // desk's own tabletop height (see FacilityMapBuilder.cpp's kDeskSize:
    // root center at half its 0.9m height -> tabletop surface at world
    // y=0.9, keycard resting a hair above that at y=0.91). The desk itself
    // is a real, searchable LootContainer too (Hold E to search the desk),
    // just one with no fresh loot of its own (LootKind::None default) --
    // the keycard sitting visibly on top is a separate, already-real
    // Pickup, not double-counted loot.
    FacilityContainerSpec entryHallDesk;
    entryHallDesk.localPosition = {0.0f, 0.45f, -2.5f};
    entryHallDesk.searchDurationSeconds = 3.0f;
    entryHallDesk.prompt = "Hold E to search the desk";
    entryHallDesk.furniture = FacilityFurnitureKind::Desk;
    layout.containers.push_back(entryHallDesk);
    layout.keycards.push_back({{0.0f, 0.91f, -2.5f}, KeycardTier::Red});

    // Back Corridor: a third, standalone Blue keycard -- the showcase's
    // 3-keycard exit objective (kKeycardsRequiredForExit, LootSystem.hpp)
    // needs a third card somewhere in the facility, and the only two doors
    // that used to gate on a specific tier (corridor door, blastDoor) no
    // longer do, so this one exists purely for the player to go find, not
    // to unlock anything itself. Same "on top of a real desk" treatment as
    // the Red keycard above.
    FacilityContainerSpec backCorridorDesk;
    backCorridorDesk.localPosition = {5.0f, 0.45f, -4.0f};
    backCorridorDesk.searchDurationSeconds = 3.0f;
    backCorridorDesk.prompt = "Hold E to search the desk";
    backCorridorDesk.furniture = FacilityFurnitureKind::Desk;
    layout.containers.push_back(backCorridorDesk);
    layout.keycards.push_back({{5.0f, 0.91f, -4.0f}, KeycardTier::Blue});

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

    // A second wardrobe against RestrictedWing's own south wall (z=3, the
    // same wall the footlocker's room mirrors) -- gives the player a real
    // place to break line-of-sight from the Tormentor/Culler guarding the
    // duffel bag a few meters away, not just EntryHall's one hiding spot
    // on the far side of the whole facility.
    layout.hidingSpots.push_back({{8.3f, 1.0f, 2.5f}, {8.3f, 0.8f, 2.75f}});

    // -----------------------------------------------------------------
    // AI: Stalker dormant in EntryHall (watches the player from a
    // corner -- deliberately close; it's Dormant and never contributes to
    // Application.cpp's nearestThreatDistance/VHS-static-burst scan, so its
    // proximity is atmosphere, not a spawn-distance concern), Culler
    // dormant in RestrictedWing and ready to be armed the instant
    // SanitySystem crosses the hallucination threshold (see
    // HorrorAIManager.hpp's own comment -- it must already exist in the
    // ECS view, not be spawned lazily).
    //
    // Tormentor stands at the Back Corridor's far (RestrictedWing) end,
    // ~13.4m from playerSpawn but around the EntryHall/corridor bend --
    // no straight raycast reaches it from the spawn point itself, so
    // hasLineOfSight() only goes clear once the player has actually
    // stepped through the north gap into the corridor. From there the
    // sightline down the corridor's x-axis is a real, unobstructed ~10m
    // (at Back Corridor's fadeRadius edge -- see
    // computeVhsStaticNoiseIntensity()), giving the intended "spot it down
    // the hall and get chased" moment without an instant, warning-free
    // catch at load.
    layout.aiSpawns.push_back({FacilityAiTier::Stalker, {2.5f, 0.0f, 2.5f}, {}});
    // The Tormentor now patrols back and forth between the Back Corridor
    // and RestrictedWing's own interior instead of standing still at that
    // one spot until the player wanders into range -- "patrols between
    // rooms until line-of-sight is established" (see HorrorAIManager.hpp's
    // own TormentorAIState::patrolWaypoints comment). Both waypoints sit at
    // a constant x=10, which is inside RestrictedWing's north wall gap
    // (x[9,11] -- see that wall's own west/east segment comment above), so
    // the straight-line step between them passes cleanly through the real
    // gap in both directions rather than clipping a wall corner.
    layout.aiSpawns.push_back({FacilityAiTier::Tormentor,
                                {10.0f, 0.0f, -4.0f},
                                {{10.0f, 0.0f, -4.0f}, {10.0f, 0.0f, 2.0f}}});
    layout.aiSpawns.push_back({FacilityAiTier::Culler, {12.5f, 0.0f, 2.7f}, {}});

    // Capsule-center height above the y=0 floor: default
    // CharacterController::Settings::capsuleHalfHeight (0.55) +
    // capsuleRadius (0.35).
    layout.playerSpawn = {-2.0f, 0.9f, 2.0f};

    // -----------------------------------------------------------------
    // Deep Storage Wing: the grid-driven showcase extension (see
    // computeDeepStorageWingGrid()'s own comment for the room/loop
    // layout). Appended last -- every hand-authored room/gate above is
    // untouched by this call.
    appendGridWing(layout, computeDeepStorageWingGrid());

    // Three breaker-gated fixtures, one per real room this wing adds
    // (hub, north room, east room) -- dark until the existing facility
    // breaker (already placed in RestrictedWing above) is activated, the
    // same "pure toggle, caller writes the Light" split
    // EscapeGameLoop.hpp's own toggleBreaker() comment documents.
    layout.lights.push_back({{29.0f, 2.7f, 0.0f}, {0.85f, 0.90f, 1.0f}, 1.4f, 6.0f}); // hub
    layout.lights.push_back({{29.0f, 2.7f, -8.0f}, {0.85f, 0.90f, 1.0f}, 1.4f, 6.0f}); // north room
    layout.lights.push_back({{35.0f, 2.7f, 0.0f}, {0.85f, 0.90f, 1.0f}, 1.4f, 6.0f}); // east room

    // One pushable crate, placed well inside the hub's own open floor
    // (row/col [6][5] center, per computeDeepStorageWingGrid()'s legend --
    // nowhere near any 2m door/corridor gap) so a misjudged push can't
    // wedge it across the only path through -- see FacilityCratePropSpec's
    // own comment on why this layout never places one inside a gap.
    layout.crates.push_back({{29.0f, 0.4f, 1.0f}, {0.4f, 0.4f, 0.4f}});

    // A locker against the east room's own east wall (grid cell row 6,
    // col 9 -- computeDeepStorageWingGrid()'s row "+........." is open the
    // whole way from col 1 to col 9, with col 10 the wall behind it), well
    // clear of the hub-entrance door gap at col 0 of that same row. World
    // position = (originX + col*cellSize, originZ + row*cellSize) =
    // (19 + 9*2, -12 + 6*2) = (37, 0), same off-origin/off-corridor
    // placement logic as the hub crate above.
    FacilityContainerSpec eastRoomLocker;
    eastRoomLocker.localPosition = {37.0f, 0.4f, 0.0f};
    eastRoomLocker.searchDurationSeconds = 3.0f;
    eastRoomLocker.prompt = "Hold E to search the locker";
    eastRoomLocker.furniture = FacilityFurnitureKind::Locker;
    eastRoomLocker.loot.kind = LootKind::SanityInjector;
    eastRoomLocker.loot.sanityRestoreAmount = 50.0f;
    layout.containers.push_back(eastRoomLocker);

    return layout;
}

} // namespace engine::despair
