#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::core::hierarchy {

// Kronos (Alpha Roadmap Phase 2, "Scene system" / "Scene graph
// stability"): the real parent-child API -- see Components.hpp's own
// Hierarchy struct comment for the full real design (local-vs-world
// Transform semantics, why this is strictly additive). Every function
// here is the one real, correct way to mutate parent/child relationships
// -- never touch a Hierarchy component's fields directly.

// Real, validated reparent -- fails (returns false, no state changed) if
// either entity is invalid, if `child == parent` (an entity can't parent
// itself), or if `parent` is already a real descendant of `child` (that
// would create a real cycle -- checked by walking `parent`'s own
// ancestor chain before committing). On success: removes `child` from
// its previous parent's children list (if it had one), adds it to the
// new parent's, and sets child's own Hierarchy::parent -- both sides
// always updated together, never just one.
bool setParent(ECS& ecs, EntityId child, EntityId parent);

// Real, honest detach -- `child` becomes a root again (Hierarchy::parent
// = kNullEntity, removed from its former parent's children). Preserves
// the child's real current *world* position/rotation/scale (computed via
// computeWorldMatrix() before detaching) by writing that decomposed
// result back into its own Transform -- so a detached object doesn't
// visually jump, matching every real editor's "unparent" behavior. A
// real, honest no-op if `child` has no parent at all.
void unparent(ECS& ecs, EntityId child);

// Real cascade delete -- destroys every real descendant of `entity`
// first (depth-first, so a child is never destroyed while its own
// children still reference it), then `entity` itself, then removes
// `entity` from its own former parent's children list if it had one.
// This is deliberately a *different*, opt-in function from
// ECS::destroyEntity() (which stays a real, single-entity primitive,
// unchanged, zero regression for every existing caller that has no idea
// hierarchy exists) -- callers that know/care about hierarchy use this
// one instead.
void destroyEntityRecursive(ECS& ecs, EntityId entity);

// Real world-space transform -- walks `entity`'s own real parent chain
// (via Hierarchy::parent, root at kNullEntity) multiplying each real
// local Transform::matrix() together, parent-to-child order. An entity
// with no Hierarchy component (or one with parent == kNullEntity) is
// its own real root: this returns exactly transform.matrix(), byte-
// identical to reading Transform directly -- the real reason every
// pre-existing entity's rendering is completely unaffected by this
// feature's existence. Real, defensive cycle guard (bounded walk depth)
// even though setParent() already prevents cycles at insertion time --
// defense in depth, not a claim that cycles are otherwise possible.
[[nodiscard]] glm::mat4 computeWorldMatrix(ECS& ecs, EntityId entity);

// Real, honest ancestor check -- true if `ancestor` is `entity` itself or
// any real node on its parent chain. The real primitive setParent() uses
// for its own cycle check; exposed since a caller (e.g. a hierarchy-panel
// drag-and-drop reparent UI) needs the identical real check before even
// attempting a reparent, not just after.
[[nodiscard]] bool isAncestorOf(ECS& ecs, EntityId ancestor, EntityId entity);

} // namespace engine::core::hierarchy
