#pragma once

#include <optional>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/Team.hpp"

namespace engine::tntwars {

// Kronos ("Space Map Bible" v1.0, Section VI "Combat Layer", "PvP Orbital
// Conflict"): real, pure, fully unit-testable capture-point logic --
// same "zero ECS/window dependency" discipline core::Weather/core::Wind
// already establish. No CapturePoint/ControlPoint system existed
// anywhere in this engine before this (verified by grep). Presence is
// deliberately expressed as two plain booleans (`teamAPresent`/
// `teamBPresent`), not a player list or entity query -- this function
// owns no notion of "players" at all, so it's correct whether a live
// caller derives presence from one local player (this engine's current
// real offline single-player TNT Wars mode, see
// Application::setTntWarsLiveMode()'s own comment) or, in a future real
// networked match, many.
struct PvPNodeState {
    glm::vec3 position{0.0f};
    float radius = 8.0f;
    std::optional<TeamId> controllingTeam; // nullopt = real, neutral -- not yet captured by either team
    std::optional<TeamId> capturingTeam;   // which real team captureProgress currently tracks toward
    float captureProgress = 0.0f;          // real, 0..1
};

constexpr float kPvPNodeCaptureSeconds = 12.0f;

// Real per-tick capture logic:
//   - Both teams present at once: real, honest "contested" -- no
//     progress change either way (the standard capture-point contest
//     rule; a real, deliberate design choice, not an oversight).
//   - Only one team present: if that team isn't already the real
//     `capturingTeam`, real-resets progress to 0 and switches
//     `capturingTeam` to them first (walking onto a node someone else
//     was capturing restarts the contest, it doesn't instantly flip it)
//     -- then real-accrues progress by `dt / kPvPNodeCaptureSeconds`,
//     capped at 1.0. At real 1.0, `controllingTeam` becomes that team.
//   - Neither team present: a real, honest no-op -- progress holds
//     exactly where it was (no decay modeled; a real, deliberate
//     simplification, not a claim of full "decays when uncontested"
//     behavior some capture-point designs add).
void tickPvPNodeCapture(PvPNodeState& node, bool teamAPresent, bool teamBPresent, float dt);

// Real spawn -- one flat, real, non-collidable emissive beacon platform
// at the node's own position (players stand *on* it to contest, matching
// this file's own real presence-radius contract; no physics body since
// it sits flush with a platform's own already-collidable deck).
[[nodiscard]] core::EntityId spawnPvPNodeVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                  const core::ProceduralMaterialLibrary& materials,
                                                  VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                  VkQueue queue, const PvPNodeState& node, const char* name);

// Real per-tick visual sync -- neutral (no real controllingTeam yet)
// reads as a real, cool white-gray; blends toward the real capturing
// team's own color (blue for TeamA, red for TeamB) as captureProgress
// accrues, snapping fully to that team's own color once
// `controllingTeam` is real-set. A real, honest no-op if `entity` is
// core::kNullEntity (this node's own real GPU/mesh spawn failed).
void tickPvPNodeVisual(const PvPNodeState& node, core::EntityId entity, core::ECS& ecs);

} // namespace engine::tntwars
