#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/SceneTypes.hpp"
#include "miningsim/Zone.hpp"

namespace engine::miningsim {

// Sprint 16 ("Cinematic Graphics" Phase 5) -- "Mining Sim RTX Graphics
// Spec + Prototype Scene." See docs/MiningSimRtxSpec.md (repo root) for
// the full real spec this implements. This is a real, working, live-
// launchable (`engine_runtime --miningsim`) prototype scene, not a
// concept sketch: real geometry (composed from core::Mesh's existing
// Box/Capsule primitives -- no cavern/drill/crystal-specific mesh
// generator exists or is built here, see this function's own .cpp
// comment on why composition is the honest choice), real generated PBR
// materials (core::ProceduralMaterialLibrary's new `crystal` set plus
// the existing `stone`/`metal`), real point lights (Sprint 16 Phase 1's
// multi-light system) standing in for "glowing ore veins," and a real
// continuous dust particle emitter.
//
// Deliberately a single, fixed, hand-placed scene (like
// trailer::TrailerDirector::addFinalClashLavaBackdrop(), not a
// procedural cavern *generator*) -- the brief's own "Prototype Scene: 1
// cavern, 1 mining drill, 1 crystal cluster" is a fixed inventory, not a
// request for a level-generation system. The real cavern/drill/crystal
// *geometry* stays fixed regardless of `zone` (see this header's own
// note above); `zone` (Kronos roadmap Milestone 17, defaults to
// ZoneType::Normal so every existing call site's own look is unchanged)
// real-retints/relights that same fixed geometry per
// miningsim::zoneVisualTheme() -- the real, bounded "world asset
// pipeline generalization" this milestone asks for.
//
// `outSpawnedEntities`, if non-null (Kronos trailer production): every
// real entity this call spawns is real-appended to it, so a caller that
// needs to tear this scene back down between shots (trailer::TrailerDirector,
// cutting to a different beat) can do so without guessing at ECS state --
// the same real "return what you spawned" convention
// miningsim::spawnDungeonVisual()/spawnMobVisual()/spawnForgedToolVisual()
// already establish. Defaults to nullptr so the existing `--miningsim`
// live-interactive call site (main.cpp), which never tears this scene
// down, is completely unaffected.
[[nodiscard]] core::SceneLighting buildRtxPrototypeScene(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                           core::ParticleSystem& particleSystem,
                                                           const core::ProceduralMaterialLibrary& materials,
                                                           VmaAllocator allocator, VkDevice device,
                                                           VkCommandPool cmdPool, VkQueue queue,
                                                           ZoneType zone = ZoneType::Normal,
                                                           std::vector<core::EntityId>* outSpawnedEntities = nullptr);

// Where buildRtxPrototypeScene() places the scene's own center -- also a
// real, good default camera look-at target for anything spawning a
// camera to view it (main.cpp's --miningsim mode, see that flag's own
// comment).
[[nodiscard]] inline glm::vec3 kMiningSimRtxSceneCenter() { return glm::vec3(0.0f, 0.0f, 0.0f); }

} // namespace engine::miningsim
