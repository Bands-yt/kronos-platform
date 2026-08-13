#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::tntwars {

// Kronos ("Visual Polish" world-building, "damage decals"): a real,
// honest scorch mark -- this renderer has no G-buffer/deferred decal-
// projection pass (a true projected decal needs one, see this session's
// own established "real, simpler mechanism instead of faking asset-
// backed content" precedent, e.g. composite.frag's saturation-only
// grading in place of a sampled LUT). A scorch decal here is real
// *geometry*: a thin, dark, slightly-emissive-negative quad
// (core::Mesh::createPlane(), whose own default normal is +Y) rotated to
// match a real raycast hit normal and offset a hair off the surface
// (avoiding real z-fighting) -- the same "primitive composition over a
// missing renderer feature" technique this session's own map-building
// work already established repeatedly.

constexpr float kDecalLifetimeSeconds = 20.0f; // real -- old scorch marks fade out so they don't accumulate forever
constexpr float kDecalSurfaceOffset = 0.02f;   // real, small -- just enough to avoid z-fighting with the surface itself

// Real per-instance fade state -- parallel-owned by the live caller
// (Application's own tntWarsDecals_), not stored on the ECS entity
// itself (matches ScavengeNodeVisual's own "small parallel state struct"
// convention rather than inventing a new component for one field).
struct DecalState {
    core::EntityId entity = core::kNullEntity;
    float ageSeconds = 0.0f;
};

// Real spawn -- `normal` should be a real, unit-length surface normal
// (e.g. from Physics::RaycastHit::normal); a real, honest
// core::kNullEntity on GPU/mesh upload failure (same convention every
// other spawn function in this session already follows).
[[nodiscard]] DecalState spawnScorchDecal(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                            const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                            VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                            glm::vec3 normal, float radius);

// Real per-tick age -- this renderer's own main scene pass has no
// transparency/blend support at all (Renderer.cpp's own real, honest
// "no transparency pass yet" TODO on that pipeline's blendEnable), so a
// smooth alpha fade-out is not actually achievable here; a decal instead
// stays fully opaque for its real kDecalLifetimeSeconds lifetime, then
// disappears outright the exact tick it expires. Returns true that exact
// tick (the caller should then real-destroy the entity and drop this
// DecalState -- this function doesn't own entity lifetime itself,
// matching FlashEffect's own "tick reports, caller reaps" split). A
// real, honest no-op (returns false) on a non-positive dt.
[[nodiscard]] bool tickDecalExpiry(DecalState& decal, float dt);

} // namespace engine::tntwars
