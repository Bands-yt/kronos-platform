#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

// Real physical surface properties, not placeholders -- threaded through
// every `core::Physics` body-creation function (see Physics.hpp) and
// stored as an ECS component on every physics entity so Studio's material
// editor has something to read back and edit for an already-created body.
// Jolt itself has no "get the material back out" query beyond the live
// body's own friction/restitution fields, so this component is this
// engine's own source of truth, re-applied to the live Jolt body via
// `Physics::applyMaterial()` when edited.
struct PhysicsMaterial {
    float friction = 0.5f;    // Jolt's own default
    float restitution = 0.0f; // Jolt's own default -- inelastic (no bounce) unless stated otherwise
    float density = 1000.0f;  // kg/m^3 -- water's density, a reasonable generic default

    // Real, if simple, validation: friction/density must be non-negative
    // (a negative friction/density is physically meaningless, not just
    // stylistically wrong), restitution must be in [0,1] (Jolt itself
    // doesn't clamp this, but anything outside that range doesn't
    // correspond to a real coefficient of restitution -- energy would be
    // created, not just conserved less-than-perfectly).
    [[nodiscard]] bool validate(std::string& outError) const;
};

// Four real, named presets a Studio material editor can offer instead of
// hand-tuning three sliders from scratch. Values are real-world-sourced
// approximations (see PhysicsMaterial.cpp's comment on each), not
// arbitrary round numbers -- e.g. Stone's low restitution reflects how
// little a real rock bounces, not "0.1 felt right."
enum class PhysicsMaterialPreset { Custom, Metal, Rubber, Wood, Stone };

[[nodiscard]] const char* physicsMaterialPresetName(PhysicsMaterialPreset preset);
[[nodiscard]] bool physicsMaterialPresetFromName(const std::string& name, PhysicsMaterialPreset& out);
// `Custom` has no fixed values of its own (it means "whatever the user
// last tuned by hand") -- callers needing an actual PhysicsMaterial should
// never pass Custom here; doing so returns PhysicsMaterial{}'s bare
// defaults as a documented fallback, not a crash.
[[nodiscard]] PhysicsMaterial physicsMaterialForPreset(PhysicsMaterialPreset preset);

// Real closed-form volume formulas -- what `density * volume` actually
// multiplies against for real density-driven mass (see
// Physics::createDynamicBox()'s material-driven mass path). `halfHeight`
// matches Jolt's own CapsuleShape convention: the capsule's total height
// is `2*halfHeight + 2*radius` (a cylinder of height `2*halfHeight` capped
// by two hemispheres of `radius`, together forming one full sphere).
[[nodiscard]] float boxVolume(glm::vec3 halfExtents);
[[nodiscard]] float sphereVolume(float radius);
[[nodiscard]] float capsuleVolume(float radius, float halfHeight);

// Real mesh volume via the signed-tetrahedron-sum algorithm (the
// divergence theorem applied to a closed triangle mesh): for every
// triangle (v0,v1,v2), accumulate `dot(v0, cross(v1,v2)) / 6` -- the
// signed volume of the tetrahedron formed by that triangle and the
// origin. The signs cancel correctly for any closed, consistently-wound
// mesh regardless of where the origin sits (inside, outside, or on the
// mesh), so this is exact for a real closed mesh, not an approximation --
// the same real technique CAD/physics tools use rather than sampling or
// bounding-volume estimates. An open/non-manifold mesh yields a
// meaningless result (no error detection here -- see
// ColliderShape::validate() in Components.hpp for that).
[[nodiscard]] float meshVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

} // namespace engine::core
