#include "studio/EntityClassification.hpp"

#include "core/Components.hpp"
#include "core/Navigation.hpp"
#include "core/OreNode.hpp"
#include "core/Shop.hpp"
#include "core/Terrain.hpp"
#include "core/WorldProp.hpp"

namespace engine::studio {

EntityCategory classifyEntity(core::ECS& ecs, core::EntityId entity) {
    if (ecs.hasComponent<core::TerrainChunkTag>(entity)) return EntityCategory::Terrain;
    if (ecs.hasComponent<core::WorldProp>(entity)) return EntityCategory::Prop;
    if (ecs.hasComponent<core::OreNode>(entity)) return EntityCategory::Economy;
    if (ecs.hasComponent<core::ShopStall>(entity) || ecs.hasComponent<core::UpgradeStation>(entity)) {
        return EntityCategory::Economy;
    }
    if (ecs.hasComponent<core::TeleportPad>(entity) || ecs.hasComponent<core::NavMarker>(entity)) {
        return EntityCategory::Navigation;
    }
    if (ecs.hasComponent<core::ColliderShape>(entity) || ecs.hasComponent<core::RigidBody>(entity)) {
        return EntityCategory::Physics;
    }
    return EntityCategory::Other;
}

Icon iconForCategory(EntityCategory category) {
    switch (category) {
        case EntityCategory::Terrain: return Icon::Terrain;
        case EntityCategory::Prop: return Icon::Prop;
        case EntityCategory::Physics: return Icon::Physics;
        case EntityCategory::Economy: return Icon::Material;
        case EntityCategory::Navigation: return Icon::WorldSpace;
        case EntityCategory::Other: return Icon::Folder;
    }
    return Icon::Folder;
}

const char* categoryDisplayName(EntityCategory category) {
    switch (category) {
        case EntityCategory::Terrain: return "Terrain";
        case EntityCategory::Prop: return "Props";
        case EntityCategory::Physics: return "Physics Objects";
        case EntityCategory::Economy: return "Economy";
        case EntityCategory::Navigation: return "Navigation";
        case EntityCategory::Other: return "Other";
    }
    return "Other";
}

} // namespace engine::studio
