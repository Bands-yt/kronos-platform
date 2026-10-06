#include "studio/EntityOps.hpp"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "core/EditableMeshComponent.hpp"
#include "core/Hierarchy.hpp"
#include "core/Interactable.hpp"
#include "core/Navigation.hpp"
#include "core/ParticleSystem.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/PropAnimation.hpp"
#include "core/ResourceManager.hpp"
#include "core/VideoPlaneComponent.hpp"
#include "core/WorldProp.hpp"

namespace engine::studio {

namespace {

template <typename... Ts>
struct TypeList {};

using SnapshotTypes =
    TypeList<core::Transform, core::Name, core::Renderable, core::MeshSource, core::RigidBody, core::ColliderShape,
             core::PhysicsMaterial, core::AudioSource, core::Light, core::Script, core::SkinnedRenderable,
             core::MovingPlatform, core::ParticleEmitter, core::EditableMeshComponent, core::WorldProp,
             core::LampState, core::Interactable, core::NavMarker, core::TeleportPad, core::PropAnimationHook,
             core::VideoPlaneComponent, core::SurfaceGraphMaterial, core::VisualScript, core::ResourceRefs>;

template <typename T>
void captureOne(entt::registry& registry, entt::entity entity,
                std::vector<std::function<void(entt::registry&, entt::entity)>>& out) {
    if constexpr (std::is_copy_constructible_v<T>) {
        if (const T* value = registry.try_get<T>(entity)) {
            out.emplace_back([copy = *value](entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e, copy); });
        }
    }
}

template <typename... Ts>
void captureAll(TypeList<Ts...>, entt::registry& registry, entt::entity entity,
                std::vector<std::function<void(entt::registry&, entt::entity)>>& out) {
    (captureOne<Ts>(registry, entity, out), ...);
}

void resetRuntimeState(core::ECS& ecs, core::EntityId entity) {
    if (auto* body = ecs.tryGetComponent<core::RigidBody>(entity)) body->joltBodyId = core::RigidBody::kInvalidBodyId;
    if (auto* script = ecs.tryGetComponent<core::Script>(entity)) {
        script->scriptId = core::kInvalidScript;
        script->loadedSource.clear();
    }
    if (auto* audio = ecs.tryGetComponent<core::AudioSource>(entity)) audio->playing = false;
}

void collectSubtree(core::ECS& ecs, core::EntityId entity, std::vector<core::EntityId>& out) {
    out.push_back(entity);
    if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity)) {
        const std::vector<core::EntityId> children = hierarchy->children;
        for (core::EntityId child : children) {
            if (ecs.raw().valid(child)) collectSubtree(ecs, child, out);
        }
    }
}

core::EntityId cloneSubtree(core::ECS& ecs, core::EntityId source, core::EntityId parent, bool renameRoot) {
    entt::registry& registry = ecs.raw();
    const core::EntityId clone = registry.create();
    for (auto [id, storage] : registry.storage()) {
        if (id == entt::type_hash<core::Hierarchy>::value() || id == entt::type_hash<entt::entity>::value()) continue;
        if (storage.contains(source) && !storage.contains(clone)) storage.push(clone, storage.value(source));
    }
    resetRuntimeState(ecs, clone);
    if (renameRoot) {
        if (auto* name = ecs.tryGetComponent<core::Name>(clone)) name->value = nextUniqueName(ecs, name->value);
    }
    if (parent != core::kNullEntity) core::hierarchy::setParent(ecs, clone, parent);

    if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(source)) {
        const std::vector<core::EntityId> children = hierarchy->children;
        for (core::EntityId child : children) {
            if (registry.valid(child)) cloneSubtree(ecs, child, clone, false);
        }
    }
    return clone;
}

} // namespace

EntitySnapshot EntitySnapshot::capture(core::ECS& ecs, const std::vector<core::EntityId>& roots) {
    EntitySnapshot snapshot;
    for (core::EntityId root : roots) {
        if (!ecs.raw().valid(root)) continue;
        std::vector<core::EntityId> subtree;
        collectSubtree(ecs, root, subtree);
        for (core::EntityId entity : subtree) {
            Node node;
            node.id = entity;
            node.root = entity == root;
            if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity)) node.parent = hierarchy->parent;
            captureAll(SnapshotTypes{}, ecs.raw(), entity, node.components);
            snapshot.nodes_.push_back(std::move(node));
        }
    }
    return snapshot;
}

std::vector<core::EntityId> EntitySnapshot::restore(core::ECS& ecs) const {
    entt::registry& registry = ecs.raw();
    std::unordered_map<core::EntityId, core::EntityId> remap;
    std::vector<core::EntityId> roots;
    for (const Node& node : nodes_) {
        const core::EntityId entity = registry.valid(node.id) ? registry.create() : registry.create(node.id);
        remap[node.id] = entity;
        for (const auto& apply : node.components) apply(registry, entity);
        resetRuntimeState(ecs, entity);
        if (node.root) roots.push_back(entity);
    }
    for (const Node& node : nodes_) {
        if (node.parent == core::kNullEntity) continue;
        auto it = remap.find(node.parent);
        const core::EntityId parent = it != remap.end() ? it->second : node.parent;
        if (registry.valid(parent)) core::hierarchy::setParent(ecs, remap[node.id], parent);
    }
    return roots;
}

std::vector<core::EntityId> topLevelEntities(core::ECS& ecs, const std::vector<core::EntityId>& entities) {
    std::vector<core::EntityId> result;
    for (core::EntityId entity : entities) {
        if (!ecs.raw().valid(entity)) continue;
        if (std::find(result.begin(), result.end(), entity) != result.end()) continue;
        const bool nested = std::any_of(entities.begin(), entities.end(), [&](core::EntityId other) {
            return other != entity && ecs.raw().valid(other) && core::hierarchy::isAncestorOf(ecs, other, entity);
        });
        if (!nested) result.push_back(entity);
    }
    return result;
}

core::EntityId duplicateEntity(core::ECS& ecs, core::EntityId source) {
    if (!ecs.raw().valid(source)) return core::kNullEntity;
    core::EntityId parent = core::kNullEntity;
    if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(source)) parent = hierarchy->parent;
    return cloneSubtree(ecs, source, parent, true);
}

std::string nextUniqueName(core::ECS& ecs, const std::string& name) {
    std::unordered_set<std::string> used;
    for (auto [entity, existing] : ecs.raw().view<core::Name>().each()) used.insert(existing.value);

    size_t digits = name.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(name[digits - 1]))) --digits;
    if (name.size() - digits > 6) digits = name.size();
    const std::string stem = name.substr(0, digits);
    int counter = digits < name.size() ? std::stoi(name.substr(digits)) : 1;
    std::string candidate;
    do {
        candidate = stem + std::to_string(++counter);
    } while (used.count(candidate) != 0);
    return candidate;
}

} // namespace engine::studio
