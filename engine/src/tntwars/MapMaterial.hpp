#pragma once

#include <string>

namespace engine::tntwars {

// Sprint 16 ("Cinematic Graphics" Phase 2): which real procedural PBR
// material (see core::ProceduralMaterialLibrary) a MapLayoutPiece should
// render with, classified purely from its own name -- pure data logic, no
// Vulkan/GPU dependency, matching MapLayout.hpp's own "this module has no
// Vulkan dependency on purpose" rule, and real, honestly-testable
// headlessly (see tests/test_main.cpp's Sprint 16 section) without a
// window or device.
// Kronos ("Four RTX Maps" Phase 3): Mud/Wood/Coral/Sand added for the new
// Volcano/Underwater/Trenches/Sky map content -- same real procedural-PBR
// pattern (core::ProceduralMaterialLibrary), no art assets.
enum class MapPieceMaterialKind { Ground, Stone, Metal, Lava, Mud, Wood, Coral, Sand, None };

// Real, deterministic name -> material classification -- both real ECS-
// spawn sites (studio::plugins::TntWarsPlugin::buildMapGeometry() and
// trailer::TrailerDirector::spawnMapGeometry()) call this exact function,
// so Studio and the trailer never disagree about which map piece gets
// which material. `None` covers pieces no generated material fits (team
// bases, water -- see ProceduralMaterialLibrary::applyTo()'s own comment):
// those keep MapLayoutPiece::color as a flat Renderable::baseColor,
// exactly the pre-Sprint-16 behavior.
[[nodiscard]] MapPieceMaterialKind classifyMapPieceMaterial(const std::string& pieceName);

} // namespace engine::tntwars
