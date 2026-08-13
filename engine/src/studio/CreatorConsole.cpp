#include "studio/CreatorConsole.hpp"

#include <cmath>

#include "core/Components.hpp"
#include "core/Navigation.hpp"

namespace engine::studio {

namespace {
bool hasInvalidComponents(const glm::vec3& v) {
    return std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z) || std::isinf(v.x) || std::isinf(v.y) ||
           std::isinf(v.z);
}
} // namespace

std::vector<ConsoleMessage> scanSceneForMessages(core::ECS& ecs, bool hasTerrain) {
    std::vector<ConsoleMessage> messages;

    auto transformView = ecs.view<core::Transform>();
    size_t entityCount = 0;
    for (auto entity : transformView) {
        ++entityCount;

        // Corrupted transform -- a real, actionable Error (see
        // InspectorPanel::hasInvalidComponents()'s own comment on how
        // this can happen).
        auto& transform = transformView.get<core::Transform>(entity);
        if (hasInvalidComponents(transform.position)) {
            const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
            std::string label = nameComponent != nullptr ? nameComponent->value : ("Entity " + std::to_string(static_cast<uint32_t>(entity)));
            messages.push_back({ConsoleMessageSeverity::Error, label + " has an invalid (NaN/Inf) position."});
        }

        // A Renderable with an unresolved mesh handle would draw nothing
        // (or crash MeshLibrary::get() callers that don't null-check) --
        // a real, actionable Error, not a cosmetic nitpick.
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
            if (renderable->meshHandle == core::Renderable::kInvalidHandle) {
                const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
                std::string label = nameComponent != nullptr ? nameComponent->value : ("Entity " + std::to_string(static_cast<uint32_t>(entity)));
                messages.push_back({ConsoleMessageSeverity::Error, label + " has a Renderable with no mesh assigned."});
            }
        }

        // A collider with no rigid body (or vice versa) is a real, honest
        // half-configured physics setup -- Physics::attachBodyToEntity()
        // needs both to make a live body.
        bool hasCollider = ecs.hasComponent<core::ColliderShape>(entity);
        bool hasRigidBody = ecs.hasComponent<core::RigidBody>(entity);
        if (hasCollider != hasRigidBody) {
            const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
            std::string label = nameComponent != nullptr ? nameComponent->value : ("Entity " + std::to_string(static_cast<uint32_t>(entity)));
            messages.push_back({ConsoleMessageSeverity::Warning,
                                 label + (hasCollider ? " has a ColliderShape but no RigidBody."
                                                       : " has a RigidBody but no ColliderShape.")});
        }
    }

    // Teleport pad link health -- a real, common authoring mistake this
    // engine's own real link-by-tag mechanism (core::findLinkedTeleportPad())
    // can genuinely detect.
    auto padView = ecs.view<core::TeleportPad>();
    for (auto entity : padView) {
        auto& pad = padView.get<core::TeleportPad>(entity);
        const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
        std::string label = nameComponent != nullptr ? nameComponent->value : ("Entity " + std::to_string(static_cast<uint32_t>(entity)));
        if (pad.linkTag.empty()) {
            messages.push_back({ConsoleMessageSeverity::Warning, label + " (Teleport Pad) has no Link Tag -- it won't auto-link to another pad."});
        } else if (core::findLinkedTeleportPad(ecs, entity) == core::kNullEntity) {
            messages.push_back({ConsoleMessageSeverity::Warning, label + " (Teleport Pad) has Link Tag \"" + pad.linkTag + "\" but no other pad shares it."});
        }
    }

    // Real, honest scene-level tips -- not errors, just useful nudges.
    if (!hasTerrain) {
        messages.push_back({ConsoleMessageSeverity::Tip, "No terrain yet -- create one in the Terrain Editor or Creator Tools panel."});
    }
    if (core::findNavMarkersOfKind(ecs, core::NavMarkerKind::Spawn).empty()) {
        messages.push_back({ConsoleMessageSeverity::Tip, "No Spawn nav marker placed yet -- consider adding one so a runtime spawn point is discoverable."});
    }
    if (entityCount == 0) {
        messages.push_back({ConsoleMessageSeverity::Tip, "The scene is empty -- use Creator Tools to place your first prop, or File > Load Scene."});
    }

    return messages;
}

} // namespace engine::studio
