#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/TntWarsUpgrades.hpp"

namespace engine::tntwars {

// Kronos ("Gameplay Loop" world-building, "give players a reason to
// return"): the real, physical *place* a player stands to spend
// ScavengedMaterials via tntwars::purchaseUpgrade() -- see
// TntWarsUpgrades.hpp's own header comment for why this is a location,
// not a third upgrade category. Sky Map places one pair at each team's
// own real forge zone (spawnSkyBase()'s own forgePos); Space Map places
// one pair at each major DerelictStation platform (the brief's own
// "Station upgrades" -- a derelict station literally is a station).

// Real, empty tag -- marks an entity as one specific real upgrade
// station's own real Interactable target, so Application's own live
// proximity-scan tick knows which UpgradeCategory pressing "Interact"
// near it should purchase (see core::Interactable's own generic
// prompt/proximity fields for the rest of the real interaction shape).
struct UpgradeStationLink {
    UpgradeCategory category = UpgradeCategory::Traversal;
};

// Real spawn -- one small emissive marker box per real UpgradeCategory
// (two per `basePosition`, offset apart so neither overlaps the other),
// each carrying a real core::Interactable (proximityEnabled=true) and
// UpgradeStationLink. A station whose real Mesh/GPU upload fails is
// simply omitted from the returned list (a real, honest partial
// failure), not a hard abort.
[[nodiscard]] std::vector<core::EntityId> spawnUpgradeStations(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                                  const core::ProceduralMaterialLibrary& materials,
                                                                  VmaAllocator allocator, VkDevice device,
                                                                  VkCommandPool cmdPool, VkQueue queue,
                                                                  glm::vec3 basePosition, const char* locationLabel);

} // namespace engine::tntwars
