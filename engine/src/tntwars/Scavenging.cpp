#include "tntwars/Scavenging.hpp"

#include <algorithm>

#include "tntwars/MapLayout.hpp"

namespace engine::tntwars {

namespace {
constexpr ScavengeMaterialInfo kScavengeMaterialInfos[] = {
    {ScavengeMaterialType::ScrapMetal, "Scrap Metal", {0.55f, 0.55f, 0.58f}},
    {ScavengeMaterialType::BlastingPowder, "Blasting Powder", {0.85f, 0.65f, 0.15f}},
    {ScavengeMaterialType::Wiring, "Wiring", {0.20f, 0.75f, 0.35f}},
};
} // namespace

const ScavengeMaterialInfo& scavengeMaterialInfo(ScavengeMaterialType type) {
    return kScavengeMaterialInfos[static_cast<size_t>(type)];
}

const char* scavengeMaterialName(ScavengeMaterialType type) { return scavengeMaterialInfo(type).name; }

const char* mapMaterialDisplayName(MapId map, ScavengeMaterialType type) {
    static constexpr const char* kSkyNames[kScavengeMaterialTypeCount] = {"Sky Crystal", "Storm Ore", "Aether Wire"};
    static constexpr const char* kSpaceNames[kScavengeMaterialTypeCount] = {"Star Ore", "Void Crystal", "Energy Shard"};
    size_t index = static_cast<size_t>(type);
    switch (map) {
        case MapId::SkyPlatforms:
            return kSkyNames[index];
        case MapId::Space:
            return kSpaceNames[index];
        default:
            return scavengeMaterialName(type);
    }
}

Territory territoryAt(glm::vec3 position) {
    if (position.z < -kNoMansLandHalfWidth) return Territory::TeamA;
    if (position.z > kNoMansLandHalfWidth) return Territory::TeamB;
    return Territory::NoMansLand;
}

namespace {
void addScavengeNode(std::vector<ScavengeNodeState>& nodes, glm::vec3 basePos, glm::vec3 offset,
                      ScavengeMaterialType material) {
    ScavengeNodeState node;
    node.position = basePos + offset;
    node.material = material;
    node.maxQuantity = kScavengeNodeMaxQuantity;
    node.quantityRemaining = kScavengeNodeMaxQuantity;
    nodes.push_back(node);
}
} // namespace

std::vector<ScavengeNodeState> buildScavengeNodesAt(glm::vec3 teamACenter, glm::vec3 teamBCenter) {
    std::vector<ScavengeNodeState> nodes;

    addScavengeNode(nodes, teamACenter, glm::vec3(-8.0f, 0.0f, 6.0f), ScavengeMaterialType::ScrapMetal);
    addScavengeNode(nodes, teamACenter, glm::vec3(8.0f, 0.0f, 6.0f), ScavengeMaterialType::BlastingPowder);
    addScavengeNode(nodes, teamACenter, glm::vec3(0.0f, 0.0f, 10.0f), ScavengeMaterialType::Wiring);

    addScavengeNode(nodes, teamBCenter, glm::vec3(-8.0f, 0.0f, -6.0f), ScavengeMaterialType::ScrapMetal);
    addScavengeNode(nodes, teamBCenter, glm::vec3(8.0f, 0.0f, -6.0f), ScavengeMaterialType::BlastingPowder);
    addScavengeNode(nodes, teamBCenter, glm::vec3(0.0f, 0.0f, -10.0f), ScavengeMaterialType::Wiring);

    return nodes;
}

std::vector<ScavengeNodeState> buildScavengeNodes(MapId map) {
    return buildScavengeNodesAt(coreWorldPosition(map, TeamId::A), coreWorldPosition(map, TeamId::B));
}

void tickScavengeNodeRespawn(ScavengeNodeState& node, float dt) {
    if (dt <= 0.0f || node.quantityRemaining > 0) return;
    node.respawnSecondsRemaining -= dt;
    if (node.respawnSecondsRemaining <= 0.0f) {
        node.respawnSecondsRemaining = 0.0f;
        node.quantityRemaining = node.maxQuantity;
    }
}

ScavengeResult scavengeNode(ScavengeNodeState& node, TeamId scavengerTeam, int amount) {
    ScavengeResult result;
    if (amount <= 0) return result;

    Territory territory = territoryAt(node.position);
    bool inOwnTerritory = (scavengerTeam == TeamId::A && territory == Territory::TeamA) ||
                           (scavengerTeam == TeamId::B && territory == Territory::TeamB);
    if (!inOwnTerritory) return result;
    if (node.quantityRemaining <= 0) return result;

    int taken = std::min(amount, node.quantityRemaining);
    node.quantityRemaining -= taken;
    if (node.quantityRemaining <= 0) {
        node.respawnSecondsRemaining = kScavengeNodeRespawnSeconds;
    }

    result.success = true;
    result.material = node.material;
    result.quantityCollected = taken;
    return result;
}

void addScavengedMaterial(ScavengedMaterials& materials, ScavengeMaterialType type, int quantity) {
    if (quantity <= 0) return;
    materials.counts[static_cast<size_t>(type)] += quantity;
}

int removeScavengedMaterial(ScavengedMaterials& materials, ScavengeMaterialType type, int quantity) {
    if (quantity <= 0) return 0;
    int& count = materials.counts[static_cast<size_t>(type)];
    int removed = std::min(quantity, count);
    count -= removed;
    return removed;
}

int scavengedMaterialCount(const ScavengedMaterials& materials, ScavengeMaterialType type) {
    return materials.counts[static_cast<size_t>(type)];
}

} // namespace engine::tntwars
