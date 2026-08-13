#include "core/Hierarchy.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>

namespace engine::core::hierarchy {

namespace {

constexpr int kMaxWalkDepth = 256; // real, defensive bound -- see computeWorldMatrix()'s own comment

void removeFromParentsChildren(ECS& ecs, EntityId parent, EntityId child) {
    if (auto* parentHierarchy = ecs.tryGetComponent<Hierarchy>(parent)) {
        auto& siblings = parentHierarchy->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
}

} // namespace

bool isAncestorOf(ECS& ecs, EntityId ancestor, EntityId entity) {
    if (!ecs.raw().valid(ancestor) || !ecs.raw().valid(entity)) return false;
    EntityId current = entity;
    for (int depth = 0; depth < kMaxWalkDepth && current != kNullEntity; ++depth) {
        if (current == ancestor) return true;
        auto* h = ecs.tryGetComponent<Hierarchy>(current);
        current = h ? h->parent : kNullEntity;
    }
    return false;
}

bool setParent(ECS& ecs, EntityId child, EntityId parent) {
    if (!ecs.raw().valid(child) || !ecs.raw().valid(parent)) return false;
    if (child == parent) return false;
    // Real cycle check: if `child` is already an ancestor of `parent`,
    // making `parent` the new parent of `child` would close a real loop
    // (parent -> ... -> child -> parent). isAncestorOf(child, parent)
    // walks parent's own chain looking for child, exactly that check.
    if (isAncestorOf(ecs, child, parent)) return false;

    EntityId previousParent = kNullEntity;
    if (auto* childHierarchy = ecs.tryGetComponent<Hierarchy>(child)) {
        previousParent = childHierarchy->parent;
    }
    if (previousParent != kNullEntity && previousParent != parent) {
        removeFromParentsChildren(ecs, previousParent, child);
    }

    // ECS::addComponent() is emplace_or_replace -- calling it on an
    // entity that already has a Hierarchy would real-wipe its existing
    // children list back to empty. Only add a fresh one if `parent`
    // genuinely doesn't have one yet; otherwise real-reuse the existing
    // component so its current children survive.
    Hierarchy& parentHierarchy =
        ecs.hasComponent<Hierarchy>(parent) ? *ecs.tryGetComponent<Hierarchy>(parent) : ecs.addComponent<Hierarchy>(parent);
    if (std::find(parentHierarchy.children.begin(), parentHierarchy.children.end(), child) ==
        parentHierarchy.children.end()) {
        parentHierarchy.children.push_back(child);
    }

    // Same real reuse-vs-replace care for `child` -- it may already have
    // a Hierarchy component (its own real children, if it's a parent of
    // other entities itself) that must not be wiped.
    Hierarchy& childHierarchy =
        ecs.hasComponent<Hierarchy>(child) ? *ecs.tryGetComponent<Hierarchy>(child) : ecs.addComponent<Hierarchy>(child);
    childHierarchy.parent = parent;
    return true;
}

void unparent(ECS& ecs, EntityId child) {
    auto* childHierarchy = ecs.tryGetComponent<Hierarchy>(child);
    if (!childHierarchy || childHierarchy->parent == kNullEntity) return; // real, honest no-op -- already a root

    // Real, honest world-preserve: decompose the child's real current
    // world matrix (computed *before* detaching) and write that back as
    // its own new local (== world, once it's a root) Transform, so it
    // doesn't visually jump the instant it's detached.
    glm::mat4 worldMatrix = computeWorldMatrix(ecs, child);
    glm::vec3 translation, scale, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    if (glm::decompose(worldMatrix, scale, rotation, translation, skew, perspective)) {
        if (auto* transform = ecs.tryGetComponent<Transform>(child)) {
            transform->position = translation;
            // glm::decompose() returns the conjugate of the real rotation
            // -- the same well-known quirk studio::panels::ViewportPanel's
            // own gizmo-manipulation code already works around identically.
            transform->rotation = glm::conjugate(rotation);
            transform->scale = scale;
        }
    }

    removeFromParentsChildren(ecs, childHierarchy->parent, child);
    childHierarchy->parent = kNullEntity;
}

void destroyEntityRecursive(ECS& ecs, EntityId entity) {
    if (!ecs.raw().valid(entity)) return;

    // Real depth-first cascade -- copy the children list first (not a
    // reference into the component), since destroying each child below
    // removes it from *this* entity's own children vector as a side
    // effect (see the removeFromParentsChildren() call at the bottom),
    // which would invalidate an iterator/reference into the real vector
    // being iterated.
    std::vector<EntityId> childrenCopy;
    if (auto* h = ecs.tryGetComponent<Hierarchy>(entity)) childrenCopy = h->children;
    for (EntityId child : childrenCopy) destroyEntityRecursive(ecs, child);

    if (auto* h = ecs.tryGetComponent<Hierarchy>(entity)) {
        if (h->parent != kNullEntity) removeFromParentsChildren(ecs, h->parent, entity);
    }
    ecs.destroyEntity(entity);
}

glm::mat4 computeWorldMatrix(ECS& ecs, EntityId entity) {
    if (!ecs.raw().valid(entity)) return glm::mat4(1.0f);
    auto* transform = ecs.tryGetComponent<Transform>(entity);
    glm::mat4 local = transform ? transform->matrix() : glm::mat4(1.0f);

    auto* h = ecs.tryGetComponent<Hierarchy>(entity);
    EntityId parent = h ? h->parent : kNullEntity;
    if (parent == kNullEntity) return local; // real root -- byte-identical to transform->matrix() alone

    glm::mat4 world = local;
    EntityId current = parent;
    for (int depth = 0; depth < kMaxWalkDepth && current != kNullEntity; ++depth) {
        auto* parentTransform = ecs.tryGetComponent<Transform>(current);
        world = (parentTransform ? parentTransform->matrix() : glm::mat4(1.0f)) * world;
        auto* parentHierarchy = ecs.tryGetComponent<Hierarchy>(current);
        current = parentHierarchy ? parentHierarchy->parent : kNullEntity;
    }
    return world;
}

} // namespace engine::core::hierarchy
