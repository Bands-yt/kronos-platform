#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"

namespace engine::core {

// A minimal, deliberately non-skeletal parent/child link: the owning
// entity's Transform is driven each tick to `parent`'s Transform
// composed with a fixed local offset, by updateAttachments() below --
// not a general scene-graph/hierarchy system (no ECS component or
// system anywhere else in this engine tracks parent/child relationships
// -- see studio/panels/ExplorerPanel.hpp's note that a real Instance/
// Parent tree doesn't exist yet), and not bone-driven skinning (this
// engine has no skeleton/bone data on Mesh/Vertex at all -- see
// Mesh.hpp's Vertex layout). It exists specifically for avatar
// equipment: an equipped catalogue item is a real, separate ECS entity
// (its own Transform + Renderable + MeshSource, so it renders, picks,
// and scene-saves exactly like anything else) that merely tracks the
// avatar's position/rotation via this component -- the same technique
// CharacterController's own "nose" facing-direction marker already used
// by hand (see CharacterController.cpp), elevated here into a small,
// reusable, documented mechanism instead of a one-off.
//
// Lives in its own header rather than core/Components.hpp because it
// needs EntityId/kNullEntity, which are declared in ECS.hpp -- and
// ECS.hpp itself includes Components.hpp, so the reverse include would
// be circular. Every other component is pure data with no ECS-type
// dependency; this is the first one that needs an entity reference, so
// it gets its own file alongside the system that interprets it, rather
// than restructuring ECS.hpp/Components.hpp's existing include order.
struct AttachedTo {
    EntityId parent = kNullEntity;
    glm::vec3 localOffset{0.0f};
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// Applies every live AttachedTo component: child.Transform.position/
// rotation = parent.Transform.position/rotation composed with
// localOffset/localRotation. Deliberately NOT scaled by the parent's own
// Transform::scale -- equipment shouldn't stretch when an avatar's scale
// slider does, only translate/rotate with it, matching how real-world
// clothing sizing is a separate concern from character height in every
// avatar system this one is modeled on. An AttachedTo whose `parent` is
// no longer valid (destroyed) is skipped, not an error -- the child
// entity is simply left at its last resolved pose until something
// removes its AttachedTo or destroys it too.
void updateAttachments(ECS& ecs);

} // namespace engine::core
