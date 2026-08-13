#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "tntwars/MapDefinition.hpp"
#include "tntwars/Team.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 5 ("Scavenging"): real, TNT-Wars-specific
// loose-material types -- deliberately NOT core::OreType (see
// Inventory.hpp's own "deliberately scoped to OreType" comment): Mining
// Sim's ore and TNT Wars' battlefield scrap are real, distinct resource
// economies with nothing to gain from sharing one enum, matching the
// brief's own "distinct from Mining Sim's OreNode" framing.
enum class ScavengeMaterialType : uint8_t { ScrapMetal, BlastingPowder, Wiring };
constexpr size_t kScavengeMaterialTypeCount = 3;

struct ScavengeMaterialInfo {
    ScavengeMaterialType type;
    const char* name;
    glm::vec3 color;
};
[[nodiscard]] const ScavengeMaterialInfo& scavengeMaterialInfo(ScavengeMaterialType type);
[[nodiscard]] const char* scavengeMaterialName(ScavengeMaterialType type);

// Kronos ("Gameplay Loop" world-building): real, per-biome *flavor* name
// for the same real underlying `ScavengeMaterialType` -- the economy
// itself (recipes in Crafting.hpp, the ScavengedMaterials ledger, the
// territory/depletion/respawn rules above) stays the one real, shared,
// already-tested 3-type backend across every map; only the UI-hint text a
// player actually reads changes per biome, matching the "Sky Crystals" /
// "Star Ore, Void Crystals, Energy Shards" naming from the map design
// briefs without forking the economy into incompatible per-map enums.
// Falls back to scavengeMaterialName() for any map without its own real
// flavor set (Trenches/Mantle/IslandSea keep the original industrial-
// battlefield names, which already fit those biomes).
[[nodiscard]] const char* mapMaterialDisplayName(MapId map, ScavengeMaterialType type);

// Real, three-way split of the map along its own Z axis (matching
// MapLayout's own kBaseZ-based symmetric team-base placement, real and
// shared by every map): each team's own half is real, exclusive
// territory, with a real, neutral no-man's-land band at the map's own
// center -- exactly the brief's own "mid = No Man's Land."
enum class Territory { TeamA, TeamB, NoMansLand };
constexpr float kNoMansLandHalfWidth = 8.0f;
[[nodiscard]] Territory territoryAt(glm::vec3 position);

// Real, territory-gated scavenge node -- a real, depleting, respawning
// resource pickup, the same real "hits deplete it, a real timer refills
// it" shape core::OreNode already established for Mining Sim, applied to
// an instant-collect battlefield resource instead of a multi-hit mined
// one.
struct ScavengeNodeState {
    glm::vec3 position{0.0f};
    ScavengeMaterialType material = ScavengeMaterialType::ScrapMetal;
    int quantityRemaining = 0;
    int maxQuantity = 0;
    float respawnSecondsRemaining = 0.0f; // real-0 while available/not yet depleted
};

constexpr int kScavengeNodeMaxQuantity = 20;
constexpr float kScavengeNodeRespawnSeconds = 25.0f;

// Kronos bugfix (live-reported: "no visual model for the resource
// nodes"): the real root cause -- buildScavengeNodes(MapId) below
// anchors every node on coreWorldPosition(map, team), which reads a
// map's own generic, flat MapLayoutPiece "TeamA_Base"/"TeamB_Base"
// position. That's the real, correct anchor for the flat maps it was
// designed for (Trenches/Mantle/IslandSea), but Sky Map/Space Map build
// their own real, custom, terrain-aware team-base positions entirely
// separately (main.cpp's own skyTeamABaseCenter/teamBBaseCenter, with a
// real terrain.heightAt() Y, and Space Map's own real platform centers)
// -- nowhere near the generic flat position. Nodes anchored on the
// wrong position aren't "invisible," they're real, correctly-spawned
// geometry sitting far from wherever the player actually is. This real,
// explicit-position overload is the fix: any caller with its own real,
// authoritative team-base positions (main.cpp's own Sky/Space Map setup)
// calls this directly instead of going through the MapId-keyed
// coreWorldPosition() lookup.
[[nodiscard]] std::vector<ScavengeNodeState> buildScavengeNodesAt(glm::vec3 teamACenter, glm::vec3 teamBCenter);

// Real, per-map, per-team-territory node layout -- placed real-close to
// each team's own Core (see MapLayout::coreWorldPosition()), staying
// real-well inside that team's own territory (never drifting toward the
// real no-man's-land band), cycling through all three real material
// types. Every real map gets the same real count/shape of nodes -- node
// placement is derived purely from each team's own real Core position,
// not bespoke per-map geometry, so it generalizes to every current and
// future real map without per-map authoring.
[[nodiscard]] std::vector<ScavengeNodeState> buildScavengeNodes(MapId map);

// Real per-tick respawn countdown -- once `respawnSecondsRemaining`
// reaches 0 on a depleted node, real-refills quantityRemaining back to
// maxQuantity. A real, honest no-op on a node that isn't depleted (there
// is nothing to respawn) or on a non-positive dt.
void tickScavengeNodeRespawn(ScavengeNodeState& node, float dt);

struct ScavengeResult {
    bool success = false;
    ScavengeMaterialType material = ScavengeMaterialType::ScrapMetal;
    int quantityCollected = 0;
};

// Real, territory-gated scavenge attempt: fails (success=false, no state
// change) if `scavengerTeam`'s own territory doesn't match the node's own
// real position (an enemy node or a no-man's-land node can't be
// scavenged), or if the node is currently real-depleted. On success,
// real-removes up to `amount` units (capped at what's actually left),
// and -- the exact real moment the node empties -- starts its real
// respawn countdown.
[[nodiscard]] ScavengeResult scavengeNode(ScavengeNodeState& node, TeamId scavengerTeam, int amount);

// Real, small per-player carried-materials ledger -- deliberately not
// core::Inventory (see this file's own top comment): a flat, real,
// unlimited-capacity count per material type is the honest scope for
// battlefield scrap (no real weight/slot-limit design exists yet for
// TNT Wars, unlike Mining Sim's real, tuned OreTypeInfo::unitWeight
// system). Kronos roadmap Milestone 6 ("Crafting explosives") is the
// real, direct consumer of this ledger.
struct ScavengedMaterials {
    std::array<int, kScavengeMaterialTypeCount> counts{};
};

void addScavengedMaterial(ScavengedMaterials& materials, ScavengeMaterialType type, int quantity);
// Real remove: caps at what's actually held, returns how many units were
// actually removed -- the same real "never go negative, report the real
// actual amount" honesty core::removeItem() already established.
int removeScavengedMaterial(ScavengedMaterials& materials, ScavengeMaterialType type, int quantity);
[[nodiscard]] int scavengedMaterialCount(const ScavengedMaterials& materials, ScavengeMaterialType type);

} // namespace engine::tntwars
