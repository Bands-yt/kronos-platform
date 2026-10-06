#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/ECS.hpp"

namespace engine::studio {

// Value copy of one or more entity subtrees, able to bring them back with
// their original ids (when still free) for undo/redo. Only component types
// Studio knows about are captured; anything else is dropped on restore.
class EntitySnapshot {
public:
    static EntitySnapshot capture(core::ECS& ecs, const std::vector<core::EntityId>& roots);

    // Recreates every captured entity and returns the root ids.
    std::vector<core::EntityId> restore(core::ECS& ecs) const;

    [[nodiscard]] bool empty() const { return nodes_.empty(); }

private:
    struct Node {
        core::EntityId id = core::kNullEntity;
        core::EntityId parent = core::kNullEntity;
        bool root = false;
        std::vector<std::function<void(entt::registry&, entt::entity)>> components;
    };
    std::vector<Node> nodes_;
};

// Drops every entity whose ancestor is also in `entities`, so subtree
// operations don't touch the same descendant twice.
[[nodiscard]] std::vector<core::EntityId> topLevelEntities(core::ECS& ecs, const std::vector<core::EntityId>& entities);

// Deep copy of `source` and its descendants under the same parent. Runtime
// handles (physics bodies, loaded scripts, playing sounds) are reset.
core::EntityId duplicateEntity(core::ECS& ecs, core::EntityId source);

// "Part" -> "Part2", "Part2" -> "Part3", skipping names already in use.
[[nodiscard]] std::string nextUniqueName(core::ECS& ecs, const std::string& name);

} // namespace engine::studio
