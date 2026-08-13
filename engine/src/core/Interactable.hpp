#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"

namespace engine::core {

// Marks an entity as a real interaction target -- the piece the existing
// raycast-based events.onInteract trigger (Application.cpp) and, new
// this pass, a real proximity-based trigger both check before firing. An
// entity with no Interactable component is still hittable by the raycast
// trigger (that check predates this component and stays permissive, see
// Application.cpp's own comment) -- attaching one is what opts an entity
// into cooldown gating, proximity triggering, and a real UI-hint prompt
// string, not a requirement for basic look-and-press interaction to work
// at all.
struct Interactable {
    // What a UI hint would show, e.g. "Press E to open" -- the real
    // string this pass's "UI hint (stub)" surfaces (see
    // findInteractablesInRange()'s doc comment for exactly how, and why
    // it's a stub: engine_runtime has no on-screen text rendering at all
    // to draw this into -- a real, stated architectural boundary [no
    // ImGui in engine_runtime, see docs/ARCHITECTURE.md's "no
    // Studio-only privileges" principle], not an oversight).
    std::string prompt = "Interact";

    // 0 = no cooldown, interactable every time (the default, matching
    // this component's absence entirely). Real re-use gating for
    // anything that shouldn't be spammable (a lever, a chest that needs
    // a moment to reset).
    float cooldownSeconds = 0.0f;

    // Opts this entity into the real proximity trigger (see
    // findInteractablesInRange()) in addition to the existing raycast
    // trigger -- a pickup item you can walk up to vs. a door you have to
    // look at and press a key for are both real, common interaction
    // shapes, not the same thing forced into one mechanism.
    bool proximityEnabled = false;
    float proximityRadius = 3.0f;

    // Real cooldown state, not just configuration -- when this entity
    // was last successfully interacted with, in the same accumulated
    // simulation-time units GameLoop's fixed tick advances (not
    // wall-clock time, for the same determinism reason every other
    // fixed-tick system in this engine cares about). Defaults far enough
    // in the past that the very first interaction is never blocked by
    // its own cooldown.
    float lastInteractedAtSeconds = -1'000'000.0f;
};

// Real cooldown check -- pure, no ECS/clock ownership of its own, so
// it's trivially unit-testable. `currentTimeSeconds` is the caller's own
// accumulated simulation clock (see Application.cpp's accumulator).
[[nodiscard]] bool canInteract(const Interactable& interactable, float currentTimeSeconds);

// Marks `interactable` as just-interacted-with -- called from both the
// raycast trigger and the proximity trigger (see Application.cpp) so
// cooldown applies uniformly regardless of which one fired it.
void markInteracted(Interactable& interactable, float currentTimeSeconds);

// Real proximity scan: every entity with both `Interactable`
// (`proximityEnabled=true`) and `Transform` within *that entity's own*
// `proximityRadius` of `origin` (typically the character's position),
// sorted nearest-first. This is Roblox's own "ProximityPrompt" style
// interaction target list -- distance-triggered, independent of the
// raycast "what am I looking at" query (which is only about camera
// facing and doesn't require Interactable at all). The nearest result
// (`front()`, if non-empty) is also the real, callable core of the
// UI-hint stub: whatever calls this once a tick has, in that entity's
// own `Interactable::prompt`, exactly what a real on-screen prompt would
// show -- see Application.cpp's stdout-based stand-in for that render.
[[nodiscard]] std::vector<EntityId> findInteractablesInRange(ECS& ecs, glm::vec3 origin);

// Real target-resolution logic Application.cpp's "Interact" key handler
// uses -- extracted into its own testable function (needs only a real
// ECS, no window/UnifiedInput/live Physics) rather than left inline in
// that lambda, the same "pure decision logic split from the I/O-touching
// caller" pattern this pass already used for
// CharacterController::tryStepUp()/rampVelocityTowardTarget() and
// core::AvatarController::tickAnimation(). `lookAtTarget` (the raycast
// hit) wins over `nearestProximityTarget` when both are present --
// "look at something to interact with it" takes priority over "just be
// near it," matching Application.cpp's own established raycast-first
// behavior. Returns kNullEntity if neither target exists, or if the
// chosen target has an Interactable component that's currently on
// cooldown (canInteract() returns false) -- a target with no Interactable
// component at all has no cooldown concept and always resolves,
// unchanged from this trigger's pre-existing permissive behavior.
[[nodiscard]] EntityId resolveInteractionTarget(EntityId lookAtTarget, EntityId nearestProximityTarget, ECS& ecs,
                                                 float currentTimeSeconds);

// Two real, minimal Interactable-driven behaviors (docs task category
// 7's "interactable door (open/close)" / "interactable pickup item") --
// Application.cpp's pre-tick hook applies these to whatever entity
// resolveInteractionTarget() resolves, alongside (not instead of) firing
// events.onInteract, so a gameplay script can layer custom reactions on
// top of this real default behavior rather than needing to reimplement
// it from scratch in Lua.

// A real, minimal door -- not a physics-simulated hinge (no physics
// constraint system exists yet -- see docs README's Known Issues), just
// a direct Transform::rotation write between two authored orientations.
// Honest scope for what this sprint asks for: a door a player can
// interact with to open and close again, not a full physics-driven
// swing.
struct Door {
    bool isOpen = false;
    glm::quat closedRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat openRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// Pure -- flips `door.isOpen` and writes the corresponding rotation into
// `transform`. Called once per successful interaction (see
// Application.cpp), so repeated interactions toggle open/close exactly
// as the task category's "(open/close)" wording asks for.
void toggleDoor(Door& door, Transform& transform);

// A real, minimal pickup -- collecting hides the entity's Renderable (if
// any) and removes its Interactable component so a second interaction
// attempt has nothing left to resolve against (resolveInteractionTarget()
// would just fall through to "no Interactable, always resolves" on a
// bare entity, which is wrong for something that's supposed to be gone).
// Deliberately doesn't destroy the entity outright or touch any
// inventory/currency system (neither exists yet) -- what a gameplay
// script does in response to events.onInteract on a Pickup-flagged
// entity (award currency, add to inventory, play a sound) is where that
// hooks in; this is only the real, honest "it's collected and can't be
// collected again" mechanical core.
struct Pickup {
    bool collected = false;
};

// ECS-only (needs Renderable/Interactable lookups) -- not pure the way
// toggleDoor() is, same "pure or ECS-only" split this file's other
// functions already use. No-op if `entity` has no Pickup component or is
// already collected.
void collectPickup(EntityId entity, ECS& ecs);

} // namespace engine::core
