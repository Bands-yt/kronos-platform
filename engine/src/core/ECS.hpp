#pragma once

#include <entt/entt.hpp>

#include "core/Components.hpp"

namespace engine::core {

using EntityId = entt::entity;
inline constexpr EntityId kNullEntity = entt::null;

// Thin wrapper around entt::registry. The wrapping exists for one reason:
// nothing outside core/ should include <entt/entt.hpp> directly. Every
// other subsystem (Physics, Scripting, the future Instance/DataModel layer
// from docs/ARCHITECTURE.md §6) goes through this API, so EnTT stays an
// implementation detail we could swap later without an engine-wide change.
class ECS {
public:
    ECS() = default;

    [[nodiscard]] EntityId createEntity(std::string name = {}) {
        EntityId e = registry_.create();
        registry_.emplace<Transform>(e);
        if (!name.empty()) {
            registry_.emplace<Name>(e, std::move(name));
        }
        return e;
    }

    void destroyEntity(EntityId e) {
        if (registry_.valid(e)) {
            registry_.destroy(e);
        }
    }

    template <typename Component, typename... Args>
    Component& addComponent(EntityId e, Args&&... args) {
        return registry_.emplace_or_replace<Component>(e, std::forward<Args>(args)...);
    }

    template <typename Component>
    [[nodiscard]] Component* tryGetComponent(EntityId e) {
        return registry_.try_get<Component>(e);
    }

    template <typename Component>
    [[nodiscard]] bool hasComponent(EntityId e) const {
        return registry_.all_of<Component>(e);
    }

    // A real no-op (not an error) if `e` doesn't have `Component` at all
    // -- matches EnTT's own `remove` semantics, so a caller doesn't need
    // to guard with hasComponent() first just to remove optimistically
    // (e.g. Inspector's "Remove Physics Collider" clearing all three
    // physics components regardless of which ones a given entity
    // actually has).
    template <typename Component>
    void removeComponent(EntityId e) {
        registry_.remove<Component>(e);
    }

    template <typename... Components>
    [[nodiscard]] auto view() {
        return registry_.view<Components...>();
    }

    // Per-tick update: right now this only prunes entities whose lifetime
    // has expired (nothing produces that yet -- it's the hook Debris /
    // TTL-driven cleanup from the DataModel layer attaches to). Scripting,
    // Physics and Audio each run their own pass over `view<...>()` from
    // GameLoop::tick() rather than being driven from inside here, so the
    // fixed-tick ordering in docs/ARCHITECTURE.md §6 (Stepped -> physics ->
    // Heartbeat -> RenderStepped) stays explicit at the call site instead of
    // being hidden inside the ECS wrapper.
    void update(float /*dt*/) {}

    [[nodiscard]] entt::registry& raw() { return registry_; }

    // NOT storage<entt::entity>()->size() -- the entity pool uses EnTT's
    // swap_only deletion policy (destroyed entities are recycled via a
    // free list threaded through the pool, not actually removed from its
    // packed array), so size() reports every slot ever allocated
    // (monotonically non-decreasing, including recycled-but-currently-
    // dead ones) rather than the currently-alive count. free_list() is
    // the documented boundary index EnTT itself uses internally for this
    // exact "how many of this swap-only pool's slots are actually alive
    // right now" question (see entt::basic_registry::size_hint's own use
    // of free_list() for swap_only pools). Caught via
    // studio::SceneManager's autosave dirty-tracking: entityCount() kept
    // reporting the pre-clear() count after core::ECS::raw().clear(),
    // which would have made "did an entity get created/destroyed"
    // comparisons silently wrong forever.
    [[nodiscard]] size_t entityCount() const { return registry_.storage<entt::entity>()->free_list(); }

private:
    entt::registry registry_;
};

// Debug dump -- see ECS.cpp.
void logEcsStats(ECS& ecs);

} // namespace engine::core
